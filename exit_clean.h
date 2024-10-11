#ifndef HTTP_SERVER_IN_C_EXIT_CLEAN_H
#define HTTP_SERVER_IN_C_EXIT_CLEAN_H

#include "defines.h"
#include "logging.h"
#include <pthread.h>
#include <stdbool.h>

struct clean_on_exit_t {
  bool binded_sock;
  int thread_in_use[THREADS];
#ifdef _WIN32
  bool wsa_started;
#endif
  int sockfd;
  pthread_t threads[THREADS];
} clean_on_exit_t;

void exit_clean(void) {
  if (clean_on_exit_t.binded_sock) {
    log_info("closing socket\n", OTHER_FUNCTION_THREAD_NUM);
    CLOSESOCKET(clean_on_exit_t.sockfd);
  }
  for (int i = 0; i < THREADS; i++) {
    if (clean_on_exit_t.thread_in_use[i]) {
      pthread_join(clean_on_exit_t.threads[i], NULL);
    }
  }
#ifdef _WIN32
  if (clean_on_exit.wsa_started) {
    log_info("cleaning wsa\n", OTHER_FUNCTION_THREAD_NUM);
    WSACleanup();
  }
#endif
  log_info("closing program\n", OTHER_FUNCTION_THREAD_NUM);
  log_info("closing logging file\n", OTHER_FUNCTION_THREAD_NUM);
  fclose(logging_file);
}

#endif
