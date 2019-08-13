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

   dly_tap = 0x03;
   led_red = 0;
   led_grn = 1;

   // Set interrupt mask to zero (enable all interrupts)
   // This is a PicoRV32 custom instruction 
   asm(".word 0x0600000b");

   term_clear();
   term_goto(0,0);
   printf("Pano Logic G1, PicoRV32 @ 25MHz, LPDDR @ 100MHz\n");
   printf("Compiled " __DATE__ " " __TIME__ "\n");
   usb_init();
   term_clear();

   term_enable_uart(true);



   while(1) {
      // Main loop
      res = f_mount(&FatFs, "", 1);
      if(res != FR_OK) {
         printf("Unable to mount filesystem: %d\n", (int)res);
         break;
      }

      // Clear screen
      term_goto(8, 5);
      printf("Current directory: %s%s", root, directory);
      term_goto(12, 7);
      printf("Filename");
      term_goto(28, 7);
      printf("Size");
      term_goto(40, 7);
      printf("Attrib");
      term_goto(52, 7);
      printf("Last modified");

      // First list all files
      res = f_opendir(&Dir, directory);
      if(res != FR_OK) {
         printf("Unable to open directory: %d\n", (int)res);
         break;
      }
      uint32_t filecount = 0;
      uint32_t dircount = 0;
      uint32_t line = 8;
      for(;;) {
         res = f_readdir(&Dir, &Finfo);
         if((res != FR_OK) || !Finfo.fname[0]) break;
         if(Finfo.fattrib & AM_DIR) {
            dircount++;
         }
         else {
            filecount++;
         }

         term_goto(12, line);
         printf("%s", Finfo.fname);
         term_goto(28, line);
         printf("%d", Finfo.fsize);
         term_goto(40, line);
         printf("%c%c%c%c%c",
                (Finfo.fattrib & AM_DIR) ? 'D' : '-',
                (Finfo.fattrib & AM_RDO) ? 'R' : '-',
                (Finfo.fattrib & AM_HID) ? 'H' : '-',
                (Finfo.fattrib & AM_SYS) ? 'S' : '-',
                (Finfo.fattrib & AM_ARC) ? 'A' : '-');
         term_goto(52, line);
         printf("0000/00/00 00:00");
         term_goto(52, line);
         printf("%d", (Finfo.fdate >> 9) + 1980);
         term_goto(57, line);
         printf("%d", (Finfo.fdate >> 5) & 15);
         term_goto(60, line);
         printf("%d", (Finfo.fdate & 31));
         term_goto(63, line);
         printf("%d", (Finfo.ftime >> 11));
         term_goto(66, line);
         printf("%d", (Finfo.ftime >> 5) & 63);
         line++;
      }
      printf("\nDirectory listing complete\n");
      break;
   }

   led_red = 1;
   while(1);
}
