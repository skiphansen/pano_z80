/**
   Modified for use with Panologic devices by Skip Hansen 8/2019
    
   This file is derived from the avr-vt100 project:
   https://github.com/mkschreder/avr-vt100 
   Copyright: Martin K. Schröder (info@fortmax.se) 2014 
    
   This file is part of FORTMAX.

   FORTMAX is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   FORTMAX is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with FORTMAX.  If not, see <http://www.gnu.org/licenses/>.

   Copyright: Martin K. Schröder (info@fortmax.se) 2014 
    
*/
#include <stdint.h>
#include <stdbool.h>
#include "string.h"
#include "vt100.h"
#include "messages.h"
#include "printf.h"
#include "ff.h"
#include "cpm_io.h"
#include "sound.h"

// #define ECHO_CONSOLE_2_SERIAL

// #define DEBUG_LOGGING
// #define VERBOSE_DEBUG_LOGGING
// #define LOG_TO_SERIAL
#include "log.h"

#define abs(x) (x < 0 ? -x : x)
#define isdigit(x) (x >= '0' && x <= '9')

#define CURSOR_CHAR     0x16

#define KEY_ESC 0x1b
#define KEY_DEL 0x7f
#define KEY_BELL 0x07

#define STATE(NAME, TERM, EV, ARG) void NAME(struct vt100 *TERM, uint8_t EV, uint16_t ARG)

// states 
enum {
   STATE_IDLE,
   STATE_ESCAPE,
   STATE_COMMAND
};

// events that are passed into states
enum {
   EV_CHAR = 1,
};

#define MAX_COMMAND_ARGS 4
static struct vt100 {
   union flags {
      uint8_t val;
      struct {
         // 0 = cursor remains on last column when it gets there
         // 1 = lines wrap after last column to next line
         uint8_t cursor_wrap : 1; 
         uint8_t scroll_mode : 1;
         uint8_t origin_mode : 1; 
      }; 
   } flags;

   //uint16_t screen_width, screen_height;
   // cursor position on the screen (0, 0) = top left corner. 
   int16_t cursor_x, cursor_y;
   int16_t saved_cursor_x, saved_cursor_y; // used for cursor save restore
   int16_t scroll_start_row, scroll_end_row; 
   // character width and height
   int8_t char_width, char_height;
   // colors used for rendering current characters
   uint16_t back_color, front_color;
   // command arguments that get parsed as they appear in the terminal
   uint8_t narg; uint16_t args[MAX_COMMAND_ARGS];
   // current arg pointer (we use it for parsing) 
   uint8_t carg;

   void (*state)(struct vt100 *term, uint8_t ev, uint16_t arg);
   void (*ret_state)(struct vt100 *term, uint8_t ev, uint16_t arg); 

// Pano memory mapped screen.  Only the bottom 8 bits are actually implemented
   uint32_t *VRam;
// currently we need this buffer to mirror screen since it's write only
// in the current hardware implementation.
// we many need to keep this anyway to support screen swapping between
// multiple virtual screens
   char ScreenBuf[VT100_WIDTH * VT100_HEIGHT];
// and, the same way, which cells are in inverse video (bit 8 in the screen
// memory of a bitstream that has it, ignored by one that has not)
   uint8_t AttrBuf[VT100_WIDTH * VT100_HEIGHT];
   char CharUnderCursor;
   uint8_t cursor_off;     // the program has switched the cursor off
} term;

#define ATTR_INVERSE    0x100    // in a screen memory word

// what goes into the screen memory for a cell
static uint32_t _vt100_cell(struct vt100 *t, int Offset)
{
   return (uint32_t) (uint8_t) t->ScreenBuf[Offset] |
          (t->AttrBuf[Offset] ? ATTR_INVERSE : 0);
}


STATE(_st_idle, term, ev, arg);
STATE(_st_esc_sq_bracket, term, ev, arg);
STATE(_st_esc_question, term, ev, arg);
STATE(_st_esc_hash, term, ev, arg);

void _vt100_reset(void)
{
   term.back_color = 0x0000;
   term.front_color = 0xffff;
   term.cursor_x = term.cursor_y = term.saved_cursor_x = term.saved_cursor_y = 0;
   term.narg = 0;
   term.state = _st_idle;
   term.ret_state = 0;
   term.scroll_start_row = 0;
   term.scroll_end_row = VT100_HEIGHT; // outside of screen = whole screen scrollable
   term.flags.cursor_wrap = 0;
   term.flags.origin_mode = 0; 
   term.VRam = (uint32_t *) VRAM_ADR;
}

void _vt100_resetScroll(void)
{
   term.scroll_start_row = 0;
   term.scroll_end_row = VT100_HEIGHT;
}

// clear screen from start_line to end_line (including end_line)
void _vt100_clearLines(struct vt100 *t, uint16_t start_line, uint16_t end_line)
{
   int Len = (end_line - start_line + 1) * VT100_WIDTH;
   int Start = start_line * VT100_WIDTH;
   uint32_t *p;
   char *cp;

   LOG("start_line: %d, end_line: %d\n",start_line,end_line);
   LOG("Start: %d, Len: %d\n",Start,Len);
   if(Len - Start > (VT100_WIDTH * VT100_HEIGHT) ) {
      // limit to the end of the screen
      Len = (VT100_WIDTH * VT100_HEIGHT) - Start;
   }

   p = (uint32_t *) &t->VRam[START_OFFSET + Start];
   cp = &t->ScreenBuf[Start];
   LOG("Start: %d, Len: %d\n",Start,Len);
   if(Start >= 0 && Len > 0 && Start + Len <= VT100_WIDTH * VT100_HEIGHT) {
      memset(&t->AttrBuf[Start],0,Len);
   }

   while(Len-- > 0) {
      *p++ = ' ';
      *cp++ = ' ';
   }
   if(cp > &t->ScreenBuf[VT100_WIDTH * VT100_HEIGHT]) {
      ELOG("Past end of ScreenBuf, cp 0x%x\n",(unsigned int) cp);
      for( ; ; );
   }
}

