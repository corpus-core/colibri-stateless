/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "../proofer/proofer.h" // Include for proofer cleanup and current_ms
#include "server.h"
#include <curl/curl.h>
#include <errno.h>
#include <llhttp.h>
#include <signal.h>
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

// File-scope handles so the signal handler can access them
static uv_timer_t            curl_timer;
static uv_timer_t            proofer_cleanup_timer;
static uv_tcp_t              server;
static uv_signal_t           sigterm_handle;
static uv_signal_t           sigint_handle;
static uv_loop_t*            loop               = NULL;
static volatile sig_atomic_t shutdown_requested = 0;

// Timer callback for proofer cache cleanup
static void on_proofer_cleanup_timer(uv_timer_t* handle) {
  c4_proofer_cache_cleanup(current_ms(), 0);
}

// Signal callback to initiate graceful shutdown
static void on_signal(uv_signal_t* handle, int signum) {
  (void) handle;
  fprintf(stderr, "C4 Server: received signal %d — initiating graceful shutdown...\n", signum);
  shutdown_requested = 1;
  // Break out of the event loop; cleanup will be performed after uv_run() returns
  if (loop) uv_stop(loop);
}

int main(int argc, char* argv[]) {
  c4_configure(argc, argv);
  // Force unbuffered output
  //  setvbuf(stdout, NULL, _IONBF, 0);
  //  setvbuf(stderr, NULL, _IONBF, 0);

  struct sockaddr_in addr;
  int                port      = http_server.port;
  loop                         = uv_default_loop();
  uint64_t cleanup_interval_ms = 3000; // 3 seconds

  // register http-handler
  c4_register_http_handler(c4_handle_proof_request);
  c4_register_http_handler(c4_handle_lcu);
  c4_register_http_handler(c4_handle_metrics);
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

  // Setup signal handlers for graceful shutdown (SIGTERM for Docker stop, SIGINT for Ctrl+C)
  UV_CHECK("SIGTERM handler init", uv_signal_init(loop, &sigterm_handle));
  UV_CHECK("SIGTERM start", uv_signal_start(&sigterm_handle, on_signal, SIGTERM));
  UV_CHECK("SIGINT handler init", uv_signal_init(loop, &sigint_handle));
  UV_CHECK("SIGINT start", uv_signal_start(&sigint_handle, on_signal, SIGINT));

  UV_CHECK("Event loop", uv_run(loop, UV_RUN_DEFAULT));

  // If shutdown was requested, perform graceful cleanup
  if (shutdown_requested) {
    // Stop and close timers and server
    uv_timer_stop(&proofer_cleanup_timer);
    uv_close((uv_handle_t*) &proofer_cleanup_timer, NULL);

    // curl integration timer
    uv_timer_stop(&curl_timer);
    uv_close((uv_handle_t*) &curl_timer, NULL);

    // Stop accepting new connections
    uv_close((uv_handle_t*) &server, NULL);

    // Cleanup CURL and related resources (may schedule uv_close on poll handles)
    c4_cleanup_curl();

    // Close signal handles
    uv_signal_stop(&sigterm_handle);
    uv_close((uv_handle_t*) &sigterm_handle, NULL);
    uv_signal_stop(&sigint_handle);
    uv_close((uv_handle_t*) &sigint_handle, NULL);

    // Let libuv process pending close callbacks
    uv_run(loop, UV_RUN_DEFAULT);

    fprintf(stderr, "C4 Server shut down gracefully.\n");
  }

  // Attempt to close the loop
  int close_rc = uv_loop_close(loop);
  if (close_rc != 0) {
    // As a fallback, drain any remaining handles
    uv_run(loop, UV_RUN_DEFAULT);
    uv_loop_close(loop);
  }

  return 0;
}