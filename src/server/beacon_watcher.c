#include "bytes.h"
#include "logger.h"
#include "proofer.h"
#include "server.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

static char* BEACON_WATCHER_URL = NULL;
// #define BEACON_WATCHER_URL    "https://lodestar-mainnet.chainsafe.io/eth/v1/events?topics=head,finalized_checkpoint"
#define ACCEPT_HEADER         "Accept: text/event-stream"
#define CACHE_CONTROL_HEADER  "Cache-Control: no-cache"
#define INACTIVITY_TIMEOUT_MS 30000
#define RECONNECT_DELAY_MS    5000

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
typedef struct {
  uv_poll_t     poll_handle;
  curl_socket_t sockfd;
} beacon_curl_context_t;

// --- Forward Declarations for new CURL/UV integration callbacks ---
static int  beacon_timer_callback(CURLM* multi, long timeout_ms, void* userp);
static int  beacon_socket_callback(CURL* easy, curl_socket_t s, int action, void* userp, void* socketp);
static void beacon_poll_cb(uv_poll_t* handle, int status, int events);
static void beacon_curl_timeout_cb(uv_timer_t* handle);
static void check_multi_info();
static void free_curl_context(beacon_curl_context_t* context);
static void destroy_poll_handle(uv_handle_t* handle);

// --- Callback Implementations ---

