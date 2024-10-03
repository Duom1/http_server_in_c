#define _POSIX_C_SOURCE 199309L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bstrlib/bstrlib.h"

#ifdef PLATFORM_WINDOWS
#include <Windows.h>
#include <io.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#ifdef PLATFORM_WINDOWS
#define SLEEP(a) Sleep(a)
// Close function call needs to be closesocket on windows.
#define close(a) closesocket(a)
#else
#define SLEEP(a)                                                               \
  do {                                                                         \
    struct timespec ts;                                                        \
    ts.tv_sec = (a) / 1000;                                                    \
    ts.tv_nsec = ((a) % 1000) * 1000000;                                       \
    nanosleep(&ts, NULL);                                                      \
  } while (0)
#endif

#define RN "\r\n"      // Macro for carridge returns newline.
#define DRN "\r\n\r\n" // Macro for two carridge returns and nelines

#define NON_BLOCKING_EXTRA_SLEEP_MILS                                          \
  (100) // Sleep time between nonblocking socket calls
#define REQUEST_TIMEOUT_AFTER_SEC                                              \
  (10)                     // Time out amount if proper request is not recieved
#define BUFFER_SIZE (1024) // Size of the request buffer.
#define CONNECTION_AMT (3) // The amount of connections given to listen()
#define PORT (8080)        // Port that the program uses.

// Structure for helping the handeling of cleanup on exit.
struct {
  bool binded_sock;
  bool buffer_created;
  bool request_string_set;
  bool drn_string_set;
#ifdef PLATFORM_WINDOWS
  bool wsa_started;
#endif
  int sockfd;
  char *buffer;
  bstring *request_string;
  bstring *drn_string;
} clean_on_exit;

// Function taht handles cleanup on exit.
void exit_clean(void) {
  if (clean_on_exit.binded_sock) {
    printf("closing socket\n");
    close(clean_on_exit.sockfd);
  }
  if (clean_on_exit.buffer_created) {
    printf("freeing buffer\n");
    free(clean_on_exit.buffer);
  }
  if (clean_on_exit.request_string_set) {
    printf("detroying request string\n");
    bdestroy(*clean_on_exit.request_string);
  }
  if (clean_on_exit.drn_string_set) {
    printf("detroying drn string\n");
    bdestroy(*clean_on_exit.drn_string);
  }
#ifdef PLAFORM_WINDOWS
  if (clean_on_exit.wsa_started) {
    printf("cleaning wsa\n");
    WSACleanup();
  }
#endif
}

// Function for handeling the interrupt signals.
void handle_sigint(int sig) {
  printf("\nrecieved sigint (%i) terninating...\n", sig);
  exit(EXIT_SUCCESS);
}

