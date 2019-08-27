/*
 *  log.h
 *
 *  Copyright (C) 2019  Skip Hansen
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
 * Copyright (C) 1987-2017 by Udo Munk
 *
 */
#ifndef _LOG_H_
#define _LOG_H_

// By default logging is sent to the screen only.  It can also be sent to the
// serial port by defining LOG_TO_BOTH or it can be sent
// to the serial port ONLY by defining LOG_TO_SERIAL

// By default only ALOG/ALOG_R (Always log) and ELOG/ELOG_R (Error log) are
// enabled.  These can be disabled by definining LOGGING_DISABLED.

// Debug logging can be enabled by defining DEBUG_LOGGING and
// verbose debug logging can be enabled by defining VERBOSE_DEBUG_LOGGING.

// use tiny printf replacment, not the standard one from the runtime library
#include "printf.h"

#define LOG_SERIAL   1
#define LOG_MONITOR  2
void LogPutc(char c,void * arg);

// These always log:
// ALOG - Normal messages 
#ifndef LOGGING_DISABLED
   #define ALOG(format, ...) printf("%s: " format,__FUNCTION__ ,## __VA_ARGS__)
   #define ALOG_R(format, ...) printf(format,## __VA_ARGS__)
   // ELOG - error errors
   #define ELOG(format, ...) printf("%s: " format,__FUNCTION__ ,## __VA_ARGS__)
#else
   #define ALOG(format, ...)
   #define ALOG_R(format, ...)
   #define ELOG(format, ...)
#endif

#ifdef DEBUG_LOGGING
   // These only log when debug is enabled

   #define PRINTF(format, ...) fctprintf(LogPutc,&gLogFlags,format,## __VA_ARGS__)
   // LOG - normal debug messages
   #define LOG(format, ...) PRINTF("%s: " format,__FUNCTION__ ,## __VA_ARGS__)
   // LOG_R - raw debug messages (function name prefix not added) 
   #define LOG_R(format, ...) PRINTF(format,## __VA_ARGS__)

   #if defined(LOG_TO_SERIAL)
      static int gLogFlags = LOG_SERIAL;
   #elif defined(LOG_TO_BOTH)
      static int gLogFlags = LOG_SERIAL | LOG_MONITOR;
   #else
      static int gLogFlags = LOG_MONITOR;
   #endif

   static void LogHex(void *Data,int Len)
   {
      int i;
      uint8_t *cp = (uint8_t *) Data;

      for(i = 0; i < Len; i++) {
         if(i != 0 && (i & 0xf) == 0) {
            LOG_R("\n");
         }
         else if(i != 0) {
            LOG_R(" ");
         }
         LOG_R("%02x",cp[i]);
      }
      if(((i - 1) & 0xf) != 0) {
         LOG_R("\n");
      }
   }
#else
   #define LogHex(x,y)
   #define LOG(format, ...)
   #define LOG_R(format, ...)
#endif

#ifdef VERBOSE_DEBUG_LOGGING
   // VLOG - verbose debug messages enabledENABLE
   #define VLOG   LOG
   #define VLOG_R LOG_R
#else
   #define VLOG(format, ...)
   #define VLOG_R(format, ...)
#endif

#endif   // _LOG_H_