// clear line from cursor right/left
void _vt100_clearLine(struct vt100 *t)
{
   int Len = 0;
   int Start = 0;
   uint32_t *p;
   char *cp;

   if(t->narg == 0 || (t->narg == 1 && t->args[0] == 0)) {
   // clear to end of line (to \n or to edge?)
   // including cursor
      Start = (t->cursor_y * VT100_WIDTH) + t->cursor_x;
      Len = VT100_WIDTH - t->cursor_x;
   }
   else if(t->narg == 1 && t->args[0] == 1) {
   // clear from left to current cursor position
      Start = (t->cursor_y * VT100_WIDTH);
      Len = t->cursor_x;
   }
   else if(t->narg == 1 && t->args[0] == 2) {
   // clear whole current line
      Start = (t->cursor_y * VT100_WIDTH);
      Len = VT100_WIDTH;
   }
   p = (uint32_t *) &t->VRam[START_OFFSET + Start];
   cp = &t->ScreenBuf[Start];
   if(Len > 0 && Start + Len <= VT100_WIDTH * VT100_HEIGHT) {
      memset(&t->AttrBuf[Start],0,Len);
   }

   while(Len-- > 0) {
      *p++ = ' ';
      *cp++ = ' ';
   }
   if(cp > &t->ScreenBuf[VT100_WIDTH * VT100_HEIGHT]) {
      ELOG("Past end of ScreenBuf\n");
      for( ; ; );
   }
   t->state = _st_idle; 
}

// scrolls the scroll region up (lines > 0) or down (lines < 0)
void _vt100_scroll(struct vt100 *t, int16_t lines)
{
   int Start;
   int Len;
   int i;
   char *cp;
   char *cp1;
   uint32_t *pFrom;
   uint32_t *pTo;
   int OffsetTop;

   if(!lines) return;

   OffsetTop = VT100_WIDTH * t->scroll_start_row;
   LOG("lines: %d OffsetTop: %d\n",lines,OffsetTop);

   // clearing of lines that we have scrolled up or down
   if(lines > 0) {
      Len = VT100_WIDTH * (VT100_HEIGHT - lines);
      LOG("Len: %d\n",Len);
      if((Len % 4) == 0) {
      // move 4 bytes at a time
         pTo = (uint32_t *) &t->ScreenBuf[OffsetTop];
         pFrom = pTo + ((VT100_WIDTH * lines)/4);
         i = Len / 4;

         LOG("from 0x%x to 0x%x, len: %d, t->ScreenBuf: 0x%x\n",
             (unsigned int) pFrom,(unsigned int) pTo,i,
             (unsigned int) t->ScreenBuf);

         while(i-- > 0) {
            *pTo++ = *pFrom++;
         }
         if((char *) pTo > &t->ScreenBuf[VT100_WIDTH * VT100_HEIGHT]) {
            ELOG("Past end of ScreenBuf\n");
            for( ; ; );
         }
      }
      else {
         cp1 = &t->ScreenBuf[OffsetTop];
         cp = cp1 + (VT100_WIDTH * lines);
         i = Len;

         while(Len-- > 0) {
            *cp1++ = *cp++;
         }
         if(cp1 > &t->ScreenBuf[VT100_WIDTH * VT100_HEIGHT]) {
            ELOG("Past end of ScreenBuf\n");
            for( ; ; );
         }
      }

      // the attributes move along with the characters
      for(i = OffsetTop + (VT100_WIDTH * lines);
          i < VT100_WIDTH * VT100_HEIGHT; i++) {
         t->AttrBuf[i - (VT100_WIDTH * lines)] = t->AttrBuf[i];
      }

      pTo = &t->VRam[START_OFFSET + OffsetTop];
      i = OffsetTop;

      while(Len-- > 0 && i < VT100_WIDTH * VT100_HEIGHT) {
         *pTo++ = _vt100_cell(t,i++);
      }
      _vt100_clearLines(t,t->scroll_end_row-(lines-1)-1,t->scroll_end_row-1);
   }
   else if(lines < 0) {
#if 0
      // update the scroll value (wraps around scroll_height)
      Len = VT100_WIDTH * (VT100_HEIGHT - lines);
      if((Len % 4) == 0) {
      // move 4 bytes at a time
         pTo = (uint32_t *) &t->ScreenBuf[OffsetTop];
         pFrom = pTo + ((VT100_WIDTH * lines)/4);
         i = Len / 4;
         while(Len-- > 0) {
            *pTo-- = *pFrom--;
         }
      }
      else {
         cp1 = &t->ScreenBuf[OffsetTop];
         cp = cp1 + (VT100_WIDTH * lines);
         i = Len;

         while(Len-- > 0) {
            *cp1++ = *cp++;
         }
      }

      pTo = &t->VRam[OffsetTop];
      cp = &t->ScreenBuf[OffsetTop];
      i = Len;

      while(Len-- > 0) {
         *pTo++ = *cp++;
      }
      _vt100_clearLines(t, t->scroll_end_row - lines, t->scroll_end_row - 1); 
      // make sure that the value wraps down 
      Len = VT100_WIDTH * -lines;
#endif
   }
}

// moves the cursor relative to current cursor position and scrolls the screen
void _vt100_move(struct vt100 *term, int16_t DeltaX, int16_t DeltaY)
{
   // calculate how many lines we need to move down or up if x movement goes outside screen
   int16_t new_x = DeltaX + term->cursor_x; 

   if(new_x >= VT100_WIDTH) {
      if(term->flags.cursor_wrap) {
      // 1 = lines wrap after last column to next line
         DeltaY += new_x / VT100_WIDTH;
         term->cursor_x = new_x % VT100_WIDTH;
      }
      else {
      // 0 = cursor remains on last column when it gets there
         term->cursor_x = VT100_WIDTH - 1;
      }
   }
   else if(new_x < 0) {
      DeltaY += new_x / VT100_WIDTH - 1;
      term->cursor_x = VT100_WIDTH - (abs(new_x) % VT100_WIDTH) + 1; 
   }
   else {
      term->cursor_x = new_x;
   }

   if(DeltaY) {
      int16_t new_y = term->cursor_y + DeltaY;
      int16_t to_scroll = 0;
      // bottom margin 39 marks last line as static on 40 line display
      // therefore, we would scroll when new cursor has moved to line 39
      // (or we could use new_y > VT100_HEIGHT here
      // NOTE: new_y >= term->scroll_end_row ## to_scroll = (new_y - term->scroll_end_row) +1
      if(new_y >= term->scroll_end_row) {
         //scroll = new_y / VT100_HEIGHT;
         //term->cursor_y = VT100_HEIGHT;
         to_scroll = (new_y - term->scroll_end_row) + 1; 
         // place cursor back within the scroll region
         term->cursor_y = term->scroll_end_row - 1; //new_y - to_scroll; 
         //scroll = new_y - term->bottom_margin; 
         //term->cursor_y = term->bottom_margin; 
      }
      else if(new_y < term->scroll_start_row) {
         to_scroll = (new_y - term->scroll_start_row); 
         term->cursor_y = term->scroll_start_row; //new_y - to_scroll; 
         //scroll = new_y / (term->bottom_margin - term->top_margin) - 1;
         //term->cursor_y = term->top_margin; 
      }
      else {
         // otherwise we move as normal inside the screen
         term->cursor_y = new_y;
      }
      _vt100_scroll(term, to_scroll);
   }
}

