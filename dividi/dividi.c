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

#define MAX_message                      100
#define QUEUE_SIZE                       1000
#define MAX_ACTIVE_CONNECTIONS           50
#define MAX_SEM_COUNT                    QUEUE_SIZE
#define MAX_LINKS                        100
#define MAX_LINE                         100

#ifdef __linux__
#define DEFAULT_CONFIG_FILE              "/etc/dividi.conf"
#elif __WIN32
#define DEAULT_CONFIG_FILE               "~/dividi.conf"
#endif


#define TCP_DATA_CHUNK_SIZE 512
#define TCP_DATA_MAX        50*TCP_DATA_CHUNK_SIZE
#define DIVIDI_IN_QUEUE  0
#define DIVIDI_OUT_QUEUE 1

// Look up table for all links
struct s_link {
  int tcp_port;
  HANDLE serial_port;
};
static struct s_link links[MAX_LINKS];

struct s_conn {
  SSL *socket;
  struct s_link *link;
};
// queue entry
struct s_entry {
  struct s_conn *conn;
  char *message;
};

static void allocate_queues();
static void deallocate_queues();
static void release_queue_sem(int queue, int inc);
static char *receive_message(SSL *ns, int *bytes_read);
static int send_message(SSL *ns, char *message);
static int out_queue_add(struct s_conn *conn, char *message);
static void close_socket(int s);

#ifdef __linux__
static void *in_queue_handler();
static void *out_queue_handler();
static void *in_tcp_handler();
static void *out_tcp_handler();
static pthread_mutex_t in_lock;
static pthread_mutex_t serial_lock;
static pthread_mutex_t out_lock;
static sem_t in_queue_sem;
static sem_t out_queue_sem;
#elif _WIN32
static DWORD WINAPI in_queue_handler( LPVOID lpParam );
static DWORD WINAPI out_queue_handler( LPVOID lpParam );
static DWORD WINAPI in_tcp_handler( LPVOID lpParam );
static DWORD WINAPI out_tcp_handler( LPVOID lpParam );
static HANDLE in_lock;
static HANDLE serial_lock;
static HANDLE out_lock;
static HANDLE in_queue_sem;
static HANDLE out_queue_sem;
#endif

static struct s_entry **in_queue;
static struct s_entry **out_queue;
static volatile int in_queue_index = 0;
static volatile int in_queue_start = 0;
static volatile int out_queue_index = 0;
static volatile int out_queue_start = 0;


static char config_file[PATH_MAX];
static char cert_file[PATH_MAX];
static char key_file[PATH_MAX];
static char root_file[PATH_MAX];

static volatile int in_tcp_running = 0;
static volatile int out_tcp_running = 0;
static volatile int in_queue_running = 0;
static volatile int out_queue_running = 0;
static volatile int dividi_running = 0;
static int total_links = 0;

////////////////////////////////////PRIVATE////////////////////////////////////////////////
/**
 * Deallocate everything
 * Should only be used when registered with atexit()
 */
static void destroy_everything()
{
  /* exit queue_handler thread */
  in_queue_running = 0;
  out_queue_running = 0;

  /* exit all connections */
  out_tcp_running = 0;
  if(in_tcp_running) {
    in_tcp_running = 0;
    while(in_tcp_running !=-1);
  }
  deallocate_queues();
}

/**
 * Block untill the semaphore is incremented by one
 */
static void get_queue_sem(int queue)
{
  if(queue == DIVIDI_IN_QUEUE) {
#ifdef __linux__
    sem_wait(&in_queue_sem);
#elif _WIN32
    WaitForSingleObject(in_queue_sem, INFINITE);
#endif
  } else if(queue == DIVIDI_OUT_QUEUE) {
#ifdef __linux__
    sem_wait(&out_queue_sem);
#elif _WIN32
    WaitForSingleObject(out_queue_sem, INFINITE);
#endif
  }
}

/**
 * Increment the semaphore
 *
 * @queue which semaphore
 * @inc the amount of the times the sem needs
 *      to be incremented
 */
