/* Test program for the flash-update simulation.  Linked with the firmware's
 * real start.s, sections.lds and spiflash.c. */
#include <stdint.h>
#include "spiflash.h"

#define REPORT (*(volatile uint32_t *)0x03000008u)
#define IMAGE_BUF ((uint8_t *)0x0C100000u)
#ifndef BITLEN
#define BITLEN 700
#endif
#ifndef FWLEN
#define FWLEN 300
#endif

void irq_handler(uint32_t pc)
{
   REPORT = 0xDEAD0000u | (pc & 0xFFFFu);     /* "HARD FAULT" */
   for(;;) ;
}

void main(void)
{
   uint32_t i;
   uint8_t *b = IMAGE_BUF;
   uint8_t *f = IMAGE_BUF + 0x1000;
   int r;

   __asm__ volatile(".word 0x0600000b");      /* unmask IRQs, like firmware.c */
   REPORT = 0x11110000u;

   REPORT = flash_read_id();                  /* runs from flash again afterwards */
   {
      uint8_t head[8];                        /* on the stack: internal RAM */
      flash_read(FLASH_BIT_BASE, head, sizeof(head));
      REPORT = ((uint32_t)head[0] << 24) | ((uint32_t)head[1] << 16) | ((uint32_t)head[2] << 8) | head[3];
      REPORT = ((uint32_t)head[4] << 24) | ((uint32_t)head[5] << 16) | ((uint32_t)head[6] << 8) | head[7];
   }
#ifdef UNMASK_AFTER_ID
   __asm__ volatile(".word 0x0600000b");      /* what the old code effectively did */
   REPORT = 0x33330000u;
#endif
#ifdef ID_ONLY
   REPORT = 0x0D0E0000u;                      /* done */
   for(;;) ;
#endif

   for(i = 0; i < BITLEN; i++)
      b[i] = (uint8_t)((i + 0x5A) ^ (i >> 8));
   for(i = 0; i < FWLEN; i++)
      f[i] = (uint8_t)((~i) ^ 0x3C ^ (i >> 8));
   REPORT = 0x22220000u;

   r = flash_program_image(FLASH_BIT_BASE, b, BITLEN, FLASH_FW_BASE, f, FWLEN, 1);
   REPORT = 0xBAD00000u | (uint32_t)r;        /* only reached if nothing was written */
   for(;;) ;
}
