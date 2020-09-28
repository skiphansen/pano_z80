#include "rtc.h"
#include "stdio.h"
#include "string.h"
#include "vt100.h"
#include <limits.h>

static time_t unix_time;
static uint32_t prev_ticks;

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
   unix_time = utime;
   prev_ticks = ticks();
   have_rtc = 1;
}

struct tm *rtc_read() {
   return gmtime(&unix_time);
}

void rtc_poll(void) {
   uint32_t curr_ticks;
   uint32_t elapsed_ticks;

   if (!have_rtc) return;
   
   curr_ticks = ticks();
   if(curr_ticks > prev_ticks) {
      elapsed_ticks = curr_ticks - prev_ticks;
   }
   else {
      // assume a single ounter wrap. No legitimate RISCV based
      // operation should span multiples.
      elapsed_ticks = (UINT_MAX - prev_ticks) + 1 + curr_ticks;
   }
   if(elapsed_ticks > CPU_HZ) {
      // has been at least 1 sec since last counter read
      unix_time += elapsed_ticks / CPU_HZ;
      // ALOG_R("\r%d", unix_time);
   }
   // try to avoid accumulated loss by rounding back to an
   // even second.
   prev_ticks = curr_ticks - (curr_ticks % CPU_HZ);
}

/* 
 * Local Variables:
 * c-basic-offset: 3
 * End:
 */
