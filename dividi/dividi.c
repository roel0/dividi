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
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <time.h>
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
  #include <termios.h>
  #include <arpa/inet.h>
  #include <sys/socket.h>
  #include <pthread.h>
  #include <semaphore.h>
  #include <linux/limits.h>
#endif
#include <netinet/in.h>
#include <openssl/crypto.h>
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "serial.h"
#include "dividi.h"
#include <getopt.h>
#include <poll.h>

#define MAX_COMMAND                      100
#define QUEUE_SIZE                       1000
#define TYPE_JUST_SEND                   0
#define TYPE_SEND_AND_VALIDATE           1
#define TYPE_SEND_AND_RECEIVE            2
#define MAX_ACTIVE_CONNECTIONS           50
#define MAX_SEM_COUNT                    QUEUE_SIZE
#define MAX_LINKS                        100
#define MAX_LINE                         100
#ifdef __linux__
#define DEFAULT_CONFIG_FILE              "/etc/dividi.conf"
#elif __WIN32
#define DEAULT_CONFIG_FILE               "~/dividi.conf"
#endif



// Look up table for all links
struct s_link {
  int tcp_port;
  HANDLE serial_port;
};
static struct s_link links[MAX_LINKS];

struct s_connection {
  SSL *socket;
  struct s_link *link;
};
// queue entry
struct s_entry {
  struct s_connection *conn;
  uint16_t timeout;
  uint16_t type;
  char *command;
};

static void allocate_queue();
static void deallocate_queue();
static void release_queue_sem(int inc);
static int receive_command(SSL *ns, struct s_message *command);
static int send_command(SSL *ns, struct s_message *answer);
static int queue_add(struct s_connection *conn, struct s_message *command);
static void close_socket(int s);

#ifdef __linux__
static void *queue_handler();
static void *connection_handler();
static pthread_mutex_t lock;
static sem_t queue_sem;
#elif _WIN32
static DWORD WINAPI queue_handler( LPVOID lpParam );
static DWORD WINAPI connection_handler( LPVOID lpParam );
static HANDLE lock;
static HANDLE queue_sem;
#endif

static struct s_entry **queue;
static volatile int queue_index = 0;
static volatile int queue_start = 0;


static char config_file[PATH_MAX];
static volatile int dividi_running = 0;
static volatile int queue_running = 0;

////////////////////////////////////PRIVATE////////////////////////////////////////////////
/**
 * Deallocate everything
 * Should only be used when registered with atexit()
 */
static void destroy_everything()
{
  /* exit queue_handler thread */
  if(queue_running) {
    queue_running = 0;
    /*while(queue_running!=-1);*/
  }
  /* exit all connections */
  if(dividi_running) {
    dividi_running = 0;
    while(dividi_running !=-1);
  }
  deallocate_queue();
}

/**
 * Block untill the semaphore is incremented by one
 */
static void get_queue_sem()
{
#ifdef __linux__
  sem_wait(&queue_sem);
#elif _WIN32
  WaitForSingleObject(queue_sem,INFINITE);
#endif
}

/**
 * Increment the semaphore
 *
 * @inc the amount of the times the sem needs
 *      to be incremented
 */
static void release_queue_sem(int inc)
{
#ifdef __linux__
  int i;
  for(i = 0; i < inc; i++) {
    if(sem_post(&queue_sem)<0) {
      perror("sem_post failed");
      exit(-1);
    }
  }
#elif _WIN32
  if(inc && !ReleaseSemaphore(queue_sem, inc, NULL)) {
    fprintf(stderr, "%s", GetLastError());
    exit(-1);
  }
#endif
}

/**
 * Initialise the queue semaphore
 */
static void init_queue_sem()
{
#ifdef __linux__
  if(sem_init(&queue_sem, 1, 0) < 0) {
    perror("sem_init failed");
    exit(-1);
  }
#elif _WIN32
  queue_sem = CreateSemaphore(NULL, 0, MAX_SEM_COUNT, NULL);
  if(queue_sem == NULL) {
    fprintf(stderr, "%s", GetLastError());
    exit(-1);
  }
#endif
}

