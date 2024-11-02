#define _POSIX_C_SOURCE 199309L
#ifdef _WIN32
#define HAVE_STRUCT_TIMESPEC
#endif
#include <ctype.h>
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

// The following code has been split into header files
// for easier reading.
// clang-format off
#include "defines.h"
#include "const_str.h"

FILE *logging_file;
bool disable_debug_log = false;

#include "logging.h"

int closing_sig = 0;
bool program_should_close = false;

#include "exit_clean.h"
#include "closing_signals.h"
// clang-format on

// Does not validate path.
// Returns 0 if successfull.
//
// This function takes in the request string and splits it into
// lines. It the splits the first line from the space characters
// and gets the three values such as path, version and method. It
// then takes all the other lines and splits them and adds them
// to the headers hash table.
int parse_http_request(http_request_t *request, bstring requets_str) {
  if (requets_str == NULL) {
    log_error("request string is null", OTHER_FUNCTION_THREAD_NUM);
    return -1;
  }
  struct bstrList *lines = NULL;
  lines = bsplit(requets_str, '\n');
  if (lines == NULL) {
    log_error("unable to parse request string: pointer is null\n",
              OTHER_FUNCTION_THREAD_NUM);
    return -1;
  }
  if (lines->qty < 1) {
    log_error("unable to parse request string: qty is less than 1\n",
              OTHER_FUNCTION_THREAD_NUM);
    bstrListDestroy(lines);
    return -1;
  }

  // Removing newlines and carridgereturns from all lines.
  for (int i = 0; i < lines->qty; i++) {
    bstring item = lines->entry[i];
    if (bchar(item, item->slen - 1) == '\r') {
      btrunc(item, item->slen - 1);
    }
    if (bchar(item, item->slen) == '\n') {
      btrunc(item, item->slen);
    }
  }

  struct bstrList *first_line = NULL;
  first_line = bsplit(lines->entry[0], ' ');
  if (first_line == NULL) {
    log_error("unable to parse request string first line: pointer is null\n",
              OTHER_FUNCTION_THREAD_NUM);
    bstrListDestroy(lines);
    return -1;
  }
  if (first_line->qty < 3) {
    log_error(
        "unable to parse request string first line: qty is leass than 3\n",
        OTHER_FUNCTION_THREAD_NUM);
    bstrListDestroy(lines);
    bstrListDestroy(first_line);
    return -1;
  }

  request->path = bstrcpy(first_line->entry[1]);
  if (request->path == NULL) {
    log_error("unable copy path\n", OTHER_FUNCTION_THREAD_NUM);
    return -1;
  }
  request->version = bstrcpy(first_line->entry[2]);
  if (request->version == NULL) {
    log_error("unable copy version\n", OTHER_FUNCTION_THREAD_NUM);
    return -1;
  }

  // propably should make a function of this in the future
  if (binstr(first_line->entry[0], 0, &get_bstr) >= 0) {
    request->type = HTTP_GET;
  } else if (binstr(first_line->entry[0], 0, &head_bstr) >= 0) {
    request->type = HTTP_HEAD;
  } else if (binstr(first_line->entry[0], 0, &options_bstr) >= 0) {
    request->type = HTTP_OPTIONS;
  } else if (binstr(first_line->entry[0], 0, &trace_bstr) >= 0) {
    request->type = HTTP_TRACE;
  } else if (binstr(first_line->entry[0], 0, &put_bstr) >= 0) {
    request->type = HTTP_PUT;
  } else if (binstr(first_line->entry[0], 0, &delete_bstr) >= 0) {
    request->type = HTTP_DELETE;
  } else if (binstr(first_line->entry[0], 0, &post_bstr) >= 0) {
    request->type = HTTP_POST;
  } else if (binstr(first_line->entry[0], 0, &patch_bstr) >= 0) {
    request->type = HTTP_PATCH;
  } else if (binstr(first_line->entry[0], 0, &connect_bstr) >= 0) {
    request->type = HTTP_CONNECT;
  } else {
    log_error("unable to get http request method\n", OTHER_FUNCTION_THREAD_NUM);
    bstrListDestroy(lines);
    bstrListDestroy(first_line);
    return -1;
  }

  sh_new_strdup(request->headers);
  for (int i = 1; i < lines->qty; i++) {
    struct bstrList *split_pair = NULL;
    split_pair = bsplit(lines->entry[i], ':');
    if (split_pair == NULL) {
      continue;
    }
    if (split_pair->qty < 2) {
      bstrListDestroy(split_pair);
      continue;
    }
    bstring value =
        bstrcpy(split_pair->entry[1]); // TODO: kailikke bstrcpy jutulle pitäis
                                       // tehä erro cheking
    char *key = malloc(split_pair->entry[0]->slen +
                       1); // plus one for the null terminator
    if (key == NULL) {
      log_error("unable to allocate space for key\n",
                OTHER_FUNCTION_THREAD_NUM);
      bstrListDestroy(split_pair);
      continue;
    }
    strcpy(key, (const char *)split_pair->entry[0]->data);
    if (isspace(bchar(value, 0))) {
      bdelete(value, 0, 1);
    }
    shput(request->headers, key, value); // TODO: erro cheking for this
    bstrListDestroy(split_pair);
  }

  bstrListDestroy(lines);
  bstrListDestroy(first_line);
  return 0;
}

