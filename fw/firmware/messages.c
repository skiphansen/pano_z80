/*
 * The firmware's messages in other languages.
 *
 * The firmware is written in English, and its messages stay where they are
 * in the code.  A translation is a file of pairs,
 *
 *    {"English text", "the same in the other language"},
 *
 * in lang/, where the English text is the message exactly as the code has
 * it - printf conversions, blanks and line ends included.  ALOG_R() looks
 * its format string up in the table of the language that is set and prints
 * the translation if there is one, so a message without a translation, or
 * one that has changed since, simply comes out in English.
 *
 * To add a language: write lang/xx.h (copy lang/de.h and replace the second
 * string of each pair), and add a line for it to gLanguages[] below.  The
 * texts are UTF-8.  The display shows the characters its font has (ASCII and
 * ä ö ü Ä Ö Ü ß §, see vt100_put_utf8()); a language that needs others needs
 * glyphs for them in fpga/vga_font.v first.  A translation must take the
 * same printf arguments in the same order as the English.
 * test/run_lang_tests.py checks all of that, and tells which messages a
 * language does not have.
 *
 * Messages printed before the settings have been read from the USB drive -
 * the banner at power-on, "No USB storage detected" - are always English.
 */
#include <stdint.h>

#include "string.h"
#include "messages.h"

struct msg {
   const char *English;
   const char *Text;
};

static const struct msg gGerman[] = {
#include "lang/de.h"
   {0, 0}
};

static const struct {
   const char *Key;           /* in the settings file */
   const char *Name;          /* in the language itself */
   const struct msg *Table;   /* NULL: the texts in the code */
   const char *Yes;           /* keys that mean yes, besides y and Y */
} gLanguages[] = {
   {"en", "English", 0,       ""},
   {"de", "Deutsch", gGerman, "jJ"},
};

#define LANGUAGES ((int) (sizeof(gLanguages) / sizeof(gLanguages[0])))

static int gLanguage;

int LanguageCount(void)
{
   return LANGUAGES;
}

void LanguageSet(int Language)
{
   if(Language < 0 || Language >= LANGUAGES) {
      Language = 0;
   }
   gLanguage = Language;
}

int LanguageGet(void)
{
   return gLanguage;
}

const char *LanguageKey(int Language)
{
   return gLanguages[Language < 0 || Language >= LANGUAGES ? 0 : Language].Key;
}

const char *LanguageName(int Language)
{
   return gLanguages[Language < 0 || Language >= LANGUAGES ? 0 : Language].Name;
}

const char *Translate(const char *English)
{
   const struct msg *p = gLanguages[gLanguage].Table;

   if(p != 0 && English != 0) {
      for( ; p->English != 0; p++) {
         /* most messages differ in their first characters */
         if(p->English[0] == English[0] && strcmp(p->English, English) == 0) {
            return p->Text;
         }
      }
   }
   return English;
}

int IsYes(char Key)
{
   const char *p;

   if(Key == 'y' || Key == 'Y') {
      return 1;
   }
   for(p = gLanguages[gLanguage].Yes; *p != 0; p++) {
      if(*p == Key) {
         return 1;
      }
   }
   return 0;
}
