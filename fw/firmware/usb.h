/*
 * This file is from u-boot project:
 *
 * (C) Copyright 2001
 * Denis Peter, MPL AG Switzerland
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.   See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 *
 * Note: Part of this code has been derived from linux
 *
 */
#ifndef _USB_H_
#define _USB_H_

#include <stdint.h>

#define __LITTLE_ENDIAN
/* Configuration for slave device support */
#undef CONFIG_USB_KEYBOARD
#define CONFIG_USB_GAMEPAD
#define CONFIG_USB_STORAGE

#define cpu_to_le16(x) (x)
#define le16_to_cpu(x) (x)
#define cpu_to_le32(x) (x)
#define le32_to_cpu(x) (x)
#define le16_to_cpus(x)

#define cpu_to_be32(x) (((x & 0xff000000) >> 16) | ((x & 0xff0000) >> 8) | \
                     ((x & 0xff00) << 8) | ((x & 0xff) << 24))

#include "usb_defs.h"

#define USB_BUFSIZ   512
extern uint8_t usb_buf[USB_BUFSIZ];

/* Everything is aribtrary */
#define USB_ALTSETTINGALLOC      4
#define USB_MAXALTSETTING     128   /* Hard limit */

#define USB_MAX_DEVICE        5
#define USB_MAXCONFIG         1
#define USB_MAXINTERFACES     4   // DualShock 4 use 4 interfaces
#define USB_MAXENDPOINTS      2
#define USB_MAXCHILDREN       3  /* This is arbitrary */
#define USB_MAX_HUB           3

#define USB_CNTL_TIMEOUT 100 /* 100ms timeout */

/* String descriptor */
struct usb_string_descriptor {
   unsigned char  bLength;
   unsigned char  bDescriptorType;
   unsigned short wData[1];
} __attribute__ ((packed));

/* device request (setup) */
struct devrequest {
   unsigned char  requesttype;
   unsigned char  request;
   unsigned short value;
   unsigned short index;
   unsigned short length;
} __attribute__ ((packed));

/* All standard descriptors have these 2 fields in common */
struct usb_descriptor_header {
   unsigned char  bLength;
   unsigned char  bDescriptorType;
} __attribute__ ((packed));

/* Device descriptor */
struct usb_device_descriptor {
   unsigned char  bLength;
   unsigned char  bDescriptorType;
   unsigned short bcdUSB;
   unsigned char  bDeviceClass;
   unsigned char  bDeviceSubClass;
   unsigned char  bDeviceProtocol;
   unsigned char  bMaxPacketSize0;
   unsigned short idVendor;
   unsigned short idProduct;
   unsigned short bcdDevice;
   unsigned char  iManufacturer;
   unsigned char  iProduct;
   unsigned char  iSerialNumber;
   unsigned char  bNumConfigurations;
} __attribute__ ((packed));

/* Endpoint descriptor */
struct usb_endpoint_descriptor {
   unsigned char  bLength;
   unsigned char  bDescriptorType;
   unsigned char  bEndpointAddress;
   unsigned char  bmAttributes;
   unsigned short wMaxPacketSize;
   unsigned char  bInterval;
   unsigned char  bRefresh;
   unsigned char  bSynchAddress;
} __attribute__ ((packed)) __attribute__ ((aligned(2)));

/* Interface descriptor */
struct usb_interface_descriptor {
   unsigned char  bLength;
   unsigned char  bDescriptorType;
   unsigned char  bInterfaceNumber;
   unsigned char  bAlternateSetting;
   unsigned char  bNumEndpoints;
   unsigned char  bInterfaceClass;
   unsigned char  bInterfaceSubClass;
   unsigned char  bInterfaceProtocol;
   unsigned char  iInterface;

   unsigned char  no_of_ep;
   unsigned char  num_altsetting;
   unsigned char  act_altsetting;

   struct usb_endpoint_descriptor ep_desc[USB_MAXENDPOINTS];
} __attribute__ ((packed));


/* Configuration descriptor information.. */
struct usb_config_descriptor {
   unsigned char  bLength;
   unsigned char  bDescriptorType;
   unsigned short wTotalLength;
   unsigned char  bNumInterfaces;
   unsigned char  bConfigurationValue;
   unsigned char  iConfiguration;
   unsigned char  bmAttributes;
   unsigned char  MaxPower;

   unsigned char  no_of_if;   /* number of interfaces */
   struct usb_interface_descriptor if_desc[USB_MAXINTERFACES];
} __attribute__ ((packed));

