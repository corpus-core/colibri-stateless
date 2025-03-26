#include "proofer.h"
#include <curl/curl.h>
#include <llhttp.h>
#include <stdlib.h>
#include <uv.h>

typedef struct {
  char*                 path;
  data_request_method_t method;
  char*                 content_type;
  char*                 accept;
  uint8_t*              payload;
  size_t                payload_len;
} http_request_t;

typedef struct {
  char* memcached_host;
  int   memcached_port;
  int   memcached_pool;
  int   port;
  int   loglevel;
  int   req_timeout;
  int   chain_id;
  char* rpc_nodes;
  char* beacon_nodes;
} http_server_t;

extern http_server_t http_server;
typedef struct {
  uv_tcp_t          handle;
  llhttp_t          parser;
  llhttp_settings_t settings;
  http_request_t    request;
  uv_write_t        write_req;
  char              current_header[128];
  bool              being_closed; // Flag to track if this client is being closed
} client_t;
typedef bool (*http_handler)(client_t*);

typedef struct request_t request_t;
typedef void (*http_client_cb)(request_t*);
typedef void (*http_request_cb)(client_t*, void* data, data_request_t*);
// Struktur für jede aktive Anfrage
typedef struct {
  char*              url;
  data_request_t*    req;
  CURL*              curl; // list of pending handles
  buffer_t           buffer;
  request_t*         parent;  // pointer to parent request_t
  struct curl_slist* headers; // Store the headers list to free it later
} single_request_t;

typedef struct request_t {
  client_t*         client; // client request
  void*             ctx;    // proofer
  single_request_t* requests;
  size_t            request_count; // count of handles
  http_client_cb    cb;

} request_t;

void c4_proofer_handle_request(request_t* req);
void c4_start_curl_requests(request_t* req);
bool c4_check_retry_request(request_t* req);
void c4_init_curl(uv_timer_t* timer);
void c4_cleanup_curl();
void c4_on_new_connection(uv_stream_t* server, int status);
void c4_http_respond(client_t* client, int status, char* content_type, bytes_t body);
void c4_register_http_handler(http_handler handler);
void c4_add_request(client_t* client, data_request_t* req, void* data, http_request_cb cb);
void c4_configure(int argc, char* argv[]);
// Handlers
bool c4_handle_proof_request(client_t* client);
bool c4_handle_status(client_t* client);
bool c4_proxy(client_t* client);
bool c4_handle_health_check(client_t* client);
