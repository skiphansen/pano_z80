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

#define DEBUG_LOGGING
// #define VERBOSE_DEBUG_LOGGING
#include "log.h"

#define CPM_SECTOR_SIZE       128
#define MAX_MOUNTED_DRIVES    4
#define MAX_LOGICAL_DRIVES    16 // A: -> P: 
#define Z80_MEMORY_BASE       0x05000000

const struct {
   const char *Desc;
   uint32_t FileSize;
   uint16_t Tracks;
   uint16_t Sectors;
} FormatLookup[] = {
   { "8 inch SD floppy", 256256, 77, 26 },
   { "4 MB hard disk", 4177920, 255, 128 },
   { "512 MB hard disk", 536870912, 256, 16384 },
   {NULL}   // end of table
};

/*
 * Structure for the disk images
 */
struct dskdef {
   unsigned int tracks;
   unsigned int sectors;
   FIL *fp;           // file object strcture
};

FIL FpArray[MAX_MOUNTED_DRIVES];
int gMountedDrives;

// possible drives A: -> P:/
struct dskdef gDisks[MAX_LOGICAL_DRIVES];

uint8_t gDiskStatus;

static void fdco_out(uint8_t Data);
void CopyToZ80(uint8_t *pTo,uint8_t *pFrom,int Len);
void CopyFromZ80(uint8_t *pTo,uint8_t *pFrom,int Len);

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
            while(gFunctionRequest == 0 && !usb_kbd_testc()) {
               usb_event_poll();
            }
            VLOG("Continuing\n");
         }
         if(usb_kbd_testc()) {
            Data = (uint8_t) usb_kbd_getc();
            if(!usb_kbd_testc()) {
               z80_con_status = 0;
               VLOG("No more data available, z80_con_status: 0x%x\n",
                    z80_con_status);
            }
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
         ELOG("\nUnexpected input from port 0x%x\n",IoPort);
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
         Data = 0;
         ELOG("\nInput from port 0x%x ignored\n",IoPort);
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
         ELOG("\nUnexpected output of 0x%x to port 0x%x\n",Data,IoPort);
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
         ELOG("\nOutput of 0x%x to port 0x%x ignored\n",Data,IoPort);
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
   void *pBuf = (void *) ((DmaAdr << 2) + Z80_MEMORY_BASE);
   struct dskdef *pDisk = &gDisks[Drive];

   do {
      if(Data > 1) {
         ELOG("\nInvalid command %d\n",Data);
         status = 7;
         break;
      }
      VLOG("Disk %s %d:%d:%d @ 0x%x\n",Data == 0 ? "read" : "write",
           Drive,Track,Sector,DmaAdr);
      if(Drive >= MAX_LOGICAL_DRIVES || (fp = pDisk->fp) == NULL) {
         ELOG("\nInvalid drive %d\n",Drive);
         status = 1;
         break;
      }
      if(Track > pDisk->tracks) {
         ELOG("\nInvalid track %d\n",Track);
         status = 2;
         break;
      }
      if(Sector > pDisk->sectors) {
         ELOG("\nInvalid sector %d\n",Sector);
         status = 3;
         break;
      }
      pos = (((long)Track) * ((long)pDisk->sectors) + Sector - 1) << 7;
      if((Err = f_lseek(fp,pos)) != FR_OK) {
         ELOG("\nf_lseek failed: %d\n",Err);
         status = 4;
         break;
      }

      switch(Data) {
         case 0:  /* read */
            if((Err = f_read(fp,&Buf,CPM_SECTOR_SIZE,&Read)) != FR_OK) {
               ELOG("\nf_read failed: %d\n",Err);
               status = 5;
            }
            else if(Read != CPM_SECTOR_SIZE) {
               ELOG("\nShort read failure, read %d, requested %d\n",Read,
                    CPM_SECTOR_SIZE);
               status = 5;
            }
            else {
               CopyToZ80(pBuf,Buf,CPM_SECTOR_SIZE);
            }
            break;

         case 1:  /* write */
            CopyFromZ80(Buf,pBuf,CPM_SECTOR_SIZE);
            if((Err = f_write(fp,Buf,CPM_SECTOR_SIZE,&Wrote)) != FR_OK) {
               ELOG("\nf_write failed: %d\n",Err);
               status = 6;
            }
            else if(Wrote != CPM_SECTOR_SIZE) {
               ELOG("\nShort write failure, wrote %d, requested %d\n",Wrote,
                    CPM_SECTOR_SIZE);
               status = 6;
            }
            break;

         default:    /* illegal command */
            ELOG("\nInvalid command 0x%x\n",Data);
            status = 7;
            break;
      }
   } while(false);

   gDiskStatus = status;
   if(status != 0) {
      ELOG("\n%s command failed, Disk %c T:%d, S:%d, status: %d\n",
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
            break;
         }
      }

      if(FormatLookup[i].Desc == NULL) {
         LOG("Couldn't mount '%s', disk format not supported\n");
         break;
      }

      if((Err = f_open(Fp,Filename,FA_READ | FA_WRITE)) != FR_OK) {
         ELOG("Couldn't open %s, %d\n",Filename,Err);
         break;
      }
      gMountedDrives++;
      LOG("Mounted %s image on %c:\n",FormatLookup[i].Desc,'A' + Drive);
      gDisks[Drive].fp = Fp;
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
   uint8_t *pBuf = (uint8_t *) Z80_MEMORY_BASE;
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
   FIL *fp = gDisks[0].fp;
   uint8_t Buf[CPM_SECTOR_SIZE];
   UINT Read;
   void *pBuf = (void *) Z80_MEMORY_BASE;
   FRESULT Err;

   do {
      if(fp == NULL) {
         ELOG("Couldn't read boot sector, drive A: not mounted\n");
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
      LOG("Boot sector:\n");
      LOG_HEX(Buf,CPM_SECTOR_SIZE);
      CopyToZ80(pBuf,Buf,CPM_SECTOR_SIZE);
   } while(false);
}

void UartPutc(char c)
{
   uart = (uint32_t) c;
}

void LogPutc(char c,void *arg)
{
   int LogFlags = *((char *) arg);

   if(LogFlags & LOG_SERIAL) {
      UartPutc(c);
   }

   if(LogFlags & LOG_MONITOR) {
      PrintfPutc(c);
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