/* HID descriptor */
struct usb_hid_descriptor {
   unsigned char  bLength;
   unsigned char  bDescriptorType;
   unsigned short  bcdHID;
   unsigned char   bCountryCode;
   unsigned char  bNumDescriptors;
   unsigned char   bReportDescriptorType;
   unsigned short wItemLength;
} __attribute__ ((packed));

enum {
   /* Maximum packet size; encoded as 0,1,2,3 = 8,16,32,64 */
   PACKET_SIZE_8   = 0,
   PACKET_SIZE_16  = 1,
   PACKET_SIZE_32  = 2,
   PACKET_SIZE_64  = 3,
};

struct usb_device {
   int   devnum;        /* Device number on USB bus */
   int   speed;         /* full/low/high */
   char  mf[32];        /* manufacturer */
   char  prod[40];      /* product */
   //char   serial[32];    /* serial number */

   /* Maximum packet size; one of: PACKET_SIZE_* */
   int maxpacketsize;
   /* one bit for each endpoint ([0] = IN, [1] = OUT) */
   unsigned int toggle[2];
   /* endpoint halts; one bit per endpoint # & direction;
    * [0] = IN, [1] = OUT
    */
   unsigned int halted[2];
   int epmaxpacketin[16];     /* INput endpoint specific maximums */
   int epmaxpacketout[16];    /* OUTput endpoint specific maximums */

   int configno;        /* selected config number */
   struct usb_device_descriptor descriptor; /* Device Descriptor */
   struct usb_config_descriptor config; /* config descriptor */

   int have_langid;     /* whether string_langid is valid yet */
   int string_langid;      /* language ID for strings */
   int (*irq_handle)(struct usb_device *dev,int result);
   unsigned long irq_status;
   void *privptr;

   /* HID specific information */
   struct usb_hid_descriptor hid_descriptor;

   /*
    * Child devices -  if this is a hub device
    * Each instance needs its own set of data structures.
    */
   unsigned long status;
   int act_len;         /* transfered bytes */
   int maxchild;        /* Number of ports if hub */
   int portnr;
   struct usb_device *parent;
   struct usb_device *children[USB_MAXCHILDREN];
};

typedef struct block_dev_desc {
   int      dev;     /* device number */
   unsigned char  target;     /* target SCSI ID */
   unsigned char  lun;     /* target LUN */
   unsigned char  type;    /* device type */
   unsigned char  removable;  /* removable device */
   unsigned long  lba;     /* number of blocks */
   unsigned long  blksz;      /* block size */
   char     vendor [40+1]; /* IDE model, SCSI Vendor */
   char     product[20+1]; /* IDE Serial no, SCSI product */
   char     revision[8+1]; /* firmware revision */
   unsigned long  (*block_read)(int dev,
                  unsigned long start,
                  unsigned long blkcnt,
                  void *buffer);
   unsigned long  (*block_write)(int dev,
                   unsigned long start,
                   unsigned long blkcnt,
                   const void *buffer);
   void     *priv;      /* driver private struct pointer */
}block_dev_desc_t;

/**********************************************************************
 * this is how the lowlevel part communicate with the outer world
 */

int usb_lowlevel_init(void);
int usb_lowlevel_stop(void);
int submit_bulk_msg(struct usb_device *dev, unsigned long pipe,
                    void *buffer, int transfer_len,int Timeout);
int submit_control_msg(struct usb_device *dev, unsigned long pipe, void *buffer,
         int transfer_len, struct devrequest *setup);
int submit_int_msg(struct usb_device *dev, unsigned long pipe, void *buffer,
         int transfer_len, int interval);
void usb_event_poll(void);

/* Defines */
#define USB_UHCI_VEND_ID   0x8086
#define USB_UHCI_DEV_ID    0x7112

// USB Mass stoarge device
#define USB_MAX_STOR_DEV 1
block_dev_desc_t *usb_stor_get_dev(int index);
int usb_stor_scan(int mode);
int usb_stor_info(void);

// USB Keyboard
extern unsigned char gCapsLockSwap;

int drv_usb_kbd_init(void);
int usb_kbd_deregister(void);
int usb_kbd_testc(void);
char usb_kbd_getc(void);

// USB Game pad
int drv_usb_gp_init(void);
int usb_gp_deregister(void);

/* routines */
int usb_init(void); /* initialize the USB Controller */
int usb_stop(void); /* stop the USB Controller */

int usb_set_protocol(struct usb_device *dev, int ifnum, int protocol);
int usb_set_idle(struct usb_device *dev, int ifnum, int duration,
         int report_id);
