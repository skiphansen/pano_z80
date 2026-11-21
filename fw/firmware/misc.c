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

// Milliseconds since power-on.
//
// The cycle counter has 32 bits and wraps every 172 seconds at 25 MHz, so
// this must not be computed from it directly: every "ticks_ms() < Timeout"
// in the firmware would go wrong for a timeout that falls beyond the wrap,
// and the one in the main loop then never ends - the Z80 keeps running, but
// the keyboard is not polled any more.  Count the cycles that have passed
// since the last call instead.  That is right as long as this is called more
// often than once in 172 seconds, which the main loop and every wait loop do,
// and the result only wraps after 49 days.
uint32_t ticks_ms() {
    static uint32_t Last;
    static uint32_t Ms;
    static uint32_t Rest;    // cycles that do not make a millisecond yet
    uint32_t Now = ticks();
    uint32_t Delta = Now - Last;

    Last = Now;
    while (Delta >= CYCLE_PER_US * 1000) {
        Delta -= CYCLE_PER_US * 1000;
        Ms++;
    }
    Rest += Delta;
    if (Rest >= CYCLE_PER_US * 1000) {
        Rest -= CYCLE_PER_US * 1000;
        Ms++;
    }
    return Ms;
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
