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

#define MAX_MOUNTED_DRIVES    6
#define MAX_LOGICAL_DRIVES    16 // A: -> P: 

#define DLY_TAP_ADR        0x03000000
#define LEDS_ADR           0x03000004
#define Z80_RST_ADR        0x0300000c
#define UART_ADR           0x03000100
#define Z80_MEMORY_ADR     0x05000000
#define VRAM_ADR           0x08000000

#define VRAM              *((volatile uint32_t *)VRAM_ADR)
#define dly_tap           *((volatile uint32_t *)DLY_TAP_ADR)
#define leds              *((volatile uint32_t *)LEDS_ADR)
#define LED_RED            0x1
#define LED_GREEN          0x2
#define LED_BLUE           0x4

#define z80_rst           *((volatile uint32_t *)Z80_RST_ADR)
#define uart              *((volatile uint32_t *)UART_ADR)

#define Z80_INTERFACE(x)   *((volatile uint8_t *)(0x03000200 + x ))
#define IO_INTERFACE(x)    *((volatile uint32_t *)(0x03000200 + x ))
#define z80_con_status     Z80_INTERFACE(0x0)
#define z80_drive          Z80_INTERFACE(0x4)
#define z80_track          Z80_INTERFACE(0x8)
#define z80_sector_lsb     Z80_INTERFACE(0xc)
#define z80_dma_lsb        Z80_INTERFACE(0x14)
#define z80_dma_msb        Z80_INTERFACE(0x18)
#define z80_sector_msb     Z80_INTERFACE(0x1c)
#define z80_io_adr         Z80_INTERFACE(0x20)  // I/O address of current in or out
#define z80_out_data       Z80_INTERFACE(0x24)  // Data output from Z80
#define z80_in_data        Z80_INTERFACE(0x28)  // Data input to Z80
#define z80_io_state       IO_INTERFACE(0x2c)
#define font_fg_color      IO_INTERFACE(0x30)
#define font_bg_color      IO_INTERFACE(0x34)

#define IO_STAT_IDLE    0
#define IO_STAT_WRITE   1
#define IO_STAT_READ    2
#define IO_STAT_READY   3
#define IO_STATE_MASK   0x7
#define IO_STAT_HALTED  0x800000

#define BLACK           0
#define WHITE           0xffffff
#define GREEN           0x00ff00

#define ANSI_CLS        "\033[2J"
#define INIT_IMAGE_FILENAME   "BOOT.IMG"

typedef enum {
   MAP_ERROR = -1,
   MAP_NONE,
   MAP_Z80PACK,
   MAP_MULTICOMP,
   MAP_DUAL,
} MapMode;

extern int gMountedDrives;
extern unsigned char gFunctionRequest;
extern uint32_t gWriteFlushTimeout;
extern DWORD gBootImageLen;
extern FIL *gSystemFp;
extern MapMode gMountMode;

int MountCpmDrives();
int LoadImage(const char *Filename,FSIZE_t Len);
void HandleIoIn(uint8_t IoPort);
void HandleIoOut(uint8_t IoPort,uint8_t Data);
void Z80MemTest(void);
void LoadDefaultBoot(void);
void UartPutc(char c);
void PrintfPutc(char c);
void FlushWriteCache(void);
void IdlePoll(void);
void DisplayString(const char *Msg,int Row,int Col);
#endif // _CPM_IO_H_

