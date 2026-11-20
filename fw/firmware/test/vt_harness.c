/* Host-side test of the console (../vt100.c): feed it a byte stream, print
 * the resulting 80 x 24 screen.
 *
 *   vt_harness [itt] [german] < bytes
 *
 * The cursor itself is not shown (its position is printed at the end).
 * Glyphs outside ASCII are shown as # (block) or ?, umlauts as their base
 * letter; with "attr" (a display with inverse video and the mosaic) a mosaic
 * glyph is the hex digit of its lit quarters, the rule is =, and cells in
 * inverse video are marked with ^ on an extra line below. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "cpm_io.h"
uint32_t host_vram[SCREEN_X * SCREEN_Y];
#undef isdigit
#include "build/vt100_copy.c"

int main(int argc, char **argv) {
   int i, c, x, y;
   vt100_init();
   for(i = 1; i < argc; i++) {
      if(!strcmp(argv[i], "itt")) vt100_set_terminal(TERM_ITT3030);
      else if(!strcmp(argv[i], "attr")) vt100_set_text_attr(1);      /* bitstream with inverse video + mosaic */
      else if(!strcmp(argv[i], "german")) vt100_set_charset(CHARSET_GERMAN);
      else if(!strcmp(argv[i], "save")) vt100_screen_save();
   }
   while((c = getchar()) != EOF) {
      if(c == 0xF0) { vt100_screen_save(); continue; }       /* test hooks */
      if(c == 0xF1) { vt100_screen_restore(); continue; }
      vt100_putc((uint8_t) c);
   }
   for(y = 0; y < VT100_HEIGHT; y++) {
      char line[VT100_WIDTH + 1], inv[VT100_WIDTH + 1]; int any_inv = 0;
      for(x = 0; x < VT100_WIDTH; x++) {
         uint32_t w = host_vram[START_OFFSET + y * VT100_WIDTH + x];
         uint32_t g = w & 0xff;
         inv[x] = (w & 0x100) ? '^' : ' ';
         if(w & 0x100) any_inv = 1;
         if(w & ~0x1ffu) { printf("[bad screen memory word %08x]\n", w); return 1; }
         if(x == term.cursor_x && y == term.cursor_y && g == CURSOR_CHAR)
            g = (uint8_t) term.CharUnderCursor;        /* what the cursor covers */
         /* mosaic C0-CF: a digit/letter for the lit quarters (0 = none ... f = all); D0: the rule */
         line[x] = (g >= 0xc0 && g <= 0xcf) ? (g == 0xc0 ? ' ' : "0123456789abcdef"[g & 15]) : g == 0xd0 ? '=' :
                   g == 0x08 ? '#' :
                   (g >= 0x20 && g < 0x7f) ? (char) g :
                   g == 0x8e ? 'A' : g == 0x99 ? 'O' : g == 0x9a ? 'U' :      /* umlauts, as their base letter */
                   g == 0x84 ? 'a' : g == 0x94 ? 'o' : g == 0x81 ? 'u' : g == 0xe1 ? 's' : '?';
      }
      for(x = VT100_WIDTH; x > 0 && line[x - 1] == ' '; x--) ;
      line[x] = 0;
      printf("%02d|%s\n", y + 1, line);
      if(any_inv) {                                        /* a line of ^ under the inverse cells */
         for(x = VT100_WIDTH; x > 0 && inv[x - 1] == ' '; x--) ;
         inv[x] = 0; printf("  |%s\n", inv);
      }
   }
   printf("[cursor row %d col %d %s]\n", term.cursor_y + 1, term.cursor_x, term.cursor_off ? "off" : "on");
   return 0;
}
