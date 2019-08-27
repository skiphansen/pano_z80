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

#define SCREEN_X    80
#define SCREEN_Y    30

#define VRAM_ADR     0x08000000
#define DLY_TAP_ADR  0x03000000
#define LED_GRN_ADR  0x03000004
#define LED_RED_ADR  0x03000008
#define Z80_RST_ADR  0x0300000c
#define UART_ADR     0x03000100

#define VRAM    *((volatile uint32_t *)VRAM_ADR)
#define dly_tap *((volatile uint32_t *)DLY_TAP_ADR)
#define led_grn *((volatile uint32_t *)LED_GRN_ADR)
#define led_red *((volatile uint32_t *)LED_RED_ADR)
#define z80_rst *((volatile uint32_t *)Z80_RST_ADR)
#define uart    *((volatile uint32_t *)UART_ADR)

#define Z80_INTERFACE(x)   *((volatile uint8_t *)(0x03000200 + x ))
#define z80_con_status  Z80_INTERFACE(0x0)
#define z80_drive       Z80_INTERFACE(0x4)
#define z80_track       Z80_INTERFACE(0x8)
#define z80_sector_lsb  Z80_INTERFACE(0xc)
#define z80_dma_lsb     Z80_INTERFACE(0x14)
#define z80_dma_msb     Z80_INTERFACE(0x18)
#define z80_sector_msb  Z80_INTERFACE(0x1c)
#define z80_io_adr      Z80_INTERFACE(0x20)  // I/O address of current in or out
#define z80_out_data    Z80_INTERFACE(0x24)  // Data output from Z80
#define z80_in_data     Z80_INTERFACE(0x28)  // Data input to Z80
#define z80_io_state    Z80_INTERFACE(0x2c)

#define IO_STAT_IDLE    0
#define IO_STAT_WRITE   1
#define IO_STAT_READ    2
#define IO_STAT_READY   3

extern int gMountedDrives;

int MountCpmDrive(char *Filename,FSIZE_t ImageSize);
int LoadImage(const char *Filename,FSIZE_t Len);
void HandleIoIn(uint8_t IoPort);
void HandleIoOut(uint8_t IoPort,uint8_t Data);
void Z80MemTest(void);
void LoadDefaultBoot(void);
void UartPutc(char c);
#endif // _CPM_IO_H_

