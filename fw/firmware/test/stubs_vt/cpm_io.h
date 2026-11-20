/* host stub: the display memory is an array */
#include <stdint.h>
#define SCREEN_X 80
#define SCREEN_Y 30
extern uint32_t host_vram[SCREEN_X * SCREEN_Y];
#define VRAM_ADR ((uintptr_t) host_vram)
