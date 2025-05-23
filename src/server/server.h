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
  uint64_t total_requests;
  uint64_t total_errors;
  uint64_t last_sync_event;
  uint64_t last_request_time;
  uint64_t open_requests;
} server_stats_t;

typedef struct {
  char*          memcached_host;
  int            memcached_port;
  int            memcached_pool;
  int            port;
  int            loglevel;
  int            req_timeout;
  int            chain_id;
  char*          rpc_nodes;
  char*          beacon_nodes;
  int            stream_beacon_events;
  char*          period_store;
  server_stats_t stats;
} http_server_t;

typedef struct {
  char** urls;
  size_t count;
} server_list_t;

extern http_server_t http_server;
typedef struct {
  uv_tcp_t          handle;
  llhttp_t          parser;
  llhttp_settings_t settings;
  http_request_t    request;
  uv_write_t        write_req;
  char              current_header[128];
  bool              being_closed;             // Flag to track if this client is being closed
  bool              message_complete_reached; // True if on_message_complete was called for the current request
  bool              keep_alive_idle;          // True if the connection is idle in keep-alive mode, awaiting next request
} client_t;
typedef bool (*http_handler)(client_t*);

typedef struct request_t request_t;
typedef void (*http_client_cb)(request_t*);
typedef void (*http_request_cb)(client_t*, void* data, data_request_t*);
typedef void (*handle_stored_data_cb)(void* u_ptr, uint64_t period, bytes_t data, char* error);
// Struktur für jede aktive Anfrage
typedef struct {
  char*              url;
  data_request_t*    req;
  CURL*              curl; // list of pending handles
  buffer_t           buffer;
  request_t*         parent;  // pointer to parent request_t
  struct curl_slist* headers; // Store the headers list to free it later
  uint64_t           start_time;
  uint64_t           end_time;
  bool               success;
  bool               cached;
} single_request_t;

typedef struct request_t {
  client_t*         client; // client request
  void*             ctx;    // proofer
  single_request_t* requests;
  size_t            request_count; // count of handles
  uint64_t          start_time;
  http_client_cb    cb;

} request_t;

typedef enum {
  STORE_TYPE_BLOCK_ROOTS  = 1,
  STORE_TYPE_BLOCK_ROOT   = 2,
  STORE_TYPE_BLOCK_HEADER = 3,
  STORE_TYPE_LCU          = 4,
} store_type_t;

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
bool           c4_handle_proof_request(client_t* client);
bool           c4_handle_status(client_t* client);
bool           c4_proxy(client_t* client);
bool           c4_handle_lcu(client_t* client);
bool           c4_handle_health_check(client_t* client);
bool           c4_handle_metrics(client_t* client);
void           c4_handle_new_head(json_t head);
void           c4_handle_finalized_checkpoint(json_t checkpoint);
void           c4_watch_beacon_events();
uint64_t       c4_get_query(char* query, char* param);
void           c4_handle_internal_request(single_request_t* r);
bool           c4_get_from_store(char* path, void* uptr, handle_stored_data_cb cb);
bool           c4_get_from_store_by_type(chain_id_t chain_id, uint64_t period, store_type_t type, uint32_t slot, void* uptr, handle_stored_data_cb cb);
server_list_t* c4_get_server_list(data_request_type_t type);
void           c4_metrics_add_request(data_request_type_t type, const char* method, uint64_t size, uint64_t duration, bool success, bool cached);
