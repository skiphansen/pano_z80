/*
 * USB firmware / bitstream updater for the Pano G1.
 *
 * A single combined update file, PANOUPD.BIN, carries BOTH the FPGA bitstream
 * and the RISC-V firmware behind a small header with a human-readable version
 * and build date.  The updater reads it from the root of the USB drive,
 * validates everything (header CRC, per-image CRCs, and that the bitstream is
 * for this exact FPGA), shows what is installed now next to what the update
 * contains, and on confirmation reprograms the SPI flash using the
 * RAM-resident driver in spiflash.c.  Invoked from the setup menu (F12).
 *
 * Build a PANOUPD.BIN with tools/mkupdate.py (see the Makefile `update` target).
 *
 * Safety model (deliberately simple - see the project README):
 *   - The whole file is read into DDR and fully validated before any flash is
 *     touched.  A corrupt, truncated, or wrong-device image is refused.
 *   - The user confirms, and is warned that a failed/interrupted update needs a
 *     JTAG programmer to recover - no worse than before the updater existed.
 *   - The source file is deleted from the USB drive once we commit to flashing
 *     (the firmware cannot run USB/FatFs code after reprogramming its own
 *     flash, so the delete must happen here, not afterwards).
 *   - Because a combined update rewrites the firmware region we execute from,
 *     the flasher never returns: it shows the result on the board LED and the
 *     user power-cycles.
 */
#include <stdint.h>

#include "ff.h"
#include "usb.h"
#include "cpm_io.h"
#include "log.h"
#include "spiflash.h"
#include "fwinfo.h"

/* Staging buffer in DDR, 1 MB above its base: clear of the firmware's
 * .data/.ramtext/.bss (which start at 0x0C000000 and total a few tens of KB;
 * the stack is in a separate internal RAM).  A combined image is at most
 * ~900 KB (768 KB bitstream + 128 KB firmware + header).  Whatever lands here
 * is CRC-checked in place before use and is what the flasher verifies against,
 * so a bad staging read can never be flashed silently. */
#ifndef IMAGE_BUF                           /* overridable for host-side tests */
#define IMAGE_BUF    ((uint8_t *)0x0C100000u)
#endif
#define IMAGE_MAX    0x00200000u            /* 2 MB cap, plenty of headroom     */

#define UPDATE_NAME  "PANOUPD.BIN"

/*
 * Combined update header - 88 bytes, little-endian, no padding.  Must match
 * tools/mkupdate.py exactly.
 *
 *   off  size  field
 *     0     8  magic "PANO-UPD"
 *     8     4  header format version (= 1)
 *    12    24  version string (NUL-padded)
 *    36    24  build date string (NUL-padded)
 *    60     4  bitstream offset in file
 *    64     4  bitstream length (0 = no bitstream)
 *    68     4  bitstream CRC-32
 *    72     4  firmware offset in file
 *    76     4  firmware length (0 = no firmware)
 *    80     4  firmware CRC-32
 *    84     4  header CRC-32 (over bytes [0..84))
 */
#define UPD_MAGIC        "PANO-UPD"
#define UPD_MAGIC_LEN    8
#define UPD_HDR_VERSION  1
#define UPD_HDR_SIZE     88
#define OFF_HVER         8
#define OFF_VERSION      12
#define OFF_DATE         36
#define OFF_STRLEN       24
#define OFF_BIT_OFF      60
#define OFF_BIT_LEN      64
#define OFF_BIT_CRC      68
#define OFF_FW_OFF       72
#define OFF_FW_LEN       76
#define OFF_FW_CRC       80
#define OFF_HDR_CRC      84

/* Wait for a key.  usb_kbd_testc() only looks at the queue - it is IdlePoll()
 * that actually polls the USB keyboard, so it must be called while waiting
 * (as every other menu in the firmware does).  Without it no key ever
 * arrives and the menu appears to hang. */
static char WaitKey(void)
{
   while(!usb_kbd_testc())
      IdlePoll();
   return usb_kbd_getc();
}

/* Little-endian 32-bit field read */
static uint32_t rd32(const uint8_t *p, uint32_t off)
{
   return (uint32_t)p[off] | ((uint32_t)p[off + 1] << 8) |
          ((uint32_t)p[off + 2] << 16) | ((uint32_t)p[off + 3] << 24);
}

