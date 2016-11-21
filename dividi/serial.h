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
#ifndef __SERIAL_H__
#define __SERIAL_H__

#define SERIAL_DEFAULT_TIMEOUT                   200

#ifdef __linux__
typedef int HANDLE;
#elif defined _WIN32
#undef PARITY_EVEN
#undef PARITY_NONE
#undef PARITY_ODD
#endif


enum e_flow {
  FLOW_NONE,
  FLOW_XON_XOFF,
  FLOW_RTS_CTS,
  FLOW_DSR_DTR
};

enum e_parity {
  PARITY_NONE,
  PARITY_ODD,
  PARITY_EVEN
};

struct s_serial {
  HANDLE serial_port;
  int timeout;
  int baudrate;
  int data_bits;
  int stop_bits;
  int auto_conf;
  enum e_parity parity;
  enum e_flow flow;
};

/**
 * Open a serial port by given port name
 *
 * @return identifier for the serial port
 *         < 0 on error
 */
HANDLE serial_open();

/**
 * Sets the serial port in blocking or timeout
 * mode
 * @fd the serial port identifier
 * @timeout_ms timeout in miliseconds, 0 for blocking
 */
int serial_set_timeout(HANDLE serial_port, int timeout_ms);

/**
 * Closes a serial port
 *
 * @serial_port the serial port handler
 */
void serial_close(HANDLE serial_port);

/**
 * Write data to a given serial port
 *
 * @serial_port the serial port identifier
 * @data pointer to a null terminated string
 */
int serial_write(HANDLE serial_port, char *data);

/**
 * Reads an unknown amount chunks of data from a given serial port
 *
 * @serial_port the serial port identifier
 * @total_bytes_read will hold the total amount of bytes
 *                  that has been read
 * @return pointer to the read data
 */
char *serial_read(HANDLE serial_port, int *total_bytes_read);

#endif
