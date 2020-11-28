/*
 *  VerilogBoy
 *
 *  Copyright (C) 2019  Wenting Zhang <zephray@outlook.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms and conditions of the GNU General Public License,
 *  version 2, as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include "misc.h"
#include "usb.h"
#include "isp1760.h"
#include "isp_roothub.h"
#include "string.h"

// #define DEBUG_LOGGING
#define LOG_TO_SERIAL
// #define VERBOSE_DEBUG_LOGGING
#include "log.h"
#include "picorv32.h"

#define INT_SAVE  uint32_t IntSave;
#define DI() IntSave = picorv32_maskirq(0xffffffff)
#define EI() picorv32_maskirq(IntSave)

extern uint8_t gDumpPtd;

#define ISP_IRQ_DRIVEN

UsbTransfer *gEmptyTransferBufs;
// Active INT transfers
UsbTransfer *gIntTransferBufs;
// Active ATL transfers
UsbTransfer *gAtlTransferBufs;
// Completed ATL transfers which have Callbacks
UsbTransfer *gReadyTransferBufs;

UsbTransfer gTransferBufs[MAX_TRANSFERS];

// Define the interrupts that the driver will handle
#define ISP_INT_MASK  0x00000180

isp_result_t isp_StartTransfer(UsbTransfer *p);
isp_result_t isp_CompleteTransfer(UsbTransfer *p);

#ifdef VERBOSE_DEBUG_LOGGING
void DumpPtd(uint32_t *p);
#else
#define DumpPtd(x)
#endif

uint32_t isp_read_word(uint32_t address) 
{
    return *((volatile uint32_t *)(ISP_BASE_ADDR | (address << 1)));
}

uint32_t isp_read_dword(uint32_t address) 
{
    uint32_t l = *((volatile uint32_t *)(ISP_BASE_ADDR | (address << 1)));
    uint32_t h = *((volatile uint32_t *)(ISP_BASE_ADDR | (address << 1) | 0x4));
    return ((h << 16) | (l & 0xFFFF));
}

void isp_write_word(uint32_t address, uint32_t data) 
{
    *((volatile uint32_t *)(ISP_BASE_ADDR | (address << 1))) = data;
}

void isp_write_dword(uint32_t address, uint32_t data) 
{  
    *((volatile uint32_t *)(ISP_BASE_ADDR | (address << 1))) = data & 0xFFFF;
    *((volatile uint32_t *)(ISP_BASE_ADDR | (address << 1) | 0x4)) = data >> 16;  
}

// Datasheet, page 17
uint32_t isp_addr_mem_to_cpu(uint32_t mem_address) 
{
    return (mem_address << 3) + MEM_BASE;
}

uint32_t isp_addr_cpu_to_mem(uint32_t cpu_address) 
{
    return (cpu_address - MEM_BASE) >> 3;
}

void isp_write_memory(uint32_t address, uint32_t *data, uint32_t length) 
{
    address = isp_addr_mem_to_cpu(address);
    for (uint32_t i = 0; i < length; i+= 4) {
        isp_write_dword(address, *data++);
        address += 4;
    }
}

void isp_read_memory(uint32_t address, uint32_t *data, uint32_t length) 
{
    // TODO: What about bank address?
    // Doesn't seem to matter if read is not interleaved
    address = isp_addr_mem_to_cpu(address);
    isp_write_dword(ISP_MEMORY, address);
    for (uint32_t i = 0; i < length; i+= 4) {
        *data++ = isp_read_dword(address);
        address += 4;
    }
}

isp_result_t isp_wait(
   uint32_t address, 
   uint32_t mask,
   uint32_t value, 
   uint32_t timeout) 
{
    uint32_t start_ticks = ticks_ms();
    do {
        if (isp_read_dword(address) & mask == value)
            return ISP_SUCCESS;
        delay_us(10);
    } while ((ticks_ms() - start_ticks) < timeout);
    return ISP_GENERAL_FAILURE;
}

void isp_reset() 
{
    isp_reset_pin = 1;
    delay_ms(50);
    isp_reset_pin = 0;
    delay_ms(50);
    isp_reset_pin = 1;
    delay_ms(50);
}

int usb_lowlevel_init() 
{
    uint32_t value;
    int i;

    LOG_R("\033[2J");
    LOG("Called, disabling logging\n");
// Init links
#if 0
   for(i = 0; i < MAX_TRANSFERS-1; i++) {
      gTransferBufs[i].TransferNum = i + 1;
      gTransferBufs[i].Result = ISP_TRANSFER_EMPTY;
      gTransferBufs[i].Link = &gTransferBufs[i + 1];
   }
#else
    for(i = 0; i < MAX_TRANSFERS; i++) {
       gTransferBufs[i].TransferNum = i;
       gTransferBufs[i].Result = ISP_TRANSFER_EMPTY;
       gTransferBufs[i].Link = &gTransferBufs[i + 1];
    }
#endif
   gTransferBufs[i-1].Link = NULL;

   gEmptyTransferBufs = gTransferBufs;
   // LOG_DISABLE();

    // Reset the ISP1760 controller
    isp_reset();

    // Set to 16 bit mode
    isp_write_dword(ISP_HW_MODE_CONTROL, 0x00000000);
    isp_write_dword(ISP_HW_MODE_CONTROL, 0x00000000);

    // Test SCRATCH register
    isp_write_dword(ISP_SCRATCH, 0x410C0C0A);
    // change bus pattern
    value = isp_read_dword(ISP_CHIP_ID);
    value = isp_read_dword(ISP_SCRATCH);
    if (value != 0x410C0C0A) {
        printf("ISP1760: Scratch RW test failed!\n");
        return -1;
    } 
    
    // Disable all buffers
    isp_write_dword(ISP_BUFFER_STATUS, 0x00000000);

    // Skip all transfers
    isp_write_dword(ISP_ATL_PTD_SKIPMAP, 0xffffffff);
    isp_write_dword(ISP_INT_PTD_SKIPMAP, 0xffffffff);
    isp_write_dword(ISP_ISO_PTD_SKIPMAP, 0xffffffff);

    // Reset all
    isp_write_dword(ISP_SW_RESET, ISP_SW_RESET_ALL);
    delay_ms(250);

    // Reset HC
    isp_write_dword(ISP_SW_RESET, ISP_SW_RESET_HC);
    delay_ms(250);

    // Execute reset command
    isp_write_dword(ISP_HW_MODE_CONTROL, 0x00000000);
    isp_write_dword(ISP_HW_MODE_CONTROL, 0x00000000);
    value = isp_read_dword(ISP_USBCMD);
    value |= ISP_USBCMD_RESET;
    isp_write_dword(ISP_USBCMD, value);

    delay_ms(100);
    /*if (isp_wait(ISP_USBCMD, ISP_USBCMD_RESET, 0, 250) != ISP_SUCCESS) {
        LOG("Failed to reset the ISP1760!\n");
        return;
    }*/

    isp_write_dword(ISP_HW_MODE_CONTROL, 0x00000000);
    isp_write_dword(ISP_HW_MODE_CONTROL, 0x00000000);

    // Clear USB reset command
    value &= ~ISP_USBCMD_RESET;
    isp_write_dword(ISP_USBCMD, value);

    // Config port 1
    // Config as USB HOST (On ISP1761 it can also be device)
    isp_write_dword(ISP_PORT_1_CONTROL, 0x00800018);

    // Configure interrupt here
    isp_write_dword(ISP_INTERRUPT, ISP_INT_MASK);

    isp_write_dword(ISP_INTERRUPT_ENABLE, ISP_INT_MASK);

    isp_write_dword(ISP_HW_MODE_CONTROL, 0x80000000);
    delay_ms(50);
    isp_write_dword(ISP_HW_MODE_CONTROL, 0x00000000);

    isp_write_dword(ISP_ATL_IRQ_MASK_AND,0);
    isp_write_dword(ISP_ATL_IRQ_MASK_OR,0xffffffff);
    isp_write_dword(ISP_INT_IRQ_MASK_AND,0);
    isp_write_dword(ISP_INT_IRQ_MASK_OR,0xffffffff);
    isp_write_dword(ISP_ISO_IRQ_MASK_AND,0);
    isp_write_dword(ISP_ISO_IRQ_MASK_OR,0);

    // Global interrupt enable
    isp_write_dword(ISP_HW_MODE_CONTROL, 0x00000001);

    // Execute run command
    isp_write_dword(ISP_USBCMD, ISP_USBCMD_RUN);
    if(isp_wait(ISP_USBCMD,ISP_USBCMD_RUN,ISP_USBCMD_RUN,50) != ISP_SUCCESS) {
        printf("Failed to start the ISP1760!\n");
        return -1;
    }

    // Enable EHCI mode
    isp_write_dword(ISP_CONFIGFLAG, ISP_CONFIGFLAG_CF);
    if(isp_wait(ISP_CONFIGFLAG,ISP_CONFIGFLAG_CF,ISP_CONFIGFLAG_CF,50) != ISP_SUCCESS) {
        printf("Failed to enable the EHCI mode!\n");
        return -1;
    }

