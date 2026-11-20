/* Host-side test harness: the firmware's real flash_update.c (included
 * verbatim, so its static functions are reachable) and its real crc32(),
 * against stub USB / FatFs / flash layers.
 *
 * flash_update.c is compiled from a copy placed in build/ so that its
 * #include "ff.h" etc. resolve to the stubs here rather than to the real
 * firmware headers sitting next to the original. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <zlib.h>

#include <time.h>

uint8_t *host_buf;
#define IMAGE_BUF host_buf
#define crc32 fw_crc32                /* zlib owns the name crc32 on the host */
static uint32_t g_fpga_magic, g_fpga_id;     /* stand-ins for the FPGA's build ID registers */
#define FPGA_BUILD_MAGIC_REG g_fpga_magic
#define FPGA_BUILD_ID_REG    g_fpga_id
#define FW_VERSION "host-test-1.0"
#include "build/flash_update_copy.c"  /* verbatim copy of ../flash_update.c */
#include "build/crc32_extract.c"      /* crc32() cut out of ../spiflash.c */
#include "build/fwinfo_copy.c"        /* verbatim copy of ../fwinfo.c */
#include "../messages.c"              /* Translate(), IsYes() */

static uint8_t g_flash_head[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xAA, 0x99, 0x55, 0x66};
static const char *g_path, *g_keys = "";
static uint32_t g_id = FLASH_ID_M25P80;
static int g_flash_ret = -1;          /* <0: behave like no_return (exit) */
static int g_idlepolls, g_flushed, g_unlinked, g_fail_read_at = -1;
static int g_fail_big_at = -1;        /* multi-sector reads fail from this offset on */
static int g_reads_big, g_reads_small, g_opens;

FRESULT f_open(FIL *fp, const char *path, int mode) {
   (void)path; (void)mode;
   fp->f = g_path ? fopen(g_path, "rb") : NULL;
   if(!fp->f) return FR_NO_FILE;
   fseek(fp->f, 0, SEEK_END); fp->size = ftell(fp->f); fseek(fp->f, 0, SEEK_SET);
   fp->err = 0; g_opens++;
   return FR_OK;
}
/* Like FatFs, a file that has seen an I/O error refuses everything until it
 * is closed.  A failed multi-sector read has delivered part of its data. */
FRESULT f_read(FIL *fp, void *buf, UINT n, UINT *got) {
   long pos = ftell(fp->f);
   if(fp->err) return FR_INT_ERR;
   if(g_fail_read_at >= 0 && pos >= g_fail_read_at) { fp->err = 1; return FR_DISK_ERR; }
   if(n > 512 && g_fail_big_at >= 0 && pos + (long)n > g_fail_big_at) {
      if(fread(buf, 1, 700, fp->f) != 700) { }      /* partial junk-free data, then the error */
      fp->err = 1; return FR_DISK_ERR;
   }
   if(n > 512) g_reads_big++; else g_reads_small++;
   *got = (UINT)fread(buf, 1, n, fp->f); return FR_OK;
}
FRESULT f_lseek(FIL *fp, unsigned long ofs) {
   if(fp->err) return FR_INT_ERR;
   fseek(fp->f, (long)ofs, SEEK_SET); return FR_OK;
}
FRESULT f_close(FIL *fp) { fclose(fp->f); return FR_OK; }
FRESULT f_stat(const char *path, FILINFO *fno) {
   FIL f; if(f_open(&f, path, 0) != FR_OK) return FR_NO_FILE;
   fno->fsize = (unsigned long)f.size; f_close(&f); return FR_OK;
}
FRESULT f_unlink(const char *path) { printf("[unlink %s]\n", path); g_unlinked++; return FR_OK; }
int usb_kbd_testc(void) { return g_idlepolls >= 3 && *g_keys; }  /* key only "arrives" after polling */
int usb_kbd_getc(void) { g_idlepolls = 0; return *g_keys++; }
void IdlePoll(void) { g_idlepolls++; if(g_idlepolls > 1000) { printf("[no key: would wait forever]\n"); exit(3); } }
void FlushWriteCache(void) { g_flushed++; }
uint32_t flash_read_id(void) { return g_id; }
void flash_read(uint32_t addr, uint8_t *dst, uint32_t len) {
   uint32_t i; for(i = 0; i < len; i++) dst[i] = (addr + i < 8) ? g_flash_head[addr + i] : 0xFF;
}
int flash_program_image(uint32_t b1, const uint8_t *s1, uint32_t l1,
                        uint32_t b2, const uint8_t *s2, uint32_t l2, int no_return) {
   printf("[reads: %d multi-sector, %d single]\n", g_reads_big, g_reads_small);
   printf("[flash: bit base=%05x len=%u crc=%08lx | fw base=%05x len=%u crc=%08lx | no_return=%d | flushed=%d unlinked=%d]\n",
          b1, l1, l1 ? crc32_z(0, s1, l1) : 0UL, b2, l2, l2 ? crc32_z(0, s2, l2) : 0UL,
          no_return, g_flushed, g_unlinked);
   if(g_flash_ret < 0) exit(0);       /* the real thing never returns */
   return g_flash_ret;
}

