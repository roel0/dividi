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

#if defined _WIN32
  #include <windows.h>
#endif
#include "serial.h"

#ifdef DEBUG
#define dbg(fmt, ...) \
          do { fprintf(stderr, "%s:%d:%s(): " \
                       fmt, __FILE__,__LINE__, __func__, ##__VA_ARGS__); } while (0)
#else
#define dbg(...) do { }while(0)
#endif


// Look up table for all links
struct s_link {
  int tcp_port;
  struct s_serial serial;
};

/**
 * Add a link to the look up table
 */
struct s_link * add_link(char *serial_port, char *tcp_port);

/**
 * Set file paths
 */
void set_root_file(char *value);
void set_key_file(char *value);
void set_cert_file(char *value);

/**
 * Outputs error message
 */
void print_error(char *error_msg);

#endif
