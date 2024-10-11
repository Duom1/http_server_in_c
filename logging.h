#ifndef LOGGING_H
#define LOGGING_H

// Variables that are not defined here are defined in the main.c file
// because they are globas that are also needed in the main file.

#include "defines.h"
#include "stb_ds.h"
#include <stdio.h>
#include <time.h>

void fprint_request_struct(http_request_t *request, FILE *file) {
  fprintf(file, "type: %s, path: %s, version: %s",
          http_request_strings[request->type], request->path->data,
          request->version->data);
  int header_len = shlen(request->headers);
  if (!(header_len > 0)) {
    fprintf(file, "\n");
    return;
  }
  for (int i = 0; i < header_len; i++) {
    fprintf(file, ", %s: %s", request->headers[i].key,
            request->headers[i].value->data);
  }
  fprintf(file, "\n");
}

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
    log_critical("unable to get proper time", OTHER_FUNCTION_THREAD_NUM);
  }
  struct tm *lt = localtime(&ut);
  struct tm backup;
  if (lt == NULL) {
    lt = &backup;
    memset(&backup, 0, sizeof(struct tm));
  }
  ret = fprintf(logging_file, "[%i/%i/%i %i:%i:%i] [%i] [thread: %i] [%s]: %s",
                lt->tm_mday, lt->tm_mon + 1, lt->tm_year + 1900, lt->tm_hour,
                lt->tm_min, lt->tm_sec, (int)ut, thread_num, tag, msg);
  if (ret < 0) {
    exit(EXIT_PRINTF_ERROR);
  }
  fflush(logging_file);
}

#endif
