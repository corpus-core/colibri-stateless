#include "../proofer/proofer.h" // Include for proofer cleanup and current_ms
#include "server.h"
#include <curl/curl.h>
#include <errno.h>
#include <llhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <uv.h>

// Macro for simplified libuv error handling
#define UV_CHECK(op, expr)                                           \
  do {                                                               \
    int r = (expr);                                                  \
    if (r != 0) {                                                    \
      if (r == UV_EADDRINUSE && strcmp(op, "TCP listening") == 0)    \
        fprintf(stderr, "Error: Port %d is already in use\n", port); \
      else                                                           \
        fprintf(stderr, "Error: %s failed: %s (%s)\n",               \
                (op),                                                \
                uv_strerror(r),                                      \
                uv_err_name(r));                                     \
      return 1;                                                      \
    }                                                                \
  } while (0)

// Timer callback for proofer cache cleanup
static void on_proofer_cleanup_timer(uv_timer_t* handle) {
  c4_proofer_cache_cleanup(current_ms(), 0);
}

int main(int argc, char* argv[]) {
  c4_configure(argc, argv);
  // Force unbuffered output
  //  setvbuf(stdout, NULL, _IONBF, 0);
  //  setvbuf(stderr, NULL, _IONBF, 0);

  uv_timer_t         curl_timer;
  uv_timer_t         proofer_cleanup_timer;
  uv_tcp_t           server;
  struct sockaddr_in addr;
  int                port                = http_server.port;
  uv_loop_t*         loop                = uv_default_loop();
  uint64_t           cleanup_interval_ms = 3000; // 3 seconds

  // register http-handler
  c4_register_http_handler(c4_handle_proof_request);
  c4_register_http_handler(c4_handle_lcu);
  c4_register_http_handler(c4_proxy);
  c4_register_http_handler(c4_handle_status);

  if (!loop) {
    fprintf(stderr, "Error: Failed to initialize default uv loop\n");
    return 1;
  }

  UV_CHECK("TCP initialization", uv_tcp_init(loop, &server));
  UV_CHECK("IP address parsing", uv_ip4_addr("0.0.0.0", port, &addr));
  UV_CHECK("TCP binding", uv_tcp_bind(&server, (const struct sockaddr*) &addr, 0));
  UV_CHECK("TCP listening", uv_listen((uv_stream_t*) &server, 128, c4_on_new_connection));

  // Initialize curl timer (was 'timer')
  UV_CHECK("Curl Timer initialization", uv_timer_init(loop, &curl_timer));

  // Initialize and start the new proofer cleanup timer
  UV_CHECK("Proofer Cleanup Timer initialization", uv_timer_init(loop, &proofer_cleanup_timer));
  UV_CHECK("Proofer Cleanup Timer start", uv_timer_start(&proofer_cleanup_timer, on_proofer_cleanup_timer, cleanup_interval_ms, cleanup_interval_ms));
  fprintf(stderr, "Started proofer cache cleanup timer with interval %llu ms\n", cleanup_interval_ms);

  fprintf(stderr, "C4 Server running on port %d\n", port);

  // Initialize curl right before starting the event loop, passing the renamed timer handle
  c4_init_curl(&curl_timer);
  c4_watch_beacon_events();

  UV_CHECK("Event loop", uv_run(loop, UV_RUN_DEFAULT));

  // Stop timers before cleaning up
  uv_timer_stop(&proofer_cleanup_timer);
  // Assuming curl_timer is stopped within c4_cleanup_curl or doesn't need explicit stop here

  c4_cleanup_curl();
  return 0;
}