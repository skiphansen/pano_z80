/*
 * Identification of the installed firmware and FPGA bitstream.
 *
 * Every firmware image contains one `struct fw_info`, recognisable by its
 * magic.  The running firmware prints its own, and the USB updater looks for
 * the same block inside the firmware part of an update image, so "what is
 * installed" and "what would be installed" are both read from the binaries
 * themselves.  tools/mkupdate.py reads it too.
 *
 * The bitstream carries a build ID - the time it was built, in seconds since
 * 1970 UTC - set by xilinx/build.tcl.  The running one is read from the GPIO
 * block; the one in a .bit file is in its header as the Xilinx UserID.
 */
#ifndef _FWINFO_H_
#define _FWINFO_H_

#include <stdint.h>

#define FWINFO_MAGIC_LEN   8
#define FWINFO_STR_LEN     24

struct fw_info {
   char magic[FWINFO_MAGIC_LEN];    /* "PANOFWID", not NUL terminated      */
   char version[FWINFO_STR_LEN];    /* e.g. git describe, NUL padded       */
   char built[FWINFO_STR_LEN];      /* "2026-11-19 21:28 UTC", NUL padded  */
};

extern const struct fw_info gFwInfo;

/* Find the info block in a firmware image.  NULL if there is none. */
const struct fw_info *FwInfoFind(const uint8_t *Buf, uint32_t Len);

/* Build ID of the running bitstream; 0 if it has none (bitstreams built
 * before build IDs existed, or not built with xilinx/build.tcl). */
uint32_t FpgaBuildId(void);

/* What the running bitstream can do beyond the original design; 0 for a
 * bitstream that cannot say. */
#define FPGA_FEATURE_TEXT_ATTR   0x0001u  /* inverse video per character cell
                                           * (bit 8 of a screen memory word),
                                           * block mosaic glyphs C0-CF, D0 */
uint32_t FpgaFeatures(void);

/* Is this a usable build ID?  (0 and 0xFFFFFFFF both mean "not set") */
#define BUILD_ID_VALID(Id)  ((Id) != 0u && (Id) != 0xFFFFFFFFu)

/* "2026-11-19 11:56 UTC" from a build ID.  Out must hold 24 bytes. */
void FormatBuildId(uint32_t Id, char *Out);

#endif /* _FWINFO_H_ */
