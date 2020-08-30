/*
 *  cpm_io.c
 *
 *  Copyright (C) 2019  Skip Hansen
 * 
 *  Code derived from Z80SIM
 *  Copyright (C) 1987-2017 by Udo Munk
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms and conditions of the GNU General Public License,
 *  version 2, as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * Copyright (C) 1987-2017 by Udo Munk
 *
 */

/*
 * This module contains the I/O handlers for hardware required for
 * a CP/M / MP/M system.
 * 
 * The I/O ports are compatiable with the cpmsim from the z80pack project by
 * Udo Munk (* https://github.com/udo-munk/z80pack.git)
 *
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "ff.h"
#include "cpm_io.h"
#include "usb.h"
#include "vt100.h"
#include "misc.h"

// #define DEBUG_LOGGING
// #define LOG_TO_BOTH
// #define VERBOSE_DEBUG_LOGGING
#include "log.h"

#define CPM_SECTOR_SIZE       128
#define WRITE_FLUSH_TO        500      // .5 seconds
#define MULTICOMP_DRIVE_SIZE  (1024*1024*8)  // 8mb

const struct {
   const char *Desc;
   uint32_t FileSize;
   uint16_t Tracks;
   uint16_t Sectors;
} FormatLookup[] = {
   { "8 inch SSSD floppy", 256256, 77, 26 },
   { "4 MB z80pack hard disk", 4177920, 255, 128 },
   { "8 MB Multicomp hard disk", 8388608, 256, 128 },
   { "512 MB hard disk", 536870912, 256, 16384 },
   {NULL}   // end of table
};

typedef enum {
   DSK_FLOPPY_SSSD,
   DSK_Z80PACK_4MB,
   DSK_MULTICOMP_8MB,
   DSK_Z80PACK_512MB,
   NUM_DSK_TYPES
} DiskType;

const char *DiskTypeDesc[] = {
   "241K Floppy disk",
   "  4MB Z80Pack HD",
   "8MB Multicomp HD",
   "512MB Z80Pack HD",
   NULL
};


/*
 * Structure for the disk images
 */
struct dskdef {
   unsigned int tracks;
   unsigned int sectors;
   bool bFlushWriteCache;
   bool bMultiCompDrive;
   FIL *fp;           // file object strcture
   DiskType Type;
};

FIL FpArray[MAX_MOUNTED_DRIVES+1];
#define BOOT_FILE_INDEX       MAX_MOUNTED_DRIVES

int gMountedDrives;
uint32_t gWriteFlushTimeout;
FIL *gSystemFp;
MapMode gMountMode;

// possible drives A: -> P:/
struct dskdef gDisks[MAX_LOGICAL_DRIVES];

uint8_t gDiskStatus;

static void fdco_out(uint8_t Data);
void CopyToZ80(uint8_t *pTo,uint8_t *pFrom,int Len);
void CopyFromZ80(uint8_t *pTo,uint8_t *pFrom,int Len);
MapMode MountBootDrive(void);
void ListMountedDrives(DiskType Type);

void CopyToZ80(uint8_t *pTo,uint8_t *pFrom,int Len)
{
   VLOG("Copying %d bytes from 0x%x to 0x%x\n",Len,(unsigned int) pFrom,
       (unsigned int) pTo);

   while(Len -- > 0) {
      *pTo = *pFrom++;
      pTo += 4;
   }
}

void CopyFromZ80(uint8_t *pTo,uint8_t *pFrom,int Len)
{
   VLOG("Copying %d bytes from 0x%x to 0x%x\n",Len,(unsigned int) pFrom,
       (unsigned int) pTo);

   while(Len -- > 0) {
      *pTo++ = *pFrom;
      pFrom += 4;
   }
}

