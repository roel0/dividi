#include "dividi.c"

#include <assert.h>

#define NBR_OF_MESSAGES 20

int serial_write_calls = 0;
int serial_read_calls = 0;

//mocks
ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
  return 0;
}

int serial_write(HANDLE serial_port, char *data)
{
  int i;
  for(i=0; i<strlen(data)-1; i++) {
    assert(data[i]=='a');
  }
  serial_write_calls++;
  return 0;
}
char *serial_read(HANDLE serial_port, int *total_bytes_read)
{
  if(serial_read_calls % 2)
    *total_bytes_read = 0;
  else
    *total_bytes_read = 10;
  serial_read_calls++;
  return NULL;
}
void serial_close(HANDLE serial_port)
{

}

HANDLE serial_open()
{
  return 0;
}

int main(int argc, char *argv[])
{
  int index;
  int i;
  int total;
  struct s_connection conn;
  char *message;
  SSL ssl;

  conn.socket = &ssl;
  conn.link = (struct s_link *) malloc(sizeof(struct s_link));
  conn.link->serial_port = 0;
  message= (char *) malloc(10*NBR_OF_MESSAGES+2);
  for(i=0; i<10*NBR_OF_MESSAGES; i++) {
    message[i]='a';
    if(i && !(i%10)) {
      message[i]='\n';
    }
  }
  message[i] = '\n';
  message[i+1] = '\0';

  init_queues();
  sleep(2);
  index = out_queue_index;
  total=out_queue_add(&conn, message);
  assert(out_queue_index == index+NBR_OF_MESSAGES);
  release_queue_sem(DIVIDI_OUT_QUEUE, total);
  sleep(1);
  assert(serial_write_calls == NBR_OF_MESSAGES);
}
