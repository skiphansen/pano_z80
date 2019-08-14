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

#include "ff.h"
#include "cpm_io.h"

#define DEBUG_LOGGING
#define VERBOSE_DEBUG_LOGGING
#include "log.h"

#define CPM_SECTOR_SIZE       128
#define MAX_MOUNTED_DRIVES    4
#define MAX_LOGICAL_DRIVES    16 // A: -> P: 
#define Z80_MEMORY_BASE       0

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

// This routine is called when the Z80 performs an IO read operation
void HandleIoIn(uint8_t IoPort)
{
   uint8_t Ret;
   uint8_t Data;

   switch(IoPort) {
      case 0:  // console status
         break;

      case 1:  // console data
         break;
               
      case 10: // FDC drive
         Data = gDrive;
         break;

      case 11: // FDC track
         Data = gTrack;
         break;

      case 12: // FDC sector (low)
         Data = (uint8_t) (gSector & 0xff);
         break;

      case 13: // FDC command
         break;

      case 14: // FDC status
         Data = gDiskStatus;
         break;

      case 15: // DMA destination address low
         Data = (uint8_t) (gDmaAdr & 0xff);
         break;

      case 16: // DMA destination address high
         Data = (uint8_t) ((gDmaAdr  >> 8) & 0xff);
         break;

      case 17: // FDC sector high
         Data = (uint8_t) ((gSector >> 8) & 0xff);
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
         break;
   }
}

// This routine is called when the Z80 performs an IO write operation
void HandleIoOut(uint8_t IoPort,uint8_t Data)
{
   uint8_t Ret;

   switch(IoPort) {
      case 0:  // console status
         break;

      case 1:  // console data
         break;

      case 10: // FDC drive
         gDrive = Data;
         break;

      case 11: // FDC track
         gTrack = Data;
         break;

      case 12: // FDC sector (low)
         gSector = (gSector & 0xff00) | Data;
         break;

      case 13: // FDC command
         fdco_out(Data);
         break;

      case 14: // FDC status
         break;

      case 15: // DMA destination address low
         gDmaAdr = (gDmaAdr & 0xff00) | Data;
         break;

      case 16: // DMA destination address high
         gDmaAdr = (gDmaAdr & 0xff) | (Data << 8);
         break;

      case 17: // FDC sector high
         gSector = (gSector & 0xff) | (Data << 8);
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
   void *pBuf = (void *) (gDmaAdr + Z80_MEMORY_BASE);
   FIL *fp = gDisks[gDrive].fp;
   FRESULT Err;
   UINT Wrote;
   UINT Read;

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
            if((Err = f_read(fp,pBuf,CPM_SECTOR_SIZE,&Read)) != FR_OK) {
               ELOG("f_read failed: %d\n",Err);
               status = 5;
            }
            else if(Wrote != CPM_SECTOR_SIZE) {
               ELOG("Short read failure, read%d, requested %d\n",Read,
                    CPM_SECTOR_SIZE);
               status = 5;
            }
            break;

         case 1:  /* write */
            if((Err = f_write(fp,pBuf,CPM_SECTOR_SIZE,&Wrote)) != FR_OK) {
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