/**
 * Lock the queue
 */
static void lock_queue()
{
#ifdef __linux__
  if(pthread_mutex_lock(&lock) < 0) {
    perror("pthread_mutex_lock failed");
    exit(-1);
  }
#elif _WIN32
  if(WaitForSingleObject(lock, INFINITE) < 0) {
    fprintf(stderr, "%s", GetLastError());
    exit(-1);
  }
#endif
}

/**
 * Unlock the queue mutex
 */
static void unlock_queue()
{
#ifdef __linux__
  if(pthread_mutex_unlock(&lock) < 0) {
    perror("pthread_mutex_lock failed");
    exit(-1);
  }
#elif _WIN32
  if(!ReleaseMutex(lock)) {
    fprintf(stderr, "%s", GetLastError());
    exit(-1);
  }
#endif
}

/**
 * Initialise the queue mutex
 */
static void create_queue_lock()
{
#ifdef __linux__
  if(pthread_mutex_init(&lock, NULL) != 0) {
    perror("phthread_mutex_init failed");
    exit(-1);
  }
#elif _WIN32
  lock = CreateMutex(NULL, FALSE, NULL);
  if(lock == NULL) {
    fprintf(stderr, "%s", GetLastError());
    exit(-1);
  }
#endif
}

/**
 * Start the queue handler thread
 */
static void start_connection_thread(struct s_connection *s_con)
{
#ifdef __linux__
  pthread_t dwThreadId;
  pthread_create( &dwThreadId, NULL, connection_handler, s_con);
#elif _WIN32
  uint32_t dwThreadId;
  CreateThread(NULL, 0, connection_handler, s_con, 0, &dwThreadId);
#endif
}

/**
 * This function will start the queue
 * handler thread
 */
static void start_queue_handler()
{
#ifdef __linux__
  pthread_t dwThreadId;
  pthread_create( &dwThreadId, NULL, queue_handler, NULL);
#elif _WIN32
  uint32_t dwThreadId;
  CreateThread(NULL, 0, queue_handler, NULL, 0, &dwThreadId);
#endif
}

/**
 * The queue handler thread
 * The messages are threated according
 * the first in- first out principle
 */
#ifdef __linux__
static void *queue_handler()
#elif _WIN32
static DWORD WINAPI queue_handler()
#endif
{
  struct s_entry *entry;
  struct s_link *link;
  int index = 0;
  int bytes_read = 0;
  HANDLE serial_port;
  struct s_message answer;

  queue_running = 1;
  while(queue_running) {
    get_queue_sem();
    if(queue_index != queue_start) {
      index = queue_start;
      lock_queue();

      entry = queue[index];
      //Check if conn is still active
      if(entry->conn != NULL) {
        link = entry->conn->link;
        serial_port = link->serial_port;

        if(serial_write(serial_port, entry->command) < 0) {
          exit(-1);
        }
        switch(entry->type) {
          case TYPE_SEND_AND_VALIDATE:
          case TYPE_SEND_AND_RECEIVE:
            answer.command = serial_read(serial_port, &bytes_read);
            if(!bytes_read) {
              answer.hdr.resp.errorcode = (entry->type == TYPE_SEND_AND_VALIDATE) ? VAL_NO_SERIAL_RESPONSE : ERR_SERIAL_TIMEOUT;
            } else {
              answer.hdr.resp.errorcode = (entry->type == TYPE_SEND_AND_VALIDATE) ? VAL_SERIAL_RESPONSE : ERR_NO_ERR;
            }
            answer.hdr.resp.length = bytes_read;
            send_command(entry->conn->socket, &answer);
            break;
          case TYPE_JUST_SEND:
          default:
            break;
        }
        dbg("Removed %s from queue\n", entry->command);
      }

      queue_start = (queue_start+1) % QUEUE_SIZE;
      unlock_queue();
    }
  }
  queue_running = -1;
#ifdef __linux__
  return NULL;
#elif _WIN32
  return 0;
#endif
}

