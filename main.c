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
#include "stb_ds.h"

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

#define RN "\r\n"
#define DRN "\r\n\r\n"
#define DN "\n\n"

#define NON_BLOCKING_EXTRA_SLEEP_MILS                                          \
  (100) // Sleep time between nonblocking socket calls
#define REQUEST_TIMEOUT_AFTER_SEC                                              \
  (20) // Time out amount if proper request is not recieved
#define OTHER_FUCNTION_THREAD_NUM                                              \
  (-2) // the thread number of other fucntions like exit_clean and handle_sigint
#define MAIN_FUCNTION_THREAD_NUM (-1) // the thread number of main function
#define THREAD_FREE_WAIT_MILS                                                  \
  (100) // The time program waits to check if a thread
        // is available
#define EXIT_PRINTF_ERROR                                                      \
  (-20) // error code return if the program is unable to use printf
#define CONSTANT_STRING_AMT (6) // the amount of constant strings
#define CONNECTION_AMT (10)     // The amount of connections given to listen()
#define BUFFER_SIZE (1024)      // Size of the request buffer.
#define LOG_FILE "http-server.log" // the path for the logging file
#define THREADS (50) // The number of threads for processing requests.
#define PORT (8080)  // Port that the program uses.

enum Http_request_types {
  HTTP_GET,
  HTTP_HEAD,
  HTTP_POST,
  HTTP_PUT,
  HTTP_DELETE,
  HTTP_CONNECT,
  HTTP_OPTIONS,
  HTTP_TRACE,
  HTTP_PATCH,
  HTTP_ENUM_MAX,
};

typedef struct {
  enum Http_request_types type;
  bstring path;
  bstring version;
  struct { // this uses the stb ds library string hashmap
    char *key;
    bstring value;
  } *headers;
} http_request_t;

struct tagbstring drn_bstr;
struct tagbstring dn_bstr;
struct tagbstring rn_bstr;
struct tagbstring n_bstr;
struct tagbstring rn_bstr_dummy;
struct tagbstring n_bstr_dummy;
struct tagbstring get_bstr;
struct tagbstring head_bstr;
struct tagbstring post_bstr;
struct tagbstring put_bstr;
struct tagbstring delete_bstr;
struct tagbstring connect_bstr;
struct tagbstring options_bstr;
struct tagbstring trace_bstr;
struct tagbstring patch_bstr;

FILE *logging_file;
bool disable_debug_log = false;

void log_generic(char *msg, char *tag, int thread_num);

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

void log_generic(char *msg, char *tag, int thread_num) {
  int ret;
  time_t ut = time(NULL);
  if ((int)ut < 0) { // if the return value of time function is less that zero
                     // it is an error
    log_critical("unable to get proper time", OTHER_FUCNTION_THREAD_NUM);
  }
  struct tm *lt = localtime(&ut);
  struct tm backup = {
      0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0}; // in case localtime fails we can use this as a fallback
  if (lt == NULL) {
    lt = &backup;
  }
  ret = fprintf(logging_file, "[%i/%i/%i %i:%i:%i] [%i] [thread: %i] [%s]: %s",
                lt->tm_mday, lt->tm_mon + 1, lt->tm_year + 1900, lt->tm_hour,
                lt->tm_min, lt->tm_sec, (int)ut, thread_num, tag, msg);
  if (ret < 0) {
    exit(EXIT_PRINTF_ERROR);
  }
  fflush(logging_file);
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
      pthread_join(clean_on_exit.threads[i], NULL);
    }
  }
#ifdef _WIN32
  if (clean_on_exit.wsa_started) {
    log_info("cleaning wsa\n", OTHER_FUCNTION_THREAD_NUM);
    WSACleanup();
  }
#endif
  log_info("closing program\n", OTHER_FUCNTION_THREAD_NUM);
  log_info("closing logging file\n", OTHER_FUCNTION_THREAD_NUM);
  fclose(logging_file);
}

int closing_sig = 0;
bool program_should_close = false;

void handle_sig(int sig) {
  closing_sig = sig;
  program_should_close = true;
}

typedef struct {
  int socket;
  int thread_number;
} handle_request_args;

int newline_dummy(bstring string) {
  int ret;
  ret = bfindreplace(string, &rn_bstr, &rn_bstr_dummy, 0);
  if (ret != BSTR_OK) {
    log_error("unable to replace newlines in request",
              OTHER_FUCNTION_THREAD_NUM);
    return -1;
  }
  ret = bfindreplace(string, &n_bstr, &n_bstr_dummy, 0);
  if (ret != BSTR_OK) {
    log_error("unable to replace newlines in request",
              OTHER_FUCNTION_THREAD_NUM);
    return -1;
  }
  return 0;
}

