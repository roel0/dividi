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
#if defined _WIN32
  #include <winsock2.h>
  #include "mswsock.h"
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <tchar.h>
#elif __linux__
  #include <sys/stat.h>
  #include <unistd.h>
  #include <errno.h>
  #include <termios.h>
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <pthread.h>
  #include <poll.h>
  #include <semaphore.h>
  #include <linux/limits.h>
#endif
#include <openssl/crypto.h>
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "conf.h"
#include "dividi.h"
#include "serial.h"
#include "util.h"
#include <getopt.h>

#define MAX_message                      100
#define QUEUE_SIZE                       1000
#define MAX_ACTIVE_CONNECTIONS           50
#define MAX_SEM_COUNT                    QUEUE_SIZE
#define MAX_LINKS                        100

#ifdef __linux__
#define DEFAULT_CONFIG_FILE              "/etc/dividi.conf"
#elif defined _WIN32
#define DEFAULT_CONFIG_FILE               "~/dividi.conf"
#define poll WSAPoll
#endif


#define TCP_DATA_CHUNK_SIZE 512
#define TCP_DATA_MAX        50*TCP_DATA_CHUNK_SIZE
#define DIVIDI_SERIAL2TCP_QUEUE  0
#define DIVIDI_TCP2SERIAL_QUEUE 1

static struct s_link links[MAX_LINKS];

#ifdef _WIN32
struct pollfd {
  int fd;
};
#endif

