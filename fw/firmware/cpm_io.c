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

// #include <stdio.h>
// #include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "ff.h"
#include "cpm_io.h"
#include "term.h"
#include "usb.h"

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
uint8_t gDrive;
uint8_t gTrack;
uint16_t gSector;
uint16_t gDmaAdr;

static void fdco_out(uint8_t Data);
void CopyToZ80(uint8_t *pTo,uint8_t *pFrom,int Len);
void CopyFromZ80(uint8_t *pTo,uint8_t *pFrom,int Len);

void CopyToZ80(uint8_t *pTo,uint8_t *pFrom,int Len)
{
   LOG("Copying %d bytes from 0x%x to 0x%x\n",Len,(unsigned int) pFrom,
       (unsigned int) pTo);

   while(Len -- > 0) {
      *pTo = *pFrom++;
      pTo += 4;
   }
}

void CopyFromZ80(uint8_t *pTo,uint8_t *pFrom,int Len)
{
   LOG("Copying %d bytes from 0x%x to 0x%x\n",Len,(unsigned int) pFrom,
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
   uint8_t Data;

   switch(IoPort) {
      case 0:  
      // console status, 0xff: input available, 0x00: no input available
         Data = usb_kbd_testc() ? 0 : 0xff;
         break;

      case 1:  // console data
         if(usb_kbd_testc()) {
            Data = (uint8_t) usb_kbd_getc();
         }
         else {
            Data = 0xff;
         }
         z80_in_data = Data;
         VLOG("0x%x <- 0x%x\n",IoPort,Data);
         break;

// The following are implemented in hardware so we should never see them here
      case 10: // FDC drive
      case 11: // FDC track
      case 12: // FDC sector (low)
      case 14: // FDC status
      case 15: // DMA destination address low
      case 16: // DMA destination address high
      case 17: // FDC sector high
// We don't expect these ports to be read
      case 13: // FDC command
         ELOG("Unexpected input from port 0x%x\n",IoPort);
         z80_in_data = 0;
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
         ELOG("Input from port 0x%x ignored\n",IoPort);
         z80_in_data = 0;
         break;
   }
}

// This routine is called when the Z80 performs an IO write operation
void HandleIoOut(uint8_t IoPort,uint8_t Data)
{
   uint8_t Ret;

   switch(IoPort) {
      case 1:  // console data
         VLOG("0x%x -> 0x%x\n",IoPort,Data);
         term_putchar(Data);
         break;

      case 13: // FDC command
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
   void *pBuf = (void *) ((gDmaAdr << 2) + Z80_MEMORY_BASE);
   FIL *fp = gDisks[gDrive].fp;
   FRESULT Err;
   UINT Wrote;
   UINT Read;
   uint8_t Buf[CPM_SECTOR_SIZE];

   do {
      if(fp == NULL) {
         ELOG("Invalid drive %d\n",gDrive);
         status = 1;
         break;
      }
      if(gTrack > gDisks[gDrive].tracks) {
         ELOG("Invalid track %d\n",gTrack);
         status = 2;
         break;
      }
      if(gSector > gDisks[gDrive].sectors) {
         ELOG("Invalid sector %d\n",gSector);
         status = 3;
         break;
      }
      pos = (((long)gTrack) * ((long)gDisks[gDrive].sectors) + gSector - 1) << 7;
      if((Err = f_lseek(fp,pos)) != FR_OK) {
         ELOG("f_lseek failed: %d\n",Err);
         status = 4;
         break;
      }

      switch(Data) {
         case 0:  /* read */
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
            break;

         case 1:  /* write */
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
            break;

         default:    /* illegal command */
            ELOG("Invalid command 0x%x\n",Data);
            status = 7;
            break;
      }
   } while(false);

   gDiskStatus = status;
   if(status != 0) {
      ELOG("%s command failed, status %d\n",Data == 0 ? "Read" : "Write",status);
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
         ELOG("Can't mount '%s', %d drives are already mounted\n",
              gMountedDrives + 1);
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

void Z80MemTest()
{
   uint8_t *pBuf = (uint8_t *) Z80_MEMORY_BASE;
   uint8_t Buf[CPM_SECTOR_SIZE];
   uint8_t i;

   LOG("Reading Z80 memory\n");

   for(i = 0; i < 16; i++) {
      LOG_R("%02x ",pBuf[i*4]);
      pBuf[i * 4] = (uint8_t) i;
   }
   LOG_R("\n");
   for(i = 0; i < 16; i++) {
     LOG_R("%02x ",pBuf[i * 4]);
   }
   LOG_R("\n");


   for(i = 0; i < CPM_SECTOR_SIZE; i++) {
      Buf[i] = i;
   }
   CopyToZ80(pBuf,Buf,sizeof(Buf));
   memset(Buf,0x55,sizeof(Buf));
   CopyFromZ80(Buf,pBuf,sizeof(Buf));
   for(i = 0; i < CPM_SECTOR_SIZE; i++) {
      if(Buf[i] != i) {
         LOG("Failure @ %d, read: %d\n",i,Buf[i]);
         break;
      }
   }

   if(i == CPM_SECTOR_SIZE) {
      LOG("Mem test passed\n");
   }
}

void Z80IoTest()
{
   uint32_t Data;
   LOG("Write 0x55 to z80_drive\n");
   z80_drive = 0x55;
   LOG("Write 0xaa to z80_track\n");
   z80_track = 0xaa;

   LOG("Write 0x1 to z80_sector_lsb\n");
   z80_sector_lsb = 0x1;

   LOG("Write 0x2 to z80_disk_status\n");
   z80_disk_status = 0x2;

   LOG("Write 0x4 to z80_dma_lsb\n");
   z80_dma_lsb = 0x4;

   LOG("Write 0x8 to z80_dma_msb\n");
   z80_dma_msb = 0x8;

   LOG("Write 0x10 to z80_sector_msb\n");
   z80_sector_msb = 0x10;

   LOG("z80_drive: 0x%x\n",z80_drive);
   LOG("z80_track: 0x%x\n",z80_track);
   LOG("z80_sector_lsb: 0x%x\n",z80_sector_lsb);
   LOG("z80_disk_status: 0x%x\n",z80_disk_status);
   LOG("z80_dma_lsb: 0x%x\n",z80_dma_lsb);
   LOG("z80_dma_msb: 0x%x\n",z80_dma_msb);
   LOG("z80_sector_msb: 0x%x\n",z80_sector_msb);
}


#include "z80_boot.h"

void LoadDefaultBoot()
{
   uint8_t Buf[16];
   uint8_t i;
   int BytesRead = 0;
   int Bytes2Read;
   uint8_t *p = (uint8_t *) Z80_MEMORY_BASE;

   CopyToZ80(p,z80_boot_img,sizeof(z80_boot_img));

   while(BytesRead < sizeof(z80_boot_img)) {
      Bytes2Read = sizeof(z80_boot_img) - BytesRead;
      if(Bytes2Read > sizeof(Buf)) {
         Bytes2Read = sizeof(Buf);
      }
      CopyFromZ80(Buf,p,Bytes2Read);
      for(i = 0; i < Bytes2Read; i++) {
         if(Buf[i] != z80_boot_img[BytesRead + i]) {
            ELOG("Verify error, data at 0x%x is 0x%x, should be 0x%x\n",
                 BytesRead + i,Buf[i],z80_boot_img[BytesRead + i]);
            BytesRead = sizeof(z80_boot_img);
            break;
         }
      }
      BytesRead += Bytes2Read;
      p += Bytes2Read << 2;
   }
}

