/*
 * Settings and the setup menu (F12).  See setup.c.
 */
#ifndef _SETUP_H_
#define _SETUP_H_

/* Screen colours, in the order F6 cycles through them.  Green is where it
 * starts, as it always has; amber came later and is at the end. */
enum {
   COLOR_GREEN = 0,
   COLOR_WHITE,
   COLOR_PAPER,      /* black on white */
   COLOR_AMBER,
   COLOR_COUNT
};

void ScreenColorSet(int Color);
int ScreenColorGet(void);

/* Read PANOZ80.CFG from the USB drive and apply it.  Call once the drive is
 * mounted; without a file, or without a drive, the defaults stay. */
void SettingsLoad(void);

/* Write the current settings to PANOZ80.CFG.  Returns 0 if that worked. */
int SettingsSave(void);

/* The setup menu.  Blocks until the user leaves it; the screen is put back
 * the way it was. */
void SetupMenu(void);

#endif
