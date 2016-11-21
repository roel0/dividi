#include "serial.c"

#include <assert.h>
#include <math.h>
#include <sys/time.h>
#include "dividi.h"


int main(int argc, char *argv[])
{
  char *data = NULL;
  int bytes_read;
  int bytes_written;
  int file_length;
  int i;
  struct s_serial serial;
  int fd = open("test/dummy/serial.io", O_RDWR);

  memset(&serial, 0, sizeof(struct s_serial));

  assert(fd > 0);
  file_length = lseek(fd, 0, SEEK_END);
  printf("Dummy file length: %d\n", file_length);
  lseek(fd, 0L, SEEK_SET);
  data = serial_read(fd, &bytes_read);
  printf("Bytes read length: %d\n", bytes_read);
  for(i=0; i<bytes_read; i++)
    printf("%c", data[i]);
  assert(bytes_read == file_length);
  lseek(fd, 0L, SEEK_SET);
  bytes_written = serial_write(fd, data);
  printf("Bytes written length: %d\n", bytes_written);
  assert(bytes_written == file_length);
  close(fd);

  fd = serial_open("/dev/ttyS11", &serial);
  // Serial port timeout test
  struct timeval tval_before, tval_after, tval_result;
  for(i = 500; i<5000; i+=500) {
    serial_set_timeout(fd, i);
    gettimeofday(&tval_before, NULL);
    data = serial_read(fd, &bytes_read);
    gettimeofday(&tval_after, NULL);
    timersub(&tval_after, &tval_before, &tval_result);
    printf("Testing timeout %dms\n", i);
    assert(round(tval_result.tv_sec*10.0+tval_result.tv_usec/100000.0) == i/100);
  }

  return 0;
}
