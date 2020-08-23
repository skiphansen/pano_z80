/*
 *  Copyright (C) 2019  Skip Hansen
 * 
 *  This file is derived from Verilogboy project:
 *  Copyright (C) 2019  Wenting Zhang <zephray@outlook.com>
 * 
 * (C) Copyright 2001
 * Denis Peter, MPL AG Switzerland
 *
 * Part of this source has been derived from the Linux USB
 * project.
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "misc.h"
#include "usb.h"

// #define LOG_TO_SERIAL
// #define DEBUG_LOGGING
// #define VERBOSE_DEBUG_LOGGING
#include "log.h"

// Define the following to send ANSI escape sequences for certain special keys
#define ANSI_SUPPORT
/*
 * if overwrite_console returns 1, the stdin, stderr and stdout
 * are switched to the serial port, else the settings in the
 * environment are used
 */
#ifdef CONFIG_SYS_CONSOLE_OVERWRITE_ROUTINE
extern int overwrite_console (void);
#else
int overwrite_console (void)
{
   return(0);
}
#endif


#define REPEAT_RATE  40/4 /* 40msec -> 25cps */
#define REPEAT_DELAY 10 /* 10 x REAPEAT_RATE = 400msec */

#define ESC             0x1b
#define F1              0x3a
#define F2              0x3b
#define F3              0x3c
#define F4              0x3d
#define F5              0x3e
#define F6              0x3f
#define F7              0x40
#define F8              0x41
#define F9              0x42
#define F10             0x43
#define F11             0x44
#define F12             0x45
#define DEL             0x4c

#define ARROW_R         0x4f
#define ARROW_L         0x50
#define ARROW_D         0x51
#define ARROW_U         0x52

#define NUMPAD_MINUS    0x56
#define NUMPAD_ENTER    0x58
#define NUMPAD_1        0x59
#define NUMPAD_2        0x5a
#define NUMPAD_3        0x5b
#define NUMPAD_4        0x5c
#define NUMPAD_5        0x5d
#define NUMPAD_6        0x5e
#define NUMPAD_7        0x5f
#define NUMPAD_8        0x60
#define NUMPAD_9        0x61
#define NUMPAD_0        0x62
#define NUMPAD_PERIOD   0x63


#define NUM_LOCK  0x53
#define CAPS_LOCK 0x39
#define SCROLL_LOCK 0x47


/* Modifier bits */
#define LEFT_CNTR    0
#define LEFT_SHIFT   1
#define LEFT_ALT     2
#define LEFT_GUI     3
#define RIGHT_CNTR   4
#define RIGHT_SHIFT  5
#define RIGHT_ALT    6
#define RIGHT_GUI    7

#define USB_KBD_BUFFER_LEN 0x20  /* size of the keyboardbuffer */

static volatile char usb_kbd_buffer[USB_KBD_BUFFER_LEN];
static volatile int usb_in_pointer = 0;
static volatile int usb_out_pointer = 0;

unsigned char new[8];
unsigned char old[8];
int repeat_delay;
#define DEVNAME "usbkbd"
static unsigned char num_lock = 0;
static unsigned char caps_lock = 0;
static unsigned char scroll_lock = 0;
static unsigned char ctrl = 0;

// Set the following to swap the caps lock and control keys (yes I'm that old)
unsigned char gCapsLockSwap = 0;

static unsigned char leds __attribute__ ((aligned (0x4)));

static unsigned char usb_kbd_numkey[] = {
   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0','\r',0x1b,'\b','\t',' ', '-',
   '=', '[', ']','\\', '#', ';', '\'', '`', ',', '.', '/'
};
static unsigned char usb_kbd_numkey_shifted[] = {
   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')','\r',0x1b,'\b','\t',' ', '_',
   '+', '{', '}', '|', '~', ':', '"', '~', '<', '>', '?'
};

void FunctionKeyCB(unsigned char Function);

/******************************************************************
 * Queue handling
 ******************************************************************/
/* puts character in the queue and sets up the in and out pointer */
static void usb_kbd_put_queue(char data)
{
#ifdef VERBOSE_DEBUG_LOGGING
   if(isprint(data)) {
      LOG_R("queued '%c'\n",data);
   }
   else {
      LOG_R("queued 0x%x\n",data);
   }
#endif
   if((usb_in_pointer+1)==USB_KBD_BUFFER_LEN) {
      if(usb_out_pointer==0) {
         return; /* buffer full */
      }
      else {
         usb_in_pointer=0;
      }
   }
   else {
      if((usb_in_pointer+1)==usb_out_pointer)
         return; /* buffer full */
      usb_in_pointer++;
   }
   usb_kbd_buffer[usb_in_pointer]=data;
   return;
}