// Set last maps
    isp_write_dword(ISP_ATL_PTD_LASTPTD, 0x80000000);
    isp_write_dword(ISP_INT_PTD_LASTPTD, 0x80000000);
    isp_write_dword(ISP_ISO_PTD_LASTPTD, 0x00000001);

 // Enable ATL and INT PTD processing
    isp_write_dword(ISP_BUFFER_STATUS, 
                    ISP_BUFFER_STATUS_ATL_FILLED | ISP_BUFFER_STATUS_INT_FILLED);


    // Enable port power
    isp_write_dword(ISP_PORTSC1, ISP_PORTSC1_PP);

    // Wait connection
    if(isp_wait(ISP_PORTSC1,ISP_PORTSC1_ECSC,ISP_PORTSC1_ECSC,10) != ISP_SUCCESS) {
        printf("Internal hub failed to connect!\n");
        return -1;
    }

    isp_write_dword(ISP_PORTSC1, 
            isp_read_dword(ISP_PORTSC1) | ISP_PORTSC1_ECSC);

    // Port reset
    isp_write_dword(ISP_PORTSC1, 
            ISP_PORTSC1_PP | 
            (2u << 10) | 
            ISP_PORTSC1_PR |
            ISP_PORTSC1_ECCS);
    delay_ms(50);

    // Clear reset
    isp_write_dword(ISP_PORTSC1, 
            isp_read_dword(ISP_PORTSC1) & ~(ISP_PORTSC1_PR));

    return 0;
}