// TODO: erro chekcing
// It is expected that confd is a valit conenction.
int send_response(http_response_t *res, int confd) {
  // TODO: null cheking for res
  int ret;
  // TODO: errro checking for all of these
  bstring response = bfromcstr("");
  bconcat(response, res->version);
  bconchar(response, ' ');
  char code_char[RESPONSE_CODE_STRLEN];
  sprintf(code_char, "%i", res->code); // TODO: error cheking
  bcatcstr(response, code_char);       // TODO: error cheking
  if (res->message != NULL) {
    bconchar(response, ' ');         // TODO: error cheking
    bconcat(response, res->message); // TODO: error cheking
  }
  if (res->content != NULL) {
    if (res->headers == NULL) {
      sh_new_strdup(res->headers); // TODO: can these be error cheked
    }
    char content_char[RESPONSE_CONTENT_STRLEN];
    sprintf(content_char, "%i", res->content->slen); // TODO: error cheking
    bstring content_bstr = bfromcstr(content_char);  // TODO: error cheking
    shput(res->headers, "content-length",
          content_bstr); // TODO: can ?error cheking
  }

  bconcat(response, &rn_bstr); // TODO: error cheking

  int headers_len = shlen(res->headers);
  for (int i = 0; i < headers_len; i++) {
    bcatcstr(response, res->headers[i].key); // TODO: error cheking
    // TODO: make this into one
    bconchar(response, ':');                  // TODO: error cheking
    bconchar(response, ' ');                  // TODO: error cheking
    bconcat(response, res->headers[i].value); // TODO: error cheking
    bconcat(response, &n_bstr);               // TODO: error cheking
  }

  bconcat(response, &rn_bstr);

  bconcat(response, res->content);

  if (response == NULL) {
    log_error("unable to create response string", OTHER_FUNCTION_THREAD_NUM);
    return -1;
  }
  ret = send(confd, response->data, response->slen, 0);
  if (ret < 0) {
    log_info("failed to send data\n", OTHER_FUNCTION_THREAD_NUM);
    bdestroy(response);
    return -1;
  }
  log_info("response: ", OTHER_FUNCTION_THREAD_NUM);
  if (newline_dummy(response) != 0) {
    log_error("unable to replace newlines with dummys\n",
              OTHER_FUNCTION_THREAD_NUM);
  }
  fprintf(logging_file, " %s\n", response->data);
  fflush(logging_file);

  bdestroy(response);
  return 0;
}