// TODO: Create a parser that parses an http request string into http request
// struct. Does not validate path.
// steps:
// 1. split first line into method, path and version.
// 2. add headers into stb hasmap
// Returns 0 if successfull.
int parse_http_request(http_request_t *request, bstring requets_str) {
  struct bstrList *lines = NULL;
  lines = bsplit(requets_str, '\n');
  if (lines == NULL) {
    log_error("unable to parse request string: pointer is null\n",
              OTHER_FUCNTION_THREAD_NUM);
    return -1;
  }
  if (lines->qty < 1) {
    log_error("unable to parse request string: qty is less than 1\n",
              OTHER_FUCNTION_THREAD_NUM);
    bstrListDestroy(lines);
    return -1;
  }
  struct bstrList *first_line = NULL;
  lines = bsplit(lines->entry[0], ' ');
  if (lines == NULL) {
    log_error("unable to parse request string first line: pointer is null\n",
              OTHER_FUCNTION_THREAD_NUM);
    bstrListDestroy(lines);
    return -1;
  }
  if (lines->qty < 3) {
    log_error(
        "unable to parse request string first line: qty is leass than 3\n",
        OTHER_FUCNTION_THREAD_NUM);
    bstrListDestroy(lines);
    bstrListDestroy(first_line);
    return -1;
  }
  // TODO: There should be a funtcin for copying strings directly but
  // i can't seem to fin it.
  request->path = bfromcstr((const char *)first_line->entry[1]->data);
  request->version = bfromcstr((const char *)first_line->entry[2]->data);
  if (binstr(first_line->entry[0], 0, &get_bstr) == 0) {
    request->type = HTTP_GET;
  } else if (binstr(first_line->entry[0], 0, &head_bstr) == 0) {
    request->type = HTTP_HEAD;
  } else if (binstr(first_line->entry[0], 0, &options_bstr) == 0) {
    request->type = HTTP_OPTIONS;
  } else if (binstr(first_line->entry[0], 0, &trace_bstr) == 0) {
    request->type = HTTP_TRACE;
  } else if (binstr(first_line->entry[0], 0, &put_bstr) == 0) {
    request->type = HTTP_PUT;
  } else if (binstr(first_line->entry[0], 0, &delete_bstr) == 0) {
    request->type = HTTP_DELETE;
  } else if (binstr(first_line->entry[0], 0, &post_bstr) == 0) {
    request->type = HTTP_POST;
  } else if (binstr(first_line->entry[0], 0, &patch_bstr) == 0) {
    request->type = HTTP_PATCH;
  } else if (binstr(first_line->entry[0], 0, &connect_bstr) == 0) {
    request->type = HTTP_CONNECT;
  } else {
    log_error("unable to get http request method\n", OTHER_FUCNTION_THREAD_NUM);
    bstrListDestroy(lines);
    bstrListDestroy(first_line);
    return -1;
  }

  sh_new_strdup(request->headers);
  for (int i = 0; i < lines->qty; i++) {
    struct bstrList *split_pair = NULL;
    bsplit(lines->entry[i], ':');
    if (split_pair == NULL) {
      continue;
    }
    if (split_pair->qty < 2) {
      continue;
    }
    bstring value_for_map = bfromcstr((const char *)split_pair->entry[1]->data);
    shput(request->headers, split_pair->entry[0]->data, value_for_map);
    bstrListDestroy(split_pair);
  }

  bstrListDestroy(lines);
  bstrListDestroy(first_line);
  return 0;
}