// This routine is called when the Z80 performs an IO read operation
void HandleIoIn(uint8_t IoPort)
{
   uint8_t Ret;
   int Data = -1;

   switch(IoPort) {
      case 1:  // console data
         if(!usb_kbd_testc()) {
            VLOG("Waiting for console input, z80_con_status: 0x%x\n",
                z80_con_status);
            while(!usb_kbd_testc()) {
               IdlePoll();
            }
            VLOG("Continuing\n");
         }
         Data = (uint8_t) (usb_kbd_getc() & 0x7f);
         if(!usb_kbd_testc()) {
            z80_con_status = 0;
            VLOG("No more data available, z80_con_status: 0x%x\n",
                 z80_con_status);
         }
         break;

      case 14: // FDC status
         Data = gDiskStatus;
         break;

// The following are implemented in hardware so we should never see them here
      case 0:  // console status
      case 10: // FDC drive
      case 11: // FDC track
      case 12: // FDC sector (low)
      case 15: // DMA destination address low
      case 16: // DMA destination address high
      case 17: // FDC sector high

// We don't expect these ports to be read
      case 13: // FDC command
         ELOG("Unexpected input from port 0x%x\n",IoPort);
         break;

      case 30: // CPU speed low
         Data = 25;
         break;

      case 31: // CPU speed high
         Data = 0;
         break;

   // The following are not used implemented
      case 2:  // printer status
      case 3:  // printer data
      case 4:  // auxiliary status
      case 5:  // auxiliary data
   // The following are not used or needed for cp/m 2
      case 20: // MMU initialisation
      case 21: // MMU bank select
      case 22: // MMU select segment size (in pages a 256 bytes)
      case 23: // MMU write protect/unprotect common memory segment
      case 25: // clock command
      case 26: // clock data
      case 27: // 10ms timer causing maskable interrupt
      case 28: // x * 10ms delay circuit for busy waiting loops
      case 29: // hardware control
      case 40: // passive socket #1 status
      case 41: // passive socket #1 data
      case 42: // passive socket #2 status
      case 43: // passive socket #2 data
      case 44: // passive socket #3 status
      case 45: // passive socket #3 data
      case 46: // passive socket #4 status
      case 47: // passive socket #4 data
      case 50: // client socket #1 status
      case 51: // client socket #1 data
      default:
         Data = 0;
         ELOG("Input from port 0x%x ignored\n",IoPort);
         break;
   }

   if(Data != -1) {
      z80_in_data = (uint8_t) Data;
      VLOG("%d <- 0x%x\n",IoPort,Data);
   }
}

// This routine is called when the Z80 performs an IO write operation
void HandleIoOut(uint8_t IoPort,uint8_t Data)
{
   switch(IoPort) {
      case 1:  // console data
         vt100_putc(Data);
         break;

      case 13: // FDC command
         VLOG("0x%x -> %d\n",Data,IoPort);
         fdco_out(Data);
         break;

// The following are implemented in hardware so we should never see them here
      case 10: // FDC drive
      case 11: // FDC track
      case 12: // FDC sector (low)
      case 14: // FDC status
      case 15: // DMA destination address low
      case 16: // DMA destination address high
      case 17: // FDC sector high
         ELOG("Unexpected output of 0x%x to port 0x%x\n",Data,IoPort);
         break;

   // The following are not used implemented
      case 2:  // printer status
      case 3:  // printer data
      case 4:  // auxiliary status
      case 5:  // auxiliary data
   // The following are not used or needed for cp/m 2
      case 20: // MMU initialisation
      case 21: // MMU bank select
      case 22: // MMU select segment size (in pages a 256 bytes)
      case 23: // MMU write protect/unprotect common memory segment
      case 25: // clock command
      case 26: // clock data
      case 27: // 10ms timer causing maskable interrupt
      case 28: // x * 10ms delay circuit for busy waiting loops
      case 29: // hardware control
      case 30: // CPU speed low
      case 31: // CPU speed high
      case 40: // passive socket #1 status
      case 41: // passive socket #1 data
      case 42: // passive socket #2 status
      case 43: // passive socket #2 data
      case 44: // passive socket #3 status
      case 45: // passive socket #3 data
      case 46: // passive socket #4 status
      case 47: // passive socket #4 data
      case 50: // client socket #1 status
      case 51: // client socket #1 data
      default:
         ELOG("Output of 0x%x to port 0x%x ignored\n",Data,IoPort);
         break;
   }
}

/*
 * I/O handler for write FDC command:
 * transfer one sector in the wanted direction,
 * 0 = read, 1 = write
 *
 * The status byte of the FDC is set as follows:
 *   0 - ok
 *   1 - illegal drive
 *   2 - illegal track
 *   3 - illegal sector
 *   4 - seek error
 *   5 - read error
 *   6 - write error
 *   7 - illegal command to FDC
 */