/**
 * The queue handler thread
 * The messages are threated according
 * the first in- first out principle
 */
#ifdef __linux__
static void *connection_handler(void *s_conn)
#elif _WIN32
static DWORD WINAPI connection_handler( LPVOID s_conn )
#endif
{
  struct s_connection conn;
  int nbr_of_commands;
  struct s_message command;
  int i;

  memcpy(&conn, s_conn, sizeof(struct s_connection));
  // Thread has ownership!
  free(s_conn);
  while(1) {
    if(receive_command(conn.socket, &command) >= 0) {
      nbr_of_commands = queue_add(&conn, &command);
      release_queue_sem(nbr_of_commands);
    }
    else {
      /* XXX
       * Connection has ended, MUST remove all
       * references in queue
       * */
      for(i=0; i<QUEUE_SIZE; i++) {
        if(queue[i]->conn == &conn) {
          queue[i]->conn = NULL;
          free(queue[i]->command);
          queue[i]->command = NULL;
        }
      }
      break;
    }
  }
  SSL_free(conn.socket);
#ifdef __linux__
  return NULL;
#elif _WIN32
  return 0;
#endif
}

/**
 * This function will split up multiple commands
 * and add them seperate to the queue
 * this is to prevent the serial_server from receiving
 * to much commands in a row
 * @command the string containing one or more commands
 * @return the number of extracted commands
 */
static int queue_add(struct s_connection *conn, struct s_message *command)
{
  char *index = command->command;
  char *start = command->command;
  struct s_entry *entry;
  int nbr_commands = 0;
  int length = 1;
  dbg("adding %s\n", command->command);
  while(*index != '\0') {
    length++;
    index++;
    if(*index == '\n') {
      entry = (struct s_entry *) malloc(sizeof(struct s_entry));
      entry->command = (char *) malloc(length+1);
      memcpy(entry->command, start, length);
      dbg("added %s\n", entry->command);
      entry->conn = conn;
      entry->timeout = command->hdr.recv.timeout & TIMEOUT_BIT_MASK;
      entry->type = command->hdr.recv.timeout>>TYPE_BIT_POS;
      queue[queue_index] = entry;
      queue_index = (queue_index+1) % QUEUE_SIZE;
      index++;
      start = index;
      length = 1;
      entry = NULL;
      nbr_commands++;
    }
  }
  dbg("added %d\n", nbr_commands);
  return nbr_commands;
}

/**
 * Receive a message over a given socket
 */
static int receive_command(SSL *ns, struct s_message *command)
{
  int bytes = SSL_read(ns, &command->hdr, sizeof(((struct s_message*)0)->hdr));
  if(bytes < 0) {
    perror("recv failed");
    return -1;
  } else if(!bytes) {
    //conenction closed
    return -2;
  }
  command->hdr.recv.timeout = ntohs(command->hdr.recv.timeout);
  command->hdr.recv.length = ntohs(command->hdr.recv.length);

  command->command = (char *) malloc(command->hdr.recv.length+1);
  bytes = SSL_read(ns, command->command, command->hdr.recv.length);
  command->command[command->hdr.recv.length] = '\0';
  if(bytes < 0) {
    perror("recv failed");
    return -1;
  } else if(!bytes) {
    //conenction closed
    return -2;
  }
  return command->hdr.recv.length;
}

/**
 * send a message over a given socket
 */
static int send_command(SSL *ns, struct s_message *answer)
{
  if(SSL_write(ns, answer, sizeof(struct s_message)+answer->hdr.resp.length)) {
    perror("send failed");
    return -1;
  }
  return 0;
}

/**
 * This function will close a given socketd
 * @param s the socket identifier
 */