void isp_callback_irq(void) 
{
   int i;
   isp_result_t result;
   UsbTransfer *p;
   UsbTransfer **pLast;
   uint32_t donemap = isp_read_dword(ISP_INT_PTD_DONEMAP);
   uint32_t PtdBit;
   int Again;
   int LogFlags;

//   donemap &= ~isp_read_dword(ISP_INT_PTD_SKIPMAP);

   if(donemap != 0) {
#if 0
      LogFlags = gLogFlags;
      gLogFlags |= LOG_DISABLED;
#endif
      p = gIntTransferBufs;
      if(p != NULL) {
         LOG_R("gIntTransferBufs:");
         while(p != NULL) {
            LOG_R(" %d",p->TransferNum);
            p = p->Link;
         }
         LOG_R("\n");
      }
      p = gIntTransferBufs;
      pLast = &gIntTransferBufs;

      while(p != NULL) {
         if(p->Result != ISP_TRANSFER_BUSY) {
            ELOG("TransferNum %d on gIntTransferBufs, but Result = %d\n",
                 p->TransferNum,p->Result);
            for( ; ; );
         }
         PtdBit = 1 << p->TransferNum;
         if(donemap & PtdBit) {
            donemap &= ~PtdBit;
            *pLast = p->Link;
            isp_CompleteTransfer(p);
            if(p->Dev->irq_handle != NULL && 
               p->Dev->irq_handle(p->Dev,p->Result)) 
            {  // Start another transfer
               isp_StartTransfer(p);
               p->Link;
            }
            else {
            // Put completed transfer buffer back on empty list
               p->Link = gEmptyTransferBufs;
               gEmptyTransferBufs = p;
               p = *pLast;
            }
         }
         else {
            pLast = &p->Link;
            p = p->Link;
         }
      }
      if(donemap != 0) {
         ELOG("Error: Unhandled INT transfers: 0x%x\n",donemap);
      }
#if 0
      gLogFlags = LogFlags;
#endif
   }

   donemap = isp_read_dword(ISP_ATL_PTD_DONEMAP);
   // donemap &= ~isp_read_dword(ISP_ATL_PTD_SKIPMAP);

   if(donemap != 0) {
      p = gAtlTransferBufs;
      if(p != NULL) {
         LOG_R("gAtlTransferBufs:");
         while(p != NULL) {
            LOG_R(" %d",p->TransferNum);
            p = p->Link;
         }
         LOG_R("\n");
      }
      p = gAtlTransferBufs;
      pLast = &gAtlTransferBufs;
      while(p != NULL) {
         if(p->Result != ISP_TRANSFER_BUSY) {
            ELOG("TransferNum %d on gAtlTransferBufs, but Result = %d\n",
                 p->TransferNum,p->Result);
            for( ; ; );
         }
         PtdBit = 1 << p->TransferNum;
         if(donemap & PtdBit) {
            donemap &= ~PtdBit;
         // Remove transfer from active list
            *pLast = p->Link;
            isp_CompleteTransfer(p);
            if(p->CallBack != NULL) {
            // Put completed transfer on ready list
               p->Link = gReadyTransferBufs;
               gReadyTransferBufs = p;
            }
            p = *pLast;
         }
         else {
            pLast = &p->Link;
            p = p->Link;
         }
      }
      if(donemap != 0) {
         ELOG("Error: Unhandled ATL transfers: 0x%x\n",donemap);
      }
      p = gAtlTransferBufs;
      if(p != NULL) {
         ELOG("gAtlTransferBufs not empty:");
         while(p != NULL) {
            LOG_R(" %d",p->TransferNum);
            p = p->Link;
         }
         LOG_R("\n");
      }
   }
}

void LogInterrupts(const char *Header,uint32_t interrupts)
{
#if 0
   bool first = true;
   LOG_R(Header);
   if(interrupts & ISP_INTERRUPT_INT) {
      LOG_R("int");
      first = false;
   }

   if(interrupts & ISP_INTERRUPT_ATL) {
      LOG_R("%satl",first ? "" : ", ");
      first = false;
   }

   if(interrupts & ISP_INTERRUPT_ISO) {
      LOG_R("%siso",first ? "" : ", ");
      first = false;
   }

   if(interrupts & ISP_INTERRUPT_CLKREADY) {
      LOG_R("%sclk_rdy",first ? "" : ", ");
      first = false;
   }
   if(interrupts & ISP_INTERRUPT_HC_SUSP) {
      LOG_R("%suspend",first ? "" : ", ");
      first = false;
   }
   if(interrupts & ISP_INTERRUPT_DMAEOTINT) {
      LOG_R("%sdma_to",first ? "" : ", ");
      first = false;
   }
   if(interrupts & ISP_INTERRUPT_SOFITLINT) {
      LOG_R("%ssof",first ? "" : ", ");
      first = false;
   }
   LOG_R(" (0x%x)\n",interrupts);
#endif
}