void _vt100_removeCursor(struct vt100 *t)
{
   char Char = t->CharUnderCursor;

   if(Char != 0) {
      int Offset = (t->cursor_y * VT100_WIDTH) + t->cursor_x;
      t->ScreenBuf[Offset] = Char;
      t->VRam[START_OFFSET + Offset] = _vt100_cell(t,Offset);
      t->CharUnderCursor = 0;
      if(&t->ScreenBuf[Offset] >= &t->ScreenBuf[VT100_WIDTH * VT100_HEIGHT]) {
         ELOG("Past end of ScreenBuf\n");
         for( ; ; );
      }
   }
}

void _vt100_drawCursor(struct vt100 *t)
{
   int Offset = (t->cursor_y * VT100_WIDTH) + t->cursor_x;

   if(t->cursor_off) {
      return;
   }
   t->CharUnderCursor = t->ScreenBuf[Offset];
   t->VRam[START_OFFSET + Offset] = CURSOR_CHAR;
}

// A menu (see setup.c) takes over the display for a while.  The screen of the
// program that was running - its text, the cursor, the scroll region - is
// kept here meanwhile, and put back when the menu is done.
static struct {
   char ScreenBuf[VT100_WIDTH * VT100_HEIGHT];
   uint8_t AttrBuf[VT100_WIDTH * VT100_HEIGHT];
   int16_t cursor_x, cursor_y;
   int16_t saved_cursor_x, saved_cursor_y;
   int16_t scroll_start_row, scroll_end_row;
   uint8_t flags;
   uint8_t cursor_off;
   uint8_t Valid;
} gSavedScreen;

static void IttAbortSequence(void);

void vt100_screen_save(void)
{
   _vt100_removeCursor(&term);
   IttAbortSequence();
   memcpy(gSavedScreen.ScreenBuf,term.ScreenBuf,sizeof(term.ScreenBuf));
   memcpy(gSavedScreen.AttrBuf,term.AttrBuf,sizeof(term.AttrBuf));
   gSavedScreen.cursor_x = term.cursor_x;
   gSavedScreen.cursor_y = term.cursor_y;
   gSavedScreen.saved_cursor_x = term.saved_cursor_x;
   gSavedScreen.saved_cursor_y = term.saved_cursor_y;
   gSavedScreen.scroll_start_row = term.scroll_start_row;
   gSavedScreen.scroll_end_row = term.scroll_end_row;
   gSavedScreen.flags = term.flags.val;
   gSavedScreen.cursor_off = term.cursor_off;
   gSavedScreen.Valid = 1;
   // the menu gets a terminal in its initial state
   term.cursor_off = 0;
   _vt100_resetScroll();
   term.state = _st_idle;
   term.ret_state = 0;
   _vt100_drawCursor(&term);
}

void vt100_screen_restore(void)
{
   int i;

   if(!gSavedScreen.Valid) {
      return;
   }
   gSavedScreen.Valid = 0;
   term.CharUnderCursor = 0;  // whatever the menu left there goes with it
   memcpy(term.ScreenBuf,gSavedScreen.ScreenBuf,sizeof(term.ScreenBuf));
   memcpy(term.AttrBuf,gSavedScreen.AttrBuf,sizeof(term.AttrBuf));
   for(i = 0; i < VT100_WIDTH * VT100_HEIGHT; i++) {
      term.VRam[START_OFFSET + i] = _vt100_cell(&term,i);
   }
   term.cursor_x = gSavedScreen.cursor_x;
   term.cursor_y = gSavedScreen.cursor_y;
   term.saved_cursor_x = gSavedScreen.saved_cursor_x;
   term.saved_cursor_y = gSavedScreen.saved_cursor_y;
   term.scroll_start_row = gSavedScreen.scroll_start_row;
   term.scroll_end_row = gSavedScreen.scroll_end_row;
   term.flags.val = gSavedScreen.flags;
   term.cursor_off = gSavedScreen.cursor_off;
   term.state = _st_idle;
   term.ret_state = 0;
   IttAbortSequence();
   _vt100_drawCursor(&term);
}

void vt100_screen_forget(void)
{
   gSavedScreen.Valid = 0;
}

// National character sets.
//
// The console only handles 7-bit ASCII, like the terminals of the era. A
// national variant of ISO 646 replaces a few of the less common symbols with
// local letters, e.g. German (DIN 66003) shows '[' as 'Ä' and '~' as 'ß'.
// Each entry maps an ASCII code to a glyph in the font ROM. Glyphs >= 0x80
// are the extended glyphs in fpga/vga_font.v, at their CP437 positions.
static const uint8_t gGermanMap[][2] = {
   {'@',  0x15},  // §
   {'[',  0x8e},  // Ä
   {'\\', 0x99},  // Ö
   {']',  0x9a},  // Ü
   {'{',  0x84},  // ä
   {'|',  0x94},  // ö
   {'}',  0x81},  // ü
   {'~',  0xe1},  // ß
   {0, 0}
};

static const struct {
   const char *Name;
   const uint8_t (*Map)[2];   // {ascii, glyph} pairs ending with {0, 0}
} gCharsets[CHARSET_COUNT] = {
   [CHARSET_US]     = {"US ASCII", NULL},
   [CHARSET_GERMAN] = {TR_N("German (DIN 66003)"), gGermanMap},
};

static int gCharset;
static uint8_t gGlyphMap[128];

void vt100_set_charset(int Charset)
{
   const uint8_t (*p)[2];
   int i;

   if(Charset < 0 || Charset >= CHARSET_COUNT) {
      Charset = CHARSET_US;
   }
   gCharset = Charset;

   for(i = 0; i < 128; i++) {
      gGlyphMap[i] = (uint8_t) i;
   }
   for(p = gCharsets[Charset].Map; p != NULL && (*p)[0] != 0; p++) {
      gGlyphMap[(*p)[0]] = (*p)[1];
   }
}

