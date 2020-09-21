

#include "string.h"

int isspace(int c) {
  switch (c) {
  case ' ':
  case '\f':
  case '\n':
  case '\r':
  case '\t':
  case '\v':
    return 1;
    break;
  default:
    return 0;
    break;
  }
}
    
int tolower(int c) {
  return (c >= 'A' && c <= 'Z') ? ('a' + c - 'A') : c;
}
 
int isdigit(int c) {
  return (c >= '0') && (c <= '9');
}

int isprint(int c) {
  return (c > 0x1f)&&(c < 0x7f);
}