void isp_isr(void) 
{
    uint32_t interrupts = isp_read_dword(ISP_INTERRUPT);
    uint32_t Enabled = isp_read_dword(ISP_INTERRUPT_ENABLE);

    LogInterrupts(" active: ",interrupts);
    LogInterrupts(" enabled: ",Enabled);

    isp_write_dword(ISP_INTERRUPT, interrupts); // Clear interrupts
    if (interrupts & (ISP_INTERRUPT_INT | ISP_INTERRUPT_ATL)) {
        isp_callback_irq();
    }
    interrupts = isp_read_dword(ISP_INTERRUPT);
    if((interrupts & Enabled) != 0) {
       LogInterrupts(" still active: ",interrupts);
    }
}

/* 
"An endpoint for control transfers specifies the maximum data payload size 
that the endpoint can accept from or transmit to the bus.  The allowable 
maximum control transfer data payload sizes for full-speed devices is 8, 
16, 32, or 64 bytes; for high-speed devices, it is 64 bytes and for 
low-speed devices, it is 8 bytes." 

"An endpoint for an interrupt pipe specifies the maximum size data payload 
that it will transmit or receive.  The maximum allowable interrupt data 
payload size is 64 bytes or less for full-speed.  High-speed endpoints are 
allowed maximum data payload sizes up to 1024 bytes." 

"All Host Controllers are required to have support for 8-, 16-, 32-, and 
64-byte maximum packet sizes for full-speed bulk endpoints and 512 bytes 
for high-speed bulk endpoints.  No Host Controller is required to support 
larger or smaller maximum packet sizes."

The isp1760 has 63k of on chip memory.  The first 3K are used for PTDs the 
remaining 60K is available for transfer buffers. 
 
Let's allocate 1K per transfer, this means we can support a maximum of 30 
simultanous transfers.
 
*/ 

UsbTransfer *GetTransferBuf(struct usb_device *Dev,unsigned long Pipe)
{
   UsbTransfer *p;
   INT_SAVE;
   int TransferNum;

   DI();
   if((p = gEmptyTransferBufs) != NULL) {
      gEmptyTransferBufs = p->Link;
      EI();
      TransferNum = p->TransferNum;
      if(p->Result != ISP_TRANSFER_EMPTY) {
         ELOG("TransferNum %d on empty list, but Result = %d\n",
             TransferNum,p->Result);
         for( ; ; );
      }
      memset(p,0,sizeof(*p));
      p->TransferNum = TransferNum;
      p->Dev = Dev;
      p->Pipe = Pipe;
   }
   else {
      EI();
   }
   p->Result = ISP_TRANSFER_INIT;

   return p;
}

void FreeTransferBuf(UsbTransfer *p)
{
   INT_SAVE;

   p->Result = ISP_TRANSFER_EMPTY;
   DI();
   p->Link = gEmptyTransferBufs;
   gEmptyTransferBufs = p;
   EI();
}


// External APIs
// -----------------------------------------------------------------------------

int usb_lowlevel_stop(void) {
    isp_reset();
}

int submit_bulk_msg(struct usb_device *dev, unsigned long pipe,
                    void *buffer, int transfer_len,int Timeout) 
{
   int Ret = -1;
   UsbTransfer *p;
   LOG_ENABLE();
   do {
      if(usb_pipetype(pipe) != PIPE_BULK) {
         ELOG("non-bulk pipe\n");
         break;
      }
      if((p = GetTransferBuf(dev,pipe)) == NULL) {
         ELOG("No empty xfer bufs\n");
         break;
      }
      p->Buf = buffer;
      p->Length = transfer_len;
      p->Timeout = Timeout;
      p->Token = usb_pipein(pipe) ? TOKEN_IN : TOKEN_OUT;
      if((Ret = isp_StartTransfer(p)) != ISP_SUCCESS) {
         break;
      }
      while(p->Result < ISP_SUCCESS) {
         usb_event_poll();
      }
      Ret = p->Result;
   } while(false);

   FreeTransferBuf(p);

   if(Ret != ISP_SUCCESS) {
      ELOG("returning %d\n",Ret);
   }
   return Ret;
}