/* Read a file from the USB root into IMAGE_BUF.  Returns length, 0 on failure
 * / size out of range.
 *
 * Every read of the USB drive costs two commands (TEST UNIT READY and
 * READ(10)) with a fixed pause each, whatever its size, so reading the image
 * a sector at a time - which is all CP/M disk I/O ever does - takes about
 * twenty seconds.  Ask for 4 KB at a time instead; FatFs passes that on as
 * one transfer of up to a cluster.  Should such a multi-sector transfer fail
 * with this drive, go back to single sectors from where it stopped.  Either
 * way the image is only used if its checksums are right. */
#define READ_CHUNK_FAST  4096u
#define READ_CHUNK_SAFE  512u

static uint32_t ReadImage(const char *Name, uint32_t MaxLen)
{
   FIL File;
   UINT Read;
   uint32_t Size;
   uint32_t Total = 0;
   uint32_t ChunkMax = READ_CHUNK_FAST;

   if(f_open(&File, Name, FA_READ) != FR_OK)
      return 0;

   Size = (uint32_t)f_size(&File);
   if(Size == 0 || Size > MaxLen) {
      ALOG_R("%s size %lu is out of range (max %lu)\n",
             Name, (unsigned long)Size, (unsigned long)MaxLen);
      f_close(&File);
      return 0;
   }

   while(Total < Size) {
      uint32_t Chunk = Size - Total;
      if(Chunk > ChunkMax)
         Chunk = ChunkMax;
      if(f_read(&File, IMAGE_BUF + Total, Chunk, &Read) == FR_OK && Read != 0) {
         Total += Read;
         continue;
      }
      f_close(&File);
      if(ChunkMax == READ_CHUNK_SAFE) {
         ALOG_R("read of %s failed at offset %lu\n", Name, (unsigned long)Total);
         return 0;
      }
      /* FatFs refuses to go on with a file after an I/O error: open it again
       * and continue, in whole sectors, from the last one that arrived. */
      ChunkMax = READ_CHUNK_SAFE;
      Total &= ~(READ_CHUNK_SAFE - 1u);
      if(f_open(&File, Name, FA_READ) != FR_OK ||
         f_lseek(&File, Total) != FR_OK) {
         ALOG_R("read of %s failed at offset %lu\n", Name, (unsigned long)Total);
         return 0;
      }
   }
   f_close(&File);
   return Total;
}

/* Read a 16-bit big-endian field length (Xilinx .bit uses big-endian) */
static uint32_t be16(const uint8_t *p) { return ((uint32_t)p[0] << 8) | p[1]; }

/* Does the n-byte field at p contain the string Needle?  (bounded strstr) */
static int FieldContains(const uint8_t *p, uint32_t n, const char *Needle)
{
   uint32_t i, j, m;
   for(m = 0; Needle[m] != '\0'; m++)
      ;
   if(m == 0 || n < m)
      return 0;
   for(i = 0; i + m <= n; i++) {
      for(j = 0; j < m && p[i + j] == (uint8_t)Needle[j]; j++)
         ;
      if(j == m)
         return 1;
   }
   return 0;
}

/* What we need to know about a Xilinx .bit file */
struct bit_info {
   uint32_t DataOff;       /* offset of the configuration data in the file  */
   uint32_t DataLen;       /* its length: this is what belongs in the flash */
   uint32_t UserId;        /* bitgen UserID = our build ID; 0xFFFFFFFF none */
   char Date[12];          /* "2026/11/19"                                   */
   char Time[10];          /* "11:56:03"                                     */
};

/* Copy a header field (normally NUL terminated, but do not rely on it) */
static void CopyText(char *Dst, uint32_t DstSize, const uint8_t *Src, uint32_t n)
{
   uint32_t i;
   for(i = 0; i < n && i < DstSize - 1 && Src[i] >= ' ' && Src[i] < 0x7f; i++)
      Dst[i] = (char)Src[i];
   Dst[i] = '\0';
}

/* Pick the 8 hex digits out of "...;UserID=0x1234ABCD" in the design field */
static uint32_t ParseUserId(const uint8_t *p, uint32_t n)
{
   static const char Key[] = "UserID=0x";
   uint32_t i, j, v;

   for(i = 0; i + sizeof(Key) - 1 + 8 <= n; i++) {
      for(j = 0; j < sizeof(Key) - 1 && p[i + j] == (uint8_t)Key[j]; j++)
         ;
      if(j != sizeof(Key) - 1)
         continue;
      v = 0;
      for(j = 0; j < 8; j++) {
         uint8_t c = p[i + sizeof(Key) - 1 + j];
         if(c >= '0' && c <= '9') c = (uint8_t)(c - '0');
         else if(c >= 'A' && c <= 'F') c = (uint8_t)(c - 'A' + 10);
         else if(c >= 'a' && c <= 'f') c = (uint8_t)(c - 'a' + 10);
         else return 0xFFFFFFFFu;
         v = (v << 4) | c;
      }
      return v;
   }
   return 0xFFFFFFFFu;
}

