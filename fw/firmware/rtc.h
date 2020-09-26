#ifndef _RTC_H_
#define _RTC_H_

#include "time.h"

time_t dateparse(const char *dtstring, size_t buflen);
void rtc_init(time_t utime);
void rtc_poll();
struct tm *rtc_read();
  
void readline(char *buf, int max);

extern int have_rtc;

#endif