void *handle_request(void *inargs) {
  handle_request_args *args = (handle_request_args *)inargs;
  clean_on_exit.thread_in_use[args->thread_number] = true;
  int ret;

  bstring request = bfromcstr("");
  if (request == NULL) {
    log_critical("could not create string for request\n", args->thread_number);
    free(inargs);
    return NULL;
  }

  char *buf = malloc(BUFFER_SIZE);
  if (buf == NULL) {
    log_critical("unable to allocate space for buffer", args->thread_number);
    bdestroy(request);
    free(inargs);
    return NULL;
  }

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
    if ((binstr(request, cur_it * BUFFER_SIZE, &drn_bstr) > 0) ||
        (binstr(request, cur_it * BUFFER_SIZE, &dn_bstr) > 0)) {
      break;
    }

    // Cheking if the request is longer than needed.
    static const int max_buffer = 10;
    if (request->slen > max_buffer * BUFFER_SIZE) {
      log_info("request exceeded", args->thread_number);
      fprintf(logging_file, " %i bytes\n", max_buffer * BUFFER_SIZE);
      fflush(logging_file);
      goto close_without_response;
    }

    cur_it++;
  }

  // making a dummy string so that it can be logged better
  bool unable_to_create_dummy_request = false;
  bstring dummy_request = bstrcpy(request);
  if (dummy_request == NULL) {
    log_error("unable to create a dummy request string\n", args->thread_number);
    unable_to_create_dummy_request = true;
  }
  if (newline_dummy(dummy_request) != 0) {
    log_error("unable to replace newlines with dummys\n", args->thread_number);
    unable_to_create_dummy_request = true;
  }

  log_debug("request:", args->thread_number);
  if (unable_to_create_dummy_request) {
    fprintf(logging_file, " %s", request->data);
  } else {
    fprintf(logging_file, " %s", dummy_request->data);
  }
  fflush(logging_file);

  // Creating a response and sending it
  bstring response = bfromcstr("HTTP/1.1 200 OK" RN "Content-Type: "
                               "text/html" RN "Content-Length: 5" DRN "hello");
  if (response == NULL) {
    log_error("unablet to create response string", args->thread_number);
    goto close_without_response;
  }
  ret = send(args->socket, response->data, response->slen, 0);
  if (ret < 0) {
    log_info("failed to send data\n", args->thread_number);
  }
  log_info("sent response to client\n", args->thread_number);
  log_debug("response: ", args->thread_number);
  if (newline_dummy(response) != 0) {
    log_error("unable to replace newlines with dummys\n", args->thread_number);
  }
  fprintf(logging_file, " %s\n", response->data);
  fflush(logging_file);

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
    log_error("Failed to set non-blocking mode:");
    fprintf(logging_file, " %d\n", OTHER_FUCNTION_THREAD_NUM);
    fflush(logging_file);
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

  clean_on_exit.binded_sock = false;

  logging_file = fopen(LOG_FILE, "w+");
  if (logging_file == NULL) {
    logging_file = stdout;
    log_error("unable to open logging file, using stdout as fallback",
              MAIN_FUCNTION_THREAD_NUM);
  }
  if (atexit(exit_clean) != 0) {
    log_critical("unable to set exit function\n", MAIN_FUCNTION_THREAD_NUM);
  }

  if (signal(SIGINT, handle_sig) == SIG_ERR) {
    log_critical("unable to set signal function\n", MAIN_FUCNTION_THREAD_NUM);
    exit(EXIT_FAILURE);
  }
  if (signal(SIGTERM, handle_sig) == SIG_ERR) {
    log_critical("unable to set signal function\n", MAIN_FUCNTION_THREAD_NUM);
    exit(EXIT_FAILURE);
  }

  if (memset(&clean_on_exit.thread_in_use, false,
             sizeof(clean_on_exit.thread_in_use)) !=
      &clean_on_exit.thread_in_use) {
    log_error("unable to use memset\n", MAIN_FUCNTION_THREAD_NUM);
  }
  if (memset(&clean_on_exit.threads, -1, sizeof(clean_on_exit.threads)) !=
      &clean_on_exit.threads) {
    log_error("unable to use memset\n", MAIN_FUCNTION_THREAD_NUM);
  }

  drn_bstr = (struct tagbstring)bsStatic(DRN);
  dn_bstr = (struct tagbstring)bsStatic(DN);
  rn_bstr_dummy = (struct tagbstring)bsStatic("\\r\\n");
  n_bstr_dummy = (struct tagbstring)bsStatic("\\n");
  n_bstr = (struct tagbstring)bsStatic("\n");
  rn_bstr = (struct tagbstring)bsStatic(RN);
  get_bstr = (struct tagbstring)bsStatic("GET");
  head_bstr = (struct tagbstring)bsStatic("HEAD");
  post_bstr = (struct tagbstring)bsStatic("POST");
  put_bstr = (struct tagbstring)bsStatic("PUT");
  delete_bstr = (struct tagbstring)bsStatic("DELETE");
  connect_bstr = (struct tagbstring)bsStatic("CONNECT");
  options_bstr = (struct tagbstring)bsStatic("OPTIONS");
  trace_bstr = (struct tagbstring)bsStatic("TRACE");
  patch_bstr = (struct tagbstring)bsStatic("PATCH");

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
  if (memset(&addr, 0, sizeof(struct sockaddr_in)) != &addr) {
    log_error("unable to memset &addr", MAIN_FUCNTION_THREAD_NUM);
  }
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  // addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  ret = bind(sockfd, (struct sockaddr *)&addr, sizeof(addr));
  if (ret < 0) {
    int err = errno;
    log_critical("could not bind with errno:", MAIN_FUCNTION_THREAD_NUM);
    fprintf(logging_file, " %i\n", err);
    fflush(logging_file);
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
      if (program_should_close) {
        log_info("recieved signal", MAIN_FUCNTION_THREAD_NUM);
        fprintf(logging_file, " (%i) closing server\n", closing_sig);
        fflush(logging_file);
        exit(EXIT_SUCCESS);
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
        if (args == NULL) {
          log_critical("unable to allocate space for thread arguments\n",
                       MAIN_FUCNTION_THREAD_NUM);
          break;
        }
        args->socket = confd;
        args->thread_number = i;
        pthread_create(&clean_on_exit.threads[i], NULL, handle_request, args);
        break;
      } else if (i + 1 == THREADS) {
        i = -1; // Setting i to -1 so that it will be 0 during the next
                // iteration.
        SLEEP(THREAD_FREE_WAIT_MILS);
      }
    }
  }

  // If the code goes here something went wrong in the main loop.
  // The code should be terminated with exit() function in the main loop or
  // ohther functions.
  return EXIT_FAILURE;
}
