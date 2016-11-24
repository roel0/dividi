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
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <tchar.h>
#elif __linux__
  #include <sys/stat.h>
  #include <unistd.h>
  #include <errno.h>
  #include <fcntl.h>
  #include <termios.h>
  #include <arpa/inet.h>
  #include <sys/socket.h>
#endif
#include "serial.h"
#include "dividi.h"

#define SERIAL_DATA_CHUNK_SIZE 512
#define SERIAL_DATA_MAX        50*SERIAL_DATA_CHUNK_SIZE

#ifdef _WIN32
  #define SERIAL_PREFIX       "\\\\.\\"
#endif

static int rate_to_constant(int baudrate);

#ifdef __linux__
/**
 * Convert data_bits in integer format
 * to the constant format
 */
static int databits_to_constant(int data_bits)
{
#define CS(x) case x: return CS##x
    switch(data_bits) {
        CS(5);     CS(6);     CS(7);    CS(8);
    default: return 0;
    }
#undef CS
}

/**
 * Sets the attributes of the serial port
 *
 */
static int set_interface_attribs(struct s_serial *serial)
{
  struct termios tty;
  int err = 0;
  memset (&tty, 0, sizeof tty);
  if (serial->auto_conf && tcgetattr (serial->serial_port, &tty) != 0) {
    err = -1;
  } else if (!serial->auto_conf) {
    /* set baudrate */
    cfsetospeed (&tty, rate_to_constant(serial->baudrate));
    cfsetispeed (&tty, rate_to_constant(serial->baudrate));

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | databits_to_constant(serial->data_bits);
    // disable IGNBRK for mismatched speed tests; otherwise receive break
    // as \000 chars
    tty.c_iflag &= ~IGNBRK;         // disable break processing
    tty.c_lflag = 0;                // no signaling chars, no echo,
                                    // no canonical processing
    tty.c_oflag = 0;                // no remapping, no delays

    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl
    tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,

    /* parity */
    if(serial->parity == PARITY_NONE) {
      tty.c_cflag &= ~(PARENB | PARODD);
    } else if(serial->parity == PARITY_ODD) {
      tty.c_cflag &= ~(PARENB | PARODD);
      tty.c_cflag |= (PARENB | PARODD);
    } else {
      tty.c_cflag &= ~(PARENB | PARODD);
      tty.c_cflag |= (PARENB);
    }

    /* stop bits */
    if(serial->stop_bits != 2) {
      tty.c_cflag &= ~CSTOPB;
    } else {
      tty.c_cflag |= CSTOPB;
    }

    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr(serial->serial_port, TCSANOW, &tty) != 0) {
      err = 1;
    }
  }

  return err;
}

/**
 * Sets the serial port in blocking or timeout
 * mode
 * @serial_port the serial port identifier
 * @timeout_ms timeout in miliseconds, 0 for blocking
 */
int serial_set_timeout(HANDLE serial_port, int timeout_ms)
{
  struct termios tty;
  int err = 0;
  int arg = FNDELAY;
  memset (&tty, 0, sizeof tty);
  if (tcgetattr (serial_port, &tty) != 0) {
    err = -1;
  } else {
    /* If we don't unset FNDELAY, VMIN and VTIME have no effect */
    if(timeout_ms) {
      arg = 0;
    }
    if(fcntl(serial_port, F_SETFL, arg) == -1) {
      err = -1;
    }
    tty.c_cc[VMIN]  = (timeout_ms) ? 0 : 1;
    tty.c_cc[VTIME] = timeout_ms/100;            // in intervals of 0.1s

    if (tcsetattr (serial_port, TCSADRAIN, &tty) != 0) {
      err = -1;
    }
  }
  return err;
}

/**
 * Open a serial port by given port name
 *
 * @return   0 on succes
 *         < 0 on error
 */
int serial_open(struct s_serial *serial)
{
  int ret;

  serial->serial_port = open(serial->str_serial_port, O_RDWR | O_NOCTTY | O_SYNC);;
  if (serial->serial_port >= 0) {
    if(set_interface_attribs(serial) < 0) {
      close(serial->serial_port);
      ret = -1;
    } else if(serial_set_timeout(serial->serial_port, serial->timeout) < 0) {
      close(serial->serial_port);
      ret = -1;
    }
  } else {
    ret = -1;
  }
  return ret;
}
/**
 * Convert a baudrate in integer format
 * to the constant format
 */
static int rate_to_constant(int baudrate)
{
 #define B(x) case x: return B##x
  switch(baudrate) {
    B(50);     B(75);     B(110);    B(134);    B(150);
    B(200);    B(300);    B(600);    B(1200);   B(1800);
    B(2400);   B(4800);   B(9600);   B(19200);  B(38400);
    B(57600);  B(115200); B(230400); B(460800); B(500000);
    B(576000); B(921600); B(1000000);B(1152000);B(1500000);
  default: return 0;
  }
#undef B
}


