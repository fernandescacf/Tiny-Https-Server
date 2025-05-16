#include "http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdatomic.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <openssl/ssl.h>
#include <async.h>
#include <queue.h>
#include <lock.h>

// NOTE: we are not expanding the arrays that store open connections we might want to change this in the future
// NOTE: we are not preventing a connection from misbehaving (send huge data blocks or spamming requests)
//       -> in case of huge data blocks we can just cap it and flag the connection to be terminated -> would be detected in http_request_process
//       -> in case of spamming we could get a metric like requests per second -> would be detected in http_server_worker

#define DEFAULT_BUFFER_SIZE     (4096)
//#define MAX_REQUEST_SIZE        (DEFAULT_BUFFER_SIZE * 16)
#define STR_LEN(str)   (sizeof(str) - 1)

static const struct HTTP_RESPONSES
{
    int code;
    char* reason;
}HttpResponses[] = {
    {.code = 100, .reason = "Continue"},
    {.code = 200, .reason = "OK"},
    {.code = 201, .reason = "Created"},
    {.code = 204, .reason = "No Content"},
    {.code = 301, .reason = "Moved Permanently"},
    {.code = 302, .reason = "Found"},
    {.code = 303, .reason = "See Other"},
    {.code = 304, .reason = "Not Modified"},
    {.code = 307, .reason = "Temporary Redirect"},
    {.code = 308, .reason = "Permanent Redirect"},
    {.code = 400, .reason = "Bad Request"},
    {.code = 401, .reason = "Unauthorized"},
    {.code = 403, .reason = "Forbidden"},
    {.code = 404, .reason = "Not Found"},
    {.code = 405, .reason = "Method Not Allowed"},
    {.code = 408, .reason = "Request Timeout"},
};

typedef struct pathname_t pathname_t;
struct pathname_t {
    void (*get)(http_request_t*, http_response_t*);
    void (*post)(http_request_t*, http_response_t*);
    void (*put)(http_request_t*, http_response_t*);
    void (*patch)(http_request_t*, http_response_t*);
    void (*delete)(http_request_t*, http_response_t*);

    size_t paths_count;
    pathname_t** paths;

    size_t len;
    char path[1];
};

typedef struct connection_t {
    int fd;
    SSL* ssl;
    int timeout;
    // Other flags and data
    lock_t lock;
    bool running;
    queue_t* requests;
}connection_t;

struct http_server_t {
    int domain;
    int type;
    int protocol;
    int bakclog;
    int timeout;
    int port;
    const char* ip;

    const char* fullchain;
    const char* privatekey;
    SSL_CTX* ctx;

    int fd;
    struct sockaddr_in serverAddr;
    nfds_t maxConnections;
    nfds_t nfds;
    struct pollfd* pfds;
    connection_t* connections;
    
    atomic_bool active;
    asyncTask_t* listener;
    pathname_t* root;
};

typedef struct rcv_data_t {
    size_t size;
    size_t bytes_received;
    char* payload;
} rcv_data_t;

typedef struct request_t {
    connection_t* con;
    http_server_t* server;
    rcv_data_t* data;
} request_t;


/************************** PRIVATE METHODS **************************/

static pathname_t* http_init_url_paths()
{
    pathname_t* path = (pathname_t*)malloc(sizeof(pathname_t));
    path->get = NULL;
    path->post = NULL;

    path->paths_count = 0;
    path->paths = NULL;

    path->len = 0;
    path->path[0] = 0;

    return path;
}

static void http_url_path_clean(pathname_t* root)
{
    if(root == NULL) return;

    for(size_t i = 0; i < root->paths_count; ++i) {
        http_url_path_clean(root->paths[i]);
    }
    free(root->paths);
    free(root);
}

static pathname_t* http_create_url_path(const char* base_path, const size_t len)
{
    pathname_t* path = (pathname_t*)malloc(sizeof(pathname_t) + len);
    path->get = NULL;
    path->post = NULL;

    path->paths_count = 0;
    path->paths = NULL;

    path->len = len;
    memcpy(path->path, base_path, len);

    return path;
}

