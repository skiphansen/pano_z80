/**
   This file is part of FORTMAX kernel.

   FORTMAX kernel is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   FORTMAX kernel is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with FORTMAX kernel.  If not, see <http://www.gnu.org/licenses/>.

   Copyright: Martin K. Schröder (info@fortmax.se) 2014
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define VT100_WIDTH  80
// The physical height is 30, but CP/M apps patched for z80pack such as
// Wordmaster and Wordstar assume a 80 x 24 terminal and don't work correctly
// on our 80 x 30 terminal

// #define VT100_HEIGHT 30
// #define START_OFFSET 0
#define VT100_HEIGHT 24
#define START_OFFSET (3 * VT100_WIDTH)

// National character sets (ISO 646 variants)
enum {
   CHARSET_US = 0,
   CHARSET_GERMAN,
   CHARSET_COUNT
};

void vt100_init(void);
void vt100_putc(uint8_t ch);
void vt100_puts(const char *str);
void vt100_set_charset(int Charset);

// Keep the screen and the cursor aside while a menu uses the display, and put
// them back afterwards (or forget them).  One level only.
void vt100_screen_save(void);
void vt100_screen_restore(void);
void vt100_screen_forget(void);
int vt100_get_charset(void);
const char *vt100_charset_name(int Charset);

#ifdef __cplusplus
}
#endif
