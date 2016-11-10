#include "dividi.c"

#include <assert.h>

#define NBR_OF_MESSAGES 20

int serial_write_calls = 0;
int serial_read_calls = 0;

//mocks
ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
  if((serial_read_calls-1) % 2)
    assert(len == sizeof(struct s_message));
  else
    assert(len == sizeof(struct s_message) + 10);
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
  struct s_message command;

  conn.socket = 1;
  command.command = (char *) malloc(10*NBR_OF_MESSAGES+2);
  for(i=0; i<10*NBR_OF_MESSAGES; i++) {
    command.command[i]='a';
    if(i && !(i%10)) {
      command.command[i]='\n';
    }
  }
  command.command[i] = '\n';
  command.command[i+1] = '\0';
  command.hdr.recv.timeout = TYPE_JUST_SEND<<TYPE_BIT_POS;

  init_queue();
  sleep(2);
  index = queue_index;
  total=queue_add(&conn, &command);
  assert(queue_index == index+NBR_OF_MESSAGES);
  release_queue_sem(total);
  sleep(1);
  assert(serial_write_calls == NBR_OF_MESSAGES);

  serial_write_calls = 0;
  command.hdr.recv.timeout = TYPE_SEND_AND_VALIDATE<<TYPE_BIT_POS;
  index = queue_index;
  total=queue_add(&conn, &command);
  assert(queue_index == index+NBR_OF_MESSAGES);
  release_queue_sem(total);
  sleep(1);
  assert(serial_write_calls == NBR_OF_MESSAGES);
  assert(serial_read_calls == NBR_OF_MESSAGES);


}
