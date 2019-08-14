/*
 *  Pano_z80pack
 *
 *  Copyright (C) 2019  Skip Hansen
 * 
 *  This file is derived from Verilogboy project:
 *  Copyright (C) 2019  Wenting Zhang <zephray@outlook.com>
 *
 *  This file is partially derived from PicoRV32 project:
 *  Copyright (C) 2017  Clifford Wolf <clifford@clifford.at>
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
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "misc.h"
#include "term.h"
#include "part.h"
#include "usb.h"
#include "ff.h"
#include "cpm_io.h"

#define DEBUG_LOGGING
#define VERBOSE_DEBUG_LOGGING
#include "log.h"

#define dly_tap *((volatile uint32_t *)0x03000000)
#define led_grn *((volatile uint32_t *)0x03000004)
#define led_red *((volatile uint32_t *)0x03000008)
#define vb_key  *((volatile uint32_t *)0x03000010)
#define vb_rst  *((volatile uint32_t *)0x0300000c)

void irq_handler(uint32_t pc) {
   term_print_string("HARD FAULT PC = ");
   term_print_hex(pc, 8);
   while(1);
}

void main() 
{
   FATFS FatFs;           /* File system object for each logical drive */
   FIL File[2];           /* File objects */
   DIR Dir;               /* Directory object */
   FILINFO Finfo;
   FRESULT res;
   int result;
   const char root[] = "USB:/";
   char directory[18] = "";
   char filename[32] = "0:/";
   char DriveSave;

   dly_tap = 0x03;
   led_red = 0;
   led_grn = 1;

   // Set interrupt mask to zero (enable all interrupts)
   // This is a PicoRV32 custom instruction 
   asm(".word 0x0600000b");

   term_clear();
   term_goto(0,0);
   ALOG_R("Pano Logic G1, PicoRV32 @ 25MHz, LPDDR @ 100MHz\n");
   ALOG_R("Compiled " __DATE__ " " __TIME__ "\n");
   usb_init();
   term_clear();

   term_enable_uart(true);

   do {
      // Main loop
      res = f_mount(&FatFs, "", 1);
      if(res != FR_OK) {
         ELOG("Unable to mount filesystem: %d\n", (int)res);
         break;
      }

      LOG("Current directory: %s%s\n", root, directory);

      // First list all files
      res = f_opendir(&Dir, directory);
      if(res != FR_OK) {
         ELOG("Unable to open directory: %d\n", (int)res);
         break;
      }
      for(;;) {
         res = f_readdir(&Dir, &Finfo);
         if((res != FR_OK) || !Finfo.fname[0]) {
            break;
         }

         LOG_R("%-12s ", Finfo.fname);
         LOG_R("%7d ", Finfo.fsize);
         LOG_R("%c%c%c%c%c ",
                (Finfo.fattrib & AM_DIR) ? 'D' : '-',
                (Finfo.fattrib & AM_RDO) ? 'R' : '-',
                (Finfo.fattrib & AM_HID) ? 'H' : '-',
                (Finfo.fattrib & AM_SYS) ? 'S' : '-',
                (Finfo.fattrib & AM_ARC) ? 'A' : '-');
         LOG_R("%2d/%02d/%d %2d:%02d:%02d\n",
                (Finfo.fdate >> 5) & 0xf,
                (Finfo.fdate & 31),
                (Finfo.fdate >> 9) + 1980,
                (Finfo.ftime >> 11),
                (Finfo.ftime >> 5) & 0x3f);

      // This is a kludge to avoid strncmp
         DriveSave = Finfo.fname[5];
         Finfo.fname[5] = 'A';
         if(strcmp(Finfo.fname,"DRIVEA.DSK") == 0) {
            LOG("Calling MountCpmDrive\n");
            Finfo.fname[5] = DriveSave;
            MountCpmDrive(Finfo.fname,Finfo.fsize);
         }
      }
      ALOG_R("%d CP/M drives mounted\n",gMountedDrives);
   } while(false);

   led_red = 1;
   while(1);
}