int submit_control_msg(struct usb_device *dev, unsigned long pipe, void *buffer,
      int transfer_len, struct devrequest *setup) 
{
   int Ret = -1;
   UsbTransfer *p;

   {
      uint32_t address = usb_pipedevice(pipe);
      uint32_t parent_address = (dev->parent != NULL) ? dev->parent->devnum : 0;
      uint32_t parent_port = dev->portnr;
      LOG("adr: 0x%x, parent_adr: 0x%x, parent_port: 0x%x\n",
          address,parent_address,parent_port);
   }

   do {
      if(usb_pipetype(pipe) != PIPE_CONTROL) {
         LOG("non-control pipe\n");
         break;
      }
      if((p = GetTransferBuf(dev,pipe)) == NULL) {
         ELOG("No empty xfer bufs\n");
         break;
      }
      p->Buf = setup;
      p->Length = sizeof(*setup);
      p->Token = TOKEN_SETUP;
      if((Ret = isp_StartTransfer(p)) != ISP_SUCCESS) {
         break;
      }
      LOG("Waiting for setup phase to complete...\n");
      while(p->Result < ISP_SUCCESS) {
         usb_event_poll();
      }
      LOG("Setup phase complete, result: %d\n",p->Result);

      p->Buf = buffer;
      p->Length = transfer_len;
      p->Token = TOKEN_IN;
      if((Ret = isp_StartTransfer(p)) != ISP_SUCCESS) {
         break;
      }
      while(p->Result < ISP_SUCCESS) {
         usb_event_poll();
      }
      LOG("Transfer phase complete, result: %d, ActualLen: %d\n",
          p->Result,p->ActualLen);
      Ret = p->Result;
   } while(false);

   FreeTransferBuf(p);

   if(Ret != ISP_SUCCESS) {
      ELOG("returning %d\n",Ret);
   }
   return Ret;
}

int submit_int_msg (
   struct usb_device *dev,
   unsigned long pipe,
   void *buffer,
   int transfer_len,
   int interval) 
{
    int Ret = -1;
    UsbTransfer *p;

    do {
       if(usb_pipetype(pipe) != PIPE_INTERRUPT) {
          ELOG("non-interrupt pipe\n");
          break;
       }
       if((p = GetTransferBuf(dev,pipe)) == NULL) {
          ELOG("No empty xfer bufs\n");
          break;
       }
       p->Buf = buffer;
       p->Length = transfer_len;
       p->Token = usb_pipein(pipe) ? TOKEN_IN : TOKEN_OUT;
       Ret = isp_StartTransfer(p);
    } while(false);

    if(Ret != ISP_SUCCESS) {
       ELOG("returning %d\n",Ret);
    }
    return Ret;
}

// Called from the foreground, check for completed transfers.
// If the transfer has a call callback the call it
void usb_event_poll(void) 
{
    UsbTransfer *p;
    INT_SAVE;

    while((p = gReadyTransferBufs) != NULL) {
       if(p->CallBack != NULL) {
          DI();
          gReadyTransferBufs = p->Link;
          EI();
          p->CallBack(p);
          FreeTransferBuf(p);
       }
    }
}

