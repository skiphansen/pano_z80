/*
 * The firmware's messages in other languages.  See messages.c.
 */
#ifndef _MESSAGES_H_
#define _MESSAGES_H_

/* The text of a message in the language that is set: English is the text
 * itself, another language has it in its table (lang/xx.h) or, if it has
 * not, gets the English.  ALOG_R() does this to its format string, so only
 * words that are handed to a message as arguments need it spelled out. */
const char *Translate(const char *English);
#define TR(English)     Translate(English)

/* Marks a word in a table as one that will be handed to Translate() later,
 * for the check of the translations (test/run_lang_tests.py) to find it. */
#define TR_N(English)   English

int LanguageCount(void);
void LanguageSet(int Language);
int LanguageGet(void);
const char *LanguageKey(int Language);    /* in the settings file: "en", "de" */
const char *LanguageName(int Language);   /* in its own language: "Deutsch" */

/* Does this key mean yes?  'y', and what the current language says instead. */
int IsYes(char Key);

#endif