static uint32_t rnd_state = 12345;
static uint32_t rnd(void) { rnd_state = rnd_state * 1664525u + 1013904223u; return rnd_state >> 8; }

int main(int argc, char **argv) {
   int i;
   host_buf = malloc(IMAGE_MAX);
   for(i = 1; i < argc; i++) {
      if(!strncmp(argv[i], "file=", 5)) g_path = argv[i] + 5;
      else if(!strncmp(argv[i], "keys=", 5)) g_keys = argv[i] + 5;
      else if(!strncmp(argv[i], "id=", 3)) g_id = (uint32_t)strtoul(argv[i] + 3, 0, 16);
      else if(!strncmp(argv[i], "flashret=", 9)) g_flash_ret = atoi(argv[i] + 9);
      else if(!strncmp(argv[i], "failread=", 9)) g_fail_read_at = atoi(argv[i] + 9);
      else if(!strncmp(argv[i], "failbig=", 8)) g_fail_big_at = atoi(argv[i] + 8);
      else if(!strncmp(argv[i], "lang=", 5)) LanguageSet(atoi(argv[i] + 5));
      else if(!strncmp(argv[i], "fpgaid=", 7)) { g_fpga_magic = 0x42494431u; g_fpga_id = (uint32_t)strtoul(argv[i] + 7, 0, 16); }
      else if(!strncmp(argv[i], "flash0=", 7)) {
         int k; for(k = 0; k < 8; k++) { unsigned v; sscanf(argv[i] + 7 + 2 * k, "%2x", &v); g_flash_head[k] = (uint8_t)v; }
      }
      else if(!strcmp(argv[i], "printinfo")) {       /* this harness's own firmware info block */
         fwrite(&gFwInfo, 1, sizeof(gFwInfo), stdout); return 0;
      }
      else if(!strcmp(argv[i], "datetest")) {
         /* FormatBuildId() must agree with the C library for any 32-bit time */
         int n, bad = 0; char mine[24], ref[24];
         for(n = 0; n < 200000; n++) {
            uint32_t t = (n < 8) ? (uint32_t[]){1, 86399, 86400, 951782400u, 951868800u, 1763553367u, 0x7FFFFFFFu, 0xFFFFFFFEu}[n]
                                 : (rnd() << 8) ^ rnd();
            time_t tt = (time_t)t; struct tm tm; gmtime_r(&tt, &tm);
            strftime(ref, sizeof(ref), "%Y-%m-%d %H:%M UTC", &tm);
            FormatBuildId(t, mine);
            if(strcmp(mine, ref)) { if(bad++ < 3) printf("  %u: %s != %s\n", t, mine, ref); }
         }
         printf("build date formatting: %s\n", bad ? "MISMATCH" : "matches the C library for 200000 timestamps"); return bad != 0;
      }
      else if(!strncmp(argv[i], "crcselftest", 11)) {
         /* firmware crc32() must agree with zlib on assorted buffers */
         uint8_t b[5000]; int n, bad = 0;
         for(n = 0; n < 5000; n++) b[n] = (uint8_t)rnd();
         for(n = 0; n < 5000; n += 37)
            if(crc32(b, (uint32_t)n) != (uint32_t)crc32_z(0, b, (size_t)n)) bad++;
         printf("crc32 self test: %s\n", bad ? "MISMATCH" : "matches zlib"); return bad != 0;
      }
      else if(!strncmp(argv[i], "fuzzbit=", 8)) {
         /* Mutate a valid .bit and run the real parser on an exact-size heap
          * copy, so AddressSanitizer catches any read past the end. */
         FILE *f = fopen(argv[i] + 8, "rb"); long n; uint8_t *orig; int it, accepted = 0;
         fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
         orig = malloc((size_t)n); if(fread(orig, 1, (size_t)n, f) != (size_t)n) return 2; fclose(f);
         freopen("/dev/null", "w", stdout);
         for(it = 0; it < 200000; it++) {
            uint32_t len = (it % 4 == 0) ? (rnd() % 400) : (uint32_t)n - ((it % 4 == 1) ? rnd() % 300 : 0);
            uint8_t *m = malloc(len ? len : 1); int k, muts = 1 + (int)(rnd() % 4);
            memcpy(m, orig, len);
            for(k = 0; k < muts && len; k++) m[rnd() % (len < 160 ? len : 160)] = (uint8_t)rnd();
            { struct bit_info info; accepted += ParseBitstream(m, len, &info); }
            free(m);
         }
         fprintf(stderr, "fuzz: 200000 mutated bitstreams parsed, %d accepted, no memory errors\n", accepted);
         return 0;
      }
   }
   FlashUpdateMenu();
   printf("[menu returned]\n");
   return 1;
}
