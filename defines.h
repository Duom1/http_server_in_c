#ifndef HTTP_SERVER_IN_C_DEFINES_H
#define HTTP_SERVER_IN_C_DEFINES_H

#include "bstrlib/bstrlib.h"

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

#define bsStaticList(a)                                                        \
  (struct tagbstring){(-__LINE__), (int)strlen(a) - 1, (unsigned char *)a};

#define RN "\r\n"
#define DRN "\r\n\r\n"
#define DN "\n\n"

#define NON_BLOCKING_EXTRA_SLEEP_MILS                                          \
  (100) // Sleep time between nonblocking socket calls
#define REQUEST_TIMEOUT_AFTER_SEC                                              \
  (20) // Time out amount if proper request is not recieved
#define OTHER_FUNCTION_THREAD_NUM                                              \
  (-2) // the thread number of other fucntions like exit_clean and handle_sigint
#define MAIN_FUCNTION_THREAD_NUM (-1) // the thread number of main function
#define RESPONSE_CONTENT_STRLEN                                                \
  (20) // Length of the ascii representation of the response content length.
#define THREAD_FREE_WAIT_MILS                                                  \
  (100) // The time program waits to check if a thread
        // is available
#define RESPONSE_CODE_STRLEN                                                   \
  (5) // Length of the ascii representaion of response status code.
#define CONSTANT_STRING_AMT (6) // the amount of constant strings
#define EXIT_PRINTF_ERROR                                                      \
  (-20) // error code return if the program is unable to use printf
#define CONNECTION_AMT (10) // The amount of connections given to listen()
#define BUFFER_SIZE (1024)  // Size of the request buffer.
#define MAX_BUFFER                                                             \
  (10) // MAX_BUFFER * BUFFER_SIZE is the mazimum size of a request
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

const char *http_request_strings[HTTP_ENUM_MAX] = {
    "GET",     "HEAD",    "POST",  "PUT",   "DELETE",
    "CONNECT", "OPTIONS", "TRACE", "PATCH",
};

typedef struct http_response_t {
  bstring version;
  int code;
  bstring message;
  struct { // this uses the stb ds library string hashmap
    char *key;
    bstring value;
  } *headers;
  bstring content;
} http_response_t;

typedef struct http_request_t {
  enum Http_request_types type;
  bstring path;
  bstring version;
  struct { // this uses the stb ds library string hashmap
    char *key;
    bstring value;
  } *headers;
} http_request_t;

#endif