int vt100_get_charset(void)
{
   return gCharset;
}

const char *vt100_charset_name(int Charset)
{
   if(Charset < 0 || Charset >= CHARSET_COUNT) {
      return "?";
   }
   return gCharsets[Charset].Name;
}

static void _vt100_putGlyph(struct vt100 *t, uint8_t ch, bool Inverse);

// sends the character to the display and updates cursor position
void _vt100_putc(struct vt100 *t, uint8_t ch)
{
   // Map to a glyph for the current character set. Bit 7 is ignored, as it
   // always has been on this console (WordStar, for one, sets it).
   _vt100_putGlyph(t, gGlyphMap[ch & 0x7f], false);
}

// puts a glyph of the font on the display and updates cursor position
static void _vt100_putGlyph(struct vt100 *t, uint8_t ch, bool Inverse)
{
   // calculate current cursor position in the display ram
   int Offset = (t->cursor_y * VT100_WIDTH) + t->cursor_x;
   t->ScreenBuf[Offset] = (char) ch;
   t->AttrBuf[Offset] = Inverse ? 1 : 0;
   t->VRam[START_OFFSET + Offset] = _vt100_cell(t,Offset);
   t->CharUnderCursor = 0;

   if(&t->ScreenBuf[Offset] >= &t->ScreenBuf[VT100_WIDTH * VT100_HEIGHT]) {
      ELOG("Past end of ScreenBuf\n");
      for( ; ; );
   }

   // move cursor right
   _vt100_move(t, 1, 0); 
}

void vt100_puts(const char *str)
{
   while(*str) {
      vt100_putc(*str++);
   }
}

// The characters beyond ASCII that the font has, by their code point (all of
// them are in U+0080..U+00FF, two bytes in UTF-8), and their glyphs.
static const uint8_t gLatin1Glyphs[][2] = {
   {0xa7, 0x15},  // §
   {0xc4, 0x8e},  // Ä
   {0xd6, 0x99},  // Ö
   {0xdc, 0x9a},  // Ü
   {0xdf, 0xe1},  // ß
   {0xe4, 0x84},  // ä
   {0xf6, 0x94},  // ö
   {0xfc, 0x81},  // ü
   {0, 0}
};

void vt100_put_utf8(uint8_t ch)
{
   static uint8_t Lead;    // first byte of a two byte sequence, 0 = none
   const uint8_t (*p)[2];
   uint8_t Code;

   if(Lead != 0 && (ch & 0xc0) == 0x80) {
      Code = (uint8_t) ((Lead << 6) | (ch & 0x3f));
      Lead = 0;
      for(p = gLatin1Glyphs; (*p)[0] != 0; p++) {
         if((*p)[0] == Code) {
            _vt100_removeCursor(&term);
            _vt100_putGlyph(&term, (*p)[1], false);
            _vt100_drawCursor(&term);
            return;
         }
      }
      vt100_putc('?');
      return;
   }
   Lead = 0;
   if(ch == 0xc2 || ch == 0xc3) {
      Lead = ch;
      return;
   }
   vt100_putc(ch);
}

STATE(_st_command_arg, term, ev, arg){
   switch(ev) {
      case EV_CHAR:
         if(isdigit(arg)) { // a digit argument
            term->args[term->narg] = term->args[term->narg] * 10 + (arg - '0');
         }
         else if(arg == ';') { // separator
            term->narg++;
         }
         else { // no more arguments
            // go back to command state 
            term->narg++;
            if(term->ret_state) {
               term->state = term->ret_state;
            }
            else {
               term->state = _st_idle;
            }
            // execute next state as well because we have already consumed a char!
            term->state(term, ev, arg);
         }
         break;
   }
}

