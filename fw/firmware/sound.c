/*
 * Sound and the Z80's speed.
 *
 * The ITT 3030 has a beeper that bit 0 of I/O port 32h switches on and off.
 * The bitstream handles that port itself (fpga/cpm_io.v) and plays it through
 * the Pano's WM8750 codec (fpga/audio_beeper.v): a tune toggling the bit at a
 * note's frequency comes out at that pitch, a bit held on beeps at 3250 Hz.  What is left for the firmware: set the codec up over
 * I2C, switch the sound on or off, sound the bell when a program prints BEL,
 * and set the Z80's speed.
 *
 * The speed matters because programs written for the ITT time their tones
 * and delays with loops: at the Pano's 25 MHz a tune is six times too fast
 * and too high.  At 4 MHz it is as on the ITT.
 *
 * All of it needs a bitstream that has it (FPGA_FEATURE_SOUND); with an older
 * one these functions keep the settings but touch no hardware.
 */
#include <stdint.h>
#include <stdbool.h>

#include "misc.h"
#include "cpm_io.h"
#include "fwinfo.h"
#include "i2c.h"
#include "log.h"
#include "sound.h"

#define sound_ctrl      IO_INTERFACE(0x38)
#define SOUND_ON        0x01
#define SOUND_BELL      0x02
#define z80_speed       IO_INTERFACE(0x3c)

#define BELL_MS         100

/* The WM8750: CSB is tied low, so its I2C address is 0x34 (write).  A
 * register write is 7 bits of register number and 9 bits of data. */
#define WM8750_I2C      0x34

static const struct {
   uint8_t Reg;
   uint16_t Val;
} gCodecSetup[] = {
   {15, 0x000},   /* reset */
   /* The Pano's speaker is wired across LOUT2 and ROUT2, so ROUT2 has to be
    * inverted or the same signal on both ends cancels out; the headphone
    * switch turns the speaker off when headphones are plugged in.  These two
    * as in the VerilogBoy project's Pano port (target/panog1/fw/firmware/
    * wm8750.c), which had working sound. */
   {23, 0x1c0},   /* additional control 1: thermal shutdown on, 3.3 V */
   {24, 0x050},   /* additional control 2: headphone switch on, ROUT2 inverted */
   {25, 0x0c0},   /* power 1: VMID 50k divider (playback), VREF */
   {26, 0x1f8},   /* power 2: left and right DAC, LOUT1/ROUT1, LOUT2/ROUT2 */
   {7,  0x002},   /* audio interface: slave, 16 bit, I2S */
   {8,  0x000},   /* sample rate: normal mode, MCLK = 256 fs */
   {34, 0x150},   /* left mixer: left DAC to left output */
   {37, 0x150},   /* right mixer: right DAC to right output */
   {2,  0x06f},   /* LOUT1 -10 dB (applied with ROUT1) */
   {3,  0x16f},   /* ROUT1 -10 dB, update both */
   {40, 0x073},   /* LOUT2 (speaker) -6 dB */
   {41, 0x173},   /* ROUT2 (speaker) -6 dB, update both */
   {5,  0x000},   /* DAC soft mute off */
};

static bool gSoundOn = true;
static bool gBell;
static uint32_t gBellEnd;
static int gZ80Mhz = Z80_MHZ_FULL;

bool SoundAvailable(void)
{
   return (FpgaFeatures() & FPGA_FEATURE_SOUND) != 0;
}

static void SoundUpdate(void)
{
   if(SoundAvailable()) {
      sound_ctrl = (gSoundOn ? SOUND_ON : 0) | (gBell ? SOUND_BELL : 0);
   }
}

void SoundInit(void)
{
   unsigned int i;

   if(!SoundAvailable()) {
      return;
   }
   for(i = 0; i < sizeof(gCodecSetup) / sizeof(gCodecSetup[0]); i++) {
      uint16_t Val = gCodecSetup[i].Val;
      if(!i2c_write_reg(WM8750_I2C,
                        (uint8_t) ((gCodecSetup[i].Reg << 1) | (Val >> 8)),
                        (uint8_t) Val)) {
         ALOG_R("Sound: the audio codec does not answer.\n");
         break;
      }
   }
   SoundUpdate();
   Z80SpeedSet(gZ80Mhz);
}

void SoundSet(bool On)
{
   gSoundOn = On;
   SoundUpdate();
}

bool SoundGet(void)
{
   return gSoundOn;
}

void Z80SpeedSet(int Mhz)
{
   if(Mhz < 0 || Mhz > Z80_MHZ_MAX) {
      Mhz = Z80_MHZ_FULL;
   }
   gZ80Mhz = Mhz;
   if(SoundAvailable()) {
      z80_speed = (uint32_t) Mhz;
   }
}

int Z80SpeedGet(void)
{
   return gZ80Mhz;
}

void Bell(void)
{
   gBell = true;
   gBellEnd = ticks_ms() + BELL_MS;
   SoundUpdate();
}

void SoundPoll(void)
{
   if(gBell && ticks_ms() >= gBellEnd) {
      gBell = false;
      SoundUpdate();
   }
}
