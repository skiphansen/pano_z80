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
#ifndef __MISC_H__
#define __MISC_H__

#include <stdint.h>

#define true 1
#define false 0

#define CYCLE_PER_US  25

// ticks wraps around every 43s
uint32_t time();
uint32_t ticks_us();
uint32_t ticks_ms();
void delay_us(uint32_t us);
void delay_ms(uint32_t ms);
void delay_loop(uint32_t t);

extern long insn();


void * memscan(void * addr, int c, uint32_t size);
// This is here because including <ctype.h> defines isprint as a macro which
// references __locale_ctype_ptr.  So we can't use the standard header
int isprint(int c);

#endif
