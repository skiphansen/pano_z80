/*
 * Software SPI-flash programming for the Pano G1 (Numonyx M25P80, 1 MB).
 *
 * The firmware executes in place (XIP) directly from this same SPI flash, so
 * the erase/program routines must run from RAM (see the .ramtext section in
 * sections.lds) and must mask interrupts while the flash is in command mode.
 *
 * This is the low level used by the USB firmware/bitstream updater.  A failed
 * update leaves the board recoverable only with a JTAG programmer - exactly the
 * situation before the updater existed.
 */
#ifndef _SPIFLASH_H_
#define _SPIFLASH_H_

#include <stdint.h>

/* Physical flash layout (byte offsets into the 1 MB M25P80).
 *
 *   0x00000 .. 0xBFFFF   FPGA bitstream (sectors 0..11, loaded by the FPGA
 *                        at power-on - the updater's own safety net, since
 *                        it is never read by the CPU and reprogramming it
 *                        cannot disturb the running firmware)
 *   0xC0000 .. 0xDFFFF   RISC-V firmware (sectors 12..13, XIP reset vector)
 *   0xE0000 .. 0xFFFFF   unused (sectors 14..15)
 */
#define FLASH_SECTOR_SIZE   0x10000u     /* 64 KB sector erase granularity   */
#define FLASH_PAGE_SIZE     0x100u       /* 256 byte page program            */

#define FLASH_FW_BASE       0x000C0000u  /* firmware region start            */
#define FLASH_FW_SIZE       0x00020000u  /* firmware region size (128 KB)    */

#define FLASH_BIT_BASE      0x00000000u  /* bitstream region start           */
#define FLASH_BIT_MAXSIZE   0x000C0000u  /* bitstream region size (768 KB)   */

/* JEDEC ID (manufacturer, type, capacity) of the Pano G1's Numonyx M25P80 */
#define FLASH_ID_M25P80     0x00202014u

/* flash_program_image() results */
#define FLASH_ERR_FAILED    1   /* erase / program / verify failed            */
#define FLASH_ERR_NO_FLASH  2   /* flash did not identify; NOTHING was written */

/*
 * Read the flash's 3-byte JEDEC ID using the bit-bang path.  Non-destructive.
 * Returns FLASH_ID_M25P80 on this board.  Used as a pre-flight check: it only
 * returns the right value if RAM execution and every bit-bang signal work.
 *
 * NOTE: like flash_program_image() this masks all interrupts and leaves them
 * masked (see spiflash.c).  The only interrupt in this design is the bus-fault
 * trap, so the firmware keeps running normally.
 */
uint32_t flash_read_id(void);

/*
 * Read `len` bytes of the flash starting at physical offset `addr` into `dst`
 * (which must be in RAM), using the bit-bang path.  Non-destructive.  Lets the
 * updater look at what is really in the flash - including the bitstream
 * region, which is not memory mapped.  Masks interrupts like the others.
 */
void flash_read(uint32_t addr, uint8_t *dst, uint32_t len);

/*
 * Bring up to two flash regions up to date from RAM/DDR source buffers, in one
 * bit-bang session that never fetches an instruction from flash (a length of 0
 * skips that region).  Used by the combined bitstream + firmware updater.
 *
 *   baseN     physical flash byte offset (must be sector aligned)
 *   srcN      source bytes in RAM/DDR (never in flash)
 *   lenN      number of bytes, 0 = skip
 *   no_return MUST be non-zero whenever the firmware region (FLASH_FW_BASE) is
 *             one of the regions: the caller lives in flash that may just have
 *             been rewritten, so instead of returning, the result is shown on
 *             the board LED (green = verified OK, red = failed) and the CPU
 *             halts until power-cycled.
 *
 * The flash's JEDEC ID is checked first; if it is wrong the function returns
 * FLASH_ERR_NO_FLASH with the flash untouched, even when no_return is set.
 * A region whose contents already match is left untouched.  The board LED is
 * yellow while working.
 *
 * Returns (when it returns): 0 on verified success, FLASH_ERR_* otherwise.
 */
int flash_program_image(uint32_t base1, const uint8_t *src1, uint32_t len1,
                        uint32_t base2, const uint8_t *src2, uint32_t len2,
                        int no_return);

/* CRC-32 (IEEE 802.3, poly 0xEDB88320) over a buffer - used to validate a
 * firmware image before flashing.  Lives in normal (flash) code. */
uint32_t crc32(const uint8_t *buf, uint32_t len);

#endif /* _SPIFLASH_H_ */
