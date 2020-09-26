#ifndef _FW_TIME_H_
#define _FW_TIME_H_

#include <time.h>

#define CYCLE_PER_US  25
#define CPU_HZ (CYCLE_PER_US * 1000000)

// ticks wraps around every 43s
uint32_t ticks();
uint32_t ticks_us();
uint32_t ticks_ms();
void delay_us(uint32_t us);
void delay_ms(uint32_t ms);
void delay_loop(uint32_t t);

char *strptime(const char *s, const char *format, struct tm *tm);

// Required by our stdlib substitute functions
int is_leap_year (int year);
extern const short __spm[13];

/* seconds per day */
#define SPD 24*60*60

#endif
