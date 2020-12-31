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
#include "string.h"
#include "misc.h"
#include "usb.h"
#include "ff.h"
#include "cpm_io.h"
#include "vt100.h"
#include "rtc.h"
#include "picorv32.h"

// #define LOG_TO_SERIAL
// #define LOG_TO_BOTH
// #define DEBUG_LOGGING
// #define VERBOSE_DEBUG_LOGGING
#include "log.h"

DWORD gBootImageLen;

#define ANSI_HOME             "\033[H"
#define ANSI_CLS              "\033[2J"

#define F_CAPS_REMAP_TOGGLE   5  // F5
#define F_SCREEN_COLOR        6  // F6
#define F_RESET_Z80           7  // F7
#define F_VERBOSE_LOG_TOGGLE  8  // F8
unsigned char gFunctionRequest;

void LoadInitProg(void);
void HandleFunctionKey(int Function);
uint32_t gTicker;

const char *RegNames[] = {
   "ra","sp","gp","tp","t0","t1","t2","s0","s1","a0","a1",
   "a2","a3","a4","a5","a6","a7","s2","s3","s4","s5","s6","s7","s8",
   "s9","s10","s11","t3","t4","t5","t6"
};

void irq_handler(uint32_t *Regs,uint32_t IRQs) 
{
   static uint32_t LastTicks;
   int i;

   if(IRQs & IRQ_TIMER) {
   // Timer interrupt, update RTC
      picorv32_timer(CPU_HZ/TICKER_PER_SEC);
      gTicker++;
   }
   if(IRQs & IRQ_BUS_FAULT) {
      uint32_t pc = (Regs[0] & 1) ? Regs[0] - 3 : Regs[0] - 4;
      ELOG("Bus fault, IRQs: 0x%x, pc: 0x%08x\n",IRQs,pc);
      LEDS = LED_RED;
      for(i = 1; i < 32; i++) {
         ELOG_R("%s: 0x%x\n",RegNames[i-1],Regs[i]);
      }
      while(1);
   }
   if(IRQs & IRQ_USB) {
      uint32_t Elapsed = ticks() - LastTicks;
#if 0
      ELOG("Ext Int1, IRQs: 0x%x, pc: 0x%08x, Elapsed: %u\n",IRQs,Regs[0],
           Elapsed);
#endif
      isp_isr();
      LastTicks = ticks();
   }
}

void main() 
{
   FATFS FatFs;           /* File system object for each logical drive */
   DIR Dir;               /* Directory object */
   FILINFO Finfo;
   FRESULT res;
   const char root[] = "USB:/";
   char directory[18] = "";
   char DriveSave;
   uint8_t LastIoState = 0xff;
   uint32_t IoState;
   uint32_t Timeout = 0;
   bool bWasHalted = false;
   
   DLY_TAP = 0x03;

// Enable all Bus fault an instruction fault interrupts
   picorv32_maskirq(~(IRQ_BUS_FAULT | IRQ_EBREAK));

   vt100_init();
   ALOG_R("Pano Logic G1, Z80 @ 25 Mhz, PicoRV32 @ 25MHz\n");
   ALOG_R("Compiled " __DATE__ " " __TIME__ "\n\n");

// Start heartbeat interrupt
   picorv32_timer(CPU_HZ/TICKER_PER_SEC);

   gCapsLockSwap = 1;
// Enable USB interrupts
   picorv32_maskirq(~(IRQ_USB | IRQ_BUS_FAULT | IRQ_EBREAK));
   usb_init();
   drv_usb_kbd_init();

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
         if(strcmp(Finfo.fname,INIT_IMAGE_FILENAME) == 0) {
         }
      }
      f_closedir(&Dir);
   } while(false);

   MountCpmDrives();
   LoadInitProg();
   z80_con_status = 0;  // No console input yet

   LOG("Releasing Z80 reset\n");
   z80_rst = 0;   // release Z80 reset

#if 0
   LOG("Disabling serial port\n");
   term_enable_uart(false);