static void close_socket(int s)
{
  int status = 0;
#ifdef __linux__
  status = shutdown(s, SHUT_RDWR);
  if (status == 0) {
    status = close(s);
  }
#elif _WIN32
  status = shutdown(s, SD_BOTH);
  if (status == 0) {
    status = closesocket(s);
  }
  WSACleanup();
#endif
}

/**
 * This function will deallocate the queue
 */
static void deallocate_queue()
{
  int i;
  if(queue) {
    for(i = 0; i<QUEUE_SIZE; i++) {
      if(queue[i] != NULL) {
        free(queue[i]);
      }
    }
    free(queue);
  }
}

/**
 * Allocates a message queue
 */
static void allocate_queue()
{
  int i,j;
  queue = (struct s_entry **) malloc(QUEUE_SIZE*sizeof(struct s_entry *));
  if(!queue) {
    perror("malloc failed");
    exit(-1);
  }
  for(i = 0; i<QUEUE_SIZE; i++) {
    queue[i] = (struct s_entry *) malloc(sizeof(struct s_entry));
    if(!queue[i]) {
      for(j = 0; j<i; j++) {
        free(queue[j]);
      }
      free(queue);
      perror("malloc failed");
      exit(-1);
    }
  }
}

/**
 * Print usage
 */
static void print_help()
{
  printf("Usage: dividi [OPTIONS]\n\n");
  printf("   -c [FILE]       Set a custom path to the configuration file\n");
}

/**
 * This function will determine the port number
 * and the serial port by the applications
 * arguments
 */
static void handle_arguments(int argc, char **argv)
{
  int c;
  while((c = getopt(argc, argv, "c:h")) > 0) {
    switch(c) {
      case 'c':
        snprintf(config_file, PATH_MAX, optarg);
        break;
      case 'h':
      default:
        print_help();
        exit(-1);
    }
  }
}

/**
 * Open a serial port by a given name
 * and put it in the lookup table
 *
 * @link pointer to the first free entry of the table
 * @port_name the system name of the serial port
 */
static void open_link(struct s_link *link, char *port_name)
{
  int fd = serial_open(port_name);
  if(fd < 0) {
    exit(-1);
  }
  dbg("Opened %s\n", port_name);
  link->serial_port = fd;
}

/**
 * Read the configuration file
 * and create the s_link lookup table
 *
 * @config_file path to the configuration file
 */
static void read_config(char *config_file)
{
  FILE* fd;
  int index = 0;
  char line[MAX_LINE];
  char com_port[MAX_LINE];

  dbg("Reading configuration file %s\n", config_file);
  fd = fopen(config_file,"rt");
  if (fd) {
    while(fgets(line, MAX_LINE, fd) != NULL && index < MAX_LINKS) {
      if (strncmp(line, "#", 1) != 0) {
        sscanf (line, "%s %d", com_port, &links[index].tcp_port);
        open_link(&links[index], com_port);
        index++;
      }
    }
  }
  else {
    perror("fopen failed");
    exit(-1);
  }
  if(!index) {
    fprintf(stderr, "No configured links in the configuration file");
    fprintf(stderr, " (%s)", config_file);
    exit(-1);
  }
}

/**
 * Will check the sockets for new connection
 * requests
 */
