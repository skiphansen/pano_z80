/*
 * Borrowed from dietlibc
 * License: GPL
 *
 */

#include "time.h"

/* days per month -- nonleap! */
const short __spm[13] =
  { 0,
    (31),
    (31+28),
    (31+28+31),
    (31+28+31+30),
    (31+28+31+30+31),
    (31+28+31+30+31+30),
    (31+28+31+30+31+30+31),
    (31+28+31+30+31+30+31+31),
    (31+28+31+30+31+30+31+31+30),
    (31+28+31+30+31+30+31+31+30+31),
    (31+28+31+30+31+30+31+31+30+31+30),
    (31+28+31+30+31+30+31+31+30+31+30+31),
  };

struct tm *gmtime_r(const time_t *timep, struct tm *r) {
  time_t i;
  register time_t work=*timep%(SPD);
  r->tm_sec=work%60; work/=60;
  r->tm_min=work%60; r->tm_hour=work/60;
  work=*timep/(SPD);
  r->tm_wday=(4+work)%7;
  for (i=1970; ; ++i) {
    register time_t k=is_leap_year(i)?366:365;
    if (work>=k)
      work-=k;
    else
      break;
  }
  r->tm_year=i-1900;
  r->tm_yday=work;

  r->tm_mday=1;
  if (is_leap_year(i) && (work>58)) {
    if (work==59) r->tm_mday=2; /* 29.2. */
    work-=1;
  }

  for (i=11; i && (__spm[i]>work); --i) ;
  r->tm_mon=i;
  r->tm_mday+=work-__spm[i];
  return r;
}

struct tm* gmtime(const time_t *t) {
  static struct tm tmp;
  return gmtime_r(t,&tmp);
}

