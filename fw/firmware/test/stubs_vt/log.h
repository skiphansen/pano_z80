#include <stdio.h>
#define LOG(...)
#define LOG_R(...) 0
#define VLOG(...)
#define ELOG(...) fprintf(stderr, __VA_ARGS__)
#define ALOG_R(...) printf(__VA_ARGS__)
