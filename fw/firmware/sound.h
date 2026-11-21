/*
 * Sound (the ITT 3030 beeper) and the Z80's speed.  See sound.c.
 */
#ifndef _SOUND_H_
#define _SOUND_H_

#include <stdbool.h>

/* Does the bitstream have sound and the speed register? */
bool SoundAvailable(void);

/* Set the codec up.  Call once at power-on; does nothing with a bitstream
 * that has no sound. */
void SoundInit(void);

/* Sound on or off: the beeper (I/O port 32h) and the bell */
void SoundSet(bool On);
bool SoundGet(void);

/* The Z80's clock in MHz; 0 = full speed (25 MHz) */
#define Z80_MHZ_FULL    0
#define Z80_MHZ_ITT3030 4
#define Z80_MHZ_MAX     24      /* above this it is full speed anyway */
void Z80SpeedSet(int Mhz);
int Z80SpeedGet(void);

/* A short beep, for the BEL character.  Returns at once; SoundPoll() ends
 * the beep. */
void Bell(void);
void SoundPoll(void);

#endif
