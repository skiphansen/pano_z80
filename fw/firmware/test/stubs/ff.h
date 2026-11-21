/* host stub of FatFs: just enough for flash_update.c */
#ifndef FF_STUB_H
#define FF_STUB_H
#include <stdio.h>
#include <stdint.h>
typedef unsigned int UINT;
typedef enum { FR_OK = 0, FR_DISK_ERR, FR_INT_ERR, FR_NOT_READY, FR_NO_FILE } FRESULT;
typedef struct { FILE *f; long size; int err; } FIL;
typedef struct { unsigned long fsize; char fname[13]; } FILINFO;
#define FA_READ 1
#define f_size(fp) ((fp)->size)
FRESULT f_open(FIL *fp, const char *path, int mode);
FRESULT f_read(FIL *fp, void *buf, UINT n, UINT *got);
FRESULT f_lseek(FIL *fp, unsigned long ofs);
FRESULT f_close(FIL *fp);
FRESULT f_stat(const char *path, FILINFO *fno);
FRESULT f_unlink(const char *path);
#endif
