#ifndef _FW_STRING_H_
#define _FW_STRING_H_

#include <string.h>
#include <stdint.h>

// Don't want c lib macro!
#undef isspace
int isspace(int c);

#undef tolower
int tolower(int c);

#undef isdigit
int isdigit(int c);

#undef isprint
extern int isprint(int c);

void * memscan(void * addr, int c, uint32_t size);

#endif
