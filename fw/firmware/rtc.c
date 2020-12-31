#include "rtc.h"
#include "stdio.h"
#include "string.h"
#include "vt100.h"
#include <limits.h>
#include "cpm_io.h"

int have_rtc = 0;

void readline(char *buf, int max) {
   char c;
   int pos = 0;
   while ((c = getchar()) != '\r' && pos < max-1) {
      // Map DEL and Ctrl-H to destructive backspace
      if (c == '\x7f' || c == '\b') {
         if (pos > 0) {
            vt100_putc('\b');
            vt100_putc('\x7f');
            pos--;
         }
      }
      else if (isprint(c)) {
         vt100_putc(c);
         buf[pos++] = c;
      }
   }
   buf[pos] = '\0';
}

time_t dateparse(const char *dtstring, size_t buflen) {
   struct tm tm;
   char* end;
   int len = strnlen(dtstring, buflen);
   memset (&tm, '\x0', sizeof(struct tm));
   end = strptime(dtstring, "%D %H:%M:%S", &tm);
   if (end == NULL) return 0;
   else             return mktime(&tm);
}

void rtc_init(time_t utime) {
   SEC_COUNTER = utime;
   have_rtc = 1;
}

struct tm *rtc_read() {
   time_t unix_time = SEC_COUNTER;
   return gmtime(&unix_time);
}

/* 
 * Local Variables:
 * c-basic-offset: 3
 * End:
 */
