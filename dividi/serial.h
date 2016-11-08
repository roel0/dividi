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
#ifndef SERIAL_H
#define SERIAL_H

#include "dividi.h"
#define SERIAL_DEFAULT_TIMEOUT                   200

/**
 * Open a serial port by given port name
 *
 * @return identifier for the serial port
 *         < 0 on error
 */
#ifdef __linux__
int serial_open();
#elif _WIN32
HANDLE serial_open();
#endif

/**
 * Sets the serial port in blocking or timeout
 * mode
 * @fd the serial port identifier
 * @timeout_ms timeout in miliseconds, 0 for blocking
 */
int serial_set_timeout(HANDLE fd, int timeout_ms);

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
