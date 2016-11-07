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
#define SERIAL_TIMEOUT                   10

/**
 * This function will open the communications port on LINUX
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
 * This function will close a serial port
 *
 * @serial_port the serial port handler
 */
void serial_close(HANDLE serial_port);

/**
 * This fynction will write data on a given
 * serial port
 *
 * @serial_port the serial port
 * @data pointer to a null terminated string
 */
int serial_write(HANDLE serial_port, char *data);

/**
 * This function will read from the serial port
 * if the device has responded to a serial_write
 * with an error (this can only be used if no
 * response was expected)
 * Note: If no error is found, this error will
 * have at least SERIAL_TIMEOUT execution time
 *
 * @serial_port the serial port handler
 * @allocated buffer by the user to store
 *            the error in
 *
 * @return > 0 if an error has been read
 */
int serial_read_error(HANDLE serial_port, char *data);

/**
 * This function will read from the serial port
 * until a newline is found
 * Or it will timeout
 *
 * @serial_port the serial port handler
 * @data user allocated buffer to store the
 *       to be read data in
 *
 * @return the amount of bytes read
 */
int serial_read(HANDLE serial_port, char *data);

#endif