static int poll_sockets(struct pollfd *s, int total_links, SSL_CTX *ctx)
{
  int index;
  int ns;
  struct s_connection *conn;
  BIO     *sbio;
  SSL     *ssl;

  dbg("Polling for incoming connections\n");
  poll(s, total_links, -1);
  for(index=0; index<total_links; index++) {
    if(s[index].revents & POLLIN) {
      if ((ns = accept(s[index].fd,NULL,NULL)) < 0) {
#ifdef __linux__
        perror("accept failed");
#elif _WIN32
        fprintf(stderr, ("accept failed with error: %d\n", WSAGetLastError());
#endif
        continue;
      }
      sbio = BIO_new_socket(ns, BIO_NOCLOSE);
      ssl = SSL_new(ctx);
      SSL_set_bio(ssl, sbio, sbio);
      if (SSL_accept(ssl) == -1) {
        fprintf(stderr, "SSL setup failed\n");
        continue;
      }

      conn = (struct s_connection *) malloc(sizeof(struct s_connection));
      conn->socket = ssl;
      conn->link = &links[index];
      // We pass ownership of the pointer to the thread!
      start_connection_thread(conn);
    }
  }
  return 0;
}

/**
 * Initialises the queue
 */
static void init_queue()
{
  allocate_queue();
  create_queue_lock();
  init_queue_sem();
  start_queue_handler();
}

/**
 * Print out SSL error
 */
void ssl_fatal(char *s)
{
  ERR_print_errors_fp(stderr);
  fprintf(stderr, "%.30s", s);
}

/**
 * Loads all the needed certificates for ssl
 */
static void ssl_load_certificates(SSL_CTX *ctx, char *root, char *cert, char *key)
{
  // Client verification
  if (!SSL_CTX_load_verify_locations(ctx, root, NULL))
    ssl_fatal("verify");
  SSL_CTX_set_client_CA_list(ctx, SSL_load_client_CA_file(root));

  // Server certification
  if (!SSL_CTX_use_certificate_file(ctx, cert, SSL_FILETYPE_PEM))
    ssl_fatal("cert");
  if (!SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM))
    ssl_fatal("key");
  if (!SSL_CTX_check_private_key(ctx))
    ssl_fatal("cert/key");
}
////////////////////////////////////PUBLIC////////////////////////////////////////////////
/**
 * Application entry-point
 */
#ifdef TEST
int entry_point(int argc, char *argv[])
#else
int main(int argc, char **argv)
#endif
{
  SSL_CTX *ctx;
  struct pollfd s[MAX_LINKS];
  int index;
  struct sockaddr_in sain;
  int optval = 1;

#ifdef _WIN32
  WSADATA wsa_data;
  WSAStartup(MAKEWORD(1,1), &wsa_data);
#endif

  SSL_load_error_strings();
  ERR_load_BIO_strings();
  OpenSSL_add_all_algorithms();
  ctx = SSL_CTX_new(SSLv2_server_method());
  if (ctx == NULL) {
    fprintf(stderr, "Can't create ssl context\n");
    exit(-1);
  }
  ssl_load_certificates(ctx, "test.crt", "test.pem", "test.pem");
  SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);

  atexit(destroy_everything);
  sprintf(config_file, DEFAULT_CONFIG_FILE);
  memset(links, 0, MAX_LINKS*sizeof(struct s_link));

  handle_arguments(argc, argv);
  read_config(config_file);
  init_queue();

  for(index=0; index<MAX_LINKS; index++) {
    if(links[index].tcp_port == 0) {
      break;
    }
    if ((s[index].fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
      perror("socket failed");
      exit(-1);
    }

    memset( (char *) (&sain),0, sizeof(sain));
    sain.sin_family = AF_INET;
    sain.sin_port = htons(links[index].tcp_port);
    sain.sin_addr.s_addr = INADDR_ANY;

    if (bind(s[index].fd, (struct sockaddr *)&sain, sizeof(sain)) < 0) {
#ifdef __linux__
      perror("bind failed");
#elif _WIN32
      fprintf(stderr, "bind failed with error: %d\n", WSAGetLastError());
#endif
      exit(-1);
    }
    if (listen(s[index].fd, MAX_ACTIVE_CONNECTIONS) < 0) {
#ifdef __linux__
      perror("listen failed");
#elif _WIN32
      fprintf(stderr, "listen failed with error: %d\n", WSAGetLastError());
#endif
      exit(-1);
    }
    s[index].events = POLLIN;
    setsockopt(s[index].fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&optval, sizeof(optval));
  }
  dividi_running = 1;
  while(dividi_running) {
    if(poll_sockets(s, index, ctx) < 0) {
      break;
    }
  }
  for(; index>=0; index--) {
    close_socket(s[index].fd);
  }
  dividi_running = -1;

  exit(0);
  return 0;
}