int main(void) {
  // Variable ret is used for functions that return an integer value for error
  // cheking.
  int ret;
  // Variable port is the port used by this program.
  int port = PORT;

  // Making it so that the exit_clean function is run when the
  // program is closed in order to clean up resources.
  atexit(exit_clean);
  // Sets a custom handler for interrupt signals.
  signal(SIGINT, handle_sigint);
  // Setting all the clean up values to false.
  clean_on_exit.buffer_created = false;
  clean_on_exit.binded_sock = false;
  clean_on_exit.drn_string_set = false;
  clean_on_exit.request_string_set = false;
  // On windows platform it is necesary to create and initialise
  // WSA.
#ifdef PLATFORM_WINDOWS
  clean_on_exit.wsa_started = false;

  WSADATA wsa_data;
  ret = WSAStartup(MAKEWORD(2, 2), &wsa_data);
  if (ret != 0) {
    printf("wsastartup failed: %d\n", ret);
    exit(EXIT_FAILURE);
  }
  clean_on_exit.wsa_started = true;
#endif

  // Attempts to create a socket.
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    fprintf(stderr, "unable to create socket\n");
    exit(EXIT_FAILURE);
  }

  // Creating sock address structure used in the program
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(struct sockaddr_in));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  // addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  // Binds the socket and does error checking
  ret = bind(sockfd, (struct sockaddr *)&addr, sizeof(addr));
  if (ret < 0) {
    int err = errno;
    fprintf(stderr, "errno: %i\n", err);
    fprintf(stderr, "could not bind\n");
    exit(EXIT_FAILURE);
  }
  clean_on_exit.binded_sock = true;
  clean_on_exit.sockfd = sockfd;

  // This code gets the socket filedescriptor flags and adds
  // non blocking to them and set them back.
  int flags = fcntl(sockfd, F_GETFL, 0);
  if (flags < 0) {
    fprintf(stderr, "something went wrong when getting socket flags\n");
    exit(EXIT_FAILURE);
  }
  flags |= O_NONBLOCK;
  if (fcntl(sockfd, F_SETFL, flags) < 0) {
    fprintf(stderr, "unable to set socket flags\n");
    exit(EXIT_FAILURE);
  }

  listen(sockfd, CONNECTION_AMT);

  // Allocating memory for the buffer.
  char *buf = malloc(BUFFER_SIZE);
  clean_on_exit.buffer_created = true;
  clean_on_exit.buffer = buf;

  // Creating strings for the main loop
  // Variable drn_bstr stands for double carridge return
  // newline better string. And like the name implies
  // is a better string library string that contains
  // two \r\n characters.
  bstring request = bfromcstr("");
  clean_on_exit.request_string_set = true;
  clean_on_exit.request_string = &request;
  bstring drn_bstr = bfromcstr(DRN);
  clean_on_exit.drn_string_set = true;
  clean_on_exit.drn_string = &drn_bstr;

  // The main loop on the program
  while (true) {
    // Truncating the request string in order to not
    // have multiple requests in the same string.
    ret = btrunc(request, 0);
    if (ret != BSTR_OK) {
      fprintf(stderr, "failed to trucate request string to 0\n");
    }

    // Variable confd stands for connection filedescriptor and is the
    // filedescriptor for the connection made by a client.
    int confd;
    while (true) {
      confd = accept(sockfd, NULL, NULL);
      int err = errno;
      if (!(confd < 0 && err == EWOULDBLOCK) && (confd >= 0)) {
        break;
      }
      SLEEP(NON_BLOCKING_EXTRA_SLEEP_MILS);
    }

    // This code gets the new connection filedescriptor flags and adds
    // non blocking to them and set them back.
    flags = fcntl(confd, F_GETFL, 0);
    if (flags < 0) {
      fprintf(stderr, "something went wrong when getting connection flags\n");
      exit(EXIT_FAILURE);
    }
    flags |= O_NONBLOCK;
    if (fcntl(confd, F_SETFL, flags) < 0) {
      fprintf(stderr, "unable to set connection flags\n");
      exit(EXIT_FAILURE);
    }

    // Variable cc_on stands for client connected on and it is the unix time
    // stamp of when it happened.
    int cc_on = (int)time(NULL);
    printf("client connected on %i\n", cc_on);

    // The loop for reading the request.
    // Variable cur_it stands for current iteration.
    int cur_it = 0;
    int bytes_recieved = 0;
    while (true) {

      // Trying to recieve bytes from the client.
      // The rbuf part of the variable recieved_rbuf stands for
      // relative to buffer since the size will be relative to the buffer.
      int recieved_rbuf = recv(confd, buf, BUFFER_SIZE, 0);
      // If no bytes where recived check if the connection should time out
      // or continue looking for them.
      if (recieved_rbuf <= 0) {
        if ((int)time(NULL) - cc_on > REQUEST_TIMEOUT_AFTER_SEC) {
          printf("connection timed out sending response anyway\n");
          break;
        }
        SLEEP(NON_BLOCKING_EXTRA_SLEEP_MILS);
        continue;
      }

      // Adding the amount of bytes recieved from recv function to the
      // cumulative count of all the bytes.
      // And then concatenating the bytes to the request string.
      bytes_recieved += recieved_rbuf;
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
      if (bytes_recieved > 10 * BUFFER_SIZE) {
        printf("request exceeded %i bytes sending response wihtout getting "
               "full request\n",
               10 * BUFFER_SIZE);
        break;
      }

      // Increment the iterator that keeps track of how many buffers have been
      // red (readed?).
      cur_it++;
    }

    // Creting a response sending it and then deallocating the strings memory.
    bstring response =
        bfromcstr("HTTP/1.1 200 OK" RN "Content-Type: "
                  "text/html" RN "Content-Length: 5" DRN "hello");
    send(confd, response->data, response->slen, 0);
    printf("sent response to client\n");
    bdestroy(response);

    close(confd);
  }

  // If the code goes here something went wrong in the main loop.
  // The code should be terminated with something like a keyboard interrupt.
  return EXIT_FAILURE;
}