int create_response(http_request_t *req, http_response_t *res) {
  if (req == NULL || res == NULL) {
    log_error("request and/or response given is null\n",
              OTHER_FUNCTION_THREAD_NUM);
    return -1;
  }
  sh_new_strdup(
      res->headers); // TODO: potential erro for cheking for all of these
  if (req->type == HTTP_GET) {
    if (bstrcmp(req->path, &root_path) == 0) {
      res->code = 200;
      res->message = &http_200;
      res->version = &http_version11;
      res->content = bfromcstr("hello world");
      shput(res->headers, "content-type", &text_html);
      return 0;
    }
  }
  res->code = 404;
  res->message = &http_404;
  res->version = &http_version11;
  res->content = &http_404;
  shput(res->headers, "content-type",
        &text_html); // TODO: potential erro for cheking for all of these
  return 0;
}

typedef struct handle_request_args_t {
  int socket;
  int thread_number;
} handle_request_args_t;

void *handle_request(void *inargs) {
  handle_request_args_t *args = (handle_request_args_t *)inargs;
  clean_on_exit_t.thread_in_use[args->thread_number] = true;
  int ret;

  bstring request_str = bfromcstr("");
  if (request_str == NULL) {
    log_critical("could not create string for request\n", args->thread_number);
    free(inargs);
    return NULL;
  }

  char *buf = malloc(BUFFER_SIZE);
  if (buf == NULL) {
    log_critical("unable to allocate space for buffer", args->thread_number);
    bdestroy(request_str);
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
    ret = bcatcstr(request_str, buf);
    if (ret != BSTR_OK) {
      log_error("failed to concat string to request string\n",
                args->thread_number);
    }

    // Cheking if the request has come to an end.
    // The staring position for the search is cur_it * BUFFER_SIZE
    // so that we don't read the first parts of the request multiple times
    if ((binstr(request_str, cur_it * BUFFER_SIZE, &drn_bstr) > 0) ||
        (binstr(request_str, cur_it * BUFFER_SIZE, &dn_bstr) > 0)) {
      break;
    }

    // Cheking if the request is longer than needed.
    if (request_str->slen > MAX_BUFFER * BUFFER_SIZE) {
      log_info("request exceeded", args->thread_number);
      fprintf(logging_file, " %i bytes\n", MAX_BUFFER * BUFFER_SIZE);
      fflush(logging_file);
      goto close_without_response;
    }

    cur_it++;
  }

  // making a dummy string so that it can be logged better
  bool unable_to_create_dummy_request = false;
  bstring dummy_request = bstrcpy(request_str);
  if (dummy_request == NULL) {
    log_error("unable to create a dummy request string\n", args->thread_number);
    unable_to_create_dummy_request = true;
  }
  if (newline_dummy(dummy_request) != 0) {
    log_error("unable to replace newlines with dummys\n", args->thread_number);
    unable_to_create_dummy_request = true;
  }

  log_info("request:", args->thread_number);
  if (unable_to_create_dummy_request) {
    fprintf(logging_file, " %s\n", request_str->data);
  } else {
    fprintf(logging_file, " %s\n", dummy_request->data);
  }
  fflush(logging_file);

  bdestroy(dummy_request);

  http_request_t request = {-1, NULL, NULL, NULL};
  ret = parse_http_request(&request, request_str);
  if (ret != 0) {
    log_error("unable to parse request\n", args->thread_number);
    goto close_without_response;
  }

  log_debug("request parsed: ", args->thread_number);
  fprint_request_struct(&request, logging_file);
  fflush(logging_file);

  http_response_t response = {NULL, -1, NULL, NULL, NULL};
  ret = create_response(&request, &response);
  if (ret != 0) {
    log_error("error creating a response\n", args->thread_number);
  } else {
    ret = send_response(&response, args->socket);
  }

close_without_response:

  if (request.path != NULL) {
    bdestroy(request.path);
  }
  if (request.version != NULL) {
    bdestroy(request.version);
  }
  int tmp_len;
  if (request.headers != NULL) {
    tmp_len = shlen(request.headers);
    for (int i = 0; i < tmp_len; i++) {
      bdestroy(request.headers[i].value);
    }
    shfree(request.headers);
  }

  // These are not necesary as long as they are stored on the stack.
  /* bdestroy(response.version); */
  /* bdestroy(response.message); */
  if (response.content != NULL) {
    bdestroy(response.content);
  }
  if (response.headers != NULL) {
    tmp_len = shlen(response.headers);
    for (int i = 0; i < tmp_len; i++) {
      bdestroy(response.headers[i].value);
    }
    shfree(response.headers);
  }

  free(buf);
  bdestroy(request_str);
  CLOSESOCKET(args->socket);

  clean_on_exit_t.thread_in_use[args->thread_number] = false;
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
    log_error("Failed to set non-blocking mode:", OTHER_FUNCTION_THREAD_NUM);
    fprintf(logging_file, " %d\n", OTHER_FUNCTION_THREAD_NUM);
    fflush(logging_file);
    return -1;
  }
