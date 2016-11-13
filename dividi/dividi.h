/*****************************************************************************
 *
 * Dividi : A ComPort TCP share tool
 *
 *          Copyright (c) 2016. Some rights reserved.
 *          See LICENSE and COPYING for usage.
 *
 * Authors: Roel Postelmans
 *
 ****************************************************************************/
#ifndef __DIVIDI_H__
#define __DIVIDI_H__

#ifdef DEBUG
#define dbg(fmt, ...) \
          do { fprintf(stderr, "%s:%d:%s(): " \
                       fmt, __FILE__,__LINE__, __func__, ##__VA_ARGS__); } while (0)
#else
#define dbg(...) do { }while(0)
#endif

#ifdef __linux__
typedef int HANDLE;
#endif


#endif