// Parses the buffer for complete SSE events
static void parse_sse_buffer() {
  char*  buf_data      = (char*) watcher_state.buffer.data.data;
  size_t buf_len       = watcher_state.buffer.data.len;
  size_t processed_len = 0;

  while (true) {
    // Find the end of an event block (\n\n)
    char* event_end = strnstr(buf_data + processed_len, "\n\n", buf_len - processed_len);
    if (!event_end) {
      break; // No complete event found in the remaining buffer
    }

    size_t event_block_len   = (event_end - (buf_data + processed_len));
    char*  event_block_start = buf_data + processed_len;

    // Process the event block line by line
    char* current_line = event_block_start;
    char* event_type   = NULL;
    char* event_data   = NULL;

    while (current_line < event_end) {
      char* next_newline = strchr(current_line, '\n');
      if (!next_newline) break; // Should not happen if event_end was found

      size_t line_len = next_newline - current_line;

      if (strncmp(current_line, "event:", 6) == 0) {
        free(event_type);                                     // Free previous if any
        event_type = strndup(current_line + 7, line_len - 7); // Skip "event: "
      }
      else if (strncmp(current_line, "data:", 5) == 0) {
        free(event_data);                                     // Free previous if any
        event_data = strndup(current_line + 6, line_len - 6); // Skip "data: "
      }
      // Ignore other lines (like comments starting with ':')

      current_line = next_newline + 1;
    }

    // If we have both event and data, call the handler
    if (event_type && event_data) {
      handle_beacon_event(event_type, event_data);
      free(event_type);
      free(event_data);
      event_type = NULL;
      event_data = NULL;
    }
    else {
      // Malformed event? Log or ignore. Free any partial data.
      free(event_type);
      free(event_data);
    }

    // Advance processed length past the event block and the "\n\n"
    processed_len += event_block_len + 2;
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

  log_debug("Beacon watcher received %zu bytes", total_size);

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
  log_warn("Beacon watcher inactivity timeout (%d ms)! Assuming connection lost.", INACTIVITY_TIMEOUT_MS);
  stop_beacon_watch();
  schedule_reconnect();
}

static void on_reconnect_timer(uv_timer_t* handle) {
  log_info("Attempting to reconnect beacon watcher...");
  start_beacon_watch(); // Try starting again
}

// --- User Handler ---

// TODO: Implement actual logic here (e.g., parse JSON, call invalidate)
static void handle_beacon_event(char* event, char* data) {
  log_info("Beacon Event Received: Type='%s'", event);
  if (strcmp(event, "head") == 0) {
    c4_handle_new_head(json_parse(data));
  }
  else {
    log_warn("Unsupported Beacon Event Received: Type='%s'", event);
  }
  // log_debug("Data: %s", data); // Can be very verbose

  // --- !!! ---
  // --- PLACEHOLDER: Add logic here ---
  // Example: If event is "head", parse data JSON, get key, call invalidate
  // if (strcmp(event, "head") == 0) {
  //    // Parse JSON in 'data'
  //    // Construct the 'Slatest' key
  //    // bytes32_t slatest_key; ...
  //    // c4_proofer_cache_invalidate(slatest_key);
  // }
  // --- !!! ---
}

// --- New CURL/UV Integration Callbacks ---

// Called by libcurl when it wants to change the timeout interval
static int beacon_timer_callback(CURLM* multi, long timeout_ms, void* userp) {
  if (timeout_ms < 0) {
    // Stop the timer if timeout_ms is -1
    uv_timer_stop(&beacon_curl_timer);
  }
  else {
    // Start or restart the timer
    // If timeout_ms is 0, libcurl wants to act immediately. Use a minimal timer value (1ms)
    // to yield to the event loop and then call curl_multi_socket_action.
    uint64_t delay = (timeout_ms == 0) ? 1 : (uint64_t) timeout_ms;
    uv_timer_start(&beacon_curl_timer, beacon_curl_timeout_cb, delay, 0); // 0 repeat = one-shot
  }
  return 0;
}

// Callback for the beacon_curl_timer (uv_timer_t)
static void beacon_curl_timeout_cb(uv_timer_t* handle) {
  int       running_handles;
  CURLMcode mc = curl_multi_socket_action(beacon_multi_handle, CURL_SOCKET_TIMEOUT, 0, &running_handles);
  if (mc != CURLM_OK) {
    log_error("beacon_curl_timeout_cb: curl_multi_socket_action error: %s", curl_multi_strerror(mc));
  }
  check_multi_info(); // Check if the timeout caused the transfer to complete
}

// Helper to safely close poll handle and free context
static void destroy_poll_handle(uv_handle_t* handle) {
  beacon_curl_context_t* context = (beacon_curl_context_t*) handle->data;
  free_curl_context(context);
}

// Helper to free context
static void free_curl_context(beacon_curl_context_t* context) {
  if (context) {
    // Ensure handle->data is NULL before free to prevent use-after-free in poll_cb
    context->poll_handle.data = NULL;
    free(context);
  }
}

// Called by libcurl when it wants to add/remove/modify socket polling
static int beacon_socket_callback(CURL* easy, curl_socket_t s, int action, void* userp, void* socketp) {
  beacon_curl_context_t* context = (beacon_curl_context_t*) socketp;
  uv_loop_t*             loop    = uv_default_loop(); // Assuming default loop

  switch (action) {
    case CURL_POLL_IN:
    case CURL_POLL_OUT:
    case CURL_POLL_INOUT:
      if (!context) {
        // Create new context if none exists for this socket
        context         = (beacon_curl_context_t*) calloc(1, sizeof(beacon_curl_context_t));
        context->sockfd = s;
        uv_poll_init_socket(loop, &context->poll_handle, s);
        context->poll_handle.data = context; // Link context to handle data
        // Assign the context back to libcurl via socketp
        curl_multi_assign(beacon_multi_handle, s, (void*) context);
      }

      int events = 0;
      if (action == CURL_POLL_IN || action == CURL_POLL_INOUT) events |= UV_READABLE;
      if (action == CURL_POLL_OUT || action == CURL_POLL_INOUT) events |= UV_WRITABLE;

      // Start polling with the requested events
      uv_poll_start(&context->poll_handle, events, beacon_poll_cb);
      break;

    case CURL_POLL_REMOVE:
      if (context) {
        // Stop polling and close the handle
        uv_poll_stop(&context->poll_handle);
        // Use uv_close for safe handle cleanup, free context in the callback
        uv_close((uv_handle_t*) &context->poll_handle, destroy_poll_handle);
        // Remove context association in libcurl
        curl_multi_assign(beacon_multi_handle, s, NULL);
      }
      break;
    default:
      // Should not happen
      break;
  }
  return 0;
}

// Callback triggered by libuv when polled socket has events
static void beacon_poll_cb(uv_poll_t* handle, int status, int events) {
  // Check if handle is still valid (might have been closed)
  if (!handle || !handle->data) {
    return;
  }
  beacon_curl_context_t* context = (beacon_curl_context_t*) handle->data;

  if (status < 0) {
    log_error("beacon_poll_cb error: %s", uv_strerror(status));
    // What to do here? Maybe trigger reconnect?
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
    if (msg->msg == CURLMSG_DONE) {
      CURL* easy = msg->easy_handle;
      // Check if it's *our* watcher handle that finished
      if (easy == watcher_state.easy_handle) {
        CURLcode result = msg->data.result;
        log_warn("Beacon watcher connection finished/failed with result: %d (%s)",
                 result, curl_easy_strerror(result));

        // Ensure handle is properly cleaned up (stop_beacon_watch does this)
        stop_beacon_watch();
        // Schedule a reconnect attempt
        schedule_reconnect();
      }
      // Ignore completions of other handles if any were somehow added
    }
  }
}

// --- Public Function ---

void c4_watch_beacon_events() {
  if (!http_server.stream_beacon_events) return;
  if (BEACON_WATCHER_URL == NULL)
    BEACON_WATCHER_URL = bprintf(NULL, "%seth/v1/events?topics=head,finalized_checkpoint", http_server.beacon_nodes);
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

  // Set headers for SSE
  // Free previous list if somehow stop wasn't called properly
  if (watcher_state.headers_list) {
    curl_slist_free_all(watcher_state.headers_list);
    watcher_state.headers_list = NULL;
  }
  watcher_state.headers_list = curl_slist_append(watcher_state.headers_list, ACCEPT_HEADER);
  watcher_state.headers_list = curl_slist_append(watcher_state.headers_list, CACHE_CONTROL_HEADER);
  // Add Keep-Alive? Often default for HTTP/1.1, but can be explicit
  // watcher_state.headers_list = curl_slist_append(watcher_state.headers_list, "Connection: keep-alive");
  curl_easy_setopt(watcher_state.easy_handle, CURLOPT_HTTPHEADER, watcher_state.headers_list);

  // Follow redirects if necessary
  curl_easy_setopt(watcher_state.easy_handle, CURLOPT_FOLLOWLOCATION, 1L);

  // Consider adding verbose logging for debugging connection issues initially
  // curl_easy_setopt(watcher_state.easy_handle, CURLOPT_VERBOSE, 1L);

  // No specific CURL timeout, rely on the libuv inactivity timer

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

static void stop_beacon_watch() {
  log_info("Stopping current beacon watch connection...");
  // TODO: Stop timers, remove from multi_handle, curl_easy_cleanup
  if (watcher_state.easy_handle) {
    // ... cleanup logic ...
    watcher_state.easy_handle = NULL;
  }
  uv_timer_stop(&watcher_state.inactivity_timer);
  uv_timer_stop(&watcher_state.reconnect_timer); // Stop pending reconnect too
}

static void schedule_reconnect() {
  log_info("Scheduling beacon watcher reconnect in %d ms", RECONNECT_DELAY_MS);
  uv_timer_start(&watcher_state.reconnect_timer, on_reconnect_timer, RECONNECT_DELAY_MS, 0);
}

// Optional: Add a function to call on server shutdown
void c4_stop_beacon_watcher() {
  log_info("Shutting down beacon watcher.");
  stop_beacon_watch();
  buffer_free(&watcher_state.buffer);
  watcher_state.is_running = false;

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
