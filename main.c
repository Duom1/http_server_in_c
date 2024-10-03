#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

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
#define SLEEP(a) Sleep(a * 1000)
#define close(a) closesocket(a)
#else
#define SLEEP(a) sleep(a)
#endif

#define PORT (8080)
#define BUFFER_SIZE (1024)

struct {
  bool binded_sock;
  bool buffer_created;
#ifdef PLATFORM_WINDOWS
  bool wsa_started;
#endif
  int sockfd;
  void *buffer;
} clean_on_exit;

void exit_clean(void) {
  if (clean_on_exit.binded_sock) {
    printf("closing socket\n");
    close(clean_on_exit.sockfd);
  }
  if (clean_on_exit.buffer_created) {
    printf("freeing buffer\n");
    free(clean_on_exit.buffer);
  }
#ifdef PLAFORM_WINDOWS
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

int main(void) {
  int ret;

  atexit(exit_clean);
  signal(SIGINT, handle_sigint);
  clean_on_exit.buffer_created = false;
  clean_on_exit.binded_sock = false;
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

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    fprintf(stderr, "unable to create socket\n");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(PORT);
  addr.sin_addr.s_addr = INADDR_ANY;

  ret = bind(sockfd, (struct sockaddr *)&addr, sizeof(addr));
  if (ret < 0) {
    int err = errno;
    fprintf(stderr, "errno: %i\n", err);
    fprintf(stderr, "could not bind\n");
    exit(EXIT_FAILURE);
  }
  clean_on_exit.binded_sock = true;
  clean_on_exit.sockfd = sockfd;

  listen(sockfd, 3);

  bstring request = bfromcstr("");
  bstring double_rn = bfromcstr("\r\n\r\n");
  char *buf = malloc(BUFFER_SIZE);
  clean_on_exit.buffer_created = true;
  clean_on_exit.buffer = buf;
  while (true) {
    int confd = accept(sockfd, NULL, NULL);
    if (confd < 0) {
      fprintf(stderr, "could not accept connection\n");
      exit(EXIT_FAILURE);
    }

    printf("client connected\n");
    int cur_it = 0;
    while (true) {
      recv(confd, buf, BUFFER_SIZE, 0);
      ret = bcatcstr(request, buf);
      if (ret != BSTR_OK) {
        fprintf(stderr, "failed to concat string to request string\n");
      }
      if (binstr(request, 5 + cur_it * BUFFER_SIZE, double_rn) > 0) {
        break;
      }
      cur_it++;
    }
    printf("%s", request->data);

    bstring bstr = bfromcstr("HTTP/1.1 200 OK\r\nContent-Type: "
                             "text/html\r\nContent-Length: 5\r\n\r\nhello");
    send(confd, bstr->data, bstr->slen, 0);
    close(confd);
  }

  return EXIT_SUCCESS;
}