static void release_queue_sem(int queue, int inc)
{
  if(queue == DIVIDI_IN_QUEUE) {
#ifdef __linux__
    int i;
    for(i = 0; i < inc; i++) {
      if(sem_post(&in_queue_sem)<0) {
        perror("sem_post failed");
        exit(-1);
      }
    }
#elif _WIN32
    if(inc && !ReleaseSemaphore(in_queue_sem, inc, NULL)) {
      fprintf(stderr, "%s", GetLastError());
      exit(-1);
    }
#endif
  } else if(queue == DIVIDI_OUT_QUEUE) {
#ifdef __linux__
    int i;
    for(i = 0; i < inc; i++) {
      if(sem_post(&out_queue_sem)<0) {
        perror("sem_post failed");
        exit(-1);
      }
    }
#elif _WIN32
    if(inc && !ReleaseSemaphore(out_queue_sem, inc, NULL)) {
      fprintf(stderr, "%s", GetLastError());
      exit(-1);
    }
#endif
  }
}

/**
 * Initialise the queue semaphore
 */
static void init_queues_sem()
{
#ifdef __linux__
  if(sem_init(&in_queue_sem, 1, 0) < 0) {
    perror("sem_init failed");
    exit(-1);
  }
  if(sem_init(&out_queue_sem, 1, 0) < 0) {
    perror("sem_init failed");
    exit(-1);
  }
#elif _WIN32
  in_queue_sem = CreateSemaphore(NULL, 0, MAX_SEM_COUNT, NULL);
  if(in_queue_sem == NULL) {
    fprintf(stderr, "%s", GetLastError());
    exit(-1);
  }
  out_queue_sem = CreateSemaphore(NULL, 0, MAX_SEM_COUNT, NULL);
  if(out_queue_sem == NULL) {
    fprintf(stderr, "%s", GetLastError());
    exit(-1);
  }
#endif
}

/**
 * Lock the queue
 */
static void lock_queue(int queue)
{
  if(queue == DIVIDI_IN_QUEUE) {
#ifdef __linux__
    if(pthread_mutex_lock(&in_lock) < 0) {
      perror("pthread_mutex_lock failed");
      exit(-1);
    }
#elif _WIN32
    if(WaitForSingleObject(in_lock, INFINITE) < 0) {
      fprintf(stderr, "%s", GetLastError());
      exit(-1);
    }
#endif
  } else if(queue == DIVIDI_OUT_QUEUE) {
#ifdef __linux__
    if(pthread_mutex_lock(&out_lock) < 0) {
      perror("pthread_mutex_lock failed");
      exit(-1);
    }
#elif _WIN32
    if(WaitForSingleObject(out_lock, INFINITE) < 0) {
      fprintf(stderr, "%s", GetLastError());
      exit(-1);
    }
#endif
  }
}

/**
 * Unlock the queue mutex
 */
static void unlock_queue(int queue)
{
  if(queue == DIVIDI_IN_QUEUE) {
#ifdef __linux__
    if(pthread_mutex_unlock(&in_lock) < 0) {
      perror("pthread_mutex_lock failed");
      exit(-1);
    }
#elif _WIN32
    if(!ReleaseMutex(in_lock)) {
      fprintf(stderr, "%s", GetLastError());
      exit(-1);
    }
#endif
  } else if(queue == DIVIDI_OUT_QUEUE) {
#ifdef __linux__
    if(pthread_mutex_unlock(&out_lock) < 0) {
      perror("pthread_mutex_lock failed");
      exit(-1);
    }
#elif _WIN32
    if(!ReleaseMutex(out_lock)) {
      fprintf(stderr, "%s", GetLastError());
      exit(-1);
    }
#endif
  }
}

/**
 * Initialise the queue mutex
 */
static void create_queues_lock()
{
#ifdef __linux__
  if(pthread_mutex_init(&in_lock, NULL) != 0) {
    perror("phthread_mutex_init failed");
    exit(-1);
  }
  if(pthread_mutex_init(&out_lock, NULL) != 0) {
    perror("phthread_mutex_init failed");
    exit(-1);
  }
#elif _WIN32
  in_lock = CreateMutex(NULL, FALSE, NULL);
  if(in_lock == NULL) {
    fprintf(stderr, "%s", GetLastError());
    exit(-1);
  }
  out_lock = CreateMutex(NULL, FALSE, NULL);
  if(out_lock == NULL) {
    fprintf(stderr, "%s", GetLastError());
    exit(-1);
  }
#endif
}
/**
 * Lock the serial ports
 */