static void fdco_out(uint8_t Data)
{
   register int i;
   unsigned long pos;
   uint8_t status = 0;  // Assume the best
   FIL *fp = NULL;
   FRESULT Err;
   UINT Wrote;
   UINT Read;
   uint8_t Buf[CPM_SECTOR_SIZE];
   uint8_t Drive = z80_drive;
   uint8_t Track = z80_track;
   uint16_t Sector = (z80_sector_msb << 8) + z80_sector_lsb;
   uint16_t DmaAdr = (z80_dma_msb << 8) + z80_dma_lsb;
   void *pBuf = (void *) ((DmaAdr << 2) + Z80_MEMORY_ADR);
   struct dskdef *pDisk = &gDisks[Drive];

   do {
      if(Data > 1) {
         ELOG("Invalid command %d\n",Data);
         status = 7;
         break;
      }
      VLOG("Disk %s %d:%d:%d @ 0x%x\n",Data == 0 ? "read" : "write",
           Drive,Track,Sector,DmaAdr);
      if(Drive >= MAX_LOGICAL_DRIVES || (fp = pDisk->fp) == NULL) {
         ELOG("Invalid drive %d\n",Drive);
         status = 1;
         break;
      }
      if(Track > pDisk->tracks) {
         ELOG("Invalid track %d\n",Track);
         status = 2;
         break;
      }
      if(Sector > pDisk->sectors) {
         ELOG("Invalid sector %d\n",Sector);
         status = 3;
         break;
      }
      pos = (((long)Track) * ((long)pDisk->sectors) + Sector - 1) << 7;

      if(pDisk->bMultiCompDrive) {
         if(Drive == 0 && Track == 0) {
         // special case for system track
            fp = &FpArray[BOOT_FILE_INDEX];
         }
         else if(Drive > 0) {
         // Add offset in SD card image for this drive
            pos += MULTICOMP_DRIVE_SIZE * Drive;
         }
         Drive = 0;
         pDisk = gDisks;
      }
      if((Err = f_lseek(fp,pos)) != FR_OK) {
         ELOG("f_lseek failed: %d\n",Err);
         status = 4;
         break;
      }

      switch(Data) {
         case 0:  /* read */
            leds = LED_GREEN;
            if((Err = f_read(fp,&Buf,CPM_SECTOR_SIZE,&Read)) != FR_OK) {
               ELOG("f_read failed: %d\n",Err);
               status = 5;
            }
            else if(Read != CPM_SECTOR_SIZE) {
               ELOG("Short read failure, read %d, requested %d\n",Read,
                    CPM_SECTOR_SIZE);
               status = 5;
            }
            else {
               CopyToZ80(pBuf,Buf,CPM_SECTOR_SIZE);
            }
            leds = 0;
            break;

         case 1:  /* write */
            leds = LED_GREEN;
            CopyFromZ80(Buf,pBuf,CPM_SECTOR_SIZE);
            if((Err = f_write(fp,Buf,CPM_SECTOR_SIZE,&Wrote)) != FR_OK) {
               ELOG("f_write failed: %d\n",Err);
               status = 6;
            }
            else if(Wrote != CPM_SECTOR_SIZE) {
               ELOG("Short write failure, wrote %d, requested %d\n",Wrote,
                    CPM_SECTOR_SIZE);
               status = 6;
            }
            else {
               gWriteFlushTimeout = ticks_ms() + WRITE_FLUSH_TO;
               pDisk->bFlushWriteCache = true;
            }
            leds = 0;
            break;

         default:    /* illegal command */
            ELOG("Invalid command 0x%x\n",Data);
            status = 7;
            break;
      }
   } while(false);

   gDiskStatus = status;
   if(status != 0) {
      ELOG("%s command failed, Disk %c T:%d, S:%d, status: %d\n",
           Data == 0 ? "Read" : "Write",'A' + Drive,Track,Sector,status);
   }
}

