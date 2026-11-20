/*
 * Software SPI-flash programming for the Pano G1.  See spiflash.h.
 *
 * Everything reachable from flash_read_id() / flash_program_image() is placed
 * in the .ramtext section so it executes from RAM, because the SPI flash cannot
 * serve instruction fetches while it is in command mode.  These functions
 * therefore must not touch flash: no string literals, no .rodata tables, no
 * calls into flash-resident code (printf, memcpy, libgcc helpers such as
 * __mulsi3, ...).  Keep them self contained, integer only, and free of
 * multiplies and divides (the CPU is rv32ic - those would be library calls).
 *
 * This file is compiled with -march=rv32i, without compressed instructions
 * (see the Makefile).  The RAM is DDR memory, and the bridge in front of it
 * cannot serve the fetch of a 32-bit instruction that straddles a word
 * boundary; with every instruction one aligned word that never happens.
 *
 * `make check-ramtext` verifies all of this against the linked image.
 */
#include <stdint.h>
#include "spiflash.h"

#define RAMTEXT __attribute__((section(".ramtext"), noinline))

/* spimemio config register, exposed to the CPU by pano_top.v */
#define CFGREG  (*(volatile uint32_t *)0x0E020000u)

/*
 * Config register bit fields (PicoSoC spimemio, fpga/spimemio.v).
 *
 * Bit 31 is "MEMIO enable": it resets to 1, which means the hardware transfer
 * engine owns the flash pins and serves memory-mapped (XIP) reads.  Clearing it
 * hands the pins to the bit-bang fields below and holds the XIP engine in
 * reset; setting it again restarts the engine exactly as at power-on.
 *
 *   spimemio.v:  assign flash_csb = config_en ? xfer_csb : config_csb;
 *
 * So: bit-bang = bit 31 CLEAR, XIP = bit 31 SET.
 */
#define CFG_MEMIO (1u << 31)  /* 1 = XIP engine drives the pins, 0 = bit-bang  */
#define CFG_OE0   (1u << 8)   /* drive io0 (MOSI) as output                    */
#define CFG_CS    (1u << 5)   /* flash_csb: 1 = deasserted, 0 = selected       */
#define CFG_CLK   (1u << 4)   /* flash_clk                                     */
#define CFG_DO0   (1u << 0)   /* io0 output data (MOSI)                         */
/* MISO (io1) is read back in CFGREG bit 1 */

/* SPI flash (M25P80) command opcodes */
#define CMD_WREN   0x06
#define CMD_RDSR   0x05
#define CMD_READ   0x03
#define CMD_PP     0x02
#define CMD_SE     0xD8   /* 64 KB sector erase */
#define CMD_RDID   0x9F   /* JEDEC ID */

/* Bit-bang mode, chip deselected, io0 output, clock low */
#define CFG_IDLE   (CFG_OE0 | CFG_CS)
/* Bit-bang mode, chip selected, clock low */
#define CFG_SEL    (CFG_OE0)
/* Hand the pins back to the hardware XIP engine */
#define CFG_XIP    (CFG_MEMIO)

/* GPIO LED register (pano_top.v: bit0 red, bit1 green, bit2 blue) */
#define LED_REG (*(volatile uint32_t *)0x03000004u)

/*
 * picorv32 maskirq custom instruction: sets the IRQ mask, returns the old one.
 * A set bit disables that IRQ; 0xFFFFFFFF disables all.
 *
 * Interrupts are masked before the first CFGREG access and are deliberately
 * NEVER unmasked again.  Two reasons: the IRQ vector is in flash, which cannot
 * be read in bit-bang mode; and the bus-fault detector in bitstreams built
 * before the cfgreg decode was added to it latches a fault (the only IRQ this
 * design has) on every CFGREG access, which would fire the moment we unmasked.
 * Running on with it masked only costs the "HARD FAULT" diagnostic until the
 * next power-cycle.
 */
RAMTEXT static void irqs_off(void)
{
   uint32_t old;
   uint32_t mask = 0xFFFFFFFFu;
   __asm__ volatile(".insn r 0x0b, 0x0, 0x03, %0, %1, x0"
                    : "=r"(old) : "r"(mask));
   (void)old;
}