struct s_conn {
  int tcp_socket;
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
static void tcp2serial_queue_add(struct s_conn *conn, char *message);
static void serial2tcp_queue_add(struct s_link *link, char *message);
static void close_socket(int s);

static int get_empty_link_slot();
#ifdef __linux__
static void *serial_in_handler();
static void *serial_out_handler();
static void *tcp_in_handler();
static void *tcp_out_handler();
static pthread_mutex_t serial2tcp_queue_lock;
static pthread_mutex_t serial_lock;
static pthread_mutex_t tcp2serial_queue_lock;
static sem_t serial2tcp_queue_sem;
static sem_t tcp2serial_queue_sem;
static sem_t thread_started_sem;
#elif _WIN32
static DWORD WINAPI serial_in_handler();
static DWORD WINAPI serial_out_handler();
static DWORD WINAPI tcp_in_handler(LPVOID lpParam);
static DWORD WINAPI tcp_out_handler(LPVOID lpParam);
static HANDLE serial2tcp_queue_lock;
static HANDLE serial_lock;
static HANDLE tcp2serial_queue_lock;
static HANDLE serial2tcp_queue_sem;
static HANDLE tcp2serial_queue_sem;
static HANDLE thread_started_sem;
#endif

static struct s_entry **serial2tcp_queue;
static struct s_entry **tcp2serial_queue;
static volatile int serial2tcp_queue_index = 0;
static volatile int serial2tcp_queue_start = 0;
static volatile int tcp2serial_queue_index = 0;
static volatile int tcp2serial_queue_start = 0;


static char config_file[PATH_MAX];
static char cert_file[PATH_MAX];
static char key_file[PATH_MAX];
static char root_file[PATH_MAX];

static volatile int in_tcp_running = 0;
static volatile int out_tcp_running = 0;
static volatile int serial2tcp_queue_running = 0;
static volatile int tcp2serial_queue_running = 0;
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
  serial2tcp_queue_running = 0;
  tcp2serial_queue_running = 0;

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
  if(queue == DIVIDI_SERIAL2TCP_QUEUE) {
#ifdef __linux__
    sem_wait(&serial2tcp_queue_sem);
#elif _WIN32
    WaitForSingleObject(serial2tcp_queue_sem, INFINITE);
#endif
  } else if(queue == DIVIDI_TCP2SERIAL_QUEUE) {
#ifdef __linux__
    sem_wait(&tcp2serial_queue_sem);
#elif _WIN32
    WaitForSingleObject(tcp2serial_queue_sem, INFINITE);
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
  if(queue == DIVIDI_SERIAL2TCP_QUEUE) {
#ifdef __linux__
    int i;
    for(i = 0; i < inc; i++) {
      if(sem_post(&serial2tcp_queue_sem)<0) {
        perror("sem_post failed");
        exit(-1);
      }
    }
#elif _WIN32
    if(inc && !ReleaseSemaphore(serial2tcp_queue_sem, inc, NULL)) {
      fprintf(stderr, "%d", WSAGetLastError());
      exit(-1);
    }
#endif
  } else if(queue == DIVIDI_TCP2SERIAL_QUEUE) {
#ifdef __linux__
    int i;
    for(i = 0; i < inc; i++) {
      if(sem_post(&tcp2serial_queue_sem)<0) {
        perror("sem_post failed");
        exit(-1);
      }
    }
#elif _WIN32
    if(inc && !ReleaseSemaphore(tcp2serial_queue_sem, inc, NULL)) {
      fprintf(stderr, "%d", WSAGetLastError());
      exit(-1);
    }
#endif
  }
}

/**
 * Block untill the thread started semaphore is
 * incremented by inc
 */
static void get_thread_started_sem(int inc)
{
  int i;
  for(i = 0; i < inc; i++) {
#ifdef __linux__
    sem_wait(&thread_started_sem);
#elif _WIN32
    WaitForSingleObject(thread_started_sem, INFINITE);
#endif
  }
}

/**
 * Increment the thread started semaphore by 1
 */
static void release_thread_started_sem()
{
#ifdef __linux__
  if(sem_post(&thread_started_sem)<0) {
    perror("sem_post failed");
    exit(-1);
  }
#elif _WIN32
  if(!ReleaseSemaphore(thread_started_sem, 1, NULL)) {
    fprintf(stderr, "%d", WSAGetLastError());
    exit(-1);
  }
#endif
}

/**
 * Initialise the semaphores
 */
static void init_sem()
{
#ifdef __linux__
  if(sem_init(&serial2tcp_queue_sem, 1, 0) < 0) {
    perror("sem_init failed");
    exit(-1);
  }
  if(sem_init(&tcp2serial_queue_sem, 1, 0) < 0) {
    perror("sem_init failed");
    exit(-1);
  }
  if(sem_init(&thread_started_sem, 1, 0) < 0) {
    perror("sem_init failed");
    exit(-1);
  }
#elif _WIN32
  serial2tcp_queue_sem = CreateSemaphore(NULL, 0, MAX_SEM_COUNT, NULL);
  if(serial2tcp_queue_sem == NULL) {
    fprintf(stderr, "%d", WSAGetLastError());
    exit(-1);
  }
  tcp2serial_queue_sem = CreateSemaphore(NULL, 0, MAX_SEM_COUNT, NULL);
  if(tcp2serial_queue_sem == NULL) {
    fprintf(stderr, "%d", WSAGetLastError());
    exit(-1);
  }
  thread_started_sem = CreateSemaphore(NULL, 0, MAX_SEM_COUNT, NULL);
  if(thread_started_sem == NULL) {
    fprintf(stderr, "%d", WSAGetLastError());
    exit(-1);
  }
#endif
}

/**
 * Lock the queue
 */
static void lock_queue(int queue)
{
  if(queue == DIVIDI_SERIAL2TCP_QUEUE) {
#ifdef __linux__
    if(pthread_mutex_lock(&serial2tcp_queue_lock) < 0) {
      perror("pthread_mutex_lock failed");
      exit(-1);
    }
#elif _WIN32
    if(WaitForSingleObject(serial2tcp_queue_lock, INFINITE) < 0) {
      fprintf(stderr, "%d", WSAGetLastError());
      exit(-1);
    }
#endif
  } else if(queue == DIVIDI_TCP2SERIAL_QUEUE) {
#ifdef __linux__
    if(pthread_mutex_lock(&tcp2serial_queue_lock) < 0) {
      perror("pthread_mutex_lock failed");
      exit(-1);
    }
#elif _WIN32
    if(WaitForSingleObject(tcp2serial_queue_lock, INFINITE) < 0) {
      fprintf(stderr, "%d", WSAGetLastError());
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
  if(queue == DIVIDI_SERIAL2TCP_QUEUE) {
#ifdef __linux__
    if(pthread_mutex_unlock(&serial2tcp_queue_lock) < 0) {
      perror("pthread_mutex_lock failed");
      exit(-1);
    }
#elif _WIN32
    if(!ReleaseMutex(serial2tcp_queue_lock)) {
      fprintf(stderr, "%d", WSAGetLastError());
      exit(-1);
    }
#endif
  } else if(queue == DIVIDI_TCP2SERIAL_QUEUE) {
#ifdef __linux__
    if(pthread_mutex_unlock(&tcp2serial_queue_lock) < 0) {
      perror("pthread_mutex_lock failed");
      exit(-1);
    }
#elif _WIN32
    if(!ReleaseMutex(tcp2serial_queue_lock)) {
      fprintf(stderr, "%d", WSAGetLastError());
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
  if(pthread_mutex_init(&serial2tcp_queue_lock, NULL) != 0) {
    perror("phthread_mutex_init failed");
    exit(-1);
  }
  if(pthread_mutex_init(&tcp2serial_queue_lock, NULL) != 0) {
    perror("phthread_mutex_init failed");
    exit(-1);
  }
#elif _WIN32
  serial2tcp_queue_lock = CreateMutex(NULL, FALSE, NULL);
  if(serial2tcp_queue_lock == NULL) {
    fprintf(stderr, "%d", WSAGetLastError());
    exit(-1);
  }
  tcp2serial_queue_lock = CreateMutex(NULL, FALSE, NULL);
  if(tcp2serial_queue_lock == NULL) {
    fprintf(stderr, "%d", WSAGetLastError());
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
    fprintf(stderr, "%d", WSAGetLastError());
    exit(-1);
  }
#endif
}


/**
 * Start the connection handlers thread
 */
static int start_connection_handlers(struct s_conn *conn)
{
  int nbr_of_references = 0;
#ifdef __linux__
  pthread_t in_tcp;
  pthread_t out_tcp;
  nbr_of_references++;
  pthread_create( &in_tcp, NULL, tcp_in_handler, conn);
  nbr_of_references++;
  pthread_create( &out_tcp, NULL, tcp_out_handler, conn);
#elif _WIN32
  nbr_of_references++;
  CreateThread(NULL, 0, tcp_in_handler, conn, 0, NULL);
  nbr_of_references++;
  CreateThread(NULL, 0, tcp_out_handler, conn, 0, NULL);
#endif
  return nbr_of_references;
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
  pthread_create( &in, NULL, serial_in_handler, NULL);
  pthread_create( &out, NULL, serial_out_handler, NULL);
#elif _WIN32
  CreateThread(NULL, 0, serial_in_handler, NULL, 0, NULL);
  CreateThread(NULL, 0, serial_out_handler, NULL, 0, NULL);
#endif
}

/**
 * The serial out handler thread
 * The messages are threated according
 * the first in- first out principle
 */
#ifdef __linux__
static void *serial_out_handler()
#elif _WIN32
static DWORD WINAPI serial_out_handler()
#endif
{
  struct s_entry *entry;
  struct s_link *link;
  int index = 0;
  HANDLE serial_port;

  tcp2serial_queue_running = 1;
  while(tcp2serial_queue_running) {
    get_queue_sem(DIVIDI_TCP2SERIAL_QUEUE);
    index = tcp2serial_queue_start;
    lock_queue(DIVIDI_TCP2SERIAL_QUEUE);
    entry = tcp2serial_queue[index];
    //Check if conn is still active
    if(entry->conn != NULL) {
      link = entry->conn->link;
      serial_port = link->serial.serial_port;

      dbg("serial_write %s", entry->message);
      if(serial_write(serial_port, entry->message) < 0) {
        exit(-1);
      }
    }
    // Don't free conn! It's still used by the conenction thread
    free(entry->message);
    free(entry);

    tcp2serial_queue_start = (tcp2serial_queue_start+1) % QUEUE_SIZE;
    unlock_queue(DIVIDI_TCP2SERIAL_QUEUE);
  }
  tcp2serial_queue_running = -1;
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
static void *tcp_in_handler(void *_conn)
#elif _WIN32
static DWORD WINAPI tcp_in_handler( LPVOID _conn )
#endif
{
  struct s_conn conn;
  char *message;
  int bytes_read;

  memcpy(&conn, _conn, sizeof(struct s_conn));
  release_thread_started_sem();
  in_tcp_running = 1;
  while(in_tcp_running) {
    message = receive_message(conn.socket, &bytes_read);
    if(bytes_read) {
      tcp2serial_queue_add(&conn, message);
      release_queue_sem(DIVIDI_TCP2SERIAL_QUEUE, 1);
    }
  }
  SSL_free(conn.socket);
  close(conn.tcp_socket);
#ifdef __linux__
  return NULL;
#elif _WIN32
  return 0;
#endif
}

/**
 * The serial2tcp_queue handler thread
 * The messages are threated according
 * the first in- first out principle
 *
 * in = serial device -> tcp
 */
#ifdef __linux__
static void *serial_in_handler()
#elif _WIN32
static DWORD WINAPI serial_in_handler()
#endif
{
  int index = 0;
  int index2 = 0;
  char *message;
  int bytes_read;

  serial2tcp_queue_running = 1;
  while(serial2tcp_queue_running) {
    for(index=0; index<total_links; index++) {
      message = serial_read(links[index].serial.serial_port, &bytes_read);
      if(bytes_read > 0) {
        serial2tcp_queue_add(&links[index], message);
        //are other ports listening to the same serial device?
        for(index2=0; index2<total_links; index2++) {
          if(index2 == index) {
            continue;
          }
          if(links[index].serial.serial_port == links[index2].serial.serial_port) {
            serial2tcp_queue_add(&links[index2], message);
          }
        }
        // Every socket has to distinguish for itself if the queue entry's
        // are for him (ugly, to be improved)
        release_queue_sem(DIVIDI_SERIAL2TCP_QUEUE, total_links);
      }
      free(message);
    }
  }
  serial2tcp_queue_running = -1;
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
 * serial_in(serial_in_handler) -> serial2tcp_queue -> tcp_out
 * out_tcp -> tcp2serial_queue -> serial_out(serial_out_handler)
 */
#ifdef __linux__
static void *tcp_out_handler(void *_conn)
#elif _WIN32
static DWORD WINAPI tcp_out_handler( LPVOID _conn )
#endif
{
  struct s_conn conn;
  struct s_link *queue_link;
  struct s_link *link;

  memcpy(&conn, _conn, sizeof(struct s_conn));
  release_thread_started_sem();
  link = conn.link;
  out_tcp_running = 1;
  while(out_tcp_running) {
    get_queue_sem(DIVIDI_SERIAL2TCP_QUEUE);
    queue_link = serial2tcp_queue[serial2tcp_queue_start]->conn->link;
    if(queue_link->tcp_port == link->tcp_port)
    {
      send_message(conn.socket, serial2tcp_queue[serial2tcp_queue_start]->message);
      lock_queue(DIVIDI_SERIAL2TCP_QUEUE);
      free(serial2tcp_queue[serial2tcp_queue_start]->message);
      // Can free conn, doesnt hold socket
      free(serial2tcp_queue[serial2tcp_queue_start]->conn);
      free(serial2tcp_queue[serial2tcp_queue_start]);
      serial2tcp_queue_start = (serial2tcp_queue_start + 1) % QUEUE_SIZE;
      unlock_queue(DIVIDI_SERIAL2TCP_QUEUE);
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
 * Add a receive message to
 * the queue
 */
static void tcp2serial_queue_add(struct s_conn *conn, char *message)
{
  struct s_entry *entry = (struct s_entry *) malloc(sizeof(struct s_entry));
  entry->message = message;
  entry->conn = conn;
  lock_queue(DIVIDI_TCP2SERIAL_QUEUE);
  tcp2serial_queue[tcp2serial_queue_index] = entry;
  tcp2serial_queue_index = (tcp2serial_queue_index+1) % QUEUE_SIZE;
  unlock_queue(DIVIDI_TCP2SERIAL_QUEUE);
  dbg("added %s", message);
}

/**
 * Add a serial message to
 * the queue
 */
static void serial2tcp_queue_add(struct s_link *link, char *message)
{
  struct s_entry *entry = (struct s_entry *) malloc(sizeof(struct s_entry));
  entry->conn = (struct s_conn *) malloc(sizeof(struct s_conn));
  entry->message = (char *) malloc(strlen(message));
  memcpy(entry->message, message, strlen(message));
  entry->conn->link = link;
  lock_queue(DIVIDI_SERIAL2TCP_QUEUE);
  serial2tcp_queue[serial2tcp_queue_index] = entry;
  serial2tcp_queue_index = (serial2tcp_queue_index+1) % QUEUE_SIZE;
  unlock_queue(DIVIDI_SERIAL2TCP_QUEUE);
  dbg("added %s", message);
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
    print_error("send failed");
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
  if(serial2tcp_queue) {
    for(i = 0; i<QUEUE_SIZE; i++) {
      if(serial2tcp_queue[i] != NULL) {
        free(serial2tcp_queue[i]);
      }
    }
    free(serial2tcp_queue);
  }
  if(tcp2serial_queue) {
    for(i = 0; i<QUEUE_SIZE; i++) {
      if(tcp2serial_queue[i] != NULL) {
        free(tcp2serial_queue[i]);
      }
    }
    free(tcp2serial_queue);
  }
}

/**
 * Allocates a message queue
 */
static void allocate_queues()
{
  int i,j;
  serial2tcp_queue = (struct s_entry **) malloc(QUEUE_SIZE*sizeof(struct s_entry *));
  tcp2serial_queue = (struct s_entry **) malloc(QUEUE_SIZE*sizeof(struct s_entry *));
  if(!serial2tcp_queue || !tcp2serial_queue) {
    print_error("malloc failed");
    exit(-1);
  }
  for(i = 0; i<QUEUE_SIZE; i++) {
    serial2tcp_queue[i] = (struct s_entry *) malloc(sizeof(struct s_entry));
    tcp2serial_queue[i] = (struct s_entry *) malloc(sizeof(struct s_entry));
    if(!serial2tcp_queue[i] || !tcp2serial_queue[i]) {
      for(j = 0; j<i; j++) {
        free(serial2tcp_queue[j]);
        free(tcp2serial_queue[j]);
      }
      free(serial2tcp_queue);
      free(tcp2serial_queue);
      print_error("malloc failed");
      exit(-1);
    }
  }
}
void set_cert_file(char *value)
{
  copy_file_path(cert_file, value);
}
void set_key_file(char *value)
{
  copy_file_path(key_file, value);
}
void set_root_file(char *value)
{
  copy_file_path(root_file, value);
}
/**
 * Print usage
 */
static void print_help()
{
  printf("Usage: dividi [OPTIONS]\n\n");
  printf("   -c/--config [FILE]       Set a custom path to the configuration file\n");
  printf("   -r/--rootca     [FILE]       Set the path to the root certification\n");
  printf("   -k/--key        [FILE]       Set the path to the private key\n");
  printf("   -s/--servercert [FILE]       Set the path to the server certification\n");
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
  int cert = 0;
  int long_index =0;
  static struct option long_options[] = {
    {"conf",       required_argument, 0,  'c' },
    {"servercert", required_argument, 0,  's' },
    {"rootca",     required_argument, 0,  'r' },
    {"key",        required_argument, 0,  'k' },
    {0,            0,                 0,   0  }
  };
  while ((c = getopt_long(argc, argv,"c:s:r:k:",
                   long_options, &long_index)) != -1) {
    if(optarg != NULL && strlen(optarg) > PATH_MAX) {
      printf("File paths may be maximum %d characters long\n", PATH_MAX);
      exit(-1);
    }
    switch(c) {
      case 'c':
        memcpy(config_file, optarg, strlen(optarg));
        break;
      case 's':
        cert = 1;
        memcpy(cert_file, optarg, strlen(optarg));
        break;
      case 'k':
        key = 1;
        memcpy(key_file, optarg, strlen(optarg));
        break;
      case 'r':
        memcpy(root_file, optarg, strlen(optarg));
        break;
      case 'h':
      default:
        print_help();
        exit(-1);
    }
  }
  if(!key && cert) {
    memcpy(key_file, cert_file, strlen(optarg));
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
  int fd;
  dbg("opening %s\n", port_name);
  fd = serial_open(&link->serial);
  if(fd < 0) {
    print_error("serial_open failed");
    exit(-1);
  }
}

static int get_empty_link_slot()
{
  int i;
  for(i = 0; i < MAX_LINKS; i++) {
    if(links[i].tcp_port == 0) {
      memset(&links[i].serial, 0, sizeof(struct s_serial));
      return i;
    }
  }
  return -1;
}

static int open_all_serial()
{
  int i;
  for(i = 0; i < MAX_LINKS; i++) {
    if(links[i].tcp_port != 0) {
      open_link(&links[i], links[i].serial.str_serial_port);
    }
  }
  return -1;
}

static void open_connection(SSL_CTX *ctx, int sock, struct s_link *link)
{
  BIO     *sbio;
  SSL     *ssl;
  int nbr_of_references = 0;
  struct s_conn *conn = (struct s_conn *) malloc(sizeof(struct s_conn));

  if(conn == NULL) {
    print_error("malloc");
    exit(-1);
  }
  if ((conn->tcp_socket = accept(sock, NULL, NULL)) < 0) {
    print_error("accept failed");
    free(conn);
    exit(-1);
  }
  sbio = BIO_new_socket(conn->tcp_socket, BIO_NOCLOSE);
  ssl = SSL_new(ctx);
  SSL_set_bio(ssl, sbio, sbio);
  if (SSL_accept(ssl) == -1) {
    print_error("SSL_accept");
    free(conn);
    exit(-1);
  }
  conn->socket = ssl;
  conn->link = link;
  nbr_of_references = start_connection_handlers(conn);
  // Wait until threads have copied conn
  get_thread_started_sem(nbr_of_references);
  free(conn);
}

#ifdef __linux__
/**
 * Will check the sockets for new connection
 * requests
 */
static int poll_sockets(struct pollfd *s, int total_links, SSL_CTX *ctx)
{
  int index;
  int polled = 0;

  dbg("Polling for incoming connections\n");
  polled = poll(s, total_links, -1);
  if(polled > 0) {
    for(index=0; index<total_links; index++) {
      if(s[index].revents & POLLIN) {
        open_connection(ctx, s[index].fd, &links[index]);
      }
    }
  } else if(polled < 0) {
    perror("poll failed");
    exit(-1);
  } else {
    dbg("Poll timed out, no incoming connections\n");
  }
  return 0;
}
#elif _WIN32
/**
 * Will check the sockets for new connection
 * requests
 */
static int select_sockets(struct pollfd *s, int total_links, SSL_CTX *ctx)
{
  int index;
  int maxfd;
  int ret;
  struct fd_set set;

  for (index = 0; index < MAX_LINKS; index++) {
    FD_SET(s[index].fd, &set);
    if (s[index].fd > maxfd)
        maxfd = s[index].fd;
  }

  dbg("Polling for incoming connections\n");
  ret = select(maxfd + 1, &set, NULL, NULL, NULL);

  if (ret) {
    for (index = 0; index < MAX_LINKS; index++) {
      if (FD_ISSET(s[index].fd, &set)) {
        open_connection(ctx, s[index].fd, &links[index]);
      }
    }
  }
  return 0;
}

#endif

/**
 * Initialises the queue
 */
static void init()
{
  allocate_queues();
  create_queues_lock();
  create_serial_lock();
  init_sem();
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
static int ssl_load_certificates(SSL_CTX *ctx, char *root, char *cert, char *key)
{
  if(!(*root && *cert && *key)) {
    fprintf(stderr, "Warning, server is unsecured!\n");
    return -1;
  }
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
  return 0;
}
////////////////////////////////////PUBLIC////////////////////////////////////////////////

/**
 * Add a link to the look-up table
 */
struct s_link * add_link(char *serial_port, char *tcp_port)
{
  int index = get_empty_link_slot();

  if(index < 0) {
    fprintf(stderr, "MAX_LINKS exceeded\n");
    exit(-1);
  }
  dbg("Adding link (serial: %s, tcp: %s)\n", serial_port, tcp_port);
  links[index].tcp_port = atoi(tcp_port);
  memcpy(links[index].serial.str_serial_port, serial_port, strlen(serial_port)+1);
  return &links[index];
}

/**
 * Outputs error message
 */
void print_error(char *error_msg)
{
#ifdef __linux__
  perror(error_msg);
#elif _WIN32
  fprintf(stderr, "%s: %d\n", error_msg, WSAGetLastError());
#endif
}

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
  int secure;

#ifdef _WIN32
  WSADATA wsa_data;
  WSAStartup(MAKEWORD(1,1), &wsa_data);
#endif

  snprintf(config_file, PATH_MAX, DEFAULT_CONFIG_FILE);
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
  secure = !ssl_load_certificates(ctx, root_file, cert_file, key_file);
  if(secure) {
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
  } else {
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
  }

  atexit(destroy_everything);
  memset(links, 0, MAX_LINKS*sizeof(struct s_link));

  conf_parse(config_file);
  open_all_serial();
  init();

  for(index=0; index<MAX_LINKS; index++) {
    if(links[index].tcp_port == 0) {
      break;
    }
    if ((s[index].fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
      print_error("socket failed");
      exit(-1);
    }

    memset( (char *) (&sain),0, sizeof(sain));
    sain.sin_family = AF_INET;
    sain.sin_port = htons(links[index].tcp_port);
    sain.sin_addr.s_addr = INADDR_ANY;

    if (bind(s[index].fd, (struct sockaddr *)&sain, sizeof(sain)) < 0) {
      print_error("bind failed");
      exit(-1);
    }
    if (listen(s[index].fd, MAX_ACTIVE_CONNECTIONS) < 0) {
      print_error("listen failed");
      exit(-1);
    }
#ifdef __linux__
    s[index].events = POLLIN;
#endif
    if(setsockopt(s[index].fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&optval, sizeof(optval)) < 0) {
      perror("setsockopt failed");
      exit(-1);
    }
  }
  dividi_running = 1;
  total_links = index;
  while(dividi_running) {
#ifdef __linux__
    if(poll_sockets(s, index, ctx) < 0) {
#elif _WIN32
    if(select_sockets(s, index, ctx) < 0) {
#endif
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