static void http_add_child_path(pathname_t* base_path, pathname_t* child_path)
{
    base_path->paths_count += 1;
    base_path->paths = (pathname_t**)realloc(base_path->paths, sizeof(pathname_t*) * base_path->paths_count);
    base_path->paths[base_path->paths_count - 1] = child_path;
}

static pathname_t* http_resolve_path(pathname_t* base_path, const char* path, const size_t len, char** remaining)
{
    pathname_t* current_path = base_path;
    size_t pos = 0;
    bool search = true;

    if(remaining != NULL) *remaining = NULL;

    while(search)
    {
        // skip all '/'
        for(; pos < len && path[pos] == '/'; ++pos);
        // If all path was consume we found the right path
        if((len - pos) == 0) return current_path;

        // No more paths to check
        if(current_path->paths_count == 0) break;
        
        // If no path is matched we break the loop
        search = false;

        for(size_t child = 0; child < current_path->paths_count; ++child)
        {
            pathname_t* temp = current_path->paths[child];
            if(temp->len > (len - pos)) continue;
            
            if(temp->len < (len - pos))
            {
                if(memcmp(temp->path, &path[pos], temp->len) == 0 && path[pos + temp->len] == '/')
                {
                    current_path = temp;
                    pos += temp->len;
                    search = true;
                    break;
                }
                continue;
            }

            if(temp->len == (len - pos) && memcmp(temp->path, &path[pos], (len - pos)) == 0)
            {
                return temp;
            }
        }
    }
    if(remaining) *remaining = (char*)&path[pos];
    return current_path;
}

static bool http_open_connection(connection_t* connection, int fd, int timeout, SSL_CTX* ctx, const char* fullchain, const char* privatekey) {
    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    SSL_use_certificate_chain_file(ssl, fullchain);
    SSL_use_PrivateKey_file(ssl, privatekey, SSL_FILETYPE_PEM);
    if(SSL_accept(ssl) == 1) {
        connection->fd = fd;
        connection->timeout = (10 * 1000) / timeout;
        connection->ssl = ssl;
        connection->requests = queue_create(10);
        connection->lock = LOCK_INITIALIZER;
        connection->running = false;
        return true;
    }
    else {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        return false;
    }
}

static void http_close_connection(connection_t* connection) {
    close(connection->fd);
    connection->fd = -1;
    connection->timeout = -1;
    SSL_shutdown(connection->ssl);
    SSL_free(connection->ssl);
    connection->ssl = NULL;
    queue_destroy(&connection->requests);
    lock_destroy(&connection->lock);
}

static size_t http_get_request_header(request_t* request) {
    // We will first assume that we already received everything
    bool is_header_completed = false;
    size_t header_end = 0;

    while(is_header_completed == false) {
        // Check if we received the complete header
        if(request->data->payload[request->data->bytes_received - 4] == '\r' &&
           request->data->payload[request->data->bytes_received - 3] == '\n' &&
           request->data->payload[request->data->bytes_received - 2] == '\r' &&
           request->data->payload[request->data->bytes_received - 1] == '\n')
        {
            is_header_completed = true;
            header_end = request->data->bytes_received;
        }
        else {
            char* hdr_end = strstr(request->data->payload, "\r\n\r\n");
            if(hdr_end) {
                is_header_completed = true;
                header_end = ((hdr_end + 4) - request->data->payload);
            }
        }

        if(is_header_completed == false) {
            // TODO: why not get the next package and use it's size to update the payload size?
            request->data->size *= 2;
            request->data->payload = realloc(request->data->payload, request->data->size);
            // Get more data
            rcv_data_t* data = pop(request->con->requests);
            memcpy(request->data->payload + request->data->bytes_received, data->payload, data->bytes_received);
            request->data->bytes_received += data->bytes_received;
            free(data->payload);
            free(data);
        }
    }
    return header_end;
}

