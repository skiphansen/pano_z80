/*
 *  cpm_io.h
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
#ifndef _CPM_IO_H_
#define _CPM_IO_H_

#define Z80_INTERFACE(x)   *((volatile uint8_t *)(0x03000200 + (x << 2)))
#define z80_con_status  Z80_INTERFACE(0)
#define z80_drive       Z80_INTERFACE(1)
#define z80_track       Z80_INTERFACE(2)
#define z80_sector_lsb  Z80_INTERFACE(3)
#define z80_disk_status Z80_INTERFACE(4)
#define z80_dma_lsb     Z80_INTERFACE(5)
#define z80_dma_msb     Z80_INTERFACE(6)
#define z80_sector_msb  Z80_INTERFACE(7)
#define z80_io_adr      Z80_INTERFACE(8)  // Data input to Z80
#define z80_out_data    Z80_INTERFACE(9)  // Data output from Z80
#define z80_in_data     Z80_INTERFACE(10)
#define z80_io_state    Z80_INTERFACE(11)

#define IO_STAT_IDLE    0
#define IO_STAT_WRITE   1
#define IO_STAT_READ    2

extern int gMountedDrives;

int MountCpmDrive(char *Filename,FSIZE_t ImageSize);
int LoadImage(const char *Filename,FSIZE_t Len);
void HandleIoIn(uint8_t IoPort);
void HandleIoOut(uint8_t IoPort,uint8_t Data);
void Z80MemTest(void);
void Z80IoTest(void);
void LoadDefaultBoot(void);

#endif // _CPM_IO_H_

