#include <stdio.h>

#include "usb.h"
#include "cpm_io.h"

int getchar() {
  while(!usb_kbd_testc()) {
    IdlePoll();
  }
  return usb_kbd_getc();
}