struct usb_device *usb_get_dev_index(int index);
int usb_control_msg(struct usb_device *dev, unsigned int pipe,
         unsigned char request, unsigned char requesttype,
         unsigned short value, unsigned short index,
         void *data, unsigned short size, int timeout);
int usb_bulk_msg(struct usb_device *dev, unsigned int pipe,
         void *data, int len, int *actual_length, int timeout);
int usb_submit_int_msg(struct usb_device *dev, unsigned long pipe,
         void *buffer, int transfer_len, int interval);
void usb_disable_asynch(int disable);
int usb_maxpacket(struct usb_device *dev, unsigned long pipe);
//inline void wait_ms(unsigned long ms);
int usb_get_configuration_no(struct usb_device *dev, unsigned char *buffer,
            int cfgno);
int usb_get_report(struct usb_device *dev, int ifnum, unsigned char type,
         unsigned char id, void *buf, int size);
int usb_get_class_descriptor(struct usb_device *dev, int ifnum,
         unsigned char type, unsigned char id, void *buf,
         int size);
int usb_clear_halt(struct usb_device *dev, int pipe);
int usb_string(struct usb_device *dev, int index, char *buf, uint32_t size);
int usb_set_interface(struct usb_device *dev, int interface, int alternate);

/* big endian -> little endian conversion */
/* some CPUs are already little endian e.g. the ARM920T */
#define __swap_16(x) \
   ({ unsigned short x_ = (unsigned short)x; \
    (unsigned short)( \
      ((x_ & 0x00FFU) << 8) | ((x_ & 0xFF00U) >> 8)); \
   })
#define __swap_32(x) \
   ({ unsigned long x_ = (unsigned long)x; \
    (unsigned long)( \
      ((x_ & 0x000000FFUL) << 24) | \
      ((x_ & 0x0000FF00UL) <<  8) | \
      ((x_ & 0x00FF0000UL) >>  8) | \
      ((x_ & 0xFF000000UL) >> 24)); \
   })

#ifdef __LITTLE_ENDIAN
# define swap_16(x) (x)
# define swap_32(x) (x)
#else
# define swap_16(x) __swap_16(x)
# define swap_32(x) __swap_32(x)
#endif

/*
 * Calling this entity a "pipe" is glorifying it. A USB pipe
 * is something embarrassingly simple: it basically consists
 * of the following information:
 *  - device number (7 bits)
 *  - endpoint number (4 bits)
 *  - current Data0/1 state (1 bit)
 *  - direction (1 bit)
 *  - speed (2 bits)
 *  - max packet size (2 bits: 8, 16, 32 or 64)
 *  - pipe type (2 bits: control, interrupt, bulk, isochronous)
 *
 * That's 18 bits. Really. Nothing more. And the USB people have
 * documented these eighteen bits as some kind of glorious
 * virtual data structure.
 *
 * Let's not fall in that trap. We'll just encode it as a simple
 * unsigned int. The encoding is:
 *
 *  - max size:      bits 0-1 (00 = 8, 01 = 16, 10 = 32, 11 = 64)
 *  - direction:  bit 7    (0 = Host-to-Device [Out],
 *             (1 = Device-to-Host [In])
 *  - device:     bits 8-14
 *  - endpoint:      bits 15-18
 *  - Data0/1:    bit 19
 *  - speed:      bit 26      (0 = Full, 1 = Low Speed, 2 = High)
 *  - pipe type:  bits 30-31  (00 = isochronous, 01 = interrupt,
 *              10 = control, 11 = bulk)
 *
 * Why? Because it's arbitrary, and whatever encoding we select is really
 * up to us. This one happens to share a lot of bit positions with the UHCI
 * specification, so that much of the uhci driver can just mask the bits
 * appropriately.
 */
/* Create various pipes... */
#define create_pipe(dev,endpoint) \
      (((dev)->devnum << 8) | (endpoint << 15) | \
      ((dev)->speed << 26) | (dev)->maxpacketsize)
#define default_pipe(dev) ((dev)->speed << 26)

#define usb_sndctrlpipe(dev, endpoint) ((PIPE_CONTROL << 30) | \
                create_pipe(dev, endpoint))
#define usb_rcvctrlpipe(dev, endpoint) ((PIPE_CONTROL << 30) | \
                create_pipe(dev, endpoint) | \
                USB_DIR_IN)
#define usb_sndisocpipe(dev, endpoint) ((PIPE_ISOCHRONOUS << 30) | \
                create_pipe(dev, endpoint))