static void lock_serial()
{
#ifdef __linux__
  if(pthread_mutex_lock(&serial_lock) < 0) {
    perror("pthread_mutex_lock failed");
    exit(-1);
  }
#elif _WIN32
  if(WaitForSingleObject(serial_lock, INFINITE) < 0) {
    fprintf(stderr, "%s", GetLastError());
    exit(-1);
  }
#endif
}

/**
 * Unlock the serial_ports
 */
static void unlock_serial()
{
#ifdef __linux__
  if(pthread_mutex_unlock(&serial_lock) < 0) {
    perror("pthread_mutex_lock failed");
    exit(-1);
  }
#elif _WIN32
  if(!ReleaseMutex(serial_lock)) {
    fprintf(stderr, "%s", GetLastError());
    exit(-1);
  }
#endif
}

/**
 * Initialise the serial mutex
 */
static void create_serial_lock()
{
#ifdef __linux__
  if(pthread_mutex_init(&serial_lock, NULL) != 0) {
    perror("phthread_mutex_init failed");
    exit(-1);
  }
#elif _WIN32
  serial_lock = CreateMutex(NULL, FALSE, NULL);
  if(serial_lock == NULL) {
    fprintf(stderr, "%s", GetLastError());
    exit(-1);
  }
#endif
}


/**
 * Start the connection handlers thread
 */
static void start_connection_handlers(struct s_conn *s_con)
{
#ifdef __linux__
  pthread_t in_tcp;
  pthread_t out_tcp;
  pthread_create( &in_tcp, NULL, in_tcp_handler, s_con);
  pthread_create( &out_tcp, NULL, out_tcp_handler, s_con);
#elif _WIN32
  uint32_t in_tcp;
  uint32_t out_tcp;
  CreateThread(NULL, 0, in_tcp_handler, s_con, 0, &in_tcp);
  CreateThread(NULL, 0, out_tcp_handler, s_con, 0, &in_tcp);
#endif
}

/**
 * This function will start the queue
 * handler thread
 */
static void start_queues_handlers()
{
#ifdef __linux__
  pthread_t in;
  pthread_t out;
  pthread_create( &in, NULL, in_queue_handler, NULL);
  pthread_create( &out, NULL, out_queue_handler, NULL);
#elif _WIN32
  uint32_t in;
  uint32_t out;
  CreateThread(NULL, 0, in_queue_handler, NULL, 0, &in);
  CreateThread(NULL, 0, out_queue_handler, NULL, 0, &out);
#endif
}

/**
 * The out_queue handler thread
 * The messages are threated according
 * the first in- first out principle
 *
 * out = tcp -> serial device
 */
