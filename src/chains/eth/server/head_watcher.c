/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */
#include "bytes.h"
#include "chains.h"
#include "eth_conf.h"
#include "handler.h"
#include "json.h"
#include "logger.h"
#include "prover.h"
#include "server.h"
#include "state.h"
#include <curl/curl.h>
#include <errno.h>
#include <stddef.h> // For size_t needed by strnstr impl
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#ifdef _WIN32
#include "../../../util/win_compat.h"
#endif

// Provide strnstr implementation if it's not available (e.g., on non-BSD/non-GNU systems)
#ifndef HAVE_STRNSTR // Guard against redefinition
char* strnstr(const char* haystack, const char* needle, size_t len) {
  size_t needle_len;

  if (!needle || *needle == '\0') {
    return (char*) haystack; // Empty needle matches immediately
  }
  needle_len = strlen(needle);
  if (needle_len == 0) {
    return (char*) haystack; // Also empty
  }
  if (needle_len > len) {
    return NULL; // Needle is longer than the search length
  }

  // Iterate up to the last possible starting position within len
  for (size_t i = 0; i <= len - needle_len; ++i) {
    // Stop if we hit the end of the haystack string itself
    if (haystack[i] == '\0') {
      break;
    }
    // Compare the needle at the current position
    if (strncmp(&haystack[i], needle, needle_len) == 0) {
      return (char*) &haystack[i]; // Found match
    }
  }

  return NULL; // Not found
}
#define HAVE_STRNSTR // Basic guard if included elsewhere
#endif               // HAVE_STRNSTR guard

static char* BEACON_WATCHER_URL = NULL;
#define ACCEPT_HEADER         "Accept: text/event-stream"
#define CACHE_CONTROL_HEADER  "Cache-Control: no-cache"
#define INACTIVITY_TIMEOUT_MS 30000
#define RECONNECT_DELAY_MS    5000

#ifdef TEST
// Test helper to override beacon watcher URL
void c4_test_set_beacon_watcher_url(const char* url) {
  if (BEACON_WATCHER_URL) {
    safe_free(BEACON_WATCHER_URL);
  }
  BEACON_WATCHER_URL = url ? strdup(url) : NULL;
}

// Test flag to disable reconnect (for file:// playback)
static bool test_disable_reconnect = false;

void c4_test_set_beacon_watcher_no_reconnect(bool disable) {
  test_disable_reconnect = disable;
}

// Record SSE data to file when test_dir is set
static FILE* test_sse_recording_file = NULL;

static void c4_record_sse_data(const char* data, size_t len) {
  if (!http_server.test_dir || !data || len == 0) return;

  if (!test_sse_recording_file) {
    // Create filename: TESTDATA_DIR/server/<test_dir>/beacon_events.sse
    // Use absolute path from TESTDATA_DIR
    char filename[512];
    snprintf(filename, sizeof(filename), "%s/server/%s/beacon_events.sse",
             TESTDATA_DIR, http_server.test_dir);

    // Ensure directory exists
    char mkdir_cmd[600];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s/server/%s",
             TESTDATA_DIR, http_server.test_dir);
    system(mkdir_cmd);

    test_sse_recording_file = fopen(filename, "a");
    if (test_sse_recording_file) {
      log_info("[RECORD] SSE events -> %s", filename);
    }
    else {
      log_error("[RECORD] Failed to open %s for writing (errno=%d)", filename, (uint32_t) errno);
      perror("[RECORD] Error details");
    }
  }

  if (test_sse_recording_file) {
    fwrite(data, 1, len, test_sse_recording_file);
    fflush(test_sse_recording_file);
  }
}
#endif

// Forward declarations
static void start_beacon_watch();
static void stop_beacon_watch();
static void schedule_reconnect();
static void on_inactivity_timeout(uv_timer_t* handle);
static void handle_beacon_event(char* event, char* data);