/* Shift one byte out (MSB first) and simultaneously in.  Chip must be
 * selected.  The flash samples MOSI on the rising clock edge and changes MISO
 * on the falling edge, so: present data with the clock low, raise the clock,
 * then read MISO while it is still high. */
RAMTEXT static uint8_t spi_byte(uint8_t out)
{
   uint8_t in = 0;
   int i;
   for(i = 7; i >= 0; i--) {
      uint32_t dobit = ((out >> i) & 1u) ? CFG_DO0 : 0u;
      CFGREG = CFG_SEL | dobit;             /* clock low, data valid        */
      CFGREG = CFG_SEL | dobit | CFG_CLK;   /* clock high: shift            */
      in = (uint8_t)((in << 1) | ((CFGREG >> 1) & 1u));
   }
   CFGREG = CFG_SEL;                        /* leave clock low              */
   return in;
}

RAMTEXT static void spi_select(void)   { CFGREG = CFG_SEL; }
RAMTEXT static void spi_deselect(void) { CFGREG = CFG_IDLE; }

/* Send a single opcode with CS cycled (e.g. WREN) */
RAMTEXT static void spi_cmd(uint8_t cmd)
{
   spi_select();
   spi_byte(cmd);
   spi_deselect();
}

/* Read the status register (bit 0 = WIP, write-in-progress) */
RAMTEXT static uint8_t spi_status(void)
{
   uint8_t s;
   spi_select();
   spi_byte(CMD_RDSR);
   s = spi_byte(0);
   spi_deselect();
   return s;
}

/*
 * Wait for the write-in-progress bit to clear.  Returns 1 when it does, 0 if
 * it never does.  The bound is a plain poll count: each poll is 16 bit-banged
 * clocks, i.e. at least ~50 bus accesses of several CPU cycles each, so one
 * poll takes no less than ~8 us at 25 MHz (in simulation, with the DDR bridge
 * the code is fetched through: ~100 us).
 * 1,000,000 polls is therefore at least 8 s and in practice over a minute -
 * comfortably beyond the M25P80's worst-case 3 s sector erase - yet a stuck
 * bus can no longer hang here forever: it shows up as a failed (red LED)
 * update instead.
 */
RAMTEXT static int spi_wait_wip(void)
{
   uint32_t n;
   for(n = 0; n < 1000000u; n++) {
      if((spi_status() & 1u) == 0)
         return 1;
   }
   return 0;
}

RAMTEXT static void spi_send_addr(uint32_t addr)
{
   spi_byte((uint8_t)(addr >> 16));
   spi_byte((uint8_t)(addr >> 8));
   spi_byte((uint8_t)(addr));
}

RAMTEXT static int spi_sector_erase(uint32_t addr)
{
   spi_cmd(CMD_WREN);
   spi_select();
   spi_byte(CMD_SE);
   spi_send_addr(addr);
   spi_deselect();
   return spi_wait_wip();
}

RAMTEXT static int spi_page_program(uint32_t addr, const uint8_t *src, uint32_t n)
{
   uint32_t i;
   spi_cmd(CMD_WREN);
   spi_select();
   spi_byte(CMD_PP);
   spi_send_addr(addr);
   for(i = 0; i < n; i++)
      spi_byte(src[i]);
   spi_deselect();
   return spi_wait_wip();
}

/* Stream-read the region and compare against src.  Returns 1 if identical. */
RAMTEXT static int spi_verify(uint32_t base, const uint8_t *src, uint32_t len)
{
   uint32_t i;
   int ok = 1;
   spi_select();
   spi_byte(CMD_READ);
   spi_send_addr(base);
   for(i = 0; i < len; i++) {
      if(spi_byte(0) != src[i]) {
         ok = 0;
         break;
      }
   }
   spi_deselect();
   return ok;
}

/* Read the 3-byte JEDEC ID.  Must already be in bit-bang mode. */
RAMTEXT static uint32_t spi_read_id(void)
{
   uint32_t id;
   spi_select();
   spi_byte(CMD_RDID);
   id  = (uint32_t)spi_byte(0) << 16;
   id |= (uint32_t)spi_byte(0) << 8;
   id |= (uint32_t)spi_byte(0);
   spi_deselect();
   return id;
}

