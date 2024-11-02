#ifndef HTTP_SERVER_IN_C_CLODING_SIGNALS_H
#define HTTP_SERVER_IN_C_CLODING_SIGNALS_H

#include "logging.h"

void handle_closing_sig(int sig) {
  closing_sig = sig;
  program_should_close = true;
}

int newline_dummy(bstring string) {
  int ret;
  ret = bfindreplace(string, &rn_bstr, &rn_bstr_dummy, 0);
  if (ret != BSTR_OK) {
    log_error("unable to replace newlines in request",
              OTHER_FUNCTION_THREAD_NUM);
    return -1;
  }
  ret = bfindreplace(string, &n_bstr, &n_bstr_dummy, 0);
  if (ret != BSTR_OK) {
    log_error("unable to replace newlines in request",
              OTHER_FUNCTION_THREAD_NUM);
    return -1;
  }
  return 0;
}

#endif