STATE(_st_esc_sq_bracket, term, ev, arg)
{
   switch(ev) {
      case EV_CHAR:
         if(isdigit(arg)) { // start of an argument
            term->ret_state = _st_esc_sq_bracket; 
            _st_command_arg(term, ev, arg);
            term->state = _st_command_arg;
         }
         else if(arg == ';') { // arg separator. 
            // skip. And also stay in the command state
         }
         else { 
         // otherwise we execute the command and go back to idle
            _vt100_removeCursor(term);
            switch(arg) {
               case 'A': {
               // move cursor up (cursor stops at top margin)
                  int n = (term->narg > 0)?term->args[0]:1;
                  term->cursor_y -= n;
                  if(term->cursor_y < 0) term->cursor_y = 0;
                  term->state = _st_idle; 
                  break;
               } 
               case 'B': {
               // cursor down (cursor stops at bottom margin)
                  int n = (term->narg > 0)?term->args[0]:1;
                  term->cursor_y += n;
                  if(term->cursor_y > VT100_HEIGHT) term->cursor_y = VT100_HEIGHT;
                  term->state = _st_idle; 
                  break;
               }
               case 'C': { 
               // cursor right (cursor stops at right margin)
                  int n = (term->narg > 0)?term->args[0]:1;
                  term->cursor_x += n;
                  if(term->cursor_x > VT100_WIDTH) term->cursor_x = VT100_WIDTH;
                  term->state = _st_idle; 
                  break;
               }
               case 'D': { 
               // cursor left
                  int n = (term->narg > 0)?term->args[0]:1;
                  term->cursor_x -= n;
                  if(term->cursor_x < 0) term->cursor_x = 0;
                  term->state = _st_idle; 
                  break;
               }
               case 'f': 
               case 'H':
               // move cursor to position (default 0;0)
            // cursor stops at respective margins
                  term->cursor_x = (term->narg >= 1)?(term->args[1]-1):0; 
                  term->cursor_y = (term->narg == 2)?(term->args[0]-1):0;
                  if(term->flags.origin_mode) {
                     term->cursor_y += term->scroll_start_row;
                     if(term->cursor_y >= term->scroll_end_row) {
                        term->cursor_y = term->scroll_end_row - 1;
                     }
                  }
                  if(term->cursor_x > VT100_WIDTH) term->cursor_x = VT100_WIDTH;
                  if(term->cursor_y > VT100_HEIGHT) term->cursor_y = VT100_HEIGHT;
                  term->state = _st_idle; 
                  break;

               case 'J':
               // clear screen from cursor up or down
                  if(term->narg == 0 || (term->narg == 1 && term->args[0] == 0)) {
                     // clear down to the bottom of screen (including cursor)
                     _vt100_clearLines(term, term->cursor_y, VT100_HEIGHT-1); 
                  }
                  else if(term->narg == 1 && term->args[0] == 1) {
                     // clear top of screen to current line (including cursor)
                     _vt100_clearLines(term, 0, term->cursor_y); 
                  }
                  else if(term->narg == 1 && term->args[0] == 2) {
                     // clear whole screen
                     _vt100_clearLines(term, 0, VT100_HEIGHT-1);
                     // reset scroll value
                     _vt100_resetScroll(); 
                  }
                  term->state = _st_idle; 
                  break;

               case 'K':
               // clear line from cursor right/left
                  _vt100_clearLine(term);
                  break;

               case 'L': // insert lines (args[0] = number of lines)
               case 'M': // delete lines (args[0] = number of lines)
                  term->state = _st_idle;
                  break; 

               case 'P': {
               // delete characters args[0] or 1 in front of cursor
                  // TODO: this needs to correctly delete n chars
                  int n = ((term->narg > 0)?term->args[0]:1);
                  _vt100_move(term, -n, 0);
                  for(int c = 0; c < n; c++) {
                     _vt100_putc(term, ' ');
                  }
                  term->state = _st_idle;
                  break;
               }
               case 'c':
               // query device code
                  term->state = _st_idle; 
                  break; 
               
               case 'x':
                  term->state = _st_idle;
                  break;
                  
               case 's': // save cursor pos
                     term->saved_cursor_x = term->cursor_x;
                     term->saved_cursor_y = term->cursor_y;
                     term->state = _st_idle; 
                     break;

               case 'u': // restore cursor pos
                     term->cursor_x = term->saved_cursor_x;
                     term->cursor_y = term->saved_cursor_y; 
                     //_vt100_moveCursor(term, term->saved_cursor_x, term->saved_cursor_y);
                     term->state = _st_idle; 
                     break;

               case 'h':
               case 'l':
                     term->state = _st_idle;
                     break;

               case 'g':
                     term->state = _st_idle;
                     break;

               case 'm': // sets colors. Accepts up to 3 args
                     // [m means reset the colors to default
#if 0
                     if(!term->narg) {
                        term->front_color = 0xffff;
                        term->back_color = 0x0000;
                     }
                     while(term->narg) {
                        term->narg--; 
                        int n = term->args[term->narg];
                        static const uint16_t colors[] = {
                           0x0000, // black
                           0xf800, // red
                           0x0780, // green
                           0xfe00, // yellow
                           0x001f, // blue
                           0xf81f, // magenta
                           0x07ff, // cyan
                           0xffff // white
                        };
                        if(n == 0) { // all attributes off
                           term->front_color = 0xffff;
                           term->back_color = 0x0000;

                           ili9340_setFrontColor(term->front_color);
                           ili9340_setBackColor(term->back_color);
                        }
                        if(n >= 30 && n < 38) { // fg colors
                           term->front_color = colors[n-30]; 
                           ili9340_setFrontColor(term->front_color);
                        }
                        else if(n >= 40 && n < 48) {
                           term->back_color = colors[n-40]; 
                           ili9340_setBackColor(term->back_color); 
                        }
                     }
#endif
                     term->state = _st_idle; 
                     break;

               case '@': // Insert Characters          
                  term->state = _st_idle;
                  break; 

               case 'r': // Set scroll region (top and bottom margins)
                  // the top value is first row of scroll region
                  // the bottom value is the first row of static region after scroll
                  if(term->narg == 2 && term->args[0] < term->args[1]) {
                     // [1;40r means scroll region between 8 and 312
                     // bottom margin is 320 - (40 - 1) * 8 = 8 pix
                     term->scroll_start_row = term->args[0] - 1;
                     term->scroll_end_row = term->args[1] - 1; 
                     LOG("Setting scroll region to %d/%d\n",
                         term->scroll_start_row,term->scroll_end_row);
                  }
                  else {
                     _vt100_resetScroll(); 
                  }
                  term->state = _st_idle; 
                  break;  

               case 'i': // Printing  
               case 'y': // self test modes..
               case '=': // argument follows... 
                  //term->state = _st_screen_mode;
                  term->state = _st_idle; 
                  break; 

               case '?': // '[?' escape mode
                  term->state = _st_esc_question;

                  break; 
               default: // unknown sequence
                     term->state = _st_idle;
                     break;
            }
            _vt100_drawCursor(term);

            //term->state = _st_idle;
         } // else
         break;
      default: { // switch (ev)
            // for all other events restore normal mode
            term->state = _st_idle; 
         }
   }
}

STATE(_st_esc_question, term, ev, arg)
{
   // DEC mode commands
   switch(ev) {
      case EV_CHAR: 
         if(isdigit(arg)) { // start of an argument
            term->ret_state = _st_esc_question; 
            _st_command_arg(term, ev, arg);
            term->state = _st_command_arg;
         }
         else if(arg == ';') { // arg separator. 
            // skip. And also stay in the command state
         }
         else {
            switch(arg) {
               case 'l': 
                  // dec mode: OFF (arg[0] = function)
               case 'h': {
                     // dec mode: ON (arg[0] = function)
                     switch(term->args[0]) {
                        case 1: { // cursor keys mode
                              // h = esc 0 A for cursor up
                              // l = cursor keys send ansi commands
                              break;
                           }
                        case 2: { // ansi / vt52
                              // h = ansi mode
                              // l = vt52 mode
                              break;
                           }
                        case 3: {
                              // h = 132 chars per line
                              // l = 80 chars per line
                              break;
                           }
                        case 4: {
                              // h = smooth scroll
                              // l = jump scroll
                              break;
                           }
                        case 5: {
                              // h = black on white bg
                              // l = white on black bg
                              break;
                           }
                        case 6: {
                              // h = cursor relative to scroll region
                              // l = cursor independent of scroll region
                              term->flags.origin_mode = (arg == 'h')?1:0; 
                              break;
                           }
                        case 7: {
                              // h = new line after last column
                              // l = cursor stays at the end of line
                              term->flags.cursor_wrap = (arg == 'h')?1:0; 
                              break;
                           }
                        case 8: {
                              // h = keys will auto repeat
                              // l = keys do not auto repeat when held down
                              break;
                           }
                        case 9: {
                              // h = display interlaced
                              // l = display not interlaced
                              break;
                           }
                           // 10-38 - all quite DEC speciffic commands so omitted here
                     }
                     term->state = _st_idle;
                     break; 
                  }
               case 'i': /* Printing */  
               case 'n': /* Request printer status */
               default:  
                  term->state = _st_idle; 
                  break;
            }
            term->state = _st_idle;
         }
   }
}

