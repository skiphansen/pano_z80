
#include <stdint.h>
#include <limits.h>
#include "string.h"

size_t strnlen(const char* str, size_t maxsize) {
  const char* s;
  for (s = str; *s && maxsize--; ++s);
  return (unsigned int)(s - str);
}

/*
 * Convert a string to a long integer.
 */
long strtol (const char *__restrict nptr,
             char **__restrict endptr, int base) {
  register const unsigned char *s = (const unsigned char *)nptr;
  register unsigned long acc;
  register int c;
  register unsigned long cutoff;
  register int neg = 0, any, cutlim;

  if (base < 0 || base == 1 || base > 36) {
    return 0;
  }

  /*
   * Skip white space and pick up leading +/- sign if any.
   * If base is 0, allow 0x for hex and 0 for octal, else
   * assume decimal; if base is already 16, allow 0x.
   */
  do {
    c = *s++;
  } while (isspace(c));
  if (c == '-') {
    neg = 1;
    c = *s++;
  } else if (c == '+')
    c = *s++;
  if ((base == 0 || base == 16) &&
      c == '0' && (*s == 'x' || *s == 'X')) {
    c = s[1];
    s += 2;
    base = 16;
  }
  if (base == 0)
    base = c == '0' ? 8 : 10;

  /*
   * Compute the cutoff value between legal numbers and illegal
   * numbers.  That is the largest legal value, divided by the
   * base.  An input number that is greater than this value, if
   * followed by a legal input character, is too big.  One that
   * is equal to this value may be valid or not; the limit
   * between valid and invalid numbers is then based on the last
   * digit.  For instance, if the range for longs is
   * [-2147483648..2147483647] and the input base is 10,
   * cutoff will be set to 214748364 and cutlim to either
   * 7 (neg==0) or 8 (neg==1), meaning that if we have accumulated
   * a value > 214748364, or equal but the next digit is > 7 (or 8),
   * the number is too big, and we will return a range error.
   *
   * Set any if any `digits' consumed; make it negative to indicate
   * overflow.
   */
  cutoff = neg ? -(unsigned long)LONG_MIN : LONG_MAX;
  cutlim = cutoff % (unsigned long)base;
  cutoff /= (unsigned long)base;
  for (acc = 0, any = 0;; c = *s++) {
    if (c >= '0' && c <= '9')
      c -= '0';
    else if (c >= 'A' && c <= 'Z')
      c -= 'A' - 10;
    else if (c >= 'a' && c <= 'z')
      c -= 'a' - 10;
    else
      break;
    if (c >= base)
      break;
    if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim)) {
      any = -1;
    } else {
      any = 1;
      acc *= base;
      acc += c;
    }
  }
  if (neg)
    acc = -acc;
  if (endptr != 0)
    *endptr = (char *) (any ? (char *)s - 1 : nptr);
  return (acc);
}

int strncasecmp (const char *s1,
                 const char *s2,
                 size_t n) {
  int d = 0;
  for ( ; n != 0; n--)
  {
    const int c1 = tolower(*s1++);
    const int c2 = tolower(*s2++);
    if (((d = c1 - c2) != 0) || (c2 == '\0'))
      break;
  }
  return d;
}

// internal ASCII string to unsigned int conversion
int atoi(const char* str) {
  unsigned int i = 0U;
  while (isdigit(*str)) {
    i = i * 10U + (unsigned int)((*str++) - '0');
  }
  return i;
}

void *memcpy(void *aa, const void *bb, size_t n)
{
   // printf("**MEMCPY**\n");
   char *a = aa;
   const char *b = bb;
   while (n--) *(a++) = *(b++);
   return aa;
}

void *memset(void *s, int c, size_t n)
{
    uint8_t *ptr = (uint8_t *)s;
    while (n--) *ptr++ = c; 
}

void * memscan(void * addr, int c, uint32_t size)
{
	unsigned char * p = (unsigned char *) addr;

	while (size) {
		if (*p == c)
			return (void *) p;
		p++;
		size--;
	}
	return (void *) p;
}

char *strcpy(char* dst, const char* src)
{
   char *r = dst;

   while ((((uint32_t)dst | (uint32_t)src) & 3) != 0)
   {
      char c = *(src++);
      *(dst++) = c;
      if (!c) return r;
   }

   while (1)
   {
      uint32_t v = *(uint32_t*)src;

      if (__builtin_expect((((v) - 0x01010101UL) & ~(v) & 0x80808080UL), 0))
      {
         dst[0] = v & 0xff;
         if ((v & 0xff) == 0)
            return r;
         v = v >> 8;

         dst[1] = v & 0xff;
         if ((v & 0xff) == 0)
            return r;
         v = v >> 8;

         dst[2] = v & 0xff;
         if ((v & 0xff) == 0)
            return r;
         v = v >> 8;

         dst[3] = v & 0xff;
         return r;
      }

      *(uint32_t*)dst = v;
      src += 4;
      dst += 4;
   }
}

int strcmp(const char *s1, const char *s2)
{
   while ((((uint32_t)s1 | (uint32_t)s2) & 3) != 0)
   {
      char c1 = *(s1++);
      char c2 = *(s2++);

      if (c1 != c2)
         return c1 < c2 ? -1 : +1;
      else if (!c1)
         return 0;
   }

   while (1)
   {
      uint32_t v1 = *(uint32_t*)s1;
      uint32_t v2 = *(uint32_t*)s2;

      if (__builtin_expect(v1 != v2, 0))
      {
         char c1, c2;

         c1 = v1 & 0xff, c2 = v2 & 0xff;
         if (c1 != c2) return c1 < c2 ? -1 : +1;
         if (!c1) return 0;
         v1 = v1 >> 8, v2 = v2 >> 8;

         c1 = v1 & 0xff, c2 = v2 & 0xff;
         if (c1 != c2) return c1 < c2 ? -1 : +1;
         if (!c1) return 0;
         v1 = v1 >> 8, v2 = v2 >> 8;

         c1 = v1 & 0xff, c2 = v2 & 0xff;
         if (c1 != c2) return c1 < c2 ? -1 : +1;
         if (!c1) return 0;
         v1 = v1 >> 8, v2 = v2 >> 8;

         c1 = v1 & 0xff, c2 = v2 & 0xff;
         if (c1 != c2) return c1 < c2 ? -1 : +1;
         return 0;
      }

      if (__builtin_expect((((v1) - 0x01010101UL) & ~(v1) & 0x80808080UL), 0))
         return 0;

      s1 += 4;
      s2 += 4;
   }
}

char *strstr(const char *searchee,const char *lookfor)
{
   /* Less code size, but quadratic performance in the worst case.  */
   if(*searchee == 0) {
      if(*lookfor)
         return(char *) 0;
      return(char *) searchee;
   }

   while(*searchee) {
      size_t i;
      i = 0;

      while(1) {
         if(lookfor[i] == 0) {
            return(char *) searchee;
         }

         if(lookfor[i] != searchee[i]) {
            break;
         }
         i++;
      }
      searchee++;
   }

   return(char *) 0;
}