// Filename "driveX.dsk" where x= 'a' -> 'p'
int MountCpmDrive(char *Filename,FSIZE_t ImageSize)
{
   int Drive = Filename[5] - 'A';
   int Ret = 1;
   int i;
   FRESULT Err;
   FIL *Fp = &FpArray[gMountedDrives];

   do {
      LOG("mounting %s\n",Filename);
      if(gMountedDrives >= MAX_MOUNTED_DRIVES) {
         ELOG("Can't mount '%s', %d drives are already mounted\n",Filename,
              gMountedDrives);
         break;
      }

      if(Drive < 0 || Drive >= MAX_LOGICAL_DRIVES) {
         ELOG("Can't mount '%s', invalid drive\n",Filename);
         break;
      }

      for(i = 0; FormatLookup[i].Desc != NULL; i++) {
         if(ImageSize == FormatLookup[i].FileSize) {
            gDisks[Drive].sectors = FormatLookup[i].Sectors;
            gDisks[Drive].tracks = FormatLookup[i].Tracks;
            gDisks[Drive].Type = i;
            break;
         }
      }

      if(FormatLookup[i].Desc == NULL) {
         LOG("Couldn't mount '%s', disk format not supported\n",Filename);
         break;
      }

      if((Err = f_open(Fp,Filename,FA_READ | FA_WRITE)) != FR_OK) {
         ELOG("Couldn't open %s, %d\n",Filename,Err);
         break;
      }
      gMountedDrives++;
      gDisks[Drive].fp = Fp;
      LOG("Mounted %s image on %c:\n",FormatLookup[gDisks[Drive].Type].Desc,
          'A' + Drive);
      Ret = 0;
   } while(false);

   return Ret;
}

int LoadImage(const char *Filename,FSIZE_t Len)
{
   FIL File;
   FIL *Fp = &File;
   FRESULT Err;
   uint8_t Buf[CPM_SECTOR_SIZE];
   int Bytes2Read;
   int BytesRead = 0;
   UINT Read;
   uint8_t *pBuf = (uint8_t *) Z80_MEMORY_ADR;
   bool bFileOpen = false;

   do {
      if((Err = f_open(Fp,Filename,FA_READ)) != FR_OK) {
         ELOG("Couldn't open %s, %d\n",Filename,Err);
         break;
      }
      bFileOpen = true;
      while(BytesRead < Len) {
         Bytes2Read = Len - BytesRead;
         if(Bytes2Read > sizeof(Buf)) {
            Bytes2Read = sizeof(Buf);
         }
         if((Err = f_read(Fp,&Buf,Bytes2Read,&Read)) != FR_OK) {
            ELOG("f_read failed: %d\n",Err);
            break;
         }
         else if(Read != Bytes2Read) {
            ELOG("Short read failure, read %d, requested %d\n",Read,
                 Bytes2Read);
            Err = -1;
            break;
         }
         CopyToZ80(pBuf,Buf,Read);
         BytesRead += Bytes2Read;
         pBuf += Bytes2Read << 2;
      }
   } while(false);

   if(bFileOpen) {
      if((Err = f_close(Fp)) != FR_OK) {
         ELOG("f_close failed: %d\n",Err);
      }
   }

   return Err;
}


// Load track 0, sector 1 from the A: drive into memory
void LoadDefaultBoot()
{
   FIL *fp = gMountMode == MAP_Z80PACK ? gDisks[0].fp : gSystemFp;
   uint8_t Buf[CPM_SECTOR_SIZE];
   UINT Read;
   void *pBuf = (void *) Z80_MEMORY_ADR;
   FRESULT Err;

   do {
      if(fp == NULL) {
         ELOG("Couldn't read boot sector, system drive not mounted\n");
         break;
      }
      if((Err = f_lseek(fp,0)) != FR_OK) {
         ELOG("f_lseek failed: %d\n",Err);
         break;
      }

      if((Err = f_read(fp,&Buf,CPM_SECTOR_SIZE,&Read)) != FR_OK) {
         ELOG("f_read failed: %d\n",Err);
         break;
      }
#ifdef VERBOSE_DEBUG_LOGGING
      LOG("Boot sector:\n");
      LOG_HEX(Buf,CPM_SECTOR_SIZE);
#endif
      CopyToZ80(pBuf,Buf,CPM_SECTOR_SIZE);
   } while(false);
}

void UartPutc(char c)
{
   uart = (uint32_t) c;
}

void LogPutc(char c,void *arg)
{
   int LogFlags = (int) arg;

   if(!(LogFlags & LOG_DISABLED)) {
      if(LogFlags & LOG_SERIAL) {
         UartPutc(c);
      }

      if(LogFlags & LOG_MONITOR) {
         PrintfPutc(c);
      }
   }
}

void PrintfPutc(char c)
{
   if(c == '\n') {
      vt100_putc((uint8_t) '\r');
      vt100_putc((uint8_t) c);
   }
   else {
      vt100_putc((uint8_t) c);
   }
}

