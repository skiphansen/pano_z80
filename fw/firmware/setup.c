/*
 * Settings and the setup menu.
 *
 * The things one can choose - screen colour, national character set, whether
 * Caps Lock and Ctrl are swapped, the terminal emulation, the keyboard layout,
 * the language of the firmware's own texts - are kept in a small text file in the root
 * of the USB drive, PANOZ80.CFG, so that they survive a power cycle:
 *
 *    # Pano Z80 settings
 *    color=green
 *    charset=german
 *    capslock=ctrl
 *    terminal=itt3030
 *    keyboard=de
 *    language=de
 *
 * The file is read once at power-on and rewritten whenever a setting is
 * changed, here or with F5 / F6.  Lines the firmware does not understand are
 * ignored (and are not kept when the file is rewritten).  Without a file the
 * defaults apply, and those are what this firmware did before it had
 * settings: green on black, US ASCII, Caps Lock and Ctrl swapped, VT100, US
 * keyboard, English.
 *
 * F12 brings up the setup menu, which changes these settings and gives access
 * to the less frequent actions (swap a disk, update from USB, reset the Z80)
 * without a function key for each of them.  While the menu is up the screen
 * of the running program is kept aside, and put back when the menu is left.
 */
#include <stdint.h>
#include <stdbool.h>

#include "string.h"
#include "misc.h"
#include "ff.h"
#include "usb.h"
#include "cpm_io.h"
#include "vt100.h"
#include "printf.h"
#include "fwinfo.h"
#include "messages.h"
#include "log.h"
#include "setup.h"

#define SETTINGS_NAME   "PANOZ80.CFG"
#define SETTINGS_MAX    512            /* more than the file ever needs */

#define ANSI_HOME       "\033[H"
#define ANSI_CLS        "\033[2J"

extern unsigned char gCapsLockSwap;    /* usb_kbd.c */
extern unsigned char gZ80_ResetRequest;
void FlashUpdateMenu(void);

static const struct {
   const char *Key;        /* in the settings file */
   const char *Name;       /* in the menu */
   uint32_t Fg;
   uint32_t Bg;
} gColors[COLOR_COUNT] = {
   [COLOR_GREEN] = {"green", TR_N("green"),          GREEN, BLACK},
   [COLOR_WHITE] = {"white", TR_N("white"),          WHITE, BLACK},
   [COLOR_PAPER] = {"paper", TR_N("black on white"), BLACK, WHITE},
   [COLOR_AMBER] = {"amber", TR_N("amber"),          AMBER, BLACK},
};

/* The names of the character sets in the settings file, by CHARSET_xxx */
static const char *const gCharsetKeys[CHARSET_COUNT] = {
   [CHARSET_US]     = "us",
   [CHARSET_GERMAN] = "german",
};

/* The names of the terminal emulations in the settings file, by TERM_xxx */
static const char *const gTerminalKeys[TERM_COUNT] = {
   [TERM_VT100]   = "vt100",
   [TERM_ITT3030] = "itt3030",
};

static int gColor = COLOR_GREEN;

void ScreenColorSet(int Color)
{
   if(Color < 0 || Color >= COLOR_COUNT) {
      Color = COLOR_GREEN;
   }
   gColor = Color;
   font_fg_color = gColors[Color].Fg;
   font_bg_color = gColors[Color].Bg;
}

int ScreenColorGet(void)
{
   return gColor;
}

/* Is the word at p (ending at a NUL, or at anything that is not part of a
 * word) the same as Word, ignoring case? */
static bool WordIs(const char *p, const char *Word)
{
   while(*Word != 0) {
      if(tolower((unsigned char) *p) != *Word) {
         return false;
      }
      p++;
      Word++;
   }
   return *p == 0;
}

