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
#define TIMEOUT_BIT_MASK       0x7FFFF
#define TYPE_BIT_POS           14
#define VAL_NO_SERIAL_RESPONSE 0
#define VAL_SERIAL_RESPONSE    1
#define ERR_NO_ERR             0
#define ERR_SERIAL_TIMEOUT     0xFF

// message structure
struct s_message {
  union {
    struct {
      uint16_t timeout;
      uint16_t length;
    } recv;
    struct {
      uint16_t errorcode;
      uint16_t length;
    } resp;
  } hdr;
  char *command;
};

#endif
