#include "util.c"
#include "conf.c"
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
  for(i=0; i<strlen(data)-2; i++) {
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
  int i,j;
  struct s_conn conn;
  char **message;
  SSL ssl;

  init();
  sleep(2);
  index = tcp2serial_queue_index;
  conn.socket = &ssl;
  conn.link = (struct s_link *) malloc(sizeof(struct s_link));
  conn.link->serial.serial_port = 1;
  message = (char **) malloc(NBR_OF_MESSAGES*sizeof(char *));
  for(j=0;j<NBR_OF_MESSAGES; j++) {
    message[j]= (char *) malloc(10+1);
    for(i=0; i<9; i++) {
      message[j][i]='a';
    }
    message[j][i]='\n';
    message[j][i+1] = '\0';
    tcp2serial_queue_add(&conn, message[j]);
  }
  assert(tcp2serial_queue_index == index+NBR_OF_MESSAGES);
  release_queue_sem(DIVIDI_TCP2SERIAL_QUEUE, NBR_OF_MESSAGES);
  sleep(1);
  assert(serial_write_calls == NBR_OF_MESSAGES);
}