/* Bring ONE region up to date.  Must already be in bit-bang mode.
 * Returns 1 if the region ends up identical to src, 0 otherwise.
 *
 * A region that already matches is left completely untouched.  A combined
 * update usually carries an unchanged bitstream, so this means the FPGA
 * configuration is only ever erased when it really has to change. */
RAMTEXT static int flash_do_region(uint32_t base, const uint8_t *src, uint32_t len)
{
   uint32_t a;
   uint32_t off;

   if(spi_verify(base, src, len))
      return 1;

   /* Erase each 64 KB sector the image touches */
   for(a = base; a < base + len; a += FLASH_SECTOR_SIZE) {
      if(!spi_sector_erase(a))
         return 0;
   }

   /* Program a page at a time */
   for(off = 0; off < len; off += FLASH_PAGE_SIZE) {
      uint32_t n = len - off;
      if(n > FLASH_PAGE_SIZE)
         n = FLASH_PAGE_SIZE;
      if(!spi_page_program(base + off, src + off, n))
         return 0;
   }

   /* Read back and verify before we trust it */
   return spi_verify(base, src, len);
}

RAMTEXT uint32_t flash_read_id(void)
{
   uint32_t id;

   irqs_off();
   CFGREG = CFG_IDLE;         /* bit-bang mode */
   id = spi_read_id();
   CFGREG = CFG_XIP;          /* back to XIP: nothing was modified */
   return id;
}

RAMTEXT void flash_read(uint32_t addr, uint8_t *dst, uint32_t len)
{
   uint32_t i;

   irqs_off();
   CFGREG = CFG_IDLE;         /* bit-bang mode */
   spi_select();
   spi_byte(CMD_READ);
   spi_send_addr(addr);
   for(i = 0; i < len; i++)
      dst[i] = spi_byte(0);
   spi_deselect();
   CFGREG = CFG_XIP;          /* back to XIP: nothing was modified */
}

RAMTEXT int flash_program_image(uint32_t base1, const uint8_t *src1, uint32_t len1,
                                uint32_t base2, const uint8_t *src2, uint32_t len2,
                                int no_return)
{
   int ok = 1;

   irqs_off();
   CFGREG = CFG_IDLE;         /* bit-bang mode (also holds XIP in reset) */

   /* Pre-flight: prove the whole bit-bang path (RAM execution, chip select,
    * clock, MOSI, MISO) against the real chip before anything is erased.  If
    * the ID is not what we expect nothing has been modified, so it is safe to
    * hand the pins back and return to the caller, even when no_return is set. */
   if(spi_read_id() != FLASH_ID_M25P80) {
      CFGREG = CFG_XIP;
      return FLASH_ERR_NO_FLASH;
   }

   LED_REG = 0x3u;            /* red + green = yellow: working */

   if(len1)
      ok = flash_do_region(base1, src1, len1);
   if(ok && len2)
      ok = flash_do_region(base2, src2, len2);

   CFGREG = CFG_XIP;          /* restart the hardware XIP engine */

   if(no_return) {
      /* The flash we execute from may have just been rewritten; never return.
       * Signal the result on the LED and halt - the user must power-cycle. */
      LED_REG = ok ? 0x2u : 0x1u;   /* green = OK, red = failed */
      for(;;)
         ;
   }

   LED_REG = 0;
   return ok ? 0 : FLASH_ERR_FAILED;
}

/* ------------------------------------------------------------------------- */
/* CRC-32 - normal (flash) code, used to validate an image before flashing.  */

uint32_t crc32(const uint8_t *buf, uint32_t len)
{
   /* A byte at a time through a table: an update image is 800 KB, and bit by
    * bit that took this CPU several seconds.  The table lives in RAM and is
    * built on first use, so it costs no flash and no flash cache misses. */
   static uint32_t Table[256];
   static uint8_t TableReady;
   uint32_t crc = 0xFFFFFFFFu;
   uint32_t i;
   int b;

   if(!TableReady) {
      for(i = 0; i < 256; i++) {
         uint32_t c = i;
         for(b = 0; b < 8; b++)
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1u)));
         Table[i] = c;
      }
      TableReady = 1;
   }
   for(i = 0; i < len; i++)
      crc = (crc >> 8) ^ Table[(crc ^ buf[i]) & 0xFFu];
   return ~crc;
}
