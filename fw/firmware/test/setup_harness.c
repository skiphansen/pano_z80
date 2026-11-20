/* Host-side test of ../setup.c: the settings file and the setup menu, against
 * stub display / keyboard / FatFs layers.
 *
 *   setup_harness load "<file text>"          apply a settings file, print state
 *   setup_harness menu "<keys>" ["<file>"]    run the menu with these keys
 *   setup_harness swap "<keys>" [relogin|reboot]  the disk swap key (F10)
 *   (menu takes relogin|reboot in place of the file, too: what a swap did)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "build/setup_copy.c"
#include "../messages.c"

uint32_t font_fg_color, font_bg_color;
unsigned char gCapsLockSwap = 1, gZ80_ResetRequest;
const struct fw_info gFwInfo = {{'P','A','N','O','F','W','I','D'}, "host-test", "now"};
static int g_charset, g_terminal, g_saved, g_restored, g_forgot;
static char g_file[2048]; static int g_filelen = -1, g_writes, g_failwrite;
/* Keys arrive the way they do from a person: one at a time, with a pause
 * between them.  A '+' in the script glues the next key to the previous one,
 * which is how the characters of a cursor key sequence arrive. */
static const char *g_keys = ""; static uint32_t g_ms, g_next_key_ms; static int g_polls;
static int g_swap_result;

void vt100_set_charset(int c) { g_charset = c; }
int vt100_get_charset(void) { return g_charset; }
const char *vt100_charset_name(int c) { return c ? "German (DIN 66003)" : "US ASCII"; }
void vt100_set_terminal(int t) { g_terminal = t; }
int vt100_get_terminal(void) { return g_terminal; }
const char *vt100_terminal_name(int t) { return t ? "ITT 3030" : "VT100"; }
static int g_kbd;
int KbdLayoutCount(void) { return 2; }
void KbdLayoutSet(int l) { g_kbd = l; }
int KbdLayoutGet(void) { return g_kbd; }
const char *KbdLayoutKey(int l) { return l ? "de" : "us"; }
const char *KbdLayoutName(int l) { return l ? "German" : "US"; }
void vt100_screen_save(void) { g_saved++; }
void vt100_screen_restore(void) { g_restored++; }
void vt100_screen_forget(void) { g_forgot++; }
uint32_t ticks_ms(void) { return g_ms += 7; }
void IdlePoll(void) { g_ms += 3; if(++g_polls > 100000) { printf("[no key: would wait forever]\n"); exit(3); } }
int usb_kbd_testc(void) { return *g_keys != 0 && g_ms >= g_next_key_ms; }
char usb_kbd_getc(void) {
   char c = *g_keys++; g_polls = 0;
   if(*g_keys == '+') g_keys++; else g_next_key_ms = g_ms + 200;
   return c;
}
/* the swap dialog does what the test says: nothing, a data drive, or A: */
int SwapDisk(void) {
   printf("[swap dialog]\n");
   if(g_swap_result == SWAP_REBOOT) gZ80_ResetRequest = 1;
   return g_swap_result;
}
void usb_kbd_inject(char c) { printf("[queued %02x, screen restored %d times]\n", c, g_restored); }
void FlashUpdateMenu(void) { printf("[update menu]\n"); }

FRESULT f_open(FIL *fp, const char *path, int mode) {
   if(strcmp(path, "PANOZ80.CFG")) return FR_NO_FILE;
   fp->pos = 0; fp->writing = (mode & FA_WRITE) != 0;
   if(fp->writing) { if(g_failwrite) return FR_DISK_ERR; g_filelen = 0; g_writes++; return FR_OK; }
   return g_filelen < 0 ? FR_NO_FILE : FR_OK;
}
FRESULT f_read(FIL *fp, void *buf, UINT n, UINT *got) {
   UINT left = (UINT)(g_filelen - fp->pos); if(n > left) n = left;
   memcpy(buf, g_file + fp->pos, n); fp->pos += (int)n; *got = n; return FR_OK;
}
FRESULT f_write(FIL *fp, const void *buf, UINT n, UINT *put) {
   memcpy(g_file + g_filelen, buf, n); g_filelen += (int)n; *put = n; return FR_OK;
}
FRESULT f_close(FIL *fp) { (void)fp; return FR_OK; }

static void state(void) {
   printf("[state: color=%d fg=%06x bg=%06x charset=%d caps=%d term=%d kbd=%d lang=%s | saved=%d restored=%d forgot=%d writes=%d reset=%d keys-left=%d]\n",
          ScreenColorGet(), font_fg_color, font_bg_color, g_charset, gCapsLockSwap, g_terminal, g_kbd, LanguageKey(LanguageGet()),
          g_saved, g_restored, g_forgot, g_writes, gZ80_ResetRequest, (int)strlen(g_keys));
   if(g_filelen >= 0) { printf("[file:\n"); fwrite(g_file, 1, (size_t)g_filelen, stdout); printf("]\n"); }
}
static void setfile(const char *t) {         /* \n, \r and \z (0x1a) escapes */
   int n = 0; for(; *t; t++) {
      if(*t == '\\' && t[1]) { t++; g_file[n++] = *t == 'n' ? '\n' : *t == 'r' ? '\r' : *t == 'z' ? 0x1a : *t; }
      else g_file[n++] = *t;
   }
   g_filelen = n;
}
int main(int argc, char **argv) {
   ScreenColorSet(COLOR_GREEN);            /* what main() does at power-on */
   if(argc >= 3 && !strcmp(argv[1], "load")) { setfile(argv[2]); SettingsLoad(); g_filelen = -1; state(); return 0; }
   if(argc >= 3 && (!strcmp(argv[1], "menu") || !strcmp(argv[1], "swap"))) {
      static char keys[256]; int n = 0; const char *t;
      for(t = argv[2]; *t; t++) keys[n++] = (*t == '\\' && t[1]) ? (++t, *t == 'e' ? 0x1b : *t == 'r' ? '\r' : *t) : *t;
      g_keys = keys;
      if(argc >= 4 && !strcmp(argv[3], "failwrite")) g_failwrite = 1;
      else if(argc >= 4 && !strcmp(argv[3], "relogin")) g_swap_result = SWAP_RELOGIN;
      else if(argc >= 4 && !strcmp(argv[3], "reboot")) g_swap_result = SWAP_REBOOT;
      else if(argc >= 4) { setfile(argv[3]); SettingsLoad(); }
      if(!strcmp(argv[1], "swap")) SwapDiskMenu(); else SetupMenu();
      state(); return 0;
   }
   return 2;
}
