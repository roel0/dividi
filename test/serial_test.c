#include "serial.c"

#include <assert.h>
#include "dividi.h"


int main(int argc, char *argv[])
{
  char *data = NULL;
  int bytes_read;
  int bytes_written;
  int file_length;
  int i;
  int fd = open("dummy/serial.io", O_RDWR);

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
  return 0;
}