#elif _WIN32
int serial_set_timeout(HANDLE serial_port, int timeout_ms)
{
  int ret = 0;
  COMMTIMEOUTS timeouts = {0};

  timeouts.ReadIntervalTimeout = 0;
  timeouts.ReadTotalTimeoutConstant = timeout_ms;
  timeouts.ReadTotalTimeoutMultiplier = 0;
  timeouts.WriteTotalTimeoutConstant = timeout_ms;
  timeouts.WriteTotalTimeoutMultiplier = 0;

  if(SetCommTimeouts(serial_port, &timeouts) == 0) {
    CloseHandle(serial_port);
    ret = -1;
  }
  return ret;
}
/**
 * This function will allow you to set
 * attributes of the serial port on WINDOWS
 */
static int set_interface_attribs (struct s_serial *serial)
{
  int err = 0;
  DCB dcbSerialParams = {0};

  dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
  if (GetCommState(serial->serial_port, &dcbSerialParams) == 0) {
    CloseHandle(serial->serial_port);
    err = -1;
  } else {
    dcbSerialParams.BaudRate = rate_to_constant(serial->baudrate);
    dcbSerialParams.ByteSize = serial->data_bits;
    dcbSerialParams.StopBits = serial->stop_bits;
    dcbSerialParams.Parity = serial->parity;
    if(SetCommState(serial->serial_port, &dcbSerialParams) == 0) {
      CloseHandle(serial->serial_port);
      err = -1;
    } else {
      // Set COM port timeout settings
      err = serial_set_timeout(serial->serial_port, serial->timeout);
    }
  }
  return err;
}

/**
 * Open a serial port by given port name
 *
 * @return   0 on succes
 *         < 0 on error
 */
int serial_open(struct s_serial *serial)
{
  int ret = 0;
  char serial_port[SERIAL_NAME_MAX];

  snprintf(serial_port, SERIAL_NAME_MAX, "%s%s", SERIAL_PREFIX, serial->str_serial_port);
  // Open the highest available serial port number
  serial->serial_port = CreateFile(
                        serial_port, GENERIC_READ|GENERIC_WRITE, 0, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
  if (serial->serial_port == INVALID_HANDLE_VALUE) {
    ret = -1;
  } else {
    if(set_interface_attribs(serial) < 0) {
      ret = -1;
    }
  }
  return ret;
}

/**
 * Convert a baudrate in integer format
 * to the constant format
 */
static int rate_to_constant(int baudrate)
{
#define CBR(x) case x: return CBR_##x
  switch(baudrate) {
    CBR(110);     CBR(300);     CBR(600);
    CBR(1200);    CBR(2400);    CBR(4800);
    CBR(9600);    CBR(14400);   CBR(19200);
    CBR(38400);   CBR(57600);   CBR(115200);
    CBR(128000);  CBR(256000);
  default: return 0;
  }
#undef B
}


#endif

/**
 * This function will close a serial port
 *
 * @serial_port the serial port handler
 */
void serial_close(HANDLE serial_port)
{
#ifdef __linux__
  close(serial_port);
#elif _WIN32
  CloseHandle(serial_port);
#endif
}

/**
 * Write data to a given serial port
 *
 * @serial_port the serial port identifier
 * @data pointer to a null terminated string
 */
int serial_write(HANDLE serial_port, char *data)
{
  int bytes_written;
#ifdef __linux__
  if((bytes_written = write(serial_port, data, strlen(data))) != strlen(data)) {
#elif _WIN32
  if(!WriteFile(serial_port,data, strlen(data), (LPDWORD) &bytes_written, NULL)) {
#endif
    serial_close(serial_port);
    bytes_written = -1;
  }
  return bytes_written;
}

/**
 * Reads an unknown amount chunks of data from a given serial port
 *
 * @serial_port the serial port identifier
 * @total_bytes_read will hold the total amount of bytes
 *                  that has been read
 * @return pointer to the read data
 */
char *serial_read(HANDLE serial_port, int *total_bytes_read)
{
  int bytes_read;
  int total_read = 0;

  char *data = (char *) malloc(SERIAL_DATA_CHUNK_SIZE);
  char *pos = data;
  while(1) {
#ifdef __linux__
    bytes_read = read(serial_port, pos, SERIAL_DATA_CHUNK_SIZE);
#elif _WIN32
    ReadFile(serial_port, pos, SERIAL_DATA_CHUNK_SIZE, (LPDWORD) &bytes_read, NULL);
#endif
    if(!bytes_read || ((total_read+=bytes_read) >= SERIAL_DATA_MAX)) {
      break;
    }
    if(bytes_read != SERIAL_DATA_CHUNK_SIZE) {
      // TImeout occured
      break;
    }
    data = (char *) realloc(data, total_read+SERIAL_DATA_CHUNK_SIZE+1);
    pos = data + total_read;
  }
  data[total_read] = '\0';
  *total_bytes_read = total_read;
  return data;
}
