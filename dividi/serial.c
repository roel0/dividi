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
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <tchar.h>
  #include <strsafe.h>
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

#ifdef __linux__

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
    perror ("error tcgetattr - get the parameters associated with the terminal");
    err = -1;
  }
  else if (!serial->auto_conf) {
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
      perror ("error tcsetattr");
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
    perror ("error tggetattr - get the parameters associated with the terminal");
    err = -1;
  }
  else {
    /* If we don't unset FNDELAY, VMIN and VTIME have no effect */
    if(timeout_ms) {
      arg = 0;
    }
    if(fcntl(serial_port, F_SETFL, arg) == -1) {
      perror("fcntl failed");
      err = -1;
    }
    tty.c_cc[VMIN]  = (timeout_ms) ? 0 : 1;
    tty.c_cc[VTIME] = timeout_ms/100;            // in intervals of 0.1s

    if (tcsetattr (serial_port, TCSADRAIN, &tty) != 0) {
      perror ("error setting term attributes");;
      err = -1;
    }
  }
  return err;
}

/**
 * Open a serial port by given port name
 *
 * @return identifier for the serial port
 *         < 0 on error
 */
HANDLE serial_open(char *port_name, struct s_serial *serial)
{
  HANDLE serial_port = open (port_name, O_RDWR | O_NOCTTY | O_SYNC);
  serial->serial_port = 0;
  serial->serial_port = serial_port;
  if (serial_port >= 0) {
    if(set_interface_attribs(serial) < 0) {
      close(serial_port);
      serial_port = -1;
    }
    else if(serial_set_timeout(serial_port, serial->timeout) < 0) {
      close(serial_port);
      serial_port = -1;
    }
  }
  else {
    serial_port = -1;
  }
  return serial_port;
}
#elif _WIN32
int serial_set_timeout(HANDLE serial_port, int timeout_ms)
{
  COMMTIMEOUTS timeouts = {0};
  timeouts.ReadIntervalTimeout = 0;
  timeouts.ReadTotalTimeoutConstant = SERIAL_DEFAULT_TIMEOUT;
  timeouts.ReadTotalTimeoutMultiplier = 0;
  timeouts.WriteTotalTimeoutConstant = SERIAL_DEFAULT_TIMEOUT;
  timeouts.WriteTotalTimeoutMultiplier = 0;
  if(SetCommTimeouts(serial_port, &timeouts) == 0) {
    fprintf(stderr, "SetCommTimeouts: %d\n", WSAGetLastError());
    CloseHandle(serial_port);
    return -1;
  }
  return 0;
}
/**
 * This function will allow you to set
 * attributes of the serial port on WINDOWS
 */
static int set_interface_attribs (HANDLE serial_port, int parity, int speed)
{
  int err = 0;
  COMMTIMEOUTS timeouts = {0};
  DCB dcbSerialParams = {0};
  // 1 stop bit, no parity)
  dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
  if (GetCommState(serial_port, &dcbSerialParams) == 0) {
    fprintf(stderr, "GetCommState: %d\n", WSAGetLastError());
    CloseHandle(serial_port);
    err = -1;
  }
  else {
    dcbSerialParams.BaudRate = speed;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = parity;
    if(SetCommState(serial_port, &dcbSerialParams) == 0) {
      fprintf(stderr, "SetCommState: %d\n", WSAGetLastError());
      CloseHandle(serial_port);
      err = -1;
    }
    else {
      // Set COM port timeout settings
      err = serial_set_timeout(serial_port, SERIAL_DEFAULT_TIMEOUT);
    }
  }
  return err;
}

/**
 * This function will open a serial port on WINDOWS
 *
 * @return the serial port handle
 *         < 0 on error
 */
HANDLE serial_open(char *port_name, struct s_serial *serial)
{
  HANDLE serial_port;

  // Open the highest available serial port number
  serial_port = CreateFile(
                port_name, GENERIC_READ|GENERIC_WRITE, 0, NULL,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
  if (serial_port == INVALID_HANDLE_VALUE) {
    serial_port = NULL;
  }
  else
  {
    if(set_interface_attribs(serial_port, NOPARITY, CBR_9600) < 0) {
      fprintf(stderr, "set_interface_attribs: %d\n", WSAGetLastError());
      serial_port = NULL;
    }
  }
  return serial_port;
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
  if(!WriteFile(serial_port,data, strlen(data), &bytes_written, NULL)) {
#endif
    print_error("serial_write");
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
    ReadFile(serial_port, pos, SERIAL_DATA_CHUNK_SIZE, &bytes_read, NULL);
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