STATE(_st_esc_left_br, term, ev, arg){
   switch(ev) {
      case EV_CHAR: 
         switch(arg) {
            case 'A':  
            case 'B':  
               // translation map command?
            case '0':  
            case 'O':
               // another translation map command?
               term->state = _st_idle;
               break;
            default:
               term->state = _st_idle;
         }
         //term->state = _st_idle;
   }
}

STATE(_st_esc_right_br, term, ev, arg)
{
   switch(ev) {
      case EV_CHAR:
         switch(arg) {
            case 'A':  
            case 'B':  
               // translation map command?
            case '0':  
            case 'O':
               // another translation map command?
               term->state = _st_idle;
               break;
            default:
               term->state = _st_idle;
         }
         break;
   }
}

STATE(_st_esc_hash, term, ev, arg)
{
   switch(ev) {
      case EV_CHAR:
         switch(arg) {
            case '8': {
                  // self test: fill the screen with 'E'

                  term->state = _st_idle;
                  break;
               }
            default:
               term->state = _st_idle;
         }
   }
}

STATE(_st_escape, term, ev, arg)
{
#define CLEAR_ARGS \
            { term->narg = 0;\
            for(int c = 0; c < MAX_COMMAND_ARGS; c++)\
               term->args[c] = 0; }
   switch(ev) {
      case EV_CHAR: 
         _vt100_removeCursor(term);
         switch(arg) {
            case '[': // command
               // prepare command state and switch to it
               CLEAR_ARGS; 
               term->state = _st_esc_sq_bracket;
               break;

            case '(': /* ESC ( */  
               CLEAR_ARGS;
               term->state = _st_esc_left_br;
               break; 
            case ')': /* ESC ) */  
               CLEAR_ARGS;
               term->state = _st_esc_right_br;
               break;  
            case '#': // ESC # 
               CLEAR_ARGS;
               term->state = _st_esc_hash;
               break;  
            case 'P': //ESC P (DCS, Device Control String)
               term->state = _st_idle; 
               break;
            case 'D': // moves cursor down one line and scrolls if necessary
               // move cursor down one line and scroll window if at bottom line
               _vt100_move(term, 0, 1); 
               term->state = _st_idle;
               break; 
            case 'M': // Cursor up
               // move cursor up one line and scroll window if at top line
               _vt100_move(term, 0, -1); 
               term->state = _st_idle;
               break; 
            case 'E': // next line
               // same as '\r\n'
               _vt100_move(term, 0, 1);
               term->cursor_x = 0; 
               term->state = _st_idle;
               break;  
            case '7': // Save attributes and cursor position  
            case 's':  
               term->saved_cursor_x = term->cursor_x;
               term->saved_cursor_y = term->cursor_y;
               term->state = _st_idle;
               break;  
            case '8': // Restore them  
            case 'u': 
               term->cursor_x = term->saved_cursor_x;
               term->cursor_y = term->saved_cursor_y; 
               term->state = _st_idle;
               break; 
            case '=': // Keypad into applications mode 
               term->state = _st_idle;
               break; 
            case '>': // Keypad into numeric mode   
               term->state = _st_idle;
               break;  
            case 'Z': // Report terminal type 
               // vt 100 response
               term->state = _st_idle;
               break;    
            case 'c': // Reset terminal to initial state 
               _vt100_reset();
               term->state = _st_idle;
               break;  
            case 'H': // Set tab in current position 
            case 'N': // G2 character set for next character only  
            case 'O': // G3 "               "     
            case '<': // Exit vt52 mode
               // ignore
               term->state = _st_idle;
               break; 
            case KEY_ESC: { // marks start of next escape sequence
                  // stay in escape state
                  break;
               }
            default: { // unknown sequence - return to normal mode
                  term->state = _st_idle;
                  break;
               }
         }
         _vt100_drawCursor(term);
         break;
      default: {
            // for all other events restore normal mode
            term->state = _st_idle; 
         }
   }
#undef CLEAR_ARGS
}

STATE(_st_idle, term, ev, arg)
{
   switch(ev) {
      case EV_CHAR: {
            _vt100_removeCursor(term);
            switch(arg) {
               case 5: // AnswerBack for vt100's  
                  break;  
               case '\n': { // new line
                     _vt100_move(term, 0, 1);
                     term->cursor_x = 0; 
                     //_vt100_moveCursor(term, 0, term->cursor_y + 1);
                     // do scrolling here! 
                     break;
                  }
               case '\r': { // carrage return (0x0d)
                     term->cursor_x = 0; 
                     //_vt100_move(term, 0, 1);
                     //_vt100_moveCursor(term, 0, term->cursor_y); 
                     break;
                  }
               case '\b': { // backspace 0x08
                     _vt100_move(term, -1, 0); 
                     // backspace does not delete the character! Only moves cursor!
                     //ili9340_drawChar(term->cursor_x * term->char_width,
                     // term->cursor_y * term->char_height, ' ');
                     break;
                  }
               case KEY_DEL: { // del - delete character under cursor
                     // Problem: with current implementation, we can't move the rest of line
                     // to the left as is the proper behavior of the delete character
                     // fill the current position with background color
                     _vt100_putc(term, ' ');
                     _vt100_move(term, -1, 0);
                     //_vt100_clearChar(term, term->cursor_x, term->cursor_y); 
                     break;
                  }
               case '\t': { // tab
                     // tab fills characters on the line until we reach a multiple of tab_stop
                     int tab_stop = 4;
                     int to_put = tab_stop - (term->cursor_x % tab_stop); 
                     while(to_put--) _vt100_putc(term, ' ');
                     break;
                  }
               case KEY_BELL: { // bell is sent by bash for ex. when doing tab completion
                     Bell();
                     break; 
                  }
               case KEY_ESC: // escape
                  term->state = _st_escape;
                  break;

               default: {
                     _vt100_putc(term, arg);
                     break;
                  }
            }
            _vt100_drawCursor(term);
            break;
         }
      default: {}
   }
}

