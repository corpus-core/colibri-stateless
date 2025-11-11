/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#ifndef C4_SERVER_H
#define C4_SERVER_H

#include "prover.h"
#include "tracing.h"
#include <curl/curl.h>
#include <llhttp.h>
#include <stdlib.h>
#include <uv.h>
// Preconf-specific callback type (uses block_number instead of period)
typedef void (*handle_preconf_data_cb)(void* user_ptr, uint64_t block_number, bytes_t data, const char* error);
typedef struct {
  char*                 path;
  data_request_method_t method;
  char*                 content_type;
  char*                 accept;
#ifdef HTTP_SERVER_GEO
  char* geo_city;
  char* geo_country;
  char* geo_latitude;
  char* geo_longitude;
#endif
  uint8_t* payload;
  size_t   payload_len;
} http_request_t;

typedef struct {
  char*    city;
  char*    country;
  char*    latitude;
  char*    longitude;
  uint64_t count;
  uint64_t last_access;
} geo_location_t;

typedef struct {
  uint64_t total_requests;
  uint64_t total_errors;
  uint64_t last_sync_event;
  uint64_t last_request_time;
  uint64_t open_requests;
  // Event-loop health metrics
  uint64_t loop_idle_ns_total; // cumulative idle time (requires UV_METRICS_IDLE_TIME)
  double   loop_idle_ratio;    // idle ratio over last sampling window (0..1)
  uint64_t loop_lag_ns_last;   // last measured loop lag (ns)
  uint64_t loop_lag_ns_max;    // max observed loop lag since start (ns)
  // Beacon watcher event counters (TEST and runtime diagnostics)
  uint64_t beacon_events_total;
  uint64_t beacon_events_head;
  uint64_t beacon_events_finalized;
#ifdef HTTP_SERVER_GEO
  geo_location_t* geo_locations;
  size_t          geo_locations_count;
  size_t          geo_locations_capacity;
#endif
} server_stats_t;

// Trace levels for request-scoped tracing
typedef enum {
  TRACE_LEVEL_MIN   = 0, // default minimal
  TRACE_LEVEL_DEBUG = 1, // verbose
  TRACE_LEVEL_NONE  = 2  // disabled
} trace_level_t;

#define STR_BYTES(msg) bytes(msg, strlen(msg) - 1)
// Global cURL connection pool configuration and runtime statistics
typedef struct {
  int http2_enabled;         // enable HTTP/2 if available (1/0)
  int pool_max_host;         // CURLMOPT_MAX_HOST_CONNECTIONS
  int pool_max_total;        // CURLMOPT_MAX_TOTAL_CONNECTIONS
  int pool_maxconnects;      // CURLMOPT_MAXCONNECTS
  int upkeep_interval_ms;    // CURLOPT_UPKEEP_INTERVAL_MS for easy handles
  int tcp_keepalive_enabled; // CURLOPT_TCP_KEEPALIVE (1/0)
  int tcp_keepidle_s;        // CURLOPT_TCP_KEEPIDLE
  int tcp_keepintvl_s;       // CURLOPT_TCP_KEEPINTVL
  // Counters
  uint64_t total_requests;           // all transfers via libcurl
  uint64_t total_connects;           // sum of CURLINFO_NUM_CONNECTS
  uint64_t reused_connections_total; // number of reused connections
  uint64_t http2_requests_total;     // transfers over HTTP/2
  uint64_t http1_requests_total;     // transfers over HTTP/1.x
  uint64_t tls_handshakes_total;     // heuristic: !reused && appconnect_time>0
  // Running averages (EWMA)
  double avg_connect_time_ms;    // avg TCP connect time
  double avg_appconnect_time_ms; // avg TLS handshake time
} curl_stats_t;

typedef struct {
  char*          host; // Host/IP to bind to (default: 127.0.0.1 for security)
  char*          memcached_host;
  int            memcached_port;
  int            memcached_pool;
  int            port;
  int            loglevel;
  int            req_timeout;
  int            chain_id;
  char*          rpc_nodes;
  char*          prover_nodes;
  char*          beacon_nodes;
  char*          checkpointz_nodes;
  int            stream_beacon_events;
  char*          period_store;
  bytes32_t      witness_key;
  server_stats_t stats;
  // Preconf storage configuration
  char* preconf_storage_dir;
  int   preconf_ttl_minutes;
  int   preconf_cleanup_interval_minutes;
  // preconf_use_gossip removed - now using automatic HTTP fallback until gossip is active

  // Web UI configuration
  int web_ui_enabled; // 0=disabled, 1=enabled (default: 0 for security)

  // Heuristic load-balancing configuration (ENV-driven)
  int max_concurrency_default;    // default per-server concurrency limit
  int max_concurrency_cap;        // absolute cap for dynamic concurrency
  int latency_target_ms;          // target latency for AIMD increase
  int conc_cooldown_ms;           // cooldown for concurrency adjustments
  int overflow_slots;             // allowed overflow slots when saturated
  int saturation_wait_ms;         // short wait before overflow on saturation
  int method_stats_half_life_sec; // half-life for method statistics EWMA
  int block_availability_window;  // optional bitmap window size
  int block_availability_ttl_sec; // TTL for block availability learning
  int rpc_head_poll_interval_ms;  // interval for eth_blockNumber polling
  int rpc_head_poll_enabled;      // enable/disable head polling

#ifdef TEST
  // Test recording mode: if set, all responses are written to TESTDATA_DIR/server/<test_dir>/
  char* test_dir;
#endif
  // Global cURL pool configuration and metrics
  curl_stats_t curl;
  // Tracing configuration
  int   tracing_enabled;        // 0/1
  char* tracing_url;            // Zipkin v2 endpoint
  char* tracing_service_name;   // service name
  int   tracing_sample_percent; // 0..100
} http_server_t;

// Method support tracking for RPC methods
typedef struct method_support {
  char*                  method_name;
  bool                   is_supported;
  struct method_support* next;
} method_support_t;

// Server health tracking structure
typedef struct {
  uint64_t          total_requests;
  uint64_t          successful_requests;
  uint64_t          total_response_time; // in milliseconds
  uint64_t          last_used;           // timestamp
  uint64_t          consecutive_failures;
  uint64_t          marked_unhealthy_at; // timestamp when marked unhealthy
  bool              is_healthy;          // false if too many consecutive failures
  bool              recovery_allowed;    // true if recovery attempt is allowed
  double            weight;              // calculated weight for load balancing
  method_support_t* unsupported_methods; // linked list of unsupported RPC methods
  // Dynamic capacity and latency tracking
  uint32_t inflight;            // current concurrent requests
  uint32_t max_concurrency;     // dynamic concurrency limit
  uint32_t min_concurrency;     // minimum concurrency (>=1)
  double   ewma_latency_ms;     // smoothed response latency
  uint64_t last_adjust_ms;      // last AIMD adjustment time
  bool     rate_limited_recent; // rate limit seen recently
  uint64_t rate_limited_at_ms;  // timestamp of last 429/limit
  // Head polling (RPC): latest observed head and staleness
  uint64_t latest_block;      // last known head block number
  uint64_t head_last_seen_ms; // timestamp when head was last observed
  // Per-method statistics (linked list)
  struct method_stats* method_stats;
} server_health_t;

/**
 * @brief Per-method statistics entry maintained per server.
 *
 * Tracks smoothed latency and success/not-found rates for a specific method.
 */
typedef struct method_stats {
  char*                name;                // method name key
  double               ewma_latency_ms;     // smoothed latency for this method
  double               success_ewma;        // success ratio EWMA
  double               not_found_ewma;      // not-found ratio EWMA
  bool                 rate_limited_recent; // method recently rate limited
  uint64_t             last_update_ms;      // last stats update timestamp
  struct method_stats* next;                // next entry in list
} method_stats_t;

typedef uint32_t beacon_client_type_t;

// Generic structure for mapping client names to their type values (defined by the chain handler)
typedef struct {
  const char*          config_name;  // "NIMBUS", "GETH", etc. for URL suffixes
  const char*          display_name; // "Nimbus", "Geth", etc. for logs
  beacon_client_type_t value;        // The actual bitmask value
} client_type_mapping_t;

// Bitmask-based beacon client types for feature detection
#define BEACON_CLIENT_UNKNOWN      0x00000000 // No specific client requirement
#define BEACON_CLIENT_EVENT_SERVER 0x01000000 // defines the first or server detecting the events

#define MAX_SERVERS 32
typedef struct {
  char**                urls;
  size_t                count;
  server_health_t*      health_stats; // health tracking per server
  beacon_client_type_t* client_types; // client type per server (for beacon API only)
  uint32_t              next_index;   // for round-robin fallback
} server_list_t;

extern http_server_t         http_server;
extern volatile sig_atomic_t graceful_shutdown_in_progress;
// Magic number to identify valid HTTP client structs (0xC4C11E47 = "C4 CLIENT")
#define C4_CLIENT_MAGIC 0xC4C11E47

typedef struct {
  uint32_t          magic; // Magic number to identify valid client structs
  uv_tcp_t          handle;
  llhttp_t          parser;
  llhttp_settings_t settings;
  http_request_t    request;
  uv_write_t        write_req;
  char              current_header[128];
  bool              being_closed;             // Flag to track if this client is being closed
  bool              message_complete_reached; // True if on_message_complete was called for the current request
  bool              keep_alive_idle;          // True if the connection is idle in keep-alive mode, awaiting next request
  size_t            headers_size_received;    // Total size of headers received (for DoS protection)
  size_t            body_size_received;       // Actual body bytes received (for request smuggling protection)
  // Incoming b3 context (optional)
  char* b3_trace_id;
  char* b3_span_id;
  char* b3_parent_span_id;
  int   b3_sampled; // -1 unknown, 0 false, 1 true
  // Tracing level (default: minimal)
  int trace_level; // trace_level_t
} client_t;
typedef bool (*http_handler)(client_t*);

typedef struct request_t request_t;
typedef void (*http_client_cb)(request_t*);
typedef void (*http_request_cb)(client_t*, void* data, data_request_t*);
typedef void (*handle_stored_data_cb)(void* u_ptr, uint64_t period, bytes_t data, const char* error);
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
  trace_span_t*      attempt_span; // tracing span for this single HTTP attempt
  // Tracing for cache/pending waits
  trace_span_t* cache_span;
  uint64_t      cache_start_ms;
  trace_span_t* wait_span;
  uint64_t      wait_start_ms;
} single_request_t;

typedef struct request_t {
  client_t*         client; // client request
  void*             ctx;    // prover
  single_request_t* requests;
  size_t            request_count; // count of handles
  uint64_t          start_time;
  http_client_cb    cb;          // callback function to call when all requests are done
  void*             parent_ctx;  // pointer to parent context or parent caller
  http_request_cb   parent_cb;   // callback function to call when the ctx (mostly prover) has a result
  trace_span_t*     trace_root;  // root tracing span for the overall proof request
  uint32_t          prover_step; // counts c4_prover_execute invocations for this request
} request_t;

typedef enum {
  STORE_TYPE_BLOCK_ROOTS  = 1,
  STORE_TYPE_BLOCK_ROOT   = 2,
  STORE_TYPE_BLOCK_HEADER = 3,
  STORE_TYPE_LCU          = 4,
} store_type_t;

typedef enum {
  C4_RESPONSE_SUCCESS                    = 0, // Request was successful
  C4_RESPONSE_ERROR_RETRY                = 1, // Error occurred, but retry may fix it
  C4_RESPONSE_ERROR_USER                 = 2, // User error, retry does not make sense
  C4_RESPONSE_ERROR_METHOD_NOT_SUPPORTED = 3  // Method not supported by this server, exclude for this method
} c4_response_type_t;

void c4_prover_handle_request(request_t* req);
void c4_start_curl_requests(request_t* req, c4_state_t* state);
bool c4_check_retry_request(request_t* req);
void c4_init_curl(uv_timer_t* timer);
void c4_cleanup_curl();
void c4_on_new_connection(uv_stream_t* server, int status);
void c4_http_respond(client_t* client, int status, char* content_type, bytes_t body);
void c4_write_error_response(client_t* client, int status, const char* error);
void c4_http_server_on_close_callback(uv_handle_t* handle); // Cleanup callback for closing client connections
void c4_register_http_handler(http_handler handler);
void c4_add_request(client_t* client, data_request_t* req, void* data, http_request_cb cb);
void c4_configure(int argc, char* argv[]);

// Config parameter registry (for dynamic Web-UI)
typedef enum {
  CONFIG_PARAM_INT,
  CONFIG_PARAM_STRING,
  CONFIG_PARAM_KEY
} config_param_type_t;

typedef struct {
  char*               name;        // env variable name
  char*               arg_name;    // command line arg name
  char*               description; // human-readable description
  config_param_type_t type;        // parameter type
  void*               value_ptr;   // pointer to actual value
  int                 min;         // min value (for int)
  int                 max;         // max value (for int)
} config_param_t;

const config_param_t* c4_get_config_params(int* count);
const char*           c4_get_config_file_path();
int                   c4_save_config_file(const char* updates);
// Handlers
bool           c4_handle_verify_request(client_t* client);
bool           c4_handle_proof_request(client_t* client);
bool           c4_handle_status(client_t* client);
bool           c4_handle_health_check(client_t* client);
bool           c4_handle_metrics(client_t* client);
bool           c4_handle_get_config(client_t* client);
bool           c4_handle_post_config(client_t* client);
bool           c4_handle_restart_server(client_t* client);
bool           c4_handle_config_ui(client_t* client);
bool           c4_handle_openapi(client_t* client);
bool           c4_handle_unverified_rpc_request(client_t* client);
uint64_t       c4_get_query(char* query, char* param);
void           c4_handle_internal_request(single_request_t* r);
bool           c4_get_preconf(chain_id_t chain_id, uint64_t block_number, char* file_name, void* uptr, handle_preconf_data_cb cb);
bool           c4_get_from_store(const char* path, void* uptr, handle_stored_data_cb cb);
bool           c4_get_from_store_by_type(chain_id_t chain_id, uint64_t period, store_type_t type, uint32_t slot, void* uptr, handle_stored_data_cb cb);
server_list_t* c4_get_server_list(data_request_type_t type);
void           c4_metrics_add_request(data_request_type_t type, const char* method, uint64_t size, uint64_t duration, bool success, bool cached);
const char*    c4_extract_server_name(const char* url);
// Load balancing functions
int c4_select_best_server(server_list_t* servers, uint32_t exclude_mask, uint32_t preferred_client_type);
int c4_select_best_server_for_method(server_list_t* servers, uint32_t exclude_mask, uint32_t preferred_client_type, const char* method, uint64_t requested_block, bool has_block);

// Method support tracking functions
void               c4_mark_method_unsupported(server_list_t* servers, int server_index, const char* method);
bool               c4_is_method_supported(server_list_t* servers, int server_index, const char* method);
void               c4_cleanup_method_support(server_health_t* health);
void               c4_update_server_health(server_list_t* servers, int server_index, uint64_t response_time, bool success);
void               c4_calculate_server_weights(server_list_t* servers);
bool               c4_should_reset_health_stats(server_list_t* servers);
void               c4_reset_server_health_stats(server_list_t* servers);
c4_response_type_t c4_classify_response(long http_code, const char* url, bytes_t response_body, data_request_t* req);
bool               c4_has_available_servers(server_list_t* servers, uint32_t exclude_mask);
void               c4_attempt_server_recovery(server_list_t* servers);

// Concurrency hooks for request lifecycle
bool c4_on_request_start(server_list_t* servers, int idx, bool allow_overflow);
void c4_on_request_end(server_list_t* servers, int idx, uint64_t resp_time_ms,
                       bool success, c4_response_type_t cls, long http_code,
                       const char* method, const char* method_context);
void c4_signal_rate_limited(server_list_t* servers, int idx, const char* method);

// Server configuration and client type detection functions
void                 c4_parse_server_config(server_list_t* list, char* servers);
void                 c4_detect_server_client_types(server_list_t* servers, data_request_type_t type);
beacon_client_type_t c4_parse_client_version_response(const char* response, data_request_type_t type);
const char*          c4_client_type_to_name(beacon_client_type_t client_type, http_server_t* http_server);
bool                 c4_start_rpc_head_poller(server_list_t* servers);
void                 c4_stop_rpc_head_poller(void);

// handle client type adjustments
char*                   c4_request_fix_url(char* url, single_request_t* r, beacon_client_type_t client_type);
data_request_encoding_t c4_request_fix_encoding(data_request_encoding_t encoding, single_request_t* r, beacon_client_type_t client_type);
bytes_t                 c4_request_fix_response(bytes_t response, single_request_t* r, beacon_client_type_t client_type);
c4_response_type_t      c4_classify_response(long http_code, const char* url, bytes_t response_body, data_request_t* req);
bool                    c4_error_indicates_not_found(long http_code, data_request_t* req, bytes_t response_body);

// Server storage functions
void c4_init_server_storage();

// Server control functions for testing
typedef struct {
  uv_loop_t*  loop;
  uv_tcp_t    server;
  uv_timer_t  curl_timer;
  uv_timer_t  prover_cleanup_timer;
  uv_timer_t  graceful_timer;
  uv_timer_t  loop_metrics_timer;
  uv_signal_t sigterm_handle;
  uv_signal_t sigint_handle;
  uv_idle_t   init_idle_handle;
  bool        is_running;
  int         port;
} server_instance_t;

int  c4_server_start(server_instance_t* instance, int port);
void c4_server_stop(server_instance_t* instance);
void c4_server_run_once(server_instance_t* instance); // For tests: non-blocking event loop iteration

#ifdef TEST
// Test hook for URL rewriting (file:// mocking)
// Set this to a custom function to intercept and rewrite URLs before curl uses them
// This allows replacing real URLs with file:// URLs pointing to mock responses
// Example: c4_test_url_rewriter = my_url_rewriter_function;
extern char* (*c4_test_url_rewriter)(const char* url, const char* payload);

// Test helper: Generate deterministic filename for mock/recorded responses
// Returns: allocated string "test_data_dir/server/test_name/host_hash.json"
// Caller must free the returned string
char* c4_file_mock_get_filename(const char* host, const char* url,
                                const char* payload, const char* test_name);

// Test helper: Clear the storage cache (for test isolation)
void c4_clear_storage_cache(void);
#endif

#endif // C4_SERVER_H
