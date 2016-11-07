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
  #include <Winsock2.h>
  #include <Ws2tcpip.h>
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

#define SERIAL_DATA_CHUNK_SIZE 512
#define SERIAL_DATA_MAX        50*SERIAL_DATA_CHUNK_SIZE

#ifdef __linux__
/**
 * This function will allow you to set
 * attributes of the serial port
 * @fd serial port identifier
 * @speed the baudrate
 * @parity the amount of parity bits
 */
static int set_interface_attribs (int fd, int speed, int parity)
{
  struct termios tty;
  int err = 0;
  memset (&tty, 0, sizeof tty);
  if (tcgetattr (fd, &tty) != 0) {
    perror ("error tcgetattr - get the parameters associated with the terminal");
    err = -1;
  }
  else {
    /* set baudrate */
    cfsetospeed (&tty, speed);
    cfsetispeed (&tty, speed);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
    // disable IGNBRK for mismatched speed tests; otherwise receive break
    // as \000 chars
    tty.c_iflag &= ~IGNBRK;         // disable break processing
    tty.c_lflag = 0;                // no signaling chars, no echo,
                                    // no canonical processing
    tty.c_oflag = 0;                // no remapping, no delays

    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl
    tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
                                    // enable reading
    tty.c_cflag &= ~(PARENB | PARODD);      // shut off parity
    tty.c_cflag |= parity;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr (fd, TCSANOW, &tty) != 0) {
      perror ("error tcsetattr");
      err = 1;
    }
  }

  return err;
}

/**
 * This function will allow you to set
 * the blocking state of the serial port
 * @fd the serial port identifier
 * @should_block boolean of blockign state
 */
static int set_blocking (int fd, int should_block) {
  struct termios tty;
  int err;
  memset (&tty, 0, sizeof tty);
  if (tcgetattr (fd, &tty) != 0) {
    perror ("error tggetattr - get the parameters associated with the terminal");
    err = -1;
  }
  else {
    /* If we don't unset FNDELAY, VMIN and VTIME have no effect */
    (should_block) ? fcntl(fd, F_SETFL, 0) : fcntl(fd, F_SETFL, FNDELAY);
    tty.c_cc[VMIN]  = should_block ? 1 : 0;
    tty.c_cc[VTIME] = SERIAL_TIMEOUT;            // 0.1 seconds read timeout

    if (tcsetattr (fd, TCSADRAIN, &tty) != 0) {
      perror ("error setting term attributes");;
      err = -1;
    }
  }
  return err;
}
/**
 * This function will open the communications port on LINUX
 *
 * @return identifier for the serial port
 *         < 0 on error
 */
int serial_open(char *port_name)
{
  int fd = open (port_name, O_RDWR | O_NOCTTY | O_SYNC);
  if (fd >= 0) {
    if(set_interface_attribs (fd, B9600, 0) < 0) {
      fd = -1;
    }
    else if(set_blocking (fd, 0) < 0) {
      fd = -1;
    }
  }
  else {
    perror ("serial_open failed");
    fd = -1;
  }
  return fd;
}
#elif _WIN32
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
      timeouts.ReadIntervalTimeout = 0;
      timeouts.ReadTotalTimeoutConstant = SERIAL_TIMEOUT;
      timeouts.ReadTotalTimeoutMultiplier = 0;
      timeouts.WriteTotalTimeoutConstant = SERIAL_TIMEOUT;
      timeouts.WriteTotalTimeoutMultiplier = 0;
      if(SetCommTimeouts(serial_port, &timeouts) == 0) {
        fprintf(stderr, "SetCommTimeouts: %d\n", WSAGetLastError());
        CloseHandle(serial_port);
        err = -1;
      }
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
HANDLE serial_open()
{
  HANDLE serial_port;

  // Open the highest available serial port number
  serial_port = CreateFile(
                port_name, GENERIC_READ|GENERIC_WRITE, 0, NULL,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
  if (serial_port == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "CreateFile: %d\n", WSAGetLastError());
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
 * This fynction will write data on a given
 * serial port
 *
 * @serial_port the serial port identifier
 * @data pointer to a null terminated string
 */
int serial_write(HANDLE serial_port, char *data)
{
  int err = 0;
#ifdef __linux__
  if(write(serial_port, data, strlen(data)) != strlen(data)) {
#elif _WIN32
  DWORD bytes_written;
  if(!WriteFile(serial_port,data, strlen(data), &bytes_written, NULL)) {
#endif
    perror("Error while writing to serial port");
    serial_close(serial_port);
    err = -1;
  }
  return err;
}

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
int serial_read_error(HANDLE serial_port, char *data)
{
  char c;
  int index = 0;

#ifdef __linux__
  while(read(serial_port, &c, 1) > 0) {
#elif _WIN32
  DWORD bytes_read;
  while(ReadFile(serial_port, &c, 1, &bytes_read, NULL)) {
    if(bytes_read != 1) {
      break;
    }
#endif
    data[index++] = c;
    if(c == '\n') {
      data[index] = '\0';
      break;
    }
  }
  return index;
}

/**
 * Reads chunks of data from a given serial port
 *
 * @serial_port the serial port identifier
 * @data location for the to be read data
 * @return the amount of bytes read
 */
int serial_read(HANDLE serial_port, char *data)
{
  int bytes_read;
  int total_read;

  data = (char *) malloc(SERIAL_DATA_CHUNK_SIZE);
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
    data = (char *) realloc(data, total_read+SERIAL_DATA_CHUNK_SIZE);
    pos = data + total_read;
  }
  return total_read;
}