#else
  int flags = fcntl(sock, F_GETFL, 0);
  if (flags == -1) {
    log_error("fcntl(F_GETFL) failed\n", OTHER_FUNCTION_THREAD_NUM);
    return -1;
  }
  if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
    log_error("fcntl(F_SETFL) failed\n", OTHER_FUNCTION_THREAD_NUM);
    return -1;
  }
#endif

  return 0;
}

int main(void) {
  int ret;
  int port = PORT;

  clean_on_exit_t.binded_sock = false;

  logging_file = fopen(LOG_FILE, "w+");
  if (logging_file == NULL) {
    logging_file = stdout;
    log_error("unable to open logging file, using stdout as fallback",
              MAIN_FUCNTION_THREAD_NUM);
  }
  if (atexit(exit_clean) != 0) {
    log_critical("unable to set exit function\n", MAIN_FUCNTION_THREAD_NUM);
  }

  if (signal(SIGINT, handle_closing_sig) == SIG_ERR) {
    log_critical("unable to set signal function\n", MAIN_FUCNTION_THREAD_NUM);
    exit(EXIT_FAILURE);
  }
  if (signal(SIGTERM, handle_closing_sig) == SIG_ERR) {
    log_critical("unable to set signal function\n", MAIN_FUCNTION_THREAD_NUM);
    exit(EXIT_FAILURE);
  }

  if (memset(&clean_on_exit_t.thread_in_use, false,
             sizeof(clean_on_exit_t.thread_in_use)) !=
      &clean_on_exit_t.thread_in_use) {
    log_error("unable to use memset\n", MAIN_FUCNTION_THREAD_NUM);
  }
  if (memset(&clean_on_exit_t.threads, -1, sizeof(clean_on_exit_t.threads)) !=
      &clean_on_exit_t.threads) {
    log_error("unable to use memset\n", MAIN_FUCNTION_THREAD_NUM);
  }

  drn_bstr = (struct tagbstring)bsStatic(DRN);
  dn_bstr = (struct tagbstring)bsStatic(DN);
  rn_bstr_dummy = (struct tagbstring)bsStatic("\\r\\n");
  n_bstr_dummy = (struct tagbstring)bsStatic("\\n");
  n_bstr = (struct tagbstring)bsStatic("\n");
  rn_bstr = (struct tagbstring)bsStatic(RN);

#define http_string(a) bsStaticList(http_request_strings[a])
  get_bstr = http_string(HTTP_GET);
  head_bstr = http_string(HTTP_HEAD);
  post_bstr = http_string(HTTP_POST);
  put_bstr = http_string(HTTP_PUT);
  delete_bstr = http_string(HTTP_DELETE);
  connect_bstr = http_string(HTTP_CONNECT);
  options_bstr = http_string(HTTP_OPTIONS);
  trace_bstr = http_string(HTTP_TRACE);
  patch_bstr = http_string(HTTP_PATCH);
#undef http_string

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
  clean_on_exit_t.binded_sock = true;
  clean_on_exit_t.sockfd = sockfd;

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
      if (!clean_on_exit_t.thread_in_use[i]) {
        handle_request_args_t *args = malloc(sizeof(handle_request_args_t));
        if (args == NULL) {
          log_critical("unable to allocate space for thread arguments\n",
                       MAIN_FUCNTION_THREAD_NUM);
          break;
        }
        args->socket = confd;
        args->thread_number = i;
        pthread_create(&clean_on_exit_t.threads[i], NULL, handle_request, args);
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