#define usb_rcvisocpipe(dev, endpoint) ((PIPE_ISOCHRONOUS << 30) | \
                create_pipe(dev, endpoint) | \
                USB_DIR_IN)
#define usb_sndbulkpipe(dev, endpoint) ((PIPE_BULK << 30) | \
                create_pipe(dev, endpoint))
#define usb_rcvbulkpipe(dev, endpoint) ((PIPE_BULK << 30) | \
                create_pipe(dev, endpoint) | \
                USB_DIR_IN)
#define usb_sndintpipe(dev, endpoint)  ((PIPE_INTERRUPT << 30) | \
                create_pipe(dev, endpoint))
#define usb_rcvintpipe(dev, endpoint)  ((PIPE_INTERRUPT << 30) | \
                create_pipe(dev, endpoint) | \
                USB_DIR_IN)
#define usb_snddefctrl(dev)      ((PIPE_CONTROL << 30) | \
                default_pipe(dev))
#define usb_rcvdefctrl(dev)      ((PIPE_CONTROL << 30) | \
                default_pipe(dev) | \
                USB_DIR_IN)

/* The D0/D1 toggle bits */
#define usb_gettoggle(dev, ep, out) (((dev)->toggle[out] >> ep) & 1)
#define usb_dotoggle(dev, ep, out)  ((dev)->toggle[out] ^= (1 << ep))
#define usb_settoggle(dev, ep, out, bit) ((dev)->toggle[out] = \
                  ((dev)->toggle[out] & \
                   ~(1 << ep)) | ((bit) << ep))

/* Endpoint halt control/status */
#define usb_endpoint_out(ep_dir) (((ep_dir >> 7) & 1) ^ 1)
#define usb_endpoint_halt(dev, ep, out) ((dev)->halted[out] |= (1 << (ep)))
#define usb_endpoint_running(dev, ep, out) ((dev)->halted[out] &= ~(1 << (ep)))
#define usb_endpoint_halted(dev, ep, out) ((dev)->halted[out] & (1 << (ep)))

#define usb_packetid(pipe) (((pipe) & USB_DIR_IN) ? USB_PID_IN : \
             USB_PID_OUT)

#define usb_pipeout(pipe)  ((((pipe) >> 7) & 1) ^ 1)
#define usb_pipein(pipe)   (((pipe) >> 7) & 1)
#define usb_pipedevice(pipe)  (((pipe) >> 8) & 0x7f)
#define usb_pipe_endpdev(pipe)   (((pipe) >> 8) & 0x7ff)
#define usb_pipeendpoint(pipe)   (((pipe) >> 15) & 0xf)
#define usb_pipedata(pipe) (((pipe) >> 19) & 1)
#define usb_pipespeed(pipe)   (((pipe) >> 26) & 3)
#define usb_pipeslow(pipe) (usb_pipespeed(pipe) == USB_SPEED_LOW)
#define usb_pipetype(pipe) (((pipe) >> 30) & 3)
#define usb_pipeisoc(pipe) (usb_pipetype((pipe)) == PIPE_ISOCHRONOUS)
#define usb_pipeint(pipe)  (usb_pipetype((pipe)) == PIPE_INTERRUPT)
#define usb_pipecontrol(pipe) (usb_pipetype((pipe)) == PIPE_CONTROL)
#define usb_pipebulk(pipe) (usb_pipetype((pipe)) == PIPE_BULK)


/*************************************************************************
 * Hub Stuff
 */
struct usb_port_status {
   unsigned short wPortStatus;
   unsigned short wPortChange;
} __attribute__ ((packed));

struct usb_hub_status {
   unsigned short wHubStatus;
   unsigned short wHubChange;
} __attribute__ ((packed));


/* Hub descriptor */
struct usb_hub_descriptor {
   unsigned char  bLength;
   unsigned char  bDescriptorType;
   unsigned char  bNbrPorts;
   unsigned short wHubCharacteristics;
   unsigned char  bPwrOn2PwrGood;
   unsigned char  bHubContrCurrent;
   unsigned char  DeviceRemovable[(USB_MAXCHILDREN+1+7)/8];
   unsigned char  PortPowerCtrlMask[(USB_MAXCHILDREN+1+7)/8];
   /* DeviceRemovable and PortPwrCtrlMask want to be variable-length
      bitmaps that hold max 255 entries. (bit0 is ignored) */
} __attribute__ ((packed));


struct usb_hub_device {
   struct usb_device *pusb_dev;
   struct usb_hub_descriptor desc;
};

#endif /*_USB_H_ */
