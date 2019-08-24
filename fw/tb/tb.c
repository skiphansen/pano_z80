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
#include <string.h>
#include "ff.h"
#include "cpm_io.h"

void irq_handler(uint32_t pc) {
   while(1);
}

void main() 
{
   uint8_t Dummy;

   z80_con_status = 0x12;
   z80_drive = 0x55;
   Dummy = z80_drive;
   z80_track = Dummy;

   for( ; ; ) {
      switch(z80_io_state) {
         case IO_STAT_WRITE:  // Z80 out
            Dummy = z80_out_data;
            break;

         case IO_STAT_READ:   // z80 In
            z80_in_data = 0xaa;
            break;
      }
   }
}