static size_t http_get_request_body(request_t* request, size_t header_end) {
    // We know that we have the complete header but do we have the complete request?
    char* temp = strstr(request->data->payload, "Content-Length: ");
    if(temp == NULL) return 0;

    size_t content_len = atoi(temp + STR_LEN("Content-Length: "));

    if(request->data->bytes_received == (header_end + content_len)) {
        if(request->data->bytes_received == request->data->size) {
            // This is a shit we will have to realloc because of 1 byte
            request->data->size += 1;
            request->data->payload = realloc(request->data->payload, request->data->size);
        }
        request->data->payload[header_end + content_len] = 0;
        return content_len;
    }

    request->data->size = header_end + content_len + 1;
    request->data->payload = realloc(request->data->payload, request->data->size);
    // Get more data
    while(request->data->bytes_received < (request->data->size - 1)) {
        printf("http_get_request_body: untested!!!!\n");
        rcv_data_t* data = pop(request->con->requests);
        memcpy(request->data->payload + request->data->bytes_received, data->payload, data->bytes_received);
        request->data->bytes_received += data->bytes_received;
        free(data->payload);
        free(data);
    }
    request->data->payload[header_end + content_len] = 0;
    return content_len;
}

static void http_parse_header(http_request_t* out_request, request_t* in_request) {
    char* temp = NULL;
    out_request->method = strtok_r(in_request->data->payload, " ", &temp);
    out_request->url = strtok_r(NULL, " ", &temp);
    out_request->version = strtok_r(NULL, " \r\n", &temp);
    out_request->raw = out_request->version + strlen(out_request->version) + 1;

    // Get parameters if any
    for(size_t i = 0; out_request->url[i]; ++i) {
        if(out_request->url[i] == '?') {
            out_request->args = &out_request->url[i + 1];
            ((char*)out_request->url)[i] = 0;
            break;
        }
    }
}

static void http_process_request(asyncTask_t* self, request_t* in_request) {
    while(true) {
        size_t header_end = http_get_request_header(in_request);
        (void)http_get_request_body(in_request, header_end);

        http_request_t request = {0};
        http_parse_header(&request, in_request);
        request.body = &in_request->data->payload[header_end];

        // Resolve path and get url suffix if there is one
        pathname_t* root = in_request->server->root;
        pathname_t* path = http_resolve_path(root, request.url, strlen(request.url), (char**)&request.url_suffix);

        // We will always have a path if it's invalid the method function will have to return the error
        http_response_t response = {0};

        if(strlen(request.method) == 3 && strcmp(request.method, "GET") == 0) {
            if(path->get == NULL) {
                http_set_response_code(&response, HTTP_405_NOT_ALLOWED);
                http_set_content_type(&response, "text/html", true);
                http_set_body(&response, 23, "Method Get not allowed", false);
            }
            else {
                path->get(&request, &response);
            }
        }
        else if(strlen(request.method) == 4 && strcmp(request.method, "POST") == 0) {
            if(path->post == NULL) {
                http_set_response_code(&response, HTTP_405_NOT_ALLOWED);
                http_set_content_type(&response, "text/html", true);
                http_set_body(&response, 24, "Method Post not allowed", false);
            }
            else {
                path->post(&request, &response);
            }
        }
        else if(strlen(request.method) == 3 && strcmp(request.method, "PUT") == 0) {
            if(path->put == NULL) {
                http_set_response_code(&response, HTTP_405_NOT_ALLOWED);
                http_set_content_type(&response, "text/html", true);
                http_set_body(&response, 23, "Method Put not allowed", false);
            }
            else {
                path->put(&request, &response);
            }
        }
        else if(strlen(request.method) == 5 && strcmp(request.method, "PATCH") == 0) {
            if(path->patch == NULL) {
                http_set_response_code(&response, HTTP_405_NOT_ALLOWED);
                http_set_content_type(&response, "text/html", true);
                http_set_body(&response, 25, "Method Patch not allowed", false);
            }
            else {
                path->patch(&request, &response);
            }
        }
        else if(strlen(request.method) == 6 && strcmp(request.method, "DELETE") == 0) {
            if(path->delete == NULL) {
                http_set_response_code(&response, HTTP_405_NOT_ALLOWED);
                http_set_content_type(&response, "text/html", true);
                http_set_body(&response, 26, "Method Delete not allowed", false);
            }
            else {
                path->delete(&request, &response);
            }
        } else {
                http_set_response_code(&response, HTTP_400_BAD_REQUEST);
                http_set_content_type(&response, "text/html", true);
                http_set_body(&response, 12, "Bad request", false);
        }

        const char response_format[] = "HTTP/1.1 %d %s%s%s%s\r\nContent-Length: %ld\r\n\r\n";
        const size_t max_len = sizeof(response_format) + strlen(response.status_line) + strlen(response.content_type) + strlen(response.location) + response.header_size + response.header_buf_size + 32;
        char* reply = malloc(max_len);
        size_t len = snprintf(
            reply, max_len, response_format,
            response.code, response.status_line,
            response.location,
            response.content_type,
            (response.header_buf ? response.header_buf : ""),
            response.payload_size
        );
        if (SSL_write(in_request->con->ssl, reply, len) > 0 && response.payload_size > 0)
            SSL_write(in_request->con->ssl, response.payload, response.payload_size);
    
        // If there is work to be done we continue otherwise we signal that this task is no longer available
        bool pending = false;
        lock(&in_request->con->lock);
        if(empty(in_request->con->requests)) {
            in_request->con->running = false;
        }
        else pending = true;
        unlock(&in_request->con->lock);

        free(response.header_buf);
        if(response.clean_payload) free(response.payload);
        free(reply);
        free(in_request->data->payload);

        // If pending is set we have requests that need to be handled
        if(!pending) break;
        // Get more work an run again
        in_request->data = pop(in_request->con->requests);
    }
    free(in_request);
}

