#include <stdio.h>
const char *Translate(const char *English);
#define ALOG_R(format, ...) printf(Translate(format), ##__VA_ARGS__)