/* test if a character is in the queue */
int usb_kbd_testc(void)
{
#ifdef CONFIG_SYS_USB_EVENT_POLL
   usb_event_poll();
#endif
   if(usb_in_pointer==usb_out_pointer)
      return(0); /* no data */
   else
      return(1);
}
/* gets the character from the queue */
char usb_kbd_getc(void)
{
   char c;
   while(usb_in_pointer==usb_out_pointer) {
#ifdef CONFIG_SYS_USB_EVENT_POLL
      usb_event_poll();
#endif
   }
   if((usb_out_pointer+1)==USB_KBD_BUFFER_LEN)
      usb_out_pointer=0;
   else
      usb_out_pointer++;
   c=usb_kbd_buffer[usb_out_pointer];
   return c;
}

/* forward decleration */
static int usb_kbd_probe(struct usb_device *dev, unsigned int ifnum);

/* search for keyboard and register it if found */
int drv_usb_kbd_init(void)
{
   int error,i;
   struct usb_device *dev;

   usb_in_pointer=0;
   usb_out_pointer=0;
   /* scan all USB Devices */
   for(i=0;i<USB_MAX_DEVICE;i++) {
      dev=usb_get_dev_index(i); /* get device */
      if(dev == NULL)
         return -1;
      if(dev->devnum!=-1) {
         if(usb_kbd_probe(dev,0)==1) { /* Ok, we found a keyboard */
            /* check, if it is already registered */
            LOG("USB KBD found set up device.\n");
            // TODO!!!
         }
      }
   }
   /* no USB Keyboard found */
   return -1;
}


/* deregistering the keyboard */
int usb_kbd_deregister(void)
{
#ifdef CONFIG_SYS_STDIO_DEREGISTER
   return stdio_deregister(DEVNAME);
#else
   return 1;
#endif
}

/**************************************************************************
 * Low Level drivers
 */

/* set the LEDs. Since this is used in the irq routine, the control job
   is issued with a timeout of 0. This means, that the job is queued without
   waiting for job completion */

static void usb_kbd_setled(struct usb_device *dev)
{
   struct usb_interface_descriptor *iface;
   iface = &dev->config.if_desc[0];
   leds=0;
   if(scroll_lock!=0)
      leds|=1;
   leds<<=1;
   if(caps_lock!=0)
      leds|=1;
   leds<<=1;
   if(num_lock!=0)
      leds|=1;
   usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
                   USB_REQ_SET_REPORT, USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                   0x200, iface->bInterfaceNumber,(void *)&leds, 1, 0);

}


