#ifndef _HTTP_H_
#define _HTTP_H_

#include <stdlib.h>
#include <stdbool.h>

#define HTTP_DEFAULT_TASKS      0

#define HTTP_GET    1
#define HTTP_POST   2
#define HTTP_PUT    3
#define HTTP_PATCH  4
#define HTTP_DELETE 5

#define HTTP_100_CONTINUE           0
#define HTTP_200_OK                 1
#define HTTP_201_CREATED            2
#define HTTP_204_NO_CONTENT         3
#define HTTP_301_MOVED_PERMANENTLY  4
#define HTTP_302_FOUND              5
#define HTTP_303_SEE_OTHER          6
#define HTTP_304_NOT_MODIFIED       7
#define HTTP_307_TEMPORARY_REDIRECT 8
#define HTTP_308_PERMANENT_REDIRECT 9
#define HTTP_400_BAD_REQUEST        10
#define HTTP_401_UNAUTHORIZED       11
#define HTTP_403_FORBIDDEN          12
#define HTTP_404_NOT_FOUND          13
#define HTTP_405_NOT_ALLOWED        14
#define HTTP_408_REQUEST_TIMEOUT    15

typedef struct http_request_t
{
    int type;
    const char* method;
    const char* url;
    const char* url_suffix;
    const char* args;
    const char* version;
    const char* body;
    const char* raw;
}http_request_t;

typedef struct http_response_t {
    int code;
    char* status_line;
    char content_type[128];
    char location[128];
    size_t header_size;
    size_t header_buf_size;
    char* header_buf;
    size_t payload_size;
    bool clean_payload;
    char* payload;
}http_response_t;

typedef struct http_server_t http_server_t;


http_server_t* http_server_init(const char* ip, int port, int tasks, const char* fullchain, const char* privatekey);

void http_server_clean(http_server_t** this);

int http_server_start(http_server_t* this, size_t max_connections);

void http_server_stop(http_server_t* this);

int http_register_method(http_server_t* this, const char* path, int type, void (*method)(http_request_t*, http_response_t*));

void http_set_response_code(http_response_t* response, int code);

void http_add_header_entry(http_response_t* response, const char* name, const char* value);

void http_set_content_type(http_response_t* response, const char* type, bool use_charset);

void http_set_redirect(http_response_t* response, const char* location);

void http_set_body(http_response_t* response, size_t len, const char* body, bool clean);

void http_set_cookie(http_response_t* response, const char* cookie, const char* value);

const char* http_get_cookie(http_request_t* request, const char* cookie);

#endif