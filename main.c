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
#define THREAD_FREE_WAIT_MILS                                                  \
  (100)                     // The time program waits to check if a thread
                            // is available
#define BUFFER_SIZE (1024)  // Size of the request buffer.
#define CONNECTION_AMT (10) // The amount of connections given to listen()
#define PORT (8080)         // Port that the program uses.
#define THREADS (50)        // The number of threads for processing requests.

bstring drn_bstr;

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
    printf("closing socket\n");
    CLOSESOCKET(clean_on_exit.sockfd);
  }
  for (int i = 0; i < THREADS; i++) {
    if (clean_on_exit.thread_in_use[i]) {
      i--;
      SLEEP(THREAD_FREE_WAIT_MILS);
    }
  }
  printf("destroying drn bstring\n");
  bdestroy(drn_bstr);
#ifdef _WIN32
  if (clean_on_exit.wsa_started) {
    printf("cleaning wsa\n");
    WSACleanup();
  }
#endif
}

void handle_sigint(int sig) {
  printf("\nrecieved sigint (%i) terninating...\n", sig);
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

  // Variable cc_on stands for client connected on and it is the unix time
  // stamp of when it happened.
  int cc_on = (int)time(NULL);
  printf("client connected on %i\n", cc_on);

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
        printf("connection timed out\n");
        goto close_without_response;
      }
      SLEEP(NON_BLOCKING_EXTRA_SLEEP_MILS);
      continue;
    }

    // Concatenate the buffer to the request string.
    ret = bcatcstr(request, buf);
    if (ret != BSTR_OK) {
      fprintf(stderr, "failed to concat string to request string\n");
    }

    // Cheking if the request has come to an end.
    // The staring position for the search is cur_it * BUFFER_SIZE
    // so that we don't read the first parts of the request multiple times
    if (binstr(request, cur_it * BUFFER_SIZE, drn_bstr) > 0) {
      break;
    }

    // Cheking if the request is longer than needed.
    if (request->slen > 10 * BUFFER_SIZE) {
      printf("request exceeded %i bytes sending response without getting "
             "full request\n",
             10 * BUFFER_SIZE);
      break;
    }

    // Increment the iterator that keeps track of how many buffers have been
    // red (readed?).
    cur_it++;
  }

  // Creting a response and sending it;
  bstring response = bfromcstr("HTTP/1.1 200 OK" RN "Content-Type: "
                               "text/html" RN "Content-Length: 5" DRN "hello");
  send(args->socket, response->data, response->slen, 0);
  printf("sent response to client\n");

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
    fprintf(stderr, "fcntl(F_GETFL) failed: %s\n", strerror(errno));
    return -1;
  }
  if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
    fprintf(stderr, "fcntl(F_SETFL) failed: %s\n", strerror(errno));
    return -1;
  }
#endif

  return 0;
}

int main(void) {
  int ret;
  int port = PORT;

  atexit(exit_clean);
  signal(SIGINT, handle_sigint);
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
    fprintf(stderr, "unable to create socket\n");
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
    int err = errno;
    fprintf(stderr, "errno: %i\n", err);
    fprintf(stderr, "could not bind\n");
    exit(EXIT_FAILURE);
  }
  clean_on_exit.binded_sock = true;
  clean_on_exit.sockfd = sockfd;

  if (set_nonblocking(sockfd) != 0) {
    fprintf(stderr, "error setting soket to non blocking mode\n");
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
      fprintf(stderr, "error setting soket to non blocking mode\n");
      exit(EXIT_FAILURE);
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