// ITT 3030 terminal mode.
//
// The ITT 3030's CP/M does not have a VT100 or ADM-3A style console: its BIOS
// has screen control codes of its own, and the BASIC programs written for it
// use them throughout.  This is that code set, as the video driver in the
// BIOS on the system tracks of an ITT 3030 CP/M 2.2 disk implements it:
//
//    07        bell                      0C   clear screen; the cursor goes
//    08        cursor left                    to the BOTTOM left
//    0A        line feed                 0D   carriage return
//    12        inverse video off         1C   inverse video on
//    11, 13    40 x 12 / 80 x 24 screen; both clear it and reset everything
//
//    ESC 10 n     print n blanks         ESC 11       cursor home (top left)
//    ESC 12       cursor off             ESC 13       cursor on
//    ESC 14       scroll up one line     ESC 15 w h   set a window
//    ESC 16 r c   cursor to row r (1 = top), column c (0 = left)
//    ESC 17       erase to the end of the screen
//    ESC 18       erase to the end of the line
//    ESC 19       erase to the end of the line, go to the next line
//    ESC 1A       cursor right           ESC 1C       cursor up
//    ESC 1D n c   print character c n times
//    ESC 1E a     set the attribute (bit 7: inverse)
//    ESC 1F r c   as ESC 16, with 20 hex added to both r and c
//
// Bit 7 of a character is the inverse video bit of the ITT's video memory,
// so A0-FF are the characters 20-7F in inverse.  80-9F are the inverse of
// the first 32 glyphs of its character generator, which programs used as
// graphics.  The shapes are in a character ROM we do not have, but a chess
// program for the ITT builds its pieces from them, black and white ones as
// exact complements, and that gives 00-0F away: a 2 x 2 block mosaic, bit 0
// the top left quarter, bit 1 top right, bit 2 bottom left, bit 3 bottom
// right.  So 80 is a solid block, and 8F - the inverse of the solid block -
// only the thin gap the glyph leaves in its cell: the horizontal rule of
// almost every screen mask.
//
// What is left out: the 40 x 12 mode and windows (accepted and ignored), and
// the glyphs 10-1F, of which nothing is known (inverse they show as a block,
// plain not at all).
//
// Inverse video and the mosaic need a bitstream that has them
// (FPGA_FEATURE_TEXT_ATTR).  With an older one the rule is drawn with
// underscores, anything else of the mosaic with a block, and inverse text
// shows up normal.
//
// Everything else, the ANSI sequences included, works as in VT100 mode, so
// the firmware's own messages and menus look the same in both.
#define GLYPH_MOSAIC       0xc0     // + the lit quarters, see fpga/vga_font.v
#define GLYPH_RULE         0xd0
#define ITT_GLYPH_RULE     '_'      // stand-ins for a display without them
#define ITT_GLYPH_BLOCK    0x08

static uint8_t gHasTextAttr;

void vt100_set_text_attr(int HasTextAttr)
{
   gHasTextAttr = HasTextAttr ? 1 : 0;
}

static int gTerminal;
static struct {
   uint8_t Esc;            // ESC seen, next character decides
   uint8_t Cmd;            // sequence waiting for parameters, 0 = none
   uint8_t Need;           // parameters still to come
   uint8_t Param;          // first parameter of a two parameter sequence
   uint8_t Attr;           // 0x80: inverse video
} gItt;

static void IttAbortSequence(void)
{
   gItt.Esc = 0;
   gItt.Cmd = 0;
   gItt.Need = 0;
}

// a character (with the ITT's inverse video bit) at the cursor
static void IttPrint(struct vt100 *t, uint8_t ch)
{
   uint8_t Code = ch & 0x7f;
   bool Inverse = ((ch | gItt.Attr) & 0x80) != 0;

   if(Code >= 0x20) {
      _vt100_putGlyph(t, gGlyphMap[Code], Inverse && gHasTextAttr);
   }
   else {
   // The mosaic: which quarters of the cell are lit?
      uint8_t Lit = (Code < 0x10) ? (Code & 0x0f) : 0;
      bool Rule = Inverse && Code == 0x0f;

      if(Inverse) {
         Lit ^= 0x0f;
      }
      if(gHasTextAttr) {
         _vt100_putGlyph(t, Rule ? GLYPH_RULE : (GLYPH_MOSAIC | Lit), false);
      }
      else {
         _vt100_putGlyph(t, Rule ? ITT_GLYPH_RULE :
                            Lit != 0 ? ITT_GLYPH_BLOCK : ' ', false);
      }
   }
}

static void IttFill(struct vt100 *t, int From, int To)
{
   int i;

   for(i = From; i < To; i++) {
      t->ScreenBuf[i] = ' ';
      t->AttrBuf[i] = 0;
      t->VRam[START_OFFSET + i] = ' ';
   }
}

// Row 1 is the top line, column 0 the left edge.  The ITT computes a video
// memory address from them and lets it wrap around.
static void IttSetCursor(struct vt100 *t, uint8_t Row, uint8_t Col)
{
   if(Row == 0) {
      Row = 1;
   }
   t->cursor_y = (Row - 1) % VT100_HEIGHT;
   t->cursor_x = Col < VT100_WIDTH ? Col : VT100_WIDTH - 1;
}

static void IttClearScreen(struct vt100 *t)
{
   IttFill(t, 0, VT100_WIDTH * VT100_HEIGHT);
   _vt100_resetScroll();
   t->cursor_x = 0;
   t->cursor_y = VT100_HEIGHT - 1;
}

// A sequence has all its parameters: C is the last one
static void IttCommand(struct vt100 *t, uint8_t Cmd, uint8_t First, uint8_t C)
{
   int n;

   switch(Cmd) {
      case 0x10:  // n blanks
         for(n = C; n > 0; n--) {
            IttPrint(t, ' ');
         }
         break;

      case 0x15:  // window: not supported
         break;

      case 0x16:  // cursor to row, column
         IttSetCursor(t, First, C);
         break;

      case 0x1d:  // character C, First times
         for(n = First; n > 0; n--) {
            IttPrint(t, C);
         }
         break;

      case 0x1e:  // attribute
         gItt.Attr = C & 0x80;
         break;

      case 0x1f:  // cursor to row + 20h, column + 20h
         IttSetCursor(t, (uint8_t) (First - 0x20), (uint8_t) (C - 0x20));
         break;

      default:
         break;
   }
}