isp_result_t isp_StartTransfer(UsbTransfer *p) 
{
   struct usb_device *dev = p->Dev;
   unsigned long pipe = p->Pipe;
   void *buffer = p->Buf;
   int length = p->Length;
   usb_token_t token = p->Token;
   isp_result_t result = ISP_SUCCESS;
   int TransferNum = p->TransferNum;
   uint32_t reg_ptd_skipmap;
   uint32_t multiplier;
   uint32_t port_number;
   uint32_t hub_address;
   uint32_t valid;
   uint32_t split;
   uint32_t se;
   uint32_t start_complete;
   uint32_t error_counter;
   uint32_t micro_frame;
   uint32_t micro_sa;
   uint32_t micro_scs;
   uint32_t ptd[PTD_SIZE_DWORD];
   uint32_t PtdBits;
   uint32_t PtdBits1;
   ptd_type_t ptd_type = (usb_pipetype(pipe) == PIPE_INTERRUPT) ? TYPE_INT : TYPE_ATL;
   usb_speed_t speed = (usb_speed_t)usb_pipespeed(pipe);
   uint32_t device_address = usb_pipedevice(pipe);
   uint32_t parent_address = (dev->parent != NULL) ? dev->parent->devnum : 0;
   uint32_t parent_port = dev->portnr;
   uint32_t max_packet_length = usb_maxpacket(dev, pipe);
   usb_ep_type_t ep_type = (usb_pipetype(pipe) == PIPE_BULK) ? EP_BULK : 
                           (usb_pipetype(pipe) == PIPE_INTERRUPT) ? EP_INTERRUPT : 
                           EP_CONTROL;
   uint32_t ep = usb_pipeendpoint(pipe);

   uint32_t payload_address = MEM_PAYLOAD_BASE + ((TransferNum * 1024) >> 3);
   uint32_t ptd_address = TransferNum * (PTD_SIZE_BYTE >> 3);
   uint32_t PtdBit = 1 << TransferNum;
   UsbTransfer **pActiveList;
   INT_SAVE;

   if(TransferNum == 1) {
      LOG_ENABLE();
      LOG("Logging enabled\n");
   }

   switch(ptd_type) {
      case TYPE_ATL:
         ptd_address += MEM_ATL_BASE;
         reg_ptd_skipmap = ISP_ATL_PTD_SKIPMAP;
         pActiveList = &gAtlTransferBufs;
         break;

      case TYPE_INT:
         ptd_address += MEM_INT_BASE;
         reg_ptd_skipmap = ISP_INT_PTD_SKIPMAP;
         pActiveList = &gIntTransferBufs;
         break;

      case TYPE_ISO:
         // not supported
      default:
         ELOG("ptd_type %d not implemented\n",ptd_type);
         result = ISP_NOT_IMPLEMENTED;
         return result;
   }

   if(token == TOKEN_SETUP) {
      usb_settoggle(dev,ep,0,0);
      usb_settoggle(dev,ep,1,0);
   }

   // If direction is output, write payload into ISP1760
   if((token == TOKEN_OUT || token == TOKEN_SETUP) && length != 0) {
      LOG("Copy %d bytes of payload into controller memeory @ 0x%x:\n",
          length,payload_address);
      LOG_HEX(buffer,length);
      isp_write_memory(payload_address,(uint32_t *)buffer,length);
   }

   // Build PTD

   LOG_R("%cD%02X:%02X ",ptd_type == TYPE_ATL ? 'A':'I',parent_address,
         parent_port,device_address);
   LOG_R((speed == SPEED_HIGH) ? "HS " : "FS ");
   LOG_R((token == TOKEN_IN) ? "TIN " : (token == TOKEN_OUT) ? "TOUT " : (token == TOKEN_SETUP) ? "TSETUP " : "TPING ");
   LOG_R((ep_type == EP_BULK) ? "EB" : (ep_type == EP_CONTROL) ? "EC" : "EI");
   LOG_R("%d L %d M %d\n",ep,length,max_packet_length);

   multiplier = (speed == SPEED_HIGH) ? 1 : 0;
   port_number = (speed == SPEED_HIGH) ? 0 : parent_port;
   hub_address = (speed == SPEED_HIGH) ? 0 : parent_address;
   valid = 0x01;
   split = (speed == SPEED_HIGH) ? 0 : 1;
   se = (speed == SPEED_HIGH) ? 0 : (speed == SPEED_FULL) ? 0 : 2;
   start_complete = 0x0;
   error_counter = 0x3;
   micro_frame = (ep_type == EP_INTERRUPT) ? // Polling every 8 ms for FS/LS
                 ((speed == SPEED_HIGH) ? (0xff) : (0x20)) : (0x00);
   micro_sa = (ep_type == EP_INTERRUPT) ? 
              ((speed == SPEED_HIGH) ? 0xff : 0x01) : 0;
   micro_scs = (ep_type == EP_INTERRUPT) ? 
               ((speed == SPEED_HIGH) ? 0 : 0xfe) : 0;

   memset(ptd, 0, 32);
   ptd[0] = ((ep & 0x1) << 31) |
            ((multiplier & 0x3) << 29) |
            ((max_packet_length & 0x7FF) << 18) |
            ((length & 0x7FFF) << 3) |
            (valid & 0x1);
   ptd[1] = ((hub_address & 0x7F) << 25) |
            ((port_number & 0x7F) << 18) |
            ((se & 0x3) << 16) |
            ((split & 0x1) << 14) |
            (((uint32_t)ep_type & 0x3) << 12) |
            (((uint32_t)token & 0x3) << 10) |
            ((device_address & 0x7F) << 3) |
            ((ep & 0xe) >> 1);
   ptd[2] = ((payload_address & 0xFFFF) << 8) |
            (micro_frame & 0xFF);
   ptd[3] = ((valid & 0x1) << 31) |
            ((start_complete & 0x1) << 27) |
            (usb_gettoggle(dev,ep,usb_pipeout(pipe)) << 25) |
            ((error_counter & 0x3) << 23);
   ptd[4] = micro_sa & 0xff;
   ptd[5] = micro_scs & 0xff;
   ptd[6] = 0;
   ptd[7] = 0;

   if(gDumpPtd) {
      LOG("Before execution transferNum: %d, ptd_address: 0x%x\n",
          TransferNum,ptd_address);;
      DumpPtd(ptd);
   }

// Copy PTD into device memory
   isp_write_memory(ptd_address,ptd,PTD_SIZE_BYTE);

   if(p->Result == ISP_TRANSFER_INIT) {
   // First time this transfer has been attempted
      p->StartTime = ticks_ms();
   }
   p->Result = ISP_TRANSFER_BUSY;

// Turn off skip bit for this PTD
   DI();
   p->Link = *pActiveList;
   *pActiveList = p;
   PtdBits = isp_read_dword(reg_ptd_skipmap);
   isp_write_dword(reg_ptd_skipmap, PtdBits & ~PtdBit);
   EI();

#if 0
   if(TransferNum == 1) {
      PtdBits = isp_read_dword(reg_ptd_skipmap);
      LOG("reg_ptd_skipmap (0x%x): 0x%x\n",reg_ptd_skipmap,PtdBits);
      PtdBits = isp_read_dword(ISP_ATL_PTD_LASTPTD);
      LOG("ISP_ATL_PTD_LASTPTD: 0x%x\n",PtdBits);
      PtdBits = isp_read_dword(ISP_BUFFER_STATUS);
      LOG("ISP_BUFFER_STATUS: 0x%x\n",PtdBits);
      delay_ms(50);
      PtdBits = isp_read_dword(ISP_ATL_PTD_DONEMAP);
      LOG("ISP_ATL_PTD_DONEMAP: 0x%x\n",PtdBits);
   }
#endif

#if 0
   if(reg_ptd_skipmap == ISP_INT_PTD_SKIPMAP) {
      PtdBits = isp_read_dword(reg_ptd_skipmap);
      LOG("reg_ptd_skipmap (0x%x): 0x%x\n",reg_ptd_skipmap,PtdBits);
      PtdBits = isp_read_dword(ISP_INT_PTD_LASTPTD);
      LOG("ISP_INT_PTD_LASTPTD: 0x%x\n",PtdBits);
      PtdBits = isp_read_dword(ISP_BUFFER_STATUS);
      LOG("ISP_BUFFER_STATUS: 0x%x\n",PtdBits);
      delay_ms(1000);
      PtdBits = isp_read_dword(ISP_INT_PTD_DONEMAP);
      LOG("ISP_INT_PTD_DONEMAP: 0x%x\n",PtdBits);
   }
#endif

   return result;
}