static void http_server_worker(asyncTask_t* self, http_server_t* this) {
    request_t* request = malloc(sizeof(request_t));
    request->data = malloc(sizeof(rcv_data_t));
    request->data->payload = malloc(DEFAULT_BUFFER_SIZE);
    request->data->size = DEFAULT_BUFFER_SIZE;

    socklen_t addrlen = sizeof(struct sockaddr_in);

    while(atomic_load(&this->active) == true) {
        int ret = poll(this->pfds, this->nfds, this->timeout);

        if(ret == -1) continue;
        else if (ret == 0) {
            for(nfds_t i = 1; i < this->nfds; i++) {
                if(this->connections[(int)i].timeout == -1) continue;
                if((--this->connections[(int)i].timeout) == 0) {
                    printf("Connection %d timeout\n", this->pfds[i].fd);
                    http_close_connection(&this->connections[(int)i]);
                    this->pfds[i].fd = -1;
                    if(i == this->nfds - 1) this->nfds -= 1;
                }
            }
            continue;
        }

        for(nfds_t i = 1; i < this->nfds; i++) {
            if(this->pfds[i].revents == 0)
                continue;
            if (this->pfds[i].revents & POLLIN) {
                connection_t* con = &this->connections[i];
            read_more:
                if((request->data->bytes_received = SSL_read(con->ssl, request->data->payload, request->data->size)) > 0) {
                    lock(&con->lock);
                    if(con->running) {
                        push(con->requests, request->data);
                        unlock(&con->lock);
                    } else {
                        unlock(&con->lock);
                        request->server = this;
                        con->running = true;
                        request->con = con;
                        asyncTask_t* task = async((async_func_t)http_process_request, request);
                        async_detach(&task);
                        request = malloc(sizeof(request_t));
                    }
                    request->data = malloc(sizeof(rcv_data_t));
                    request->data->payload = malloc(DEFAULT_BUFFER_SIZE);
                    request->data->size = DEFAULT_BUFFER_SIZE;
                    if(SSL_pending(con->ssl) > 0) goto read_more;
                }
                else {
                    if(!con->running) {
                        printf("Closing bad connection %d\n", this->pfds[i].fd);
                        http_close_connection(&this->connections[(int)i]);
                        this->pfds[i].fd = -1;
                        if(i == this->nfds - 1) this->nfds -= 1;
                    }
                    continue;
                }
                this->connections[(int)i].timeout = (10 * 1000) / this->timeout;
            }
            else { // POLLERR | POLLHUP
                printf("Closing connection %d\n", this->pfds[i].fd);
                http_close_connection(&this->connections[(int)i]);
                this->pfds[i].fd = -1;
                if(i == this->nfds - 1) this->nfds -= 1;
            }
        }

        if(this->pfds[0].revents & POLLIN) {
            int32_t connfd = accept(this->fd, (struct sockaddr*)&this->serverAddr, &addrlen);
            if(connfd == 0) continue;
            for(nfds_t i = 1; i < this->maxConnections; ++i) {
                if(this->pfds[i].fd == -1) {
                    if(http_open_connection(&this->connections[(int)i], connfd, this->timeout, this->ctx, this->fullchain, this->privatekey)) {
                        printf("New connection %d\n", connfd);
                        this->pfds[i].fd = connfd;
                        if(i >= this->nfds) this->nfds += 1;
                    }
                    else {
                        printf("New connection %d rejected\n", connfd);
                        close(connfd);
                    }
                    break;
                }
            }
        }
    }
}

