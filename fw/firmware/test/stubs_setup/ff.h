/* host stub of FatFs for setup.c: one file, kept in memory */
#ifndef FF_STUB_H
#define FF_STUB_H
typedef unsigned int UINT;
typedef enum { FR_OK = 0, FR_DISK_ERR, FR_INT_ERR, FR_NOT_READY, FR_NO_FILE } FRESULT;
typedef struct { int pos; int writing; } FIL;
#define FA_READ 1
#define FA_WRITE 2
#define FA_CREATE_ALWAYS 8
FRESULT f_open(FIL *fp, const char *path, int mode);
FRESULT f_read(FIL *fp, void *buf, UINT n, UINT *got);
FRESULT f_write(FIL *fp, const void *buf, UINT n, UINT *put);
FRESULT f_close(FIL *fp);
#endif
