/* Identification of the installed firmware and bitstream - see fwinfo.h.
 *
 * This file is recompiled on every build (see the Makefile) so that the build
 * date below is the date of the build, not of the last edit.  The Makefile
 * passes the version and the build time; the latter in the same form as
 * FormatBuildId() prints a bitstream's, so the two can be compared. */
#include "fwinfo.h"

#ifndef FW_VERSION
#define FW_VERSION "unknown"
#endif
#ifndef FW_BUILT
#define FW_BUILT __DATE__ " " __TIME__
#endif

const struct fw_info gFwInfo = {
   {'P', 'A', 'N', 'O', 'F', 'W', 'I', 'D'},
   FW_VERSION,
   FW_BUILT
};

/* A field is acceptable if it is NUL terminated inside its 24 bytes and holds
 * printable ASCII up to there. */
static int FieldOk(const char *p)
{
   int i;
   for(i = 0; i < FWINFO_STR_LEN; i++) {
      if(p[i] == '\0')
         return i > 0;
      if(p[i] < ' ' || p[i] > '~')
         return 0;
   }
   return 0;
}

const struct fw_info *FwInfoFind(const uint8_t *Buf, uint32_t Len)
{
   uint32_t i;
   int j;

   if(Len < sizeof(struct fw_info))
      return 0;
   /* Compare against our own block's magic rather than a string literal, so
    * that the only place the magic occurs in an image is its info block. */
   for(i = 0; i + sizeof(struct fw_info) <= Len; i++) {
      for(j = 0; j < FWINFO_MAGIC_LEN && Buf[i + j] == (uint8_t)gFwInfo.magic[j]; j++)
         ;
      if(j == FWINFO_MAGIC_LEN) {
         const struct fw_info *p = (const struct fw_info *)(Buf + i);
         if(FieldOk(p->version) && FieldOk(p->built))
            return p;
      }
   }
   return 0;
}

/* GPIO registers added to pano_top.v together with the BUILD_ID parameter.
 * A bitstream without them returns whatever the GPIO block read last, which
 * is never the magic, so this is safe to call on any bitstream. */
#ifndef FPGA_BUILD_MAGIC_REG
#define FPGA_BUILD_MAGIC_REG  (*(volatile uint32_t *)0x03000038u)
#define FPGA_BUILD_ID_REG     (*(volatile uint32_t *)0x0300003Cu)
#endif
#define FPGA_BUILD_MAGIC      0x42494431u    /* "BID1" */

uint32_t FpgaBuildId(void)
{
   uint32_t Id;

   if(FPGA_BUILD_MAGIC_REG != FPGA_BUILD_MAGIC)
      return 0;
   Id = FPGA_BUILD_ID_REG;
   return BUILD_ID_VALID(Id) ? Id : 0;
}

static char *Put(char *p, uint32_t v, int digits)
{
   int i;
   for(i = digits - 1; i >= 0; i--) {
      p[i] = (char)('0' + v % 10);
      v /= 10;
   }
   return p + digits;
}

void FormatBuildId(uint32_t Id, char *Out)
{
   /* days since 1970-01-01 -> civil date (proleptic Gregorian) */
   uint32_t days = Id / 86400u;
   uint32_t secs = Id % 86400u;
   uint32_t z = days + 719468u;
   uint32_t era = z / 146097u;
   uint32_t doe = z - era * 146097u;
   uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
   uint32_t y = yoe + era * 400u;
   uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
   uint32_t mp = (5u * doy + 2u) / 153u;
   uint32_t d = doy - (153u * mp + 2u) / 5u + 1u;
   uint32_t m = mp < 10u ? mp + 3u : mp - 9u;
   char *p = Out;

   if(m <= 2u)
      y++;
   p = Put(p, y, 4); *p++ = '-';
   p = Put(p, m, 2); *p++ = '-';
   p = Put(p, d, 2); *p++ = ' ';
   p = Put(p, secs / 3600u, 2); *p++ = ':';
   p = Put(p, (secs % 3600u) / 60u, 2);
   *p++ = ' '; *p++ = 'U'; *p++ = 'T'; *p++ = 'C'; *p = '\0';
}