// ESC and one of 10..1F
static void IttEscape(struct vt100 *t, uint8_t c)
{
   int Offset = (t->cursor_y * VT100_WIDTH) + t->cursor_x;
   int LineEnd = (t->cursor_y + 1) * VT100_WIDTH;

   switch(c) {
      case 0x10:
      case 0x1e:
         gItt.Cmd = c;
         gItt.Need = 1;
         break;

      case 0x15:
      case 0x16:
      case 0x1d:
      case 0x1f:
         gItt.Cmd = c;
         gItt.Need = 2;
         break;

      case 0x11:  // home
         t->cursor_x = 0;
         t->cursor_y = 0;
         break;

      case 0x12:  // cursor off
         t->cursor_off = 1;
         break;

      case 0x13:  // cursor on
         t->cursor_off = 0;
         break;

      case 0x14:  // scroll up
         _vt100_scroll(t, 1);
         break;

      case 0x17:  // erase to the end of the screen
         IttFill(t, Offset, VT100_WIDTH * VT100_HEIGHT);
         break;

      case 0x18:  // erase to the end of the line
         IttFill(t, Offset, LineEnd);
         break;

      case 0x19:  // erase to the end of the line, next line
         IttFill(t, Offset, LineEnd);
         t->cursor_x = 0;
         _vt100_move(t, 0, 1);
         break;

      case 0x1a:  // cursor right
         _vt100_move(t, 1, 0);
         break;

      case 0x1c:  // cursor up, not beyond the top line
         if(t->cursor_y > 0) {
            t->cursor_y--;
         }
         break;

      default:    // 1B and the unassigned ones: nothing
         break;
   }
}

// Returns true if the character has been dealt with here, false if it is
// for the VT100 state machine.
static bool IttPutc(struct vt100 *t, uint8_t c)
{
   if(t->state != _st_idle) {
   // in the middle of an ANSI sequence
      return false;
   }

   if(gItt.Need != 0) {
   // a parameter
      _vt100_removeCursor(t);
      if(--gItt.Need == 0) {
         uint8_t Cmd = gItt.Cmd;
         gItt.Cmd = 0;
         IttCommand(t, Cmd, gItt.Param, c);
      }
      else {
         gItt.Param = c;
      }
      _vt100_drawCursor(t);
      return true;
   }

   if(gItt.Esc) {
      gItt.Esc = 0;
      if(c >= 0x10 && c <= 0x1f) {
         _vt100_removeCursor(t);
         IttEscape(t, c);
         _vt100_drawCursor(t);
         return true;
      }
   // not one of ours: let the VT100 have the ESC and this character
      t->state(t, EV_CHAR, KEY_ESC);
      return false;
   }

   if(c >= 0x20) {
      _vt100_removeCursor(t);
      IttPrint(t, c);
      _vt100_drawCursor(t);
      return true;
   }

   switch(c) {
      case KEY_ESC:
         gItt.Esc = 1;
         return true;

      case '\b':
      // cursor left, from the left edge to the end of the line above
         _vt100_removeCursor(t);
         if(t->cursor_x > 0) {
            t->cursor_x--;
         }
         else if(t->cursor_y > 0) {
            t->cursor_y--;
            t->cursor_x = VT100_WIDTH - 1;
         }
         _vt100_drawCursor(t);
         return true;

      case 0x0c:  // clear screen
         _vt100_removeCursor(t);
         IttClearScreen(t);
         _vt100_drawCursor(t);
         return true;

      case 0x11:  // 40 x 12 screen: there is none, but it clears and resets
      case 0x13:  // 80 x 24 screen
         _vt100_removeCursor(t);
         gItt.Attr = 0;
         t->cursor_off = 0;
         IttClearScreen(t);
         _vt100_drawCursor(t);
         return true;

      case 0x12:  // inverse video off
         gItt.Attr = 0;
         return true;

      case 0x1c:  // inverse video on
         gItt.Attr = 0x80;
         return true;

      case '\n':
      case '\r':
      case '\t':        // (a glyph on the ITT, but CP/M expands tabs anyway)
      case KEY_BELL:
         return false;  // as on the VT100

      default:
      // any other control code is a glyph of the mosaic on the ITT
         _vt100_removeCursor(t);
         IttPrint(t, c);
         _vt100_drawCursor(t);
         return true;
   }
}

void vt100_set_terminal(int Terminal)
{
   if(Terminal < 0 || Terminal >= TERM_COUNT) {
      Terminal = TERM_VT100;
   }
   gTerminal = Terminal;
   IttAbortSequence();
   gItt.Attr = 0;
   // The ITT continues on the next line when a line is full, and programs
   // rely on it; the VT100 stays in the last column unless told otherwise.
   term.flags.cursor_wrap = (Terminal == TERM_ITT3030) ? 1 : 0;
   if(term.cursor_off) {
      term.cursor_off = 0;
      _vt100_drawCursor(&term);
   }
}

int vt100_get_terminal(void)
{
   return gTerminal;
}

const char *vt100_terminal_name(int Terminal)
{
   return Terminal == TERM_ITT3030 ? "ITT 3030" : "VT100";
}

void vt100_init()
{
   _vt100_reset(); 
   vt100_set_charset(CHARSET_US);
   memset(term.ScreenBuf,' ',VT100_WIDTH * VT100_HEIGHT);
   memset(term.AttrBuf,0,VT100_WIDTH * VT100_HEIGHT);
   memset((void *)VRAM_ADR,' ',SCREEN_X * SCREEN_Y * sizeof(uint32_t));
}

void UartPutc(char c);

void vt100_putc(uint8_t c)
{
#ifdef ECHO_CONSOLE_2_SERIAL
   if(c >= 0x20 && c < 0x7f) {
      UartPutc(c);
   }
   else if(c == '\r' || c == '\n') {
      UartPutc(c);
   }

#endif

#ifdef VERBOSE_DEBUG_LOGGING
   static int Pos = 0;
   if(c < 0x20 || c > 0x7f) {
      Pos += LOG_R("[%02x]",c);
   }
   else {
      Pos += LOG_R("%c",c);
   }
   if(c == '\n' || Pos >= (80-4)) {
      Pos = 0;
      LOG_R("\n");
   }
#endif
   if(gTerminal == TERM_ITT3030 && IttPutc(&term, c)) {
      return;
   }
   term.state(&term, EV_CHAR, 0x0000 | c);
}