#define CAPITAL_MASK 0x20
/* Translate the scancode in ASCII
   Modifier:
      0 - key released
      1 - key pressed
      2 - key repeat
*/
static int usb_kbd_translate(unsigned char scancode,unsigned char modifier,int pressed)
{
   unsigned char keycode;
#ifdef ANSI_SUPPORT
   char EscKey = 0;
#endif
   static unsigned char RepeatScanCode;

#ifdef VERBOSE_DEBUG_LOGGING
   VLOG("0x%x,0x%x,%d\n",scancode,modifier,pressed);
#endif

   if(pressed==0) {
      /* key released */
      repeat_delay=0;
      if(RepeatScanCode == scancode) {
         RepeatScanCode = 0;
      }
#ifdef ANSI_SUPPORT
      if(scancode == ARROW_U) {
         EscKey = 'A';
      }
      else if(scancode == ARROW_D) {
         EscKey = 'B';
      }
      else if(scancode == ARROW_R) {
         EscKey = 'C';
      }
      else if(scancode == ARROW_L) {
         EscKey = 'D';
      }

      if(EscKey != 0) {
         usb_kbd_put_queue(ESC);
         usb_kbd_put_queue(EscKey);
      }
#endif
      return 0;
   }
   if(pressed == 2) {
      if(scancode != RepeatScanCode || ++repeat_delay < REPEAT_DELAY) {
         return 0;
      }
      repeat_delay=REPEAT_DELAY;
   }
   keycode=0;
   if((scancode>3) && (scancode<=0x1d)) { /* alpha numeric values */
      keycode=scancode-4 + 0x61;
      if(caps_lock)
         keycode&=~CAPITAL_MASK; /* switch to capital Letters */
      if(((modifier&(1<<LEFT_SHIFT))!=0)||((modifier&(1<<RIGHT_SHIFT))!=0)) {
         if(keycode & CAPITAL_MASK)
            keycode&=~CAPITAL_MASK; /* switch to capital Letters */
         else
            keycode|=CAPITAL_MASK; /* switch to non capital Letters */
      }
   }


   if((scancode>0x1d) && (scancode<0x3A)) {
      if(((modifier&(1<<LEFT_SHIFT))!=0)||((modifier&(1<<RIGHT_SHIFT))!=0))  /* shifted */
         keycode=usb_kbd_numkey_shifted[scancode-0x1e];
      else /* non shifted */
         keycode=usb_kbd_numkey[scancode-0x1e];
   }
   else if(scancode > F4 && scancode <= F12) {
   // F5 -> F12
      FunctionKeyCB(scancode - F1 + 1);
   }
   else if(scancode == DEL) {
      keycode = 0x7f;
   }
#ifdef ANSI_SUPPORT
   else if(scancode >= F1 && scancode <= F4) {
   // F1..F4 translate to PF1..PF4 <esc>OP...
      EscKey = 'P' + scancode - F1;
   }
   else if(scancode == NUMPAD_0) {
      EscKey = 'p';
   }
   else if(scancode >= NUMPAD_1 && scancode <= NUMPAD_9) {
   // Numeric keypad keys
      EscKey = 'q' + scancode - NUMPAD_1;
   }
   else if(scancode == NUMPAD_MINUS) {
      EscKey = 'm';
   }
   else if(scancode == NUMPAD_PERIOD) {
      EscKey = 'n';
   }
   else if(scancode == NUMPAD_ENTER) {
      EscKey = 'M';
   }
   else if(scancode == ARROW_U) {
      EscKey = 'A';
   }
   else if(scancode == ARROW_D) {
      EscKey = 'B';
   }
   else if(scancode == ARROW_R) {
      EscKey = 'C';
   }
   else if(scancode == ARROW_L) {
      EscKey = 'D';
   }
#else
   if(scancode == NUMPAD_0) {
      keycode = '0';
   }
   else if(scancode >= NUMPAD_1 && scancode <= NUMPAD_9) {
   // Numeric keypad keys
      keycode = '1' + scancode - NUMPAD_1;
   }
   else if(scancode == NUMPAD_MINUS) {
      keycode = '-';
   }
   else if(scancode == NUMPAD_PERIOD) {
      keycode = '.';
   }
   else if(scancode == NUMPAD_ENTER) {
      keycode = '\r';
   }
#endif

   if(ctrl) {
      keycode = scancode - 0x3;
   }

   if(pressed==1) {
      if(scancode==NUM_LOCK) {
         num_lock=~num_lock;
         return 1;
      }
      if(scancode==CAPS_LOCK) {
         caps_lock=~caps_lock;
         return 1;
      }
      if(scancode==SCROLL_LOCK) {
         scroll_lock=~scroll_lock;
         return 1;
      }
   }
   if(keycode != 0) {
      if(RepeatScanCode != scancode) {
      // New key pressed, save scan code and reset key repeat delay
         RepeatScanCode = scancode;
         repeat_delay = 0;
      }
      usb_kbd_put_queue(keycode);
   }
#ifdef ANSI_SUPPORT
   else {
      if(EscKey != 0) {
         usb_kbd_put_queue(ESC);
         usb_kbd_put_queue('O');
         usb_kbd_put_queue(EscKey);
      }
   }
#endif
   return 0;
}