/*
 * Take a Xilinx .bit file apart and confirm it is for this exact device.  The
 * container is: 00 09 <9 bytes> 00 01 then keyed fields
 *   'a' design name (with the bitgen UserID), 'b' part, 'c' date, 'd' time
 *       - each prefixed by a 16-bit big-endian length -
 *   'e' <32-bit length> <configuration data>.
 * Checks the header magic, that the part name contains "3s1600e" (a bitstream
 * for another chip is the worst brick cause), that the declared data length
 * matches, and that the configuration sync word 0xAA995566 is where it should
 * be.  Only the configuration data goes into the flash: that is what a JTAG
 * programmer writes there and what the FPGA reads at power-on.
 */
static int ParseBitstream(const uint8_t *Buf, uint32_t Len, struct bit_info *Info)
{
   uint32_t p;
   uint32_t l;
   uint32_t dlen;
   uint32_t i;
   char PartName[24];

   if(Len < 128)
      return 0;
   if(Buf[0] != 0x00 || Buf[1] != 0x09) {
      ALOG_R(".bit header magic is wrong\n");
      return 0;
   }
   p = 2 + be16(Buf);     /* skip the initial 9-byte field          */
   p += 2;                /* skip the 00 01 length                  */

   if(p + 3 > Len || Buf[p] != 'a') return 0;
   p++; l = be16(Buf + p); p += 2;                     /* design name */
   if(p + l > Len) return 0;
   Info->UserId = ParseUserId(Buf + p, l);
   p += l;

   if(p + 3 > Len || Buf[p] != 'b') return 0;
   p++; l = be16(Buf + p); p += 2;                     /* part name   */
   if(p + l > Len) return 0;
   if(!FieldContains(Buf + p, l, "3s1600e")) {
      CopyText(PartName, sizeof(PartName), Buf + p, l);
      ALOG_R(".bit is for the wrong device (part: %s)\n", PartName);
      return 0;
   }
   p += l;

   if(p + 3 > Len || Buf[p] != 'c') return 0;
   p++; l = be16(Buf + p); p += 2;                     /* date        */
   if(p + l > Len) return 0;
   CopyText(Info->Date, sizeof(Info->Date), Buf + p, l);
   p += l;

   if(p + 3 > Len || Buf[p] != 'd') return 0;
   p++; l = be16(Buf + p); p += 2;                     /* time        */
   if(p + l > Len) return 0;
   CopyText(Info->Time, sizeof(Info->Time), Buf + p, l);
   p += l;

   if(p + 5 > Len || Buf[p] != 'e') return 0;
   p++;
   dlen = ((uint32_t)Buf[p] << 24) | ((uint32_t)Buf[p + 1] << 16) |
          ((uint32_t)Buf[p + 2] << 8) | Buf[p + 3];
   p += 4;
   if(dlen > Len || p + dlen != Len) {
      ALOG_R(".bit data length %lu does not match image\n", (unsigned long)dlen);
      return 0;
   }
   if(dlen < 64 || dlen > FLASH_BIT_MAXSIZE) {
      ALOG_R(".bit configuration data is %lu bytes, flash region is %lu\n",
             (unsigned long)dlen, (unsigned long)FLASH_BIT_MAXSIZE);
      return 0;
   }
   Info->DataOff = p;
   Info->DataLen = dlen;
   for(i = p; i + 4 <= Len && i < p + 64; i++) {
      if(Buf[i] == 0xAA && Buf[i + 1] == 0x99 &&
         Buf[i + 2] == 0x55 && Buf[i + 3] == 0x66)
         return 1;
   }
   ALOG_R(".bit sync word not found\n");
   return 0;
}

static int SameText(const char *a, const char *b)
{
   while(*a != '\0' && *a == *b) { a++; b++; }
   return *a == *b;
}

/* The firmware's freestanding libc has no memcmp() */
static int MagicOk(const uint8_t *p)
{
   static const char Magic[UPD_MAGIC_LEN + 1] = UPD_MAGIC;
   uint32_t i;
   for(i = 0; i < UPD_MAGIC_LEN; i++) {
      if(p[i] != (uint8_t)Magic[i])
         return 0;
   }
   return 1;
}