/* One "key=value" line, already NUL terminated and without its line end */
static void ApplyLine(char *Line)
{
   char *Key;
   char *Value;
   char *p;
   int i;

   while(*Line == ' ' || *Line == '\t') {
      Line++;
   }
   if(*Line == 0 || *Line == '#' || *Line == ';') {
      return;
   }
   Key = Line;
   for(p = Line; *p != 0 && *p != '='; p++)
      ;
   if(*p != '=') {
      return;
   }
   Value = p + 1;
   /* trim: blanks before the '=', around the value and at the end */
   while(p > Key && (p[-1] == ' ' || p[-1] == '\t')) {
      p--;
   }
   *p = 0;
   while(*Value == ' ' || *Value == '\t') {
      Value++;
   }
   for(p = Value; *p != 0; p++)
      ;
   while(p > Value && (p[-1] == ' ' || p[-1] == '\t')) {
      p--;
   }
   *p = 0;

   if(WordIs(Key, "color") || WordIs(Key, "colour")) {
      for(i = 0; i < COLOR_COUNT; i++) {
         if(WordIs(Value, gColors[i].Key)) {
            ScreenColorSet(i);
         }
      }
   }
   else if(WordIs(Key, "charset")) {
      for(i = 0; i < CHARSET_COUNT; i++) {
         if(WordIs(Value, gCharsetKeys[i])) {
            vt100_set_charset(i);
         }
      }
   }
   else if(WordIs(Key, "terminal")) {
      for(i = 0; i < TERM_COUNT; i++) {
         if(WordIs(Value, gTerminalKeys[i])) {
            vt100_set_terminal(i);
         }
      }
   }
   else if(WordIs(Key, "keyboard")) {
      for(i = 0; i < KbdLayoutCount(); i++) {
         if(WordIs(Value, KbdLayoutKey(i))) {
            KbdLayoutSet(i);
         }
      }
   }
   else if(WordIs(Key, "language")) {
      for(i = 0; i < LanguageCount(); i++) {
         if(WordIs(Value, LanguageKey(i))) {
            LanguageSet(i);
         }
      }
   }
   else if(WordIs(Key, "capslock")) {
      if(WordIs(Value, "ctrl")) {
         gCapsLockSwap = 1;
      }
      else if(WordIs(Value, "capslock")) {
         gCapsLockSwap = 0;
      }
   }
}

/* Apply the text of a settings file.  Text[Len] must be writable. */
static void ApplySettings(char *Text, unsigned int Len)
{
   unsigned int Start = 0;
   unsigned int i;

   for(i = 0; i <= Len; i++) {
      if(i == Len || Text[i] == '\n' || Text[i] == '\r' || Text[i] == 0x1a) {
         bool End = (i < Len && Text[i] == 0x1a);   /* CP/M end of file */
         Text[i] = 0;
         ApplyLine(&Text[Start]);
         Start = i + 1;
         if(End) {
            break;
         }
      }
   }
}

void SettingsLoad(void)
{
   FIL File;
   UINT Read = 0;
   char Text[SETTINGS_MAX + 1];

   if(f_open(&File, SETTINGS_NAME, FA_READ) != FR_OK) {
      return;                          /* no file: keep the defaults */
   }
   if(f_read(&File, Text, SETTINGS_MAX, &Read) == FR_OK && Read > 0) {
      ApplySettings(Text, Read);
   }
   f_close(&File);
}

int SettingsSave(void)
{
   FIL File;
   UINT Written = 0;
   char Text[SETTINGS_MAX];
   int Len;
   FRESULT Err;

   Len = snprintf(Text, sizeof(Text),
                  "# Pano Z80 settings - change them with the setup menu (F12)\r\n"
                  "color=%s\r\n"
                  "charset=%s\r\n"
                  "capslock=%s\r\n"
                  "terminal=%s\r\n"
                  "keyboard=%s\r\n"
                  "language=%s\r\n",
                  gColors[gColor].Key,
                  gCharsetKeys[vt100_get_charset()],
                  gCapsLockSwap ? "ctrl" : "capslock",
                  gTerminalKeys[vt100_get_terminal()],
                  KbdLayoutKey(KbdLayoutGet()),
                  LanguageKey(LanguageGet()));
   if(Len <= 0 || Len >= (int) sizeof(Text)) {
      return -1;
   }
   if((Err = f_open(&File, SETTINGS_NAME, FA_WRITE | FA_CREATE_ALWAYS)) != FR_OK) {
      return (int) Err;
   }
   Err = f_write(&File, Text, (UINT) Len, &Written);
   if(Err == FR_OK && Written != (UINT) Len) {
      Err = FR_DISK_ERR;
   }
   if(f_close(&File) != FR_OK && Err == FR_OK) {
      Err = FR_DISK_ERR;
   }
   return (int) Err;
}

/* Wait for a key.  It is IdlePoll() that polls the USB keyboard. */
static char WaitKey(void)
{
   while(!usb_kbd_testc()) {
      IdlePoll();
   }
   return usb_kbd_getc();
}

/* A cursor or other special key arrives as ESC and more characters, one
 * right after the other.  Tell that from the ESC key on its own: give the
 * rest a moment to arrive, and throw it away. */
static bool EscapeAlone(void)
{
   uint32_t Until = ticks_ms() + 30;
   bool Alone = true;

   while(ticks_ms() < Until) {
      IdlePoll();
      while(usb_kbd_testc()) {
         (void) usb_kbd_getc();
         Alone = false;
      }
   }
   return Alone;
}

static void DrawMenu(void)
{
   ALOG_R(ANSI_HOME ANSI_CLS);
   ALOG_R("== Pano Z80 setup ==\n");
   ALOG_R("Firmware %s, built %s\n\n", gFwInfo.version, gFwInfo.built);
   ALOG_R("  1  Screen colour        %s\n", TR(gColors[gColor].Name));
   ALOG_R("  2  Character set        %s\n",
          TR(vt100_charset_name(vt100_get_charset())));
   ALOG_R("  3  Caps Lock key        %s\n",
          gCapsLockSwap ? TR("is Ctrl (and Ctrl is Caps Lock)") : TR("is Caps Lock"));
   ALOG_R("  4  Terminal             %s\n",
          vt100_terminal_name(vt100_get_terminal()));
   ALOG_R("  5  Keyboard             %s\n", TR(KbdLayoutName(KbdLayoutGet())));
   ALOG_R("  6  Language             %s\n", LanguageName(LanguageGet()));
   ALOG_R("\n");
   ALOG_R("  D  Swap a disk image\n");
   ALOG_R("  U  Update firmware and bitstream from the USB drive\n");
   ALOG_R("  R  Reset the Z80\n");
   ALOG_R("\n");
   ALOG_R("  Enter or Esc  back to where you were\n");
   ALOG_R("\n");
   ALOG_R("Settings are kept in %s on the USB drive.\n", SETTINGS_NAME);
}

void SetupMenu(void)
{
   bool Changed = false;
   bool Restore = true;
   bool Done = false;
   int Err;
   char Key;

   vt100_screen_save();

   while(!Done) {
      DrawMenu();
      Key = WaitKey();
      switch(Key) {
         case '1':
            ScreenColorSet((gColor + 1) % COLOR_COUNT);
            Changed = true;
            break;

         case '2':
            vt100_set_charset((vt100_get_charset() + 1) % CHARSET_COUNT);
            Changed = true;
            break;

         case '3':
            gCapsLockSwap = gCapsLockSwap ? 0 : 1;
            Changed = true;
            break;

         case '4':
            vt100_set_terminal((vt100_get_terminal() + 1) % TERM_COUNT);
            Changed = true;
            break;

         case '5':
            KbdLayoutSet((KbdLayoutGet() + 1) % KbdLayoutCount());
            Changed = true;
            break;

         case '6':
            LanguageSet((LanguageGet() + 1) % LanguageCount());
            Changed = true;
            break;

         case 'd':
         case 'D':
            /* The swap menu ends by making CP/M log the disks in again (it
             * queues a Ctrl-C, or resets the Z80 for drive A:), so the old
             * screen is of no use after it, and its last message should
             * stay readable: leave without putting the screen back. */
            ALOG_R(ANSI_HOME ANSI_CLS);
            SwapDiskMenu();
            if(usb_kbd_testc() || gZ80_ResetRequest) {
               Restore = false;
               Done = true;
            }
            else {
               /* nothing was swapped */
               ALOG_R("\nPress a key.");
               (void) WaitKey();
               EscapeAlone();
            }
            break;

         case 'u':
         case 'U':
            /* Does not come back if it flashes; if it does come back there
             * is something to read. */
            ALOG_R(ANSI_HOME ANSI_CLS);
            FlashUpdateMenu();
            ALOG_R("\nPress a key.");
            (void) WaitKey();
            EscapeAlone();
            break;

         case 'r':
         case 'R':
            gZ80_ResetRequest = 1;     /* the main loop clears the screen */
            Restore = false;
            Done = true;
            break;

         case '\r':
         case '\n':
         case 'q':
         case 'Q':
            Done = true;
            break;

         case 0x1b:
            if(EscapeAlone()) {
               Done = true;
            }
            break;

         default:
            break;
      }
   }

   if(Changed && (Err = SettingsSave()) != 0) {
      ALOG_R("\nCould not save the settings to %s (error %d).\nPress a key.",
             SETTINGS_NAME, Err);
      (void) WaitKey();
      EscapeAlone();
   }

   if(Restore) {
      vt100_screen_restore();
   }
   else {
      vt100_screen_forget();
   }
}