/* Interrupt service routine */
static int usb_kbd_irq(struct usb_device *dev)
{
   int i,res;
   unsigned char Modifiers = new[0];
   bool KeyChange = false;

   if((dev->irq_status!=0)||(dev->act_len!=8)) {
      LOG("usb_keyboard Error %lX, len %d\n",dev->irq_status,dev->act_len);
      return 1;
   }
   res=0;

#ifdef VERBOSE_DEBUG_LOGGING
   for(i = 0; i < 8; i++) {
      if(old[i] != new[i]) {
         break;
      }
   }

   if(i < 8) {
      LOG_R("old: ");
      LOG_HEX(old,8);
      LOG_R("new: ");
      LOG_HEX(new,8);
   }
#endif

   if(gCapsLockSwap) {
   // Swap caps lock and left control key, disable right control key
      Modifiers &= ~1;
      Modifiers |= ctrl;

      if((old[0] & 1) != (new[0] & 1)) {
         // Left control changed state
         VLOG("Ctrl translated to caps lock %s\n",
              (new[0] & 1) ? "pressed" : "released");
         res |= usb_kbd_translate(CAPS_LOCK,0,new[0] & 1);
      }


      for(i = 2; i < 8; i++) {
         if(old[i] == CAPS_LOCK && memscan(&new[2], old[i], 6) == &new[8]) {
            VLOG("caps lock translated to ctrl released\n");
            ctrl = 0;
         }
         if(new[i] == CAPS_LOCK && memscan(&old[2], new[i], 6) == &old[8]) {
            VLOG("caps lock translated to ctrl pressed\n");
            ctrl = 1;
         }
      }
   }
   else {
      switch(new[0]) {
         case 0x0:   /* No combo key pressed */
            ctrl = 0;
            break;
         case 0x01:  /* Left Ctrl pressed */
         case 0x10:  /* Right Ctrl pressed */
            ctrl = 1;
            break;
      }
   }

   for(i = 2; i < 8; i++) {
      if(old[i] > 3 && 
         (!gCapsLockSwap || old[i] != CAPS_LOCK) &&
         memscan(&new[2], old[i], 6) == &new[8]) 
      {
#ifdef VERBOSE_DEBUG_LOGGING
         VLOG_R("#%d:\n",__LINE__);
#endif
         KeyChange = true;
         res|=usb_kbd_translate(old[i],Modifiers,0);
      }
      if(new[i] > 3 && 
         (!gCapsLockSwap || new[i] != CAPS_LOCK) &&
         memscan(&old[2], new[i], 6) == &old[8]) 
      {
#ifdef VERBOSE_DEBUG_LOGGING
         VLOG_R("#%d:\n",__LINE__);
#endif
         KeyChange = true;
         res|=usb_kbd_translate(new[i],Modifiers,1);
      }
   }

   if(!KeyChange) {
      for(i = 7; i >= 2; i--) {
         if(new[i] > 3 && 
            (!gCapsLockSwap || new[i] != CAPS_LOCK) &&
            new[i] != CAPS_LOCK &&
            old[i]==new[i]) 
         { // still pressed
#ifdef VERBOSE_DEBUG_LOGGING
            VLOG_R("#%d:\n",__LINE__);
#endif
            res |= usb_kbd_translate(new[i],Modifiers,2);
         }
      }
   }

   if(res==1) {
      usb_kbd_setled(dev);
   }
   memcpy(&old[0],&new[0], 8);
   return 1; /* install IRQ Handler again */
}

/* probes the USB device dev for keyboard type */
static int usb_kbd_probe(struct usb_device *dev, unsigned int ifnum)
{
   struct usb_interface_descriptor *iface;
   struct usb_endpoint_descriptor *ep;
   int pipe,maxp;

   if(dev->descriptor.bNumConfigurations != 1) return 0;
   iface = &dev->config.if_desc[ifnum];

   if(iface->bInterfaceClass != 3) return 0;
   if(iface->bInterfaceSubClass != 1) return 0;
   if(iface->bInterfaceProtocol != 1) return 0;
   if(iface->bNumEndpoints != 1) return 0;

   ep = &iface->ep_desc[0];

   if(!(ep->bEndpointAddress & 0x80)) return 0;
   if((ep->bmAttributes & 3) != 3) return 0;
   LOG("USB KBD found set protocol...\n");
   /* ok, we found a USB Keyboard, install it */
   /* usb_kbd_get_hid_desc(dev); */
   usb_set_protocol(dev, iface->bInterfaceNumber, 0);
   LOG("USB KBD found set idle...\n");
   usb_set_idle(dev, iface->bInterfaceNumber, REPEAT_RATE, 0);
   memset(&new[0], 0, 8);
   memset(&old[0], 0, 8);
   repeat_delay=0;
   pipe = usb_rcvintpipe(dev, ep->bEndpointAddress);
   maxp = usb_maxpacket(dev, pipe);
   dev->irq_handle=usb_kbd_irq;
   LOG("USB KBD enable interrupt pipe...\n");
   usb_submit_int_msg(dev,pipe,&new[0], maxp > 8 ? 8 : maxp,ep->bInterval);
   LOG("USB KBD found\n");
   return 1;
}


