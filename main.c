#define _POSIX_C_SOURCE 199309L
#ifdef _WIN32
#define HAVE_STRUCT_TIMESPEC
#endif
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bstrlib/bstrlib.h"

#ifdef _WIN32
#include <Windows.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#define SLEEP(a) Sleep(a)
#define CLOSESOCKET(a) closesocket(a)
#else
#define CLOSESOCKET(a) close(a)
#define SLEEP(a)                                                               \
  {                                                                            \
    struct timespec ts;                                                        \
    ts.tv_sec = (a) / 1000;                                                    \
    ts.tv_nsec = ((a) % 1000) * 1000000;                                       \
    nanosleep(&ts, NULL);                                                      \
  }
#endif

#define RN "\r\n"      // Macro for carridge returns newline.
#define DRN "\r\n\r\n" // Macro for two carridge returns and nelines

#define NON_BLOCKING_EXTRA_SLEEP_MILS                                          \
  (100) // Sleep time between nonblocking socket calls
#define REQUEST_TIMEOUT_AFTER_SEC                                              \
  (20) // Time out amount if proper request is not recieved
#define OTHER_FUCNTION_THREAD_NUM                                              \
  (-2) // the thread number of other fucntions like exit_clean and handle_sigint
#define MAIN_FUCNTION_THREAD_NUM (-1) // the thread number of main function
#define THREAD_FREE_WAIT_MILS                                                  \
  (100)                     // The time program waits to check if a thread
                            // is available
#define CONNECTION_AMT (10) // The amount of connections given to listen()
#define BUFFER_SIZE (1024)  // Size of the request buffer.
#define LOG_FILE "http-server.log" // the path for the logging file
#define THREADS (50) // The number of threads for processing requests.
#define PORT (8080)  // Port that the program uses.

bstring drn_bstr;
FILE *logging_file;
bool disable_debug_log = false;

void log_generic(char *msg, char *tag, int thread_num) {
  time_t ut = time(NULL);
  struct tm *lt = localtime(&ut);
  fprintf(logging_file, "[%i/%i/%i %i:%i:%i] [%i] [thread: %i] [%s]: %s",
          lt->tm_mday, lt->tm_mon + 1, lt->tm_year + 1900, lt->tm_hour,
          lt->tm_min, lt->tm_sec, (int)ut, thread_num, tag, msg);
  fflush(logging_file);
}
void log_debug(char *msg, int thread_num) {
  if (!(disable_debug_log)) {
    log_generic(msg, "DEBUG", thread_num);
  }
}
void log_info(char *msg, int thread_num) {
  log_generic(msg, "INFO", thread_num);
}
void log_error(char *msg, int thread_num) {
  log_generic(msg, "ERROR", thread_num);
}
void log_critical(char *msg, int thread_num) {
  log_generic(msg, "CRITICAL", thread_num);
}

struct {
  bool binded_sock;
  int thread_in_use[THREADS];
#ifdef _WIN32
  bool wsa_started;
#endif
  int sockfd;
  pthread_t threads[THREADS];
} clean_on_exit;

void exit_clean(void) {
  if (clean_on_exit.binded_sock) {
    log_info("closing socket\n", OTHER_FUCNTION_THREAD_NUM);
    CLOSESOCKET(clean_on_exit.sockfd);
  }
  for (int i = 0; i < THREADS; i++) {
    if (clean_on_exit.thread_in_use[i]) {
      i--;
      SLEEP(THREAD_FREE_WAIT_MILS);
    }
  }
  log_info("destroying drn bstring\n", OTHER_FUCNTION_THREAD_NUM);
  bdestroy(drn_bstr);
#ifdef _WIN32
  if (clean_on_exit.wsa_started) {
    log_info("cleaning wsa\n", OTHER_FUCNTION_THREAD_NUM);
    WSACleanup();
  }
#endif
  log_info("closing logging file\n", OTHER_FUCNTION_THREAD_NUM);
  fclose(logging_file);
}

void handle_sig(int sig) {
  log_info("recieved signal", OTHER_FUCNTION_THREAD_NUM);
  fprintf(logging_file, " (%i) closing server\n", sig);
  exit(EXIT_SUCCESS);
}

typedef struct {
  int socket;
  int thread_number;
} handle_request_args;