// Call by interrupt handler on error or transfer complete
isp_result_t isp_CompleteTransfer(UsbTransfer *p)
{
   struct usb_device *dev = p->Dev;
   unsigned long pipe = p->Pipe;
   uint32_t ep = usb_pipeendpoint(pipe);
   int length = p->Length;
   isp_result_t result = ISP_SUCCESS;
   bool Retry = false;
   INT_SAVE;
   int TransferNum = p->TransferNum;
   uint32_t payload_address = MEM_PAYLOAD_BASE + ((TransferNum * 1024) >> 3);
   uint32_t ptd_address = TransferNum * (PTD_SIZE_BYTE >> 3);
   uint32_t reg_ptd_skipmap;
   uint32_t ptd[PTD_SIZE_DWORD];
   uint32_t Toggle;
   uint32_t TransferLen;
   uint32_t PtdBits;
   uint32_t PtdBit = 1 << TransferNum;
   int NakTimeout = p->Timeout != 0 ? p->Timeout : NACK_TIMEOUT_MS;
   ptd_type_t ptd_type = (usb_pipetype(pipe) == PIPE_INTERRUPT) ? 
                         TYPE_INT : TYPE_ATL;

   LOG("Called\n");
   switch(ptd_type) {
      case TYPE_ATL:
         LOG_R("A");
         ptd_address += MEM_ATL_BASE;
         reg_ptd_skipmap = ISP_ATL_PTD_SKIPMAP;
         break;

      case TYPE_INT:
         LOG_R("I");
         ptd_address += MEM_INT_BASE;
         reg_ptd_skipmap = ISP_INT_PTD_SKIPMAP;
         break;

      case TYPE_ISO:
         // not supported
      default:
         ELOG("ptd_type %d not implemented\n",ptd_type);
         result = ISP_NOT_IMPLEMENTED;
         return result;
   }
// Readback the PTD
   isp_read_memory(ptd_address,ptd,PTD_SIZE_BYTE);
   TransferLen = ptd[3] & 0x7FFF;

   if(gDumpPtd) {
      LOG("Readback transferNum: %d, ptd_address: 0x%x\n",TransferNum,ptd_address);;
      DumpPtd(ptd);
   }

   do {
      // Check A bit
      if(ptd[3] & (1u << 31)) {
         ELOG("Error: PTD still active\n");
         result = ISP_SETUP_TIMEOUT;
         break;
      }
      // Check H bit
      if(ptd[3] & (1u << 30)) {
         // halt, do not retry
         LOG_R("HALT");
         result = ISP_TRANSFER_HALT;
         dev->status = USB_ST_STALLED;
         break;
      }
      // Check B bit
      if(ptd[3] & (1u << 29)) {
         LOG_R("BABBLE");
         result = ISP_BABBLE;
         dev->status = USB_ST_BABBLE_DET;
         break;
      }

      // Check X bit
      if(ptd[3] & (1u << 28)) {
         LOG_R("ERR");
         result = ISP_TRANSFER_ERROR;
         dev->status = USB_ST_BUF_ERR;
         break;
      }

      if(usb_pipeout(pipe) && TransferLen != p->Length) {
      // Wrote less than requested
         result = ISP_WRONG_LENGTH;
         dev->status = USB_ST_WRONG_LEN;
         LOG_R("WLEN");
         break;
      }
#if 0
      // NACK, retry later
      if(ticks_ms() - p->StartTime >= NakTimeout) {
         result = ISP_NACK_TIMEOUT;
         break;
      }
      else {
         Retry = true;
         // LOG_R("NACK");
         //delay_us(100);
         break;
      }
#endif
      if(p->Token == TOKEN_SETUP) {
         Toggle = 1;
         usb_settoggle(dev,ep,0,Toggle);
         usb_settoggle(dev,ep,1,Toggle);
      }
      else {
         Toggle = (ptd[3] >> 25) & 0x1;
         usb_settoggle(dev,ep,usb_pipeout(pipe),Toggle);
      }
      result = ISP_SUCCESS;
      dev->status = 0;
      LOG_R("OK\n");
      // If current direction is input, read payload back
      if(usb_pipein(pipe) && result == ISP_SUCCESS) {
         if(TransferLen != 0 && TransferLen <= p->Length) {
            if(TransferLen > p->Length) {
               ELOG("Requested %d, got %d\n",p->Length,TransferLen);
            // Only copy what will fit in the buffer
               TransferLen = p->Length;
            }
            p->ActualLen = TransferLen;
            isp_read_memory(payload_address,p->Buf,TransferLen);
         }
      }
   // This should probably be changed !!!
      dev->act_len = TransferLen;
   } while(false);
// No matter what happens, end this PTD
   DI();
   PtdBits = isp_read_dword(reg_ptd_skipmap);
   isp_write_dword(reg_ptd_skipmap,PtdBits | PtdBit);
   EI();

   LOG("Set Result to %d\n",result);
   p->Result = result;
   if(Retry) {
      isp_StartTransfer(p);
   }
   else if(usb_pipetype(pipe) != PIPE_INTERRUPT) {
      if(p->CallBack != NULL) {
         // There is a callback function, put the transfer on the ready list
         p->Link = gReadyTransferBufs;
         gReadyTransferBufs = p;
      }
   }

   return result;
}