#ifdef __linux__
static void *out_queue_handler()
#elif _WIN32
static DWORD WINAPI out_queue_handler()
#endif
{
  struct s_entry *entry;
  struct s_link *link;
  int index = 0;
  HANDLE serial_port;

  out_queue_running = 1;
  while(out_queue_running) {
    get_queue_sem(DIVIDI_OUT_QUEUE);
    if(out_queue_index != out_queue_start) {
      index = out_queue_start;
      lock_queue(DIVIDI_OUT_QUEUE);
      entry = out_queue[index];
      //Check if conn is still active
      if(entry->conn != NULL) {
        link = entry->conn->link;
        serial_port = link->serial_port;

        lock_serial();
        if(serial_write(serial_port, entry->message) < 0) {
          exit(-1);
        }
        unlock_serial();

        dbg("Removed %s from queue\n", entry->message);
      }

      out_queue_start = (out_queue_start+1) % QUEUE_SIZE;
      unlock_queue(DIVIDI_OUT_QUEUE);
    }
  }
  out_queue_running = -1;
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
 * in_tcp -> out_queue -> serial_out(out_queue_handler)
 */
#ifdef __linux__
static void *in_tcp_handler(void *_conn)
#elif _WIN32
static DWORD WINAPI in_tcp_handler( LPVOID _conn )
#endif
{
  struct s_conn conn;
  int nbr_of_messages;
  char *message;
  int bytes_read;

  memcpy(&conn, _conn, sizeof(struct s_conn));
  in_tcp_running = 1;
  while(in_tcp_running) {
    message = receive_message(conn.socket, &bytes_read);
    if(bytes_read) {
      nbr_of_messages = out_queue_add(&conn, message);
      release_queue_sem(DIVIDI_OUT_QUEUE, nbr_of_messages);
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
 * The in_queue handler thread
 * The messages are threated according
 * the first in- first out principle
 *
 * in = serial device -> tcp
 */
#ifdef __linux__
static void *in_queue_handler()
#elif _WIN32
static DWORD WINAPI in_queue_handler()
#endif
{
  int index = 0;
  char *message;
  struct s_entry *entry;
  int bytes_read;

  in_queue_running = 1;
  while(in_queue_running) {
    for(index=0; index<MAX_LINKS; index++) {
      if(links[index].tcp_port == 0) {
        break;
      }
      message = serial_read(links[index].serial_port, &bytes_read);
      if(bytes_read) {
        entry = (struct s_entry*) malloc(sizeof(struct s_entry));
        entry->message = message;
        entry->conn = (struct s_conn *) malloc(sizeof(struct s_conn));
        entry->conn->link = (struct s_link *) malloc(sizeof(struct s_link));
        entry->conn->link->serial_port = links[index].serial_port;
        entry->conn->link->tcp_port = links[index].tcp_port;
        lock_queue(DIVIDI_IN_QUEUE);
        in_queue[in_queue_index] = entry;
        in_queue_index = (in_queue_index+1) % QUEUE_SIZE;
        unlock_queue(DIVIDI_IN_QUEUE);
        release_queue_sem(DIVIDI_IN_QUEUE, total_links);
      }
    }
  }
  in_queue_running = -1;
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
 * serial_in(in_queue_handler) -> in_queue -> tcp_out
 * out_tcp -> out_queue -> serial_out(out_queue_handler)
 */
#ifdef __linux__
static void *out_tcp_handler(void *_conn)
#elif _WIN32
static DWORD WINAPI out_tcp_handler( LPVOID _conn )
#endif
{
  struct s_conn conn;
  struct s_link *queue_link;
  struct s_link *link;

  memcpy(&conn, _conn, sizeof(struct s_conn));
  link = conn.link;
  out_tcp_running = 1;
  while(out_tcp_running) {
    get_queue_sem(DIVIDI_IN_QUEUE);
    queue_link = in_queue[in_queue_start]->conn->link;
    if(queue_link->tcp_port == link->tcp_port)
    {
      send_message(conn.socket, in_queue[in_queue_start]->message);
      in_queue_start = (in_queue_start + 1) % QUEUE_SIZE;
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
 * This function will split up multiple messages
 * and add them seperate to the queue
 * this is to prevent the serial_server from receiving
 * to much messages in a row
 * @message the string containing one or more messages
 * @return the number of extracted messages
 */
static int out_queue_add(struct s_conn *conn, char *message)
{
  char *index = message;
  char *start = message;
  struct s_entry *entry;
  int nbr_messages = 0;
  int length = 1;
  dbg("adding %s\n", message);
  while(*index != '\0') {
    length++;
    index++;
    if(*index == '\n') {
      entry = (struct s_entry *) malloc(sizeof(struct s_entry));
      entry->message = (char *) malloc(length+1);
      memcpy(entry->message, start, length);
      dbg("added %s\n", entry->message);
      entry->conn = conn;
      out_queue[out_queue_index] = entry;
      out_queue_index = (out_queue_index+1) % QUEUE_SIZE;
      index++;
      start = index;
      length = 1;
      entry = NULL;
      nbr_messages++;
    }
  }
  dbg("added %d\n", nbr_messages);
  return nbr_messages;
}

/**
 * Receive a message over a given socket
 */
static char *receive_message(SSL *ns, int *total_bytes_read)
{
  int bytes_read;
  int total_read = 0;
  char *data = (char *) malloc(TCP_DATA_CHUNK_SIZE);
  char *pos = data;
  while(1) {
    bytes_read = SSL_read(ns, pos, TCP_DATA_CHUNK_SIZE);
    if(!bytes_read || ((total_read+=bytes_read) >= TCP_DATA_MAX)) {
      break;
    }
    if(bytes_read != TCP_DATA_CHUNK_SIZE) {
      // TImeout occured
      break;
    }
    data = (char *) realloc(data, total_read+TCP_DATA_CHUNK_SIZE+1);
    pos = data + total_read;
  }
  data[total_read] = '\0';
  *total_bytes_read = total_read;
  return data;
}

/**
 * send a message over a given socket
 */
static int send_message(SSL *ns, char *message)
{
  if(!SSL_write(ns, message, strlen(message))) {
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
static void deallocate_queues()
{
  int i;
  if(in_queue) {
    for(i = 0; i<QUEUE_SIZE; i++) {
      if(in_queue[i] != NULL) {
        free(in_queue[i]);
      }
    }
    free(in_queue);
  }
  if(out_queue) {
    for(i = 0; i<QUEUE_SIZE; i++) {
      if(out_queue[i] != NULL) {
        free(out_queue[i]);
      }
    }
    free(out_queue);
  }
}

/**
 * Allocates a message queue
 */
static void allocate_queues()
{
  int i,j;
  in_queue = (struct s_entry **) malloc(QUEUE_SIZE*sizeof(struct s_entry *));
  out_queue = (struct s_entry **) malloc(QUEUE_SIZE*sizeof(struct s_entry *));
  if(!in_queue || !out_queue) {
    perror("malloc failed");
    exit(-1);
  }
  for(i = 0; i<QUEUE_SIZE; i++) {
    in_queue[i] = (struct s_entry *) malloc(sizeof(struct s_entry));
    out_queue[i] = (struct s_entry *) malloc(sizeof(struct s_entry));
    if(!in_queue[i] || !out_queue[i]) {
      for(j = 0; j<i; j++) {
        free(in_queue[j]);
        free(out_queue[j]);
      }
      free(in_queue);
      free(out_queue);
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
  printf("   -s [FILE]       Set a custom path to the configuration file\n");
  printf("   -r [FILE]       Set the path to the root certification\n");
  printf("   -k [FILE]       Set the path to the private key\n");
  printf("   -c [FILE]       Set the path to the private key\n");
}

/**
 * This function will determine the port number
 * and the serial port by the applications
 * arguments
 */
static void handle_arguments(int argc, char **argv)
{
  int c;
  int key = 0;
  while((c = getopt(argc, argv, "s:c:r:k:h")) > 0) {
    switch(c) {
      case 's':
        snprintf(config_file, PATH_MAX, optarg);
        break;
      case 'c':
        snprintf(cert_file, PATH_MAX, optarg);
        break;
      case 'k':
        key = 1;
        snprintf(key_file, PATH_MAX, optarg);
        break;
      case 'r':
        snprintf(root_file, PATH_MAX, optarg);
        break;
      case 'h':
      default:
        print_help();
        exit(-1);
    }
  }
  if(!key) {
    sprintf(key_file, cert_file);
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
  struct s_conn *conn;
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

      conn = (struct s_conn *) malloc(sizeof(struct s_conn));
      conn->socket = ssl;
      conn->link = &links[index];
      start_connection_handlers(conn);
    }
  }
  return 0;
}

/**
 * Initialises the queue
 */
static void init_queues()
{
  allocate_queues();
  create_queues_lock();
  create_serial_lock();
  init_queues_sem();
  start_queues_handlers();
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

  sprintf(config_file, DEFAULT_CONFIG_FILE);
  handle_arguments(argc, argv);

  SSL_library_init();
  SSL_load_error_strings();
  ERR_load_BIO_strings();
  OpenSSL_add_all_algorithms();
  ctx = SSL_CTX_new(SSLv23_server_method());
  if (ctx == NULL) {
    fprintf(stderr, "Can't create ssl context\n");
    exit(-1);
  }
  ssl_load_certificates(ctx, root_file, cert_file, key_file);
  SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);

  atexit(destroy_everything);
  memset(links, 0, MAX_LINKS*sizeof(struct s_link));

  read_config(config_file);
  init_queues();

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
  total_links = index;
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