// Needs connection fd and thread number as inputs.
void *handle_request(void *inargs) {
  handle_request_args *args = (handle_request_args *)inargs;
  clean_on_exit.thread_in_use[args->thread_number] = true;
  int ret;
  bstring request = bfromcstr("");
  char *buf = malloc(BUFFER_SIZE);

  int cc_on = (int)time(NULL);
  log_info("client connected\n", args->thread_number);

  // The loop for reading the request.
  // Variable cur_it stands for current iteration.
  int cur_it = 0;
  while (true) {

    // Trying to recieve bytes from the client.
    int bytes_recieved = recv(args->socket, buf, BUFFER_SIZE, 0);
    // If no bytes where recived check if the connection should time out
    // or continue looking for them.
    if (bytes_recieved <= 0) {
      if ((int)time(NULL) - cc_on > REQUEST_TIMEOUT_AFTER_SEC) {
        log_info("connection timed out\n", args->thread_number);
        goto close_without_response;
      }
      SLEEP(NON_BLOCKING_EXTRA_SLEEP_MILS);
      continue;
    }

    // Concatenate the buffer to the request string.
    ret = bcatcstr(request, buf);
    if (ret != BSTR_OK) {
      log_error("failed to concat string to request string\n",
                args->thread_number);
    }

    // Cheking if the request has come to an end.
    // The staring position for the search is cur_it * BUFFER_SIZE
    // so that we don't read the first parts of the request multiple times
    if (binstr(request, cur_it * BUFFER_SIZE, drn_bstr) > 0) {
      break;
    }

    // Cheking if the request is longer than needed.
    static const int max_buffer = 10;
    if (request->slen > max_buffer * BUFFER_SIZE) {
      log_info("request exceeded", args->thread_number);
      fprintf(logging_file, " %i bytes\n", max_buffer * BUFFER_SIZE);
      break;
    }

    cur_it++;
  }

  log_debug("request:", args->thread_number);
  fprintf(logging_file, " %s", request->data);

  // Creting a response and sending it
  bstring response = bfromcstr("HTTP/1.1 200 OK" RN "Content-Type: "
                               "text/html" RN "Content-Length: 5" DRN "hello");
  send(args->socket, response->data, response->slen, 0);
  log_info("sent response to client\n", args->thread_number);
  log_debug("response: ", args->thread_number);
  fprintf(logging_file, " %s\n", response->data);

  bdestroy(response);

close_without_response:

  free(buf);
  bdestroy(request);
  CLOSESOCKET(args->socket);

  clean_on_exit.thread_in_use[args->thread_number] = false;
  free(inargs);
  return NULL;
}

// Function to set the socket to non-blocking mode.
// Returns 0 on success.
int set_nonblocking(int sock) {
#ifdef _WIN32
  unsigned long mode = 1;
  if (ioctlsocket(sock, FIONBIO, &mode) != 0) {
    int err = WSAGetLastError();
    fprintf(stderr, "Failed to set non-blocking mode: %d\n", err);
    return -1;
  }
#else
  int flags = fcntl(sock, F_GETFL, 0);
  if (flags == -1) {
    log_error("fcntl(F_GETFL) failed\n", OTHER_FUCNTION_THREAD_NUM);
    return -1;
  }
  if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
    log_error("fcntl(F_SETFL) failed\n", OTHER_FUCNTION_THREAD_NUM);
    return -1;
  }
#endif

  return 0;
}

int main(void) {
  int ret;
  int port = PORT;

  logging_file = fopen(LOG_FILE, "w+");
  if (logging_file == NULL) {
    logging_file = stdout;
    log_error("unable to open logging file, using stdout as fallback",
              MAIN_FUCNTION_THREAD_NUM);
  }
  atexit(exit_clean);
  signal(SIGINT, handle_sig);
  signal(SIGTERM, handle_sig);
  clean_on_exit.binded_sock = false;
  memset(&clean_on_exit.thread_in_use, false,
         sizeof(clean_on_exit.thread_in_use));
  memset(&clean_on_exit.threads, -1, sizeof(clean_on_exit.threads));
  drn_bstr = bfromcstr(DRN);
  // On windows platform it is necesary to create and initialise
  // WSA.
#ifdef _WIN32
  clean_on_exit.wsa_started = false;

  WSADATA wsa_data;
  ret = WSAStartup(MAKEWORD(2, 2), &wsa_data);
  if (ret != 0) {
    printf("wsastartup failed: %d\n", ret);
    exit(EXIT_FAILURE);
  }
  clean_on_exit.wsa_started = true;
#endif

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    log_critical("unable to create socket\n", MAIN_FUCNTION_THREAD_NUM);
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(struct sockaddr_in));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  // addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  ret = bind(sockfd, (struct sockaddr *)&addr, sizeof(addr));
  if (ret < 0) {
    log_critical("could not bind\n", MAIN_FUCNTION_THREAD_NUM);
    exit(EXIT_FAILURE);
  }
  clean_on_exit.binded_sock = true;
  clean_on_exit.sockfd = sockfd;

  if (set_nonblocking(sockfd) != 0) {
    log_critical("error setting soket to non blocking mode\n",
                 MAIN_FUCNTION_THREAD_NUM);
    exit(EXIT_FAILURE);
  }

  listen(sockfd, CONNECTION_AMT);

  // The main loop on the program
  while (true) {
    // Variable confd stands for connection filedescriptor and is the
    // filedescriptor for the connection made by a client.
    int confd = 0;
    while (true) {
      confd = accept(sockfd, NULL, NULL);
      int err = errno;
      if (!(confd < 0 && err == EWOULDBLOCK) && (confd >= 0)) {
        break;
      }
      SLEEP(NON_BLOCKING_EXTRA_SLEEP_MILS);
    }

    if (set_nonblocking(confd) != 0) {
      log_error("error setting socket to non blocking mode\n",
                MAIN_FUCNTION_THREAD_NUM);
    }

    for (int i = 0; i < THREADS; i++) {
      if (!clean_on_exit.thread_in_use[i]) {
        handle_request_args *args = malloc(sizeof(handle_request_args));
        args->socket = confd;
        args->thread_number = i;
        pthread_create(&clean_on_exit.threads[i], NULL, handle_request, args);
        break;
      } else if (i + 1 == THREADS) {
        i = -1;
        SLEEP(THREAD_FREE_WAIT_MILS);
      }
    }
  }

  // If the code goes here something went wrong in the main loop.
  // The code should be terminated with something like a keyboard interrupt.
  return EXIT_FAILURE;
}
