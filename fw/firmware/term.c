/*
 *  VerilogBoy
 *
 *  Copyright (C) 2019  Wenting Zhang <zephray@outlook.com>
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
#include <stdarg.h>
#include <string.h>

#include "term.h"
#include "misc.h"

#define SCREEN_X    80
#define SCREEN_Y    30

#define vram ((volatile uint32_t *)0x08000000)
#define uart ((volatile uint32_t *)0x03000100)

volatile uint32_t *uart_ptr = uart;

bool uart_en = true;
int term_x = 0;
int term_y = 0;

void term_goto(uint8_t x, uint8_t y) {
   term_x = x;
   term_y = y;
}                             

char Buf[SCREEN_X * SCREEN_Y];

#if 0
void term_newline() {
   volatile uint32_t *vram_ptr;

   term_x = 0;
   if(term_y == 30 - 1) {
      term_y = 0;
   }
   else {
      term_y ++;
   }
   // Clear next line
   vram_ptr = vram + term_y * 80 + term_x;
   for(int i = 0; i < 80; i++) {
      *vram_ptr++ = 0x20;
   }
}
#else
void term_newline() {
   char *cp;
   uint32_t *pFrom;
   uint32_t *pTo;
   int Len;

   term_x = 0;
   if(term_y == (SCREEN_Y - 1)) {
   // Scroll the screen
      Len = (SCREEN_X * (SCREEN_Y - 1)) / 4;
      pTo = (uint32_t *) Buf;
      pFrom = (uint32_t *) &Buf[SCREEN_X];
      while(Len-- > 0) {
         *pTo++ = *pFrom++;
      }

      Len = SCREEN_X * (SCREEN_Y - 1);

      pTo = vram;
      cp = Buf;
      while(Len-- > 0) {
         *pTo++ = *cp++;
      }
   }
   else {
      term_y++;
   }
   // Clear next line
   memset(vram + (term_y * SCREEN_X),' ',SCREEN_X*4);
   memset(Buf + (term_y * SCREEN_X),' ',SCREEN_X);
}
#endif

void term_clear() {
   memset(vram,' ',SCREEN_X * SCREEN_Y);
   memset(Buf,' ',SCREEN_X * SCREEN_Y);
}

void term_enable_uart(bool en) {
   uart_en = en;
}

void term_putchar(char c) {
   volatile uint32_t *vram_ptr;
   int Offset = (term_y * SCREEN_X) + term_x;

   if(uart_en) {
      *uart_ptr = (uint32_t)c;
   }

   if(c == '\r') {
   // Ignore carrage returns
   }
   else if(c == '\n') {
      term_newline();
   }
   else if(c == '\b') {
      if(term_x > 0) {
         term_x--;
      }
   }
   else {
      vram[Offset] = (uint32_t) c;
      Buf[Offset] = c;
      if(term_x == SCREEN_X - 1) {
         term_newline();
      }
      else {
         term_x++;
      }
   }
   //delay_us(200);
}

void term_print_string(const char *p) {
   while((*p) && (*p != 0xFF))
      term_putchar(*p++);
}

void term_print_hex(uint32_t v, int digits) {
   for(int i = 7; i >= 0; i--) {
      char c = "0123456789abcdef"[(v >> (4*i)) & 15];
      if(c == '0' && i >= digits) continue;
      term_putchar(c);
      digits = i;
   }
}


