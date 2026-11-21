#include <stdint.h>
extern uint32_t font_fg_color, font_bg_color;
#define BLACK 0
#define WHITE 0xffffff
#define GREEN 0x00ff00
#define AMBER 0xffb000
void IdlePoll(void);
enum { SWAP_NONE = 0, SWAP_RELOGIN, SWAP_REBOOT };
int SwapDisk(void);
extern unsigned char gZ80_ResetRequest;
