#ifndef HTTP_SERVER_IN_C_CONST_STR_H
#define HTTP_SERVER_IN_C_CONST_STR_H

#include "bstrlib/bstrlib.h"

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

struct tagbstring http_100 = bsStatic("Continue");
struct tagbstring http_101 = bsStatic("Switching Protocols");
struct tagbstring http_200 = bsStatic("OK");
struct tagbstring http_201 = bsStatic("Created");
struct tagbstring http_204 = bsStatic("No Content");
struct tagbstring http_301 = bsStatic("Moved Permanently");
struct tagbstring http_302 = bsStatic("Found");
struct tagbstring http_304 = bsStatic("Not Modified");
struct tagbstring http_400 = bsStatic("Bad Request");
struct tagbstring http_401 = bsStatic("Unauthorized");
struct tagbstring http_403 = bsStatic("Forbidden");
struct tagbstring http_404 = bsStatic("Not Found");
struct tagbstring http_500 = bsStatic("Internal Server Error");
struct tagbstring http_502 = bsStatic("Bad Gateway");
struct tagbstring http_503 = bsStatic("Service Unavailable");
struct tagbstring http_version11 = bsStatic("HTTP/1.1");

struct tagbstring root_path = bsStatic("/");
struct tagbstring text_html = bsStatic("text/html");

#endif