#ifdef VERBOSE_DEBUG_LOGGING
void LOG_R_1cr(const char *label,int value)
{
   LOG_R("%s: 0x%x\n",label,value);
}

void DumpPtd(uint32_t *p)
{
   int i;
   const char *TokenTbl[] = {
      "OUT",
      "IN",
      "SETUP",
      "PING"
   };
   const char *EpTypeTbl[] = {
      "control",
      "???",
      "bulk",
      "interrupt"
   };
   const char *SeTypeTbl[] = {
      "full-speed",
      "???",
      "low-speed",
      "???"
   };
   int EndPt;
   bool Die = false;

   EndPt = ((p[0] >> 31) & 1) + ((p[1] & 07) << 1);

   LOG_R_1cr("V",p[0] & 1);
   if((p[3] >> 29) & 0x1) {
      LOG_R("Babble!\n");
      Die = true;
   }
   if((p[3] >> 30) & 0x1) {
      LOG_R("Halt!\n");
      Die = true;
   }

   if((p[3] >> 28) & 0x1) {
      LOG_R("Error!\n");
      Die = true;
   }
   LOG_R_1cr("A",(p[3] >> 31) & 0x1);
   LOG_R_1cr("BytesTodo",(p[0] >> 3) & 0x7fff);
   LOG_R_1cr("BytesDone",p[3] & 0x7fff);

   LOG_R_1cr("NakCnt",(p[3] >> 19) & 0xf);
   LOG_R_1cr("RL",(p[2] >> 25) & 0xf);

   LOG_R_1cr("MaxPak",(p[0] >> 18) & 0x7ff);
   LOG_R_1cr("Multp",(p[0] >> 29) & 0x3);
   LOG_R_1cr("EndPt",EndPt);
   LOG_R_1cr("DevAdr",(p[1] >> 3) & 0x7f);

   LOG_R("Token: %s\n",TokenTbl[(p[1] >> 10) & 0x3]);
   LOG_R("EpType: %s\n",EpTypeTbl[(p[1] >> 12) & 0x3]);
   LOG_R_1cr("DT",(p[3] >> 25) & 0x1);

   if((p[1] >> 14) & 0x1) {
      if(p[3] & (1 << 27)) {
         LOG_R("End Split\n");
      }
      else {
         LOG_R("Start Split\n");
      }
      LOG_R("  SE: %s\n",SeTypeTbl[(p[1] >> 16) & 0x3]);
      LOG_R_1cr("  Port",(p[1] >> 18) & 0x7f);
      LOG_R_1cr("  HubAdr",(p[1] >> 25) & 0x7f);
   }
   LOG_R_1cr("Start Adr",(((p[2] >> 8) & 0xffff) << 3) + 0x400);
   LOG_R_1cr("Cerr",(p[3] >> 23) & 0x3);
   LOG_R_1cr("Ping",(p[3] >> 26) & 0x1);

   LOG_R_1cr("J",(p[4] >> 5) & 0x1);

   if(((p[1] >> 12) & 0x3) == 3) {
   // Interrupt PTD
      LOG_R_1cr("uSA",p[4] & 0xf);
      LOG_R_1cr("uFrame",p[2] & 0xff);
      LOG_R_1cr("uSCS",p[5] & 0xff);
   }
   else {
      LOG_R_1cr("NextPTD",p[4] & 0x1f);
   }

   for(i = 0; i < 8; i++) {
      LOG_R("DW%d: 0x%08x\n",i,p[i]);
   }

   while(Die);
}
#endif

EnableUsbDebug()
{
   LOG_ENABLE();
}