#ifndef LOGGING_DISABLED
void LogHex(char *LogFlags,void *Data,int Len)
{
   int i;
   uint8_t *cp = (uint8_t *) Data;

   for(i = 0; i < Len; i++) {
      if(i != 0 && (i & 0xf) == 0) {
         _LOG(LogFlags,"\n");
      }
      else if(i != 0) {
         _LOG(LogFlags," ");
      }
      _LOG(LogFlags,"%02x",cp[i]);
   }
   if(((i - 1) & 0xf) != 0) {
      _LOG(LogFlags,"\n");
   }
}
#endif

void FlushWriteCache()
{
   int i;
   FRESULT Err;

   for(i = 0; i < MAX_LOGICAL_DRIVES; i++) {
      if(gDisks[i].bFlushWriteCache) {
         gDisks[i].bFlushWriteCache = false;
         if(gDisks[i].bMultiCompDrive) {
            LOG("Flushing Multicomp write cache\n");
         }
         else {
            LOG("Flushing write cache drive %c\n",'A' + i);
         }
         if((Err = f_sync(gDisks[i].fp)) != FR_OK) {
            ELOG("f_sync failed: %d\n",Err);
         }
      }
   }
}

// Return mappihg mode or MAP_ERROR on error
MapMode MountBootDrive()
{
   FRESULT Err;
   DIR Dir;               /* Directory object */
   FILINFO Files[5];
   MapMode Mode[5];
   int NumFiles = 0;
   int BootFile = -1;
   MapMode Ret = MAP_ERROR;
   FIL *Fp = &FpArray[BOOT_FILE_INDEX];
   char *Filename;
   const char *Type;
   char Choice;
   int i;

   do {
      if((Err = f_opendir(&Dir,"")) != FR_OK) {
         ELOG("f_opendir failed: %d\n",Err);
         break;
      }
      for( ; ;) {
         Filename = Files[NumFiles].fname;
         Err = f_readdir(&Dir, &Files[NumFiles]);
         if(Err != FR_OK || !Filename[0]) {
            if(Err != FR_OK) {
               ELOG("f_readdir failed: %d\n",Err);
            }
            break;
         }
         if(strcmp(Filename,"DRIVEA.DSK") == 0) {
            Mode[NumFiles++] = MAP_Z80PACK;
         }
         else if(strcmp(Filename,"BOOT.DSK") == 0) {
            Mode[NumFiles++] = MAP_MULTICOMP;
         }
         else if(strcmp(Filename,"DUAL.DSK") == 0) {
            Mode[NumFiles++] = MAP_DUAL;
         }
         else if(strcmp(Filename,INIT_IMAGE_FILENAME) == 0) {
            Mode[NumFiles++] = MAP_NONE;
         }
      }
      f_closedir(&Dir);
      if(Err != FR_OK) {
         break;
      }

      LOG("NumFiles %d\n",NumFiles);
      if(NumFiles == 0) {
         ELOG("Error - boot file not found\n");
         break;
      }
      
      if(NumFiles == 1) {
         BootFile = 0;
         Ret = Mode[BootFile];
      }
      else if(NumFiles > 0) {
      // Ask user to select
         for( ; ; ) {
            ALOG_R("Boot modes:\n");
            ALOG_R("1: Only Z80pack disks\n");
            for(i = 0; i < NumFiles; i++) {
               switch(Mode[i]) {
                  case MAP_MULTICOMP:
                     Type = "Only Multicomp disks";
                     break;

                  case MAP_DUAL:
                     Type = "Both Z80pack and Multicomp disks";
                     break;

                  case MAP_NONE:
                     Type = "Z80 standalone program (no OS)";
                     break;

                  default:
                     ELOG("Internal error, on line %d\n",__LINE__);
                     break;
               }
               ALOG_R("%d: %s\n",i + 2,Type);
            }
            ALOG_R("\nSelect boot mode: ");

            while(!usb_kbd_testc()) {
               IdlePoll();
            }
            Choice = usb_kbd_getc();
            ALOG_R("%c\n",Choice);

            Choice -= '1';

            if(Choice == 0) {
               Ret = MAP_Z80PACK;
               break;
            }
            if(Choice > 0 && Choice <= NumFiles) {
               BootFile = Choice - 1;
               Ret = Mode[BootFile];
               break;
            }
            ALOG_R("Invaild option: please pick an option between 1 and %d\n",
                   NumFiles);
         }
      }
      
      if(BootFile >= 0) {
         if(Ret == MAP_NONE) {
            gBootImageLen = Files[BootFile].fsize;
            LOG("Set gBootImageLen to %d\n",gBootImageLen);
            break;
         }
         Filename = Files[BootFile].fname;
         LOG("Opening %s, Fp 0x%x\n",Filename,(unsigned int) Fp);
         if((Err = f_open(Fp,Filename,FA_READ)) != FR_OK) {
            ELOG("Couldn't open %s, %d\n",Filename,Err);
            Ret = MAP_ERROR;
            break;
         }
         LOG("%s opened successfully\n",Filename);
         if(Mode[BootFile] == MAP_Z80PACK) {
            gDisks[0].fp = Fp;
         }
         else {
            gSystemFp = Fp;
         }
      }
   } while(false);

   return Ret;
}