/************************** PUBLIC METHODS **************************/

http_server_t* http_server_init(const char* ip, int port, int tasks, const char* fullchain, const char* privatekey) {
    http_server_t* this = calloc(1, sizeof(http_server_t));
    this->domain = AF_INET;
    this->type = SOCK_STREAM;
    this->protocol = 0;
    this->bakclog = 0;
    this->timeout = 100;
    this->active = ATOMIC_VAR_INIT(false);
    this->fd = -1;
    this->port = port;
    this->ip = ip;
    this->root = http_init_url_paths();

    this->fullchain = fullchain;
    this->privatekey = privatekey;
    this->ctx = SSL_CTX_new(TLS_server_method());

    async_engine_start((tasks < 0) ? HTTP_DEFAULT_TASKS : tasks);

    return this;
}

void http_server_clean(http_server_t** this) {
    if(atomic_load(&(*this)->active) == true) {
        http_server_stop(*this);
    }

    // Async engine will ensure that all tasks terminate
    async_engine_stop();

    http_url_path_clean((*this)->root);

    SSL_CTX_free((*this)->ctx);
    free((*this)->pfds);
    free((*this)->connections);
    free(*this);
    *this = NULL;
}

int http_server_start(http_server_t* this, size_t max_connections) {
    extern int32_t errno;
    socklen_t addrlen = sizeof(struct sockaddr_in);

    // Create Server socket
    if((this->fd  = socket(this->domain, this->type, this->protocol)) < 0) {
        printf("Socket creation failed: %s\n", strerror(errno));
        return -1;
    }

    // Set communication timeout
    const struct timeval timeout = {.tv_sec = this->timeout / 1000, .tv_usec = (this->timeout % 1000)};
    setsockopt(this->fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    // Attaching socket to specified port
    int opt = 1;
    if(setsockopt(this->fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        printf("Failed to attach socket to port: %d\n", this->port);
        return -1;
    }

    // Initialize server address
    this->serverAddr.sin_family = this->domain;
    this->serverAddr.sin_addr.s_addr = (this->ip ? inet_addr(this->ip) : htonl(INADDR_ANY));
    this->serverAddr.sin_port = htons(this->port);

    // Bind socket to the address and port number specified
    if(bind(this->fd , (struct sockaddr*)&this->serverAddr, addrlen) < 0) {
        printf("Bind failed: %s\n", strerror(errno));
        return -1;
    }

    // Put server socket listening to incoming connection requests
    if(listen(this->fd , this->bakclog) < 0) {
        printf("Listen failed: %s\n", strerror(errno));
        return -1;
    }

    this->maxConnections = (max_connections == 0) ? 1 : max_connections;
    this->nfds = 1;
    this->pfds = calloc(this->maxConnections, sizeof(*this->pfds));
    this->pfds[0].fd = this->fd;
    this->pfds[0].events = POLLIN;

    this->connections = calloc(this->maxConnections, sizeof(*this->connections));
    this->connections[0].fd = this->fd;
    this->connections[0].timeout = -1;

    int i;
    for(i = 1; i < this->maxConnections; ++i) {
        this->pfds[i].fd = -1;
        this->pfds[i].events = POLLIN;
        this->connections[i].fd = -1;
        this->connections[i].timeout = -1;
    }

    atomic_store(&this->active, true);
    this->listener = async((async_func_t)http_server_worker, this);

    return 0;
}

void http_server_stop(http_server_t* this) {
    // Signal threads to stop
    atomic_store(&this->active, false);

    // Wait for listener thread termination
    await(&this->listener);

    // Close all open connections
    for(nfds_t i = 1; i < this->nfds; i++) {
        if(this->connections[i].ssl != NULL) {
            http_close_connection(&this->connections[i]);
        }
    }

    close(this->fd);

    this->fd = -1;
}

int http_register_method(http_server_t* this, const char* path, int type, void (*method)(http_request_t*, http_response_t*)) {
    if(this == NULL || this->root == NULL) return -1;

    char* remaining = NULL;
    pathname_t* uri = http_resolve_path(this->root, path, strlen(path), &remaining);

    // Do we need to register new paths
    if(remaining != NULL) {
        // Check if the path has '/'s
        size_t remain_len = strlen(remaining);
        size_t new_len;
        while(remain_len)
        {
            for(new_len = 0; new_len < remain_len && remaining[new_len] != '/'; ++new_len);
            pathname_t* new_uri = http_create_url_path(remaining, remain_len);
            http_add_child_path(uri, new_uri);
            uri = new_uri;
            for(; new_len < remain_len && remaining[new_len] == '/'; ++new_len);
            remain_len -= new_len;
            remaining += new_len;
        }
    }

    switch (type) {
    case HTTP_GET:
        if(uri->get != NULL) return -1;
        uri->get = method;
        break;
    case HTTP_POST:
        if(uri->post != NULL) return -1;
        uri->post = method;
        break;
    case HTTP_PUT:
        if(uri->put != NULL) return -1;
        uri->put = method;
        break;
    case HTTP_PATCH:
        if(uri->patch != NULL) return -1;
        uri->patch = method;
        break;
    case HTTP_DELETE:
        if(uri->delete != NULL) return -1;
        uri->delete = method;
        break;
    default:
        return -1;
    }

    return 0;
}

void http_set_response_code(http_response_t* response, int code) {
    response->code = HttpResponses[code].code;
    response->status_line = HttpResponses[code].reason;
}

void http_add_header_entry(http_response_t* response, const char* name, const char* value) {
    size_t len = strlen(name) + 2 + strlen(value) + 2;
    if((response->header_size + len) > response->header_buf_size) {
        response->header_buf_size = response->header_buf_size + ((len < 128) ? (128) : (len));
        response->header_buf = realloc(response->header_buf, response->header_buf_size);
    }
    sprintf(response->header_buf + response->header_size, "\r\n%s: %s", name, value);
}

void http_set_cookie(http_response_t* response, const char* cookie, const char* value) {
    const char cookie_format[] = "%s=%s";
    char* cookie_entry = malloc(sizeof(cookie_format) + strlen(cookie) + strlen(value));
    sprintf(cookie_entry, cookie_format, cookie, value);
    http_add_header_entry(response, "Set-Cookie", cookie_entry);
    free(cookie_entry);
}

const char* http_get_cookie(http_request_t* request, const char* cookie) {
    char* str = (char*)request->raw;
    size_t len = strlen(cookie);
    do {
        str = strstr(str, "Cookie: ");
        if(str == NULL) return NULL;
        str += (sizeof("Cookie: ") - 1);
        if(strncmp(str, cookie, len) == 0 && str[len] == '=') {
            return &str[len + 1];
        }
    } while(str);
    return NULL;
}

void http_set_content_type(http_response_t* response, const char* type, bool use_charset) {
    const char* charset = (use_charset ? "; charset=utf-8" : "");
    sprintf(response->content_type, "\r\nContent-Type: %s%s", type, charset);
}

void http_set_redirect(http_response_t* response, const char* location) {
    sprintf(response->location, "\r\nLocation: %s", location);
}

void http_set_body(http_response_t* response, size_t len, const char* body, bool clean) {
    response->payload_size = len;
    response->payload = (char*)body;
    response->clean_payload = clean;
}