// --- State ---
typedef struct {
  CURL*              easy_handle;
  uv_timer_t         inactivity_timer;
  uv_timer_t         reconnect_timer;
  buffer_t           buffer;
  struct curl_slist* headers_list; // Store headers list to free later
  bool               is_running;
  char               error_buffer[CURL_ERROR_SIZE];
} beacon_watcher_state_t;

// Global state for the watcher
static beacon_watcher_state_t watcher_state = {0};

// Global CURL multi handle specific to the watcher
static CURLM* beacon_multi_handle = NULL;

// Libuv timer for curl_multi_socket_action timeouts
static uv_timer_t beacon_curl_timer;

// Structure to hold socket context for libuv polling
typedef struct beacon_curl_context_s {
  uv_poll_t                     poll_handle;
  curl_socket_t                 sockfd;
  struct beacon_curl_context_s* next;
} beacon_curl_context_t;

// Track all active poll contexts for robust shutdown
static beacon_curl_context_t* beacon_context_head = NULL;

// --- Forward Declarations for new CURL/UV integration callbacks ---
static int  beacon_timer_callback(CURLM* multi, long timeout_ms, void* userp);
static int  beacon_socket_callback(CURL* easy, curl_socket_t s, int action, void* userp, void* socketp);
static void beacon_poll_cb(uv_poll_t* handle, int status, int events);
static void beacon_curl_timeout_cb(uv_timer_t* handle);
static void check_multi_info();
static void free_curl_context(beacon_curl_context_t* context);
static void destroy_poll_handle(uv_handle_t* handle);
static void add_curl_context(beacon_curl_context_t* context);
static void remove_curl_context(beacon_curl_context_t* context);

// --- Callback Implementations ---

// Parses the buffer for complete SSE events
static void parse_sse_buffer() {
  char*  buf_data      = (char*) watcher_state.buffer.data.data;
  size_t buf_len       = watcher_state.buffer.data.len;
  size_t processed_len = 0;

  while (true) {
    // Find the end of an event block (\n\n)
    char* event_end = strnstr(buf_data + processed_len, "\n\n", buf_len - processed_len);
    if (!event_end) event_end = strnstr(buf_data + processed_len, "\r\n\r\n", buf_len - processed_len);
    //    bytes_write(bytes(buf_data + processed_len, buf_len - processed_len), fopen("buf_data.txt", "w"), true);
    if (!event_end) break; // No complete event found in the remaining buffer

    // Process the event block line by line
    size_t event_block_len   = (event_end - (buf_data + processed_len));
    char*  event_block_start = buf_data + processed_len;
    char*  current_line      = event_block_start;
    char*  event_type        = NULL;
    char*  event_data        = NULL;

    while (current_line < event_end) {
      char* next_newline = strchr(current_line, '\n');
      if (next_newline && next_newline[-1] == '\r') next_newline--;
      if (!next_newline) break; // Should not happen if event_end was found

      size_t line_len = next_newline - current_line;

      if (strncmp(current_line, "event:", 6) == 0) {
        safe_free(event_type); // Free previous if any
        size_t skip = 6;
        if (line_len > 6 && current_line[6] == ' ') skip = 7; // Skip optional space
        event_type = strndup(current_line + skip, line_len - skip);
      }
      else if (strncmp(current_line, "data:", 5) == 0) {
        safe_free(event_data); // Free previous if any
        size_t skip = 5;
        if (line_len > 5 && current_line[5] == ' ') skip = 6; // Skip optional space
        event_data = strndup(current_line + skip, line_len - skip);
      }
      // Ignore other lines (like comments starting with ':')

      current_line = next_newline + 1;
      if (*current_line == '\n') current_line++;
    }

    // If we have both event and data, call the handler
    if (event_type && event_data) {
      http_server.stats.last_sync_event = current_ms();
      handle_beacon_event(event_type, event_data);
      safe_free(event_type);
      safe_free(event_data);
      event_type = NULL;
      event_data = NULL;
    }
    else {
      // Malformed event? Log or ignore. Free any partial data.
      safe_free(event_type);
      safe_free(event_data);
    }

    // Advance processed length past the event block and the "\n\n"
    processed_len += event_block_len;
    // skip any trailing whitespace
    while (processed_len < buf_len && buf_data[processed_len] && buf_data[processed_len] < 14) processed_len++;
  }

  // Remove processed data from the buffer
  if (processed_len > 0) {
    // remove the processed data until processed_len from the buffer
    buffer_splice(&watcher_state.buffer, 0, processed_len, NULL_BYTES);
  }
}

// libcurl write callback for SSE data
static size_t sse_write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  beacon_watcher_state_t* state      = (beacon_watcher_state_t*) userdata;
  size_t                  total_size = size * nmemb;

  log_debug("Beacon watcher received %d bytes", (int) total_size);

  // Guard against writes after stop
  if (!state || !state->is_running) {
    log_warn("Beacon watcher write after stop (dropping %d bytes)", (int) total_size);
    return total_size; // swallow bytes to avoid curl error
  }

#ifdef TEST
  // Record SSE data when test_dir is set
  if (http_server.test_dir) {
    c4_record_sse_data(ptr, total_size);
  }
#endif

  // Append data to buffer
  if (!buffer_append(&state->buffer, bytes((uint8_t*) ptr, total_size))) {
    log_error("Failed to append data to beacon watcher buffer!");
    // Signal curl error? How? Returning 0 signals error to curl.
    return 0;
  }

  // Reset inactivity timer (we received data)
  uv_timer_start(&state->inactivity_timer, (uv_timer_cb) on_inactivity_timeout, INACTIVITY_TIMEOUT_MS, 0);

  // Parse buffer for complete events
  parse_sse_buffer();

  return total_size; // Tell curl we processed all data
}

// --- Timer Callbacks ---

static void on_inactivity_timeout(uv_timer_t* handle) {
#ifdef TEST
  if (test_disable_reconnect) {
    log_info("Inactivity timeout in test mode - stopping watcher (no reconnect)");
    stop_beacon_watch();
    watcher_state.is_running = false;
    return;
  }
#endif

  log_warn("Beacon watcher inactivity timeout (%d ms)! Assuming connection lost.", INACTIVITY_TIMEOUT_MS);
  stop_beacon_watch();
  schedule_reconnect();
}

static void on_reconnect_timer(uv_timer_t* handle) {
  log_info("Attempting to reconnect beacon watcher...");
  start_beacon_watch(); // Try starting again
}

// --- User Handler ---

static void handle_beacon_event(char* event, char* data) {
  json_t json = json_parse(data);
  http_server.stats.beacon_events_total++;
  if (strcmp(event, "head") == 0) {
    http_server.stats.beacon_events_head++;
    log_info("Beacon Event Received: Type: " YELLOW("%s") " - Slot: " YELLOW("%j"), event, json_get(json, "slot"));
    c4_handle_new_head(json_parse(data));
  }
  else if (strcmp(event, "finalized_checkpoint") == 0) {
    http_server.stats.beacon_events_finalized++;
    log_info("Beacon Event Received: Type: " YELLOW("%s") " - Epoch: " YELLOW("%j"), event, json_get(json, "epoch"));
    c4_handle_finalized_checkpoint(json_parse(data));
  }
  else {
    log_warn("Unsupported Beacon Event Received: Type='%s'", event);
  }
}

// Called by libcurl when it wants to change the timeout interval
static int beacon_timer_callback(CURLM* multi, long timeout_ms, void* userp) {
  if (timeout_ms < 0) // Stop the timer if timeout_ms is -1
    uv_timer_stop(&beacon_curl_timer);
  else
    // Start or restart the timer
    // If timeout_ms is 0, libcurl wants to act immediately. Use a minimal timer value (1ms)
    // to yield to the event loop and then call curl_multi_socket_action.
    uv_timer_start(&beacon_curl_timer, beacon_curl_timeout_cb, (timeout_ms == 0) ? 1 : (uint64_t) timeout_ms, 0); // 0 repeat = one-shot

  return 0;
}

// Callback for the beacon_curl_timer (uv_timer_t)
static void beacon_curl_timeout_cb(uv_timer_t* handle) {
  int       running_handles;
  CURLMcode mc = curl_multi_socket_action(beacon_multi_handle, CURL_SOCKET_TIMEOUT, 0, &running_handles);
  if (mc != CURLM_OK) log_error("beacon_curl_timeout_cb: curl_multi_socket_action error: %s", curl_multi_strerror(mc));
  check_multi_info(); // Check if the timeout caused the transfer to complete
}

// Add/remove context helpers
static void add_curl_context(beacon_curl_context_t* context) {
  if (!context) return;
  context->next       = beacon_context_head;
  beacon_context_head = context;
}

static void remove_curl_context(beacon_curl_context_t* context) {
  if (!context) return;
  beacon_curl_context_t** cur = &beacon_context_head;
  while (*cur) {
    if (*cur == context) {
      *cur          = context->next;
      context->next = NULL;
      return;
    }
    cur = &((*cur)->next);
  }
}

// Helper to safely close poll handle and free context
static void destroy_poll_handle(uv_handle_t* handle) {
  beacon_curl_context_t* ctx = (beacon_curl_context_t*) handle->data;
  if (ctx) remove_curl_context(ctx);
  free_curl_context(ctx);
}

// Helper to free context
static void free_curl_context(beacon_curl_context_t* context) {
  if (context) {
    context->poll_handle.data = NULL; // Ensure handle->data is NULL before free to prevent use-after-free in poll_cb
    safe_free(context);
  }
}

// Called by libcurl when it wants to add/remove/modify socket polling
static int beacon_socket_callback(CURL* easy, curl_socket_t s, int action, void* userp, void* socketp) {
  beacon_curl_context_t* context = (beacon_curl_context_t*) socketp;
  uv_loop_t*             loop    = uv_default_loop(); // Assuming default loop

  // Ignore invalid sockets unless this is a REMOVE notification
  if (s == CURL_SOCKET_BAD && action != CURL_POLL_REMOVE) return 0;

  switch (action) {
    case CURL_POLL_IN:
    case CURL_POLL_OUT:
    case CURL_POLL_INOUT:
      if (!context) {
        // Create new context if none exists for this socket
        context         = (beacon_curl_context_t*) safe_calloc(1, sizeof(beacon_curl_context_t));
        context->sockfd = s;
        int rc          = uv_poll_init_socket(loop, &context->poll_handle, s);
        if (rc != 0) {
          log_error("uv_poll_init_socket failed for socket %d: %s", (int) s, uv_strerror(rc));
          safe_free(context);
          return -1; // signal error back to libcurl
        }
        context->poll_handle.data = context;                        // Link context to handle data
        curl_multi_assign(beacon_multi_handle, s, (void*) context); // Assign the context back to libcurl via socketp
        add_curl_context(context);
      }

      int events = 0;
      if (action == CURL_POLL_IN || action == CURL_POLL_INOUT) events |= UV_READABLE;
      if (action == CURL_POLL_OUT || action == CURL_POLL_INOUT) events |= UV_WRITABLE;

      // Start polling with the requested events
      uv_poll_start(&context->poll_handle, events, beacon_poll_cb);
      break;

    case CURL_POLL_REMOVE:
      if (context) {
        uv_poll_stop(&context->poll_handle);                                 // Stop polling and close the handle
        remove_curl_context(context);                                        // Unlink from active list before close
        uv_close((uv_handle_t*) &context->poll_handle, destroy_poll_handle); // Use uv_close for safe handle cleanup, free context in the callback
        curl_multi_assign(beacon_multi_handle, s, NULL);                     // Remove context association in libcurl
      }
      break;
    default:
      break; // Should not happen
  }
  return 0;
}

// Callback triggered by libuv when polled socket has events
static void beacon_poll_cb(uv_poll_t* handle, int status, int events) {
  // Check if handle is still valid (might have been closed)
  if (!handle || !handle->data) return;
  beacon_curl_context_t* context = (beacon_curl_context_t*) handle->data;

  if (status < 0) {
    log_error("beacon_poll_cb error on fd %d: %s", (int) context->sockfd, uv_strerror(status));
    // Stop and close the poll handle proactively to avoid polling an invalid fd
    uv_poll_stop(&context->poll_handle);
    // Clear libcurl's association for this socket to avoid stale pointers
    curl_multi_assign(beacon_multi_handle, context->sockfd, NULL);
    remove_curl_context(context); // Ensure it's unlinked from our list
    uv_close((uv_handle_t*) &context->poll_handle, destroy_poll_handle);
    stop_beacon_watch();
    schedule_reconnect();
    return;
  }

  int flags = 0;
  if (events & UV_READABLE) flags |= CURL_CSELECT_IN;
  if (events & UV_WRITABLE) flags |= CURL_CSELECT_OUT;

  int       running_handles;
  CURLMcode mc = curl_multi_socket_action(beacon_multi_handle, context->sockfd, flags, &running_handles);
  if (mc != CURLM_OK) {
    log_error("beacon_poll_cb: curl_multi_socket_action error: %s", curl_multi_strerror(mc));
    // This might indicate a bigger problem, maybe reconnect
    stop_beacon_watch();
    schedule_reconnect();
    return;
  }

  check_multi_info(); // Check if this action resulted in completion
}

// Check for completed transfers on the beacon multi handle
static void check_multi_info() {
  CURLMsg* msg;
  int      msgs_left;
  while ((msg = curl_multi_info_read(beacon_multi_handle, &msgs_left))) {
    if (msg->msg != CURLMSG_DONE || msg->easy_handle != watcher_state.easy_handle) continue;
    // Check if it's *our* watcher handle that finished
    if (watcher_state.error_buffer[0] != '\0')
      log_warn("Beacon watcher  failed with %s", watcher_state.error_buffer);

    log_warn("Beacon watcher connection finished/failed with result: %d (%s)",
             msg->data.result, curl_easy_strerror(msg->data.result));

    stop_beacon_watch();  // Ensure handle is properly cleaned up (stop_beacon_watch does this)
    schedule_reconnect(); // Schedule a reconnect attempt
  }
}

static char* join_paths(const char* path1, const char* path2) {
  if (!path1 || !path2) return NULL;
  return bprintf(NULL, "%s%s%s", path1, path1[strlen(path1) - 1] == '/' ? "" : "/", path2);
}

// --- Public Function ---

void c4_watch_beacon_events() {
  if (!eth_config.stream_beacon_events) return;
  if (BEACON_WATCHER_URL == NULL) {
    server_list_t* list = c4_get_server_list(C4_DATA_TYPE_BEACON_API);
    if (list->count == 0) {
      log_error("No beacon nodes configured!");
      return;
    }

    BEACON_WATCHER_URL = join_paths(list->urls[0], "eth/v1/events?topics=head,finalized_checkpoint");
    list->client_types[0] |= BEACON_CLIENT_EVENT_SERVER; // mark the first as Beacon Event Server
  }
  if (watcher_state.is_running) {
    log_warn("Beacon watcher already running.");
    return;
  }

  log_info("Initializing beacon watcher...");

  // Initialize multi handle for watcher
  if (!beacon_multi_handle) {
    beacon_multi_handle = curl_multi_init();
    if (!beacon_multi_handle) {
      log_error("curl_multi_init() failed for beacon watcher!");
      // Cleanup potential partial state?
      return;
    }
  }

  // Initialize state (only needed once conceptually, but good practice)
  watcher_state.easy_handle  = NULL;
  watcher_state.headers_list = NULL; // Initialize headers list
  // Initialize buffer (assuming create function)
  if (!watcher_state.buffer.data.data) // Check if buffer needs creation
    watcher_state.buffer.allocated = 1024;

  watcher_state.is_running = true; // Mark as attempting to run

  // Get default loop (assuming it's initialized in main)
  uv_loop_t* loop = uv_default_loop();
  if (!loop) {
    log_error("Cannot initialize beacon watcher: Default UV loop not available.");
    watcher_state.is_running = false;
    buffer_free(&watcher_state.buffer);
    return;
  }

  // Initialize timers (inactivity, reconnect, AND curl timer)
  uv_timer_init(loop, &watcher_state.inactivity_timer);
  watcher_state.inactivity_timer.data = &watcher_state;
  uv_timer_init(loop, &watcher_state.reconnect_timer);
  watcher_state.reconnect_timer.data = &watcher_state;
  uv_timer_init(loop, &beacon_curl_timer); // Initialize the CURL timer

  // --- Configure CURL Multi Handle for Libuv ---
  curl_multi_setopt(beacon_multi_handle, CURLMOPT_SOCKETFUNCTION, beacon_socket_callback);
  curl_multi_setopt(beacon_multi_handle, CURLMOPT_SOCKETDATA, NULL); // Can pass data if needed
  curl_multi_setopt(beacon_multi_handle, CURLMOPT_TIMERFUNCTION, beacon_timer_callback);
  curl_multi_setopt(beacon_multi_handle, CURLMOPT_TIMERDATA, NULL); // Can pass data if needed
  // -------------------------------------------

  // Start the first connection attempt
  start_beacon_watch();
}

// --- Helper Functions (Implement next) ---
static void start_beacon_watch() {
  // Prevent starting if already running or if multi handle isn't ready
  if (watcher_state.easy_handle) {
    log_warn("start_beacon_watch called, but easy_handle already exists. Ignoring.");
    return;
  }
  if (!beacon_multi_handle) {
    log_error("start_beacon_watch called, but beacon_multi_handle is NULL. Cannot start.");
    // Should c4_watch_beacon_events() be called first?
    return;
  }

  log_info("Starting beacon watch connection to %s...", BEACON_WATCHER_URL);

  watcher_state.easy_handle = curl_easy_init();
  if (!watcher_state.easy_handle) {
    log_error("curl_easy_init() failed for beacon watcher!");
    schedule_reconnect(); // Try again later
    return;
  }

  // Clear any previous error message
  watcher_state.error_buffer[0] = '\0';

  // Set CURL options
  curl_easy_setopt(watcher_state.easy_handle, CURLOPT_URL, BEACON_WATCHER_URL);
  curl_easy_setopt(watcher_state.easy_handle, CURLOPT_WRITEFUNCTION, sse_write_callback);
  curl_easy_setopt(watcher_state.easy_handle, CURLOPT_WRITEDATA, &watcher_state);
  curl_easy_setopt(watcher_state.easy_handle, CURLOPT_ERRORBUFFER, watcher_state.error_buffer);
  curl_easy_setopt(watcher_state.easy_handle, CURLOPT_PRIVATE, &watcher_state); // Link state via private pointer
  // Force IPv4 to avoid dual-stack connect races causing spurious EBADF on IPv6-only failures
  curl_easy_setopt(watcher_state.easy_handle, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

  // Set headers for SSE
  // Free previous list if somehow stop wasn't called properly
  if (watcher_state.headers_list) {
    curl_slist_free_all(watcher_state.headers_list);
    watcher_state.headers_list = NULL;
  }
  watcher_state.headers_list = curl_slist_append(watcher_state.headers_list, ACCEPT_HEADER);
  watcher_state.headers_list = curl_slist_append(watcher_state.headers_list, CACHE_CONTROL_HEADER);
  watcher_state.headers_list = curl_slist_append(watcher_state.headers_list, "Connection: keep-alive"); // Add Keep-Alive? Often default for HTTP/1.1, but can be explicit

  curl_easy_setopt(watcher_state.easy_handle, CURLOPT_HTTPHEADER, watcher_state.headers_list);
  curl_easy_setopt(watcher_state.easy_handle, CURLOPT_TCP_KEEPALIVE, 1L);
  curl_easy_setopt(watcher_state.easy_handle, CURLOPT_FOLLOWLOCATION, 1L); // Follow redirects if necessary

  // Add the handle to the beacon multi stack
  CURLMcode mc = curl_multi_add_handle(beacon_multi_handle, watcher_state.easy_handle);
  if (mc != CURLM_OK) {
    log_error("curl_multi_add_handle() failed for beacon watcher: %s", curl_multi_strerror(mc));
    // Cleanup resources allocated for this attempt
    if (watcher_state.headers_list) {
      curl_slist_free_all(watcher_state.headers_list);
      watcher_state.headers_list = NULL;
    }
    curl_easy_cleanup(watcher_state.easy_handle);
    watcher_state.easy_handle = NULL;
    schedule_reconnect(); // Try again later
    return;
  }

  // Start the inactivity timer ONLY after successfully adding the handle
  uv_timer_start(&watcher_state.inactivity_timer, (uv_timer_cb) on_inactivity_timeout, INACTIVITY_TIMEOUT_MS, 0);

  log_debug("Beacon watcher connection initiated and added to multi handle.");
}

// --- Test helpers ---
#ifdef TEST
bool c4_beacon_watcher_is_running(void) {
  return watcher_state.is_running;
}
#endif

static void stop_beacon_watch() {
  log_info("Stopping current beacon watch connection...");
  if (watcher_state.easy_handle) {
    if (beacon_multi_handle) {
      curl_multi_remove_handle(beacon_multi_handle, watcher_state.easy_handle);
    }
    curl_easy_cleanup(watcher_state.easy_handle);
    watcher_state.easy_handle = NULL;

    if (watcher_state.headers_list) {
      curl_slist_free_all(watcher_state.headers_list);
      watcher_state.headers_list = NULL;
    }
  }
  uv_timer_stop(&watcher_state.inactivity_timer);
  uv_timer_stop(&watcher_state.reconnect_timer); // Stop pending reconnect too
  // Stop CURL timeout timer to avoid actions after stop
  uv_timer_stop(&beacon_curl_timer);

  // Proactively close any remaining poll contexts to avoid stale handles
  while (beacon_context_head) {
    beacon_curl_context_t* ctx = beacon_context_head;
    beacon_context_head        = ctx->next; // unlink before closing
    uv_poll_stop(&ctx->poll_handle);
    uv_close((uv_handle_t*) &ctx->poll_handle, destroy_poll_handle);
  }
}

static void schedule_reconnect() {
#ifdef TEST
  if (test_disable_reconnect) {
    log_info("Reconnect disabled in test mode - stopping watcher");
    watcher_state.is_running = false;
    return;
  }
#endif

  log_info("Scheduling beacon watcher reconnect in %d ms", RECONNECT_DELAY_MS);
  uv_timer_start(&watcher_state.reconnect_timer, on_reconnect_timer, RECONNECT_DELAY_MS, 0);
}

void c4_stop_beacon_watcher() {
  log_info("Shutting down beacon watcher.");
  stop_beacon_watch();
  // wait a second to ensure the watcher is stopped
  watcher_state.is_running = false;
  uv_sleep(500);
  buffer_free(&watcher_state.buffer);

  // Cleanup multi handle
  if (beacon_multi_handle) {
    curl_multi_cleanup(beacon_multi_handle);
    beacon_multi_handle = NULL;
  }

  // Close timer handles properly (assuming loop is still running briefly)
  // Need proper close callbacks if handles might be active
  // uv_close((uv_handle_t*)&beacon_curl_timer, NULL);
  // uv_close((uv_handle_t*)&watcher_state.inactivity_timer, NULL);
  // uv_close((uv_handle_t*)&watcher_state.reconnect_timer, NULL);
}
