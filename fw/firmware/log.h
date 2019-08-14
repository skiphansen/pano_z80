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

// use tiny printf replacment, not the standard one from the runtime library
#include "printf.h"

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

// These only log when debug is enabled
#ifdef DEBUG_LOGGING
// LOG - normal debug messages
#define LOG(format, ...) printf("%s: " format,__FUNCTION__ ,## __VA_ARGS__)
// LOG_R - raw debug messages (function name prefix not added) 
#define LOG_R(format, ...) printf(format,## __VA_ARGS__)
#else
#define LOG(format, ...)
#define LOG_R(format, ...)
#endif

#ifdef VERBOSE_DEBUG_LOGGING
// VLOG - verbose debug messages
#define VLOG(format, ...) printf("%s: " format,__FUNCTION__ ,## __VA_ARGS__)
// VLOG_R - raw debug messages (function name prefix not added)
#define VLOG_R(format, ...) printf(format,## __VA_ARGS__)
#else
#define VLOG(format, ...)
#define VLOG_R(format, ...)
#endif

#endif   // _LOG_H_