#endif

   time_t t = 0;
   while (1) {
      char line[40];
      ALOG_R("Time and date (MM/DD/YY HH:MM:SS) or <Enter> for no RTC: ");
      readline(line, 40);
      ALOG_R("\n");
      if (strnlen(line, 40) == 0) {
         ALOG_R("Continuing with no RTC\n");
         break;
      }
      t = dateparse(line, 40);
      if (t != 0) {
         rtc_init(t);
         break;
      }
   }

   for( ; ; ) {
      IdlePoll();

      do {
         IoState = z80_io_state;

         if(LastIoState != IoState) {
            LastIoState = IoState;
            VLOG("z80_io_state: %d\n",IoState);
            if((IoState & IO_STAT_HALTED) && !bWasHalted) {
               bWasHalted = true;
               LOG("Z80 HALTED\n");
               DisplayString("Z80 HALTED",29,0);
            }
            else if((IoState & IO_STAT_HALTED) == 0 && bWasHalted) {
               bWasHalted = false;
               DisplayString("          ",29,0);
            }
         }
         
         switch((IoState & IO_STATE_MASK)) {
            case IO_STAT_WRITE:  // Z80 out
               HandleIoOut(z80_io_adr,z80_out_data);
               if(Timeout == 0) {
               // Give the z80 a chance to output another character before
               // we break out of this loop and call usb_event_poll again...
                  Timeout = ticks_ms() + 50;
               }
               break;

            case IO_STAT_READ:   // z80 In
               HandleIoIn(z80_io_adr);
               break;

            case IO_STAT_IDLE:
            case IO_STAT_READY:
               break;

            default:
               LOG("IoState 0x%x\n",IoState);
               break;
         }
      } while(ticks_ms() < Timeout && gFunctionRequest == 0);
      Timeout = 0;

      if(gZ80_ResetRequest) {
         gZ80_ResetRequest = 0;
         z80_rst = 1;
         LoadInitProg();
         ALOG_R(ANSI_HOME ANSI_CLS "Resetting Z80\n");
         z80_rst = 0;
      }
   }

   LEDS = LED_BLUE;  // "blue screen of death"
   while(1);
}

void FunctionKeyCB(unsigned char Function)
{
   VLOG("F%d key pressed\n",Function);
   gFunctionRequest = Function;
}

void HandleFunctionKey(int Function)
{
   switch(Function) {
      case F_CAPS_REMAP_TOGGLE:
      // toggle swapping of caps lock and control key
         LOG("%swapping CAPS lock and control key\n",
             gCapsLockSwap ? "Not s" : "S");
         if(gCapsLockSwap) {
            gCapsLockSwap = 0;
         }
         else {
            gCapsLockSwap = 1;
         }
         break;

      case F_SCREEN_COLOR:
      // toggle green screen
         if(font_fg_color == GREEN) {
            font_fg_color = WHITE;
            font_bg_color = BLACK;
         }
         else if(font_fg_color == WHITE) {
            font_fg_color = BLACK;
            font_bg_color = WHITE;
         }
         else {
            font_fg_color = GREEN;
            font_bg_color = BLACK;
         }
         break;

      case F_RESET_Z80:
      // reset Z80
         gZ80_ResetRequest = 1;
         break;

      case F_VERBOSE_LOG_TOGGLE:
         LOG("z80_io_state: %d, z80_io_adr: %d\n",z80_io_state,z80_io_adr);
         break;
   }
}

void LoadInitProg()
{
   bool BootImageLoaded = false;
   if(gBootImageLen > 0) {
      LOG("Calling LoadImage\n");
      if(LoadImage(INIT_IMAGE_FILENAME,gBootImageLen) == 0) {
         BootImageLoaded = true;
      }
   }

   if(!BootImageLoaded) {
      LOG("Loading default Z80 boot image\n");
      LoadDefaultBoot();
   }
}

void IdlePoll()
{
   usb_event_poll();
   if(gWriteFlushTimeout != 0 && ticks_ms() >= gWriteFlushTimeout) {
      gWriteFlushTimeout = 0;
      FlushWriteCache();
   }
   if(gFunctionRequest != 0) {
      HandleFunctionKey(gFunctionRequest);
      gFunctionRequest = 0;
   }
}

/* 
 * Local Variables:
 * c-basic-offset: 3
 * End:
 */
