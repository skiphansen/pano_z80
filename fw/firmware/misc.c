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
#include <limits.h>
#include <stdio.h>
#include "misc.h"
#include "string.h"
#include "log.h"
#include "time.h"

long insn()
{
   int insns;
   asm volatile ("rdinstret %0" : "=r"(insns));
   // printf("[insn() -> %d]", insns);
   return insns;
}

uint32_t ticks() {
	uint32_t cycles;
	asm volatile ("rdcycle %0" : "=r"(cycles));
	return cycles;
}

uint32_t ticks_us() {
    return ticks() / CYCLE_PER_US;
}

uint32_t ticks_ms() {
    return ticks() / CYCLE_PER_US / 1000;
}

void delay_us(uint32_t us) {
   uint32_t elapsed;
   uint32_t start = ticks(); 
    do {
       uint32_t curr = ticks();
       if (curr > start)
          elapsed = curr - start;
       else
          elapsed = (UINT_MAX - start) + 1 + curr;
    } while (elapsed < CYCLE_PER_US * us);
}

void delay_ms(uint32_t ms) {
    while (ms--) { delay_us(1000); }
}

void delay_loop(uint32_t t) {
	volatile int i;
	while(t--) {
		for (i=0;i<20;i++);
	}
}

/* 
 * Local Variables:
 * c-basic-offset: 3
 * End:
 */