// Individual z80pack disk images: "driveX.dsk" where x= 'a' -> 'p'
// 
// To mount an Multicomp SD card image we need either a boot.dsk or a dual.dsk
// system disk.
// 
// When the boot disk is boot.dsk the entire Multicomp SD card image is
// mounted on CP/M drives A: to P:.
// 
// When the boot disk is dual.dsk the drive mappings are:
// 
// A: 8 MB hard disk image from Multicomp SD card image (*)
// B: 8 MB hard disk image from Multicomp SD card image
// C: 8 MB hard disk image from Multicomp SD card image
// D: 8 MB hard disk image from Multicomp SD card image
// E: 8 MB hard disk image from Multicomp SD card image
// F: 8 MB hard disk image from Multicomp SD card image
//
// G: 241k 8" SD floppy
// H: 241k 8" SD floppy
//
// I: 4 MB hard disk image using Udo Munk's z80pack format
// J: 4 MB hard disk image using Udo Munk's z80pack format
//
// (*) The system track from the SD card image is replaced by the 
//     I/O processor on the fly with data from the dual.dsk image.
// 
int MountCpmDrives()
{
   char *cp;
   char Drive;
   #define MAX_FILES 5
   FILINFO Files[MAX_FILES];
   int NumFiles = 0;
   char Choice = 0;
   FIL *Fp;
   FRESULT Err;
   int MultiCompDrives;
   char *Filename;
   DIR Dir;
   int i;
   int Ret = 1;   // Assume the worse
   struct dskdef *pDisk;

   do {
      gMountMode = MountBootDrive();
      LOG("gMountMode %d\n",gMountMode);
      if(gMountMode == MAP_ERROR || gMountMode == MAP_NONE) {
         ELOG("Couldn't determine mount mode\n");
         break;
      }
      if((Err = f_opendir(&Dir,"")) != FR_OK) {
         ELOG("f_opendir failed: %d\n",Err);
         break;
      }
      for(;;) {
         Filename = Files[NumFiles].fname;
         Err = f_readdir(&Dir, &Files[NumFiles]);
         if(Err != FR_OK || !Filename[0]) {
            break;
         }

         if(Filename[0] == '.' || Filename[0] == '_') {
         // Note: the filesystem is configured for 8.3 mode to save space
         // So leading dots which are illegal in 8.3 mode are filtered out
         // before we see them so we need to look check for '_' to ignore
         // actual files with a suffix of "._"
            LOG("Ignoring %s'\n",Filename);
            continue;
         }
         cp = Filename;
         while(*cp && *cp != '.') {
            cp++;
         }
         Drive = cp[-1];
         switch(gMountMode) {
            case MAP_Z80PACK:
               if(strstr(Filename,"DRIVE") != 0 && strstr(cp,".DSK") != 0 &&
                  Drive != 'G' && Drive != 'H')
               {
                  LOG("Filename: %s, drive '%c'\n",Filename,Drive);
                  MountCpmDrive(Files[NumFiles].fname,Files[NumFiles].fsize);
               }
               break;

            case MAP_MULTICOMP:
               if(strstr(cp,".IMG") != 0) {
               // Defer MultiComp images until later
                  if(NumFiles < MAX_FILES) {
                     NumFiles++;
                  }
               }
               break;

            case MAP_DUAL:
               Drive = Files[NumFiles].fname[5];

               if(strstr(cp,".DSK") != NULL && Drive >= 'G' && Drive <= 'J') {
                  LOG("Calling MountCpmDrive for %s\n",Files[NumFiles].fname);
                  MountCpmDrive(Files[NumFiles].fname,Files[NumFiles].fsize);
               }
               else if(strstr(cp,".IMG") != 0) {
               // Defer MultiComp images until later
                  if(NumFiles < MAX_FILES) {
                     NumFiles++;
                  }
               }
               break;
         }
      }
      f_closedir(&Dir);

      if(gMountMode == MAP_Z80PACK) {
      // Ignore Multicomp image files if the user selected z80pack only
         Ret = 0;
         break;
      }

      if(gMountMode != MAP_Z80PACK && NumFiles == 0) {
         ELOG("Error - no Multicomp image files found\n");
         break;
      }

      if(NumFiles == 1) {
         Choice = 0;
      }
      else {
      // Ask user to select
         for( ; ; ) {
            ALOG_R("Multicomp images:\n");
            for(i = 0; i < NumFiles; i++) {
               ALOG_R("%d: %s\n",i + 1, Files[i].fname);
            }
            ALOG_R("\nSelect image to mount: ");

            while(!usb_kbd_testc()) {
               IdlePoll();
            }
            Choice = usb_kbd_getc();
            ALOG_R("%c\n",Choice);
            Choice -= '1';
            if(Choice >= 0 && Choice < NumFiles) {
               break;
            }
            ALOG_R("Invaild option: please pick an option between 1 and %d\n",
                   NumFiles);
         }
      }

   // Mount the chosen Multicomp image
      Fp = &FpArray[gMountedDrives];
      Filename = Files[Choice].fname;

      if((Err = f_open(Fp,Filename,FA_READ | FA_WRITE)) != FR_OK) {
         ELOG("Couldn't open %s, %d\n",Filename,Err);
         break;
      }
      gMountedDrives++;

      MultiCompDrives = Files[Choice].fsize / MULTICOMP_DRIVE_SIZE;
      if(gMountMode == MAP_MULTICOMP) {
         if(MultiCompDrives > 16) {
            MultiCompDrives = 16;
         }
      }
      else {
         if(MultiCompDrives > 6) {
            MultiCompDrives = 6;
         }
      }

      pDisk = gDisks;
      for(i = 0; i < MultiCompDrives; i++) {
         pDisk->fp = Fp;
         pDisk->bMultiCompDrive = true;
         pDisk->sectors = 128;
         pDisk->tracks = 512;
         pDisk->Type = DSK_MULTICOMP_8MB;
         pDisk++;
      }
      Ret = 0;
   } while(false);

   ALOG_R("Mounted drives:\n");

   {
      bool TypeDone[NUM_DSK_TYPES];
      int j;

      memset(TypeDone,0,sizeof(TypeDone));

      for(i = 0; i < MAX_LOGICAL_DRIVES; i++) {
         if(!TypeDone[gDisks[i].Type]) {
            if(gDisks[i].Type == DSK_MULTICOMP_8MB) {
               if(gMountMode == MAP_MULTICOMP || gMountMode == MAP_DUAL) {
                  ListMountedDrives(gDisks[i].Type);
               }
            }
            else if(gMountMode == MAP_Z80PACK || gMountMode == MAP_DUAL) {
               ListMountedDrives(gDisks[i].Type);
            }
            TypeDone[gDisks[i].Type] = true;
         }
      }
   }
   #undef MAX_FILES

   return Ret;
}

void ListMountedDrives(DiskType Type)
{
   bool First = true;
   int i;
   for(i = 0; i < MAX_LOGICAL_DRIVES; i++) {
      if(gDisks[i].fp != NULL && gDisks[i].Type == Type) {
         if(First) {
            First = false;
            ALOG_R("%s: ",DiskTypeDesc[Type]);
         }
         else {
            ALOG_R(", ");
         }
         ALOG_R("%c:",'A' + i);
      }
   }
   if(!First) {
      ALOG_R("\n");
   }
}

// 
void DisplayString(const char *Msg,int Row,int Col)
{
   uint32_t *p = (uint32_t *) (VRAM_ADR + (Row * VT100_WIDTH * sizeof(uint32_t)));
   const char *cp = Msg;
   while(*cp) {
      *p++ = *cp++;
   }
}