/* Copy a fixed-width NUL-padded header string out, guaranteeing termination. */
static void CopyField(char *dst, const uint8_t *src, uint32_t off, uint32_t n)
{
   uint32_t i;
   for(i = 0; i < n; i++)
      dst[i] = (char)src[off + i];
   dst[n] = '\0';
}

void FlashUpdateMenu(void)
{
   FILINFO Info;
   uint32_t Len;
   uint32_t BitOff, BitLen, BitCrc;
   uint32_t FwOff, FwLen, FwCrc;
   uint32_t Id;
   uint32_t RunningBit;
   uint32_t i;
   struct bit_info Bit;
   const struct fw_info *NewFw = 0;
   uint8_t Head[8];
   char Version[OFF_STRLEN + 1];
   char Date[OFF_STRLEN + 1];
   char When[24];
   char Sel;
   int Err;

   ALOG_R("\n== USB firmware / bitstream update ==\n");

   if(f_stat(UPDATE_NAME, &Info) != FR_OK) {
      ALOG_R("No %s found on the USB drive.\n", UPDATE_NAME);
      return;
   }

   ALOG_R("Reading %s (%lu bytes)...\n", UPDATE_NAME, (unsigned long)Info.fsize);
   Len = ReadImage(UPDATE_NAME, IMAGE_MAX);
   if(Len == 0) {
      ALOG_R("Could not read %s.\n", UPDATE_NAME);
      return;
   }

   /* --- Header validation --- */
   if(Len < UPD_HDR_SIZE || !MagicOk(IMAGE_BUF)) {
      ALOG_R("%s is not a Pano update image.\n", UPDATE_NAME);
      return;
   }
   if(rd32(IMAGE_BUF, OFF_HVER) != UPD_HDR_VERSION) {
      ALOG_R("Unsupported update header version %lu.\n",
             (unsigned long)rd32(IMAGE_BUF, OFF_HVER));
      return;
   }
   if(crc32(IMAGE_BUF, OFF_HDR_CRC) != rd32(IMAGE_BUF, OFF_HDR_CRC)) {
      ALOG_R("Update header CRC mismatch - corrupt file.\n");
      return;
   }

   BitOff = rd32(IMAGE_BUF, OFF_BIT_OFF);
   BitLen = rd32(IMAGE_BUF, OFF_BIT_LEN);
   BitCrc = rd32(IMAGE_BUF, OFF_BIT_CRC);
   FwOff  = rd32(IMAGE_BUF, OFF_FW_OFF);
   FwLen  = rd32(IMAGE_BUF, OFF_FW_LEN);
   FwCrc  = rd32(IMAGE_BUF, OFF_FW_CRC);
   CopyField(Version, IMAGE_BUF, OFF_VERSION, OFF_STRLEN);
   CopyField(Date,    IMAGE_BUF, OFF_DATE,    OFF_STRLEN);

   /* --- Bounds and size limits (written so nothing can overflow) --- */
   if((BitLen && (BitOff < UPD_HDR_SIZE || BitOff > Len || BitLen > Len - BitOff)) ||
      (FwLen && (FwOff < UPD_HDR_SIZE || FwOff > Len || FwLen > Len - FwOff ||
                 FwLen > FLASH_FW_SIZE))) {
      ALOG_R("Update image has bad offsets/sizes.\n");
      return;
   }
   if(BitLen == 0 && FwLen == 0) {
      ALOG_R("Update image contains neither a bitstream nor firmware.\n");
      return;
   }

   /* --- Content validation --- */
   if(BitLen) {
      if(crc32(IMAGE_BUF + BitOff, BitLen) != BitCrc) {
         ALOG_R("Bitstream CRC mismatch - corrupt file.\n");
         return;
      }
      if(!ParseBitstream(IMAGE_BUF + BitOff, BitLen, &Bit)) {
         ALOG_R("Bitstream validation FAILED - refusing to flash.\n");
         return;
      }
   }
   if(FwLen) {
      if(FwLen < 16 || crc32(IMAGE_BUF + FwOff, FwLen) != FwCrc) {
         ALOG_R("Firmware CRC mismatch - corrupt file.\n");
         return;
      }
      NewFw = FwInfoFind(IMAGE_BUF + FwOff, FwLen);
   }

   /* --- What is installed now --- */
   RunningBit = FpgaBuildId();
   ALOG_R("\nInstalled now:\n");
   ALOG_R("  firmware:  %s, built %s\n", gFwInfo.version, gFwInfo.built);
   if(RunningBit != 0) {
      FormatBuildId(RunningBit, When);
      ALOG_R("  bitstream: built %s\n", When);
   }
   else {
      ALOG_R("  bitstream: unknown (it has no build ID)\n");
   }

   /* --- What the update contains --- */
   ALOG_R("Update %s (packaged %s):\n", Version, Date);
   if(FwLen) {
      if(NewFw != 0) {
         ALOG_R("  firmware:  %s, built %s%s\n", NewFw->version, NewFw->built,
                SameText(NewFw->version, gFwInfo.version) &&
                SameText(NewFw->built, gFwInfo.built) ? "  (same as installed)" : "");
      }
      else {
         ALOG_R("  firmware:  %lu bytes, no version information\n", (unsigned long)FwLen);
      }
   }
   else {
      ALOG_R("  firmware:  not included, stays as it is\n");
   }
   if(BitLen) {
      if(BUILD_ID_VALID(Bit.UserId)) {
         FormatBuildId(Bit.UserId, When);
         ALOG_R("  bitstream: built %s%s\n", When,
                Bit.UserId == RunningBit ? "  (same as installed)" : "");
      }
      else {
         ALOG_R("  bitstream: built %s %s (no build ID)\n", Bit.Date, Bit.Time);
      }
   }
   else {
      ALOG_R("  bitstream: not included, stays as it is\n");
   }

   /* --- Pre-flight: can we actually talk to the flash? ---
    * Non-destructive.  Done before asking and before the file is deleted, so
    * a board on which the software SPI path does not work loses nothing. */
   Id = flash_read_id();
   if(Id != FLASH_ID_M25P80) {
      ALOG_R("\nSPI flash did not identify (ID %06lx, expected %06lx).\n",
             (unsigned long)Id, (unsigned long)FLASH_ID_M25P80);
      ALOG_R("Nothing has been written. Update aborted.\n");
      return;
   }
   if(BitLen) {
      /* The configuration data of every bitstream for this FPGA starts the
       * same way (dummy word, sync word).  If the flash does not start like
       * the new image does, then our idea of how the bitstream is stored in
       * it is wrong for this board - do not write a bitstream on a guess. */
      flash_read(FLASH_BIT_BASE, Head, sizeof(Head));
      for(i = 0; i < sizeof(Head) && Head[i] == IMAGE_BUF[BitOff + Bit.DataOff + i]; i++)
         ;
      if(i != sizeof(Head)) {
         ALOG_R("\nThe bitstream in the flash starts with %02x %02x %02x %02x %02x %02x %02x %02x,\n",
                Head[0], Head[1], Head[2], Head[3], Head[4], Head[5], Head[6], Head[7]);
         ALOG_R("not like the one in this update.  Nothing has been written. Update aborted.\n");
         return;
      }
   }
   ALOG_R("Flash: M25P80 detected\n");

   ALOG_R("\n*** This erases and reprograms the SPI flash. ***\n");
   ALOG_R("If it fails or is interrupted, the board needs a JTAG programmer to recover.\n");
   ALOG_R("Do NOT power off during the update.\nProceed? (y/N) ");
   Sel = WaitKey();
   ALOG_R("%c\n", Sel);
   if(Sel != 'y' && Sel != 'Y') {
      ALOG_R("Cancelled.\n");
      return;
   }

   /* Committed.  Push any pending CP/M disk writes out to the USB drive, then
    * remove the update file - no USB access is possible once the firmware
    * region has been rewritten, so both have to happen now. */
   FlushWriteCache();
   if(f_unlink(UPDATE_NAME) != FR_OK)
      ALOG_R("Warning: could not delete %s from the USB drive.\n", UPDATE_NAME);

   ALOG_R("\nFlashing - this can take up to three minutes.\n");
   ALOG_R("Board LED: YELLOW = working, GREEN = done, RED = failed.\n");
   ALOG_R("When it is GREEN or RED, power-cycle the Pano.\n");

   /* Never returns on success or failure: the firmware we are executing from
    * may have been rewritten, so the result goes to the LED and the CPU halts.
    * The one exception is the flasher's own pre-flight ID check failing, in
    * which case nothing was touched and it is safe to come back here. */
   Err = flash_program_image(FLASH_BIT_BASE, IMAGE_BUF + BitOff + (BitLen ? Bit.DataOff : 0),
                             BitLen ? Bit.DataLen : 0,
                             FLASH_FW_BASE,  IMAGE_BUF + FwOff,  FwLen,
                             1 /* no_return */);

   ALOG_R("Update aborted (error %d): flash did not respond, nothing was written.\n",
          Err);
}
