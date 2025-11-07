/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "../prover/prover.h"
#include "../util/version.h"
#include "server.h"
#include "logger.h"
#include "server_handlers.h"
#include <curl/curl.h>
#include <errno.h>
#include <llhttp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <uv.h>

// Macro for simplified libuv error handling
#define UV_CHECK(op, expr, instance)                                                       \
  do {                                                                                    \
    int r = (expr);                                                                       \
    if (r != 0) {                                                                         \
      if (r == UV_EADDRINUSE && strcmp(op, "TCP listening") == 0)                        \
        log_error("Error: Port %d is already in use", (uint32_t) (instance ? instance->port : 0)); \
      else                                                                                \
        log_error("Error: %s failed: %s (%s)", (op), uv_strerror(r), uv_err_name(r));    \
      return 1;                                                                           \
    }                                                                                     \
  } while (0)

static volatile sig_atomic_t shutdown_requested            = 0;
volatile sig_atomic_t        graceful_shutdown_in_progress = 0;

// Timer callback for prover cache cleanup
static void on_prover_cleanup_timer(uv_timer_t* handle) {
  c4_prover_cache_cleanup(current_ms(), 0);
}

// Idle callback to initialize server handlers after event loop starts
static void on_init_idle(uv_idle_t* handle) {
  uv_idle_stop(handle);
  c4_server_handlers_init(&http_server);
}

// Timer callback to check if graceful shutdown can proceed
static void on_graceful_shutdown_timer(uv_timer_t* handle) {
  server_instance_t* instance = (server_instance_t*) handle->data;
  if (http_server.stats.open_requests == 0) {
    log_info("C4 Server: All requests completed, proceeding with shutdown...");
    shutdown_requested = 1;
    uv_timer_stop(handle);
    if (instance && instance->loop) uv_stop(instance->loop);
  }
  else {
    log_info("C4 Server: Waiting for %l open requests to complete...",
             (uint64_t) http_server.stats.open_requests);
  }
}

// Signal callback to initiate graceful shutdown
static void on_signal(uv_signal_t* handle, int signum) {
  server_instance_t* instance = (server_instance_t*) handle->data;
  log_info("C4 Server: received signal %d — initiating graceful shutdown...", (uint32_t) signum);

  if (graceful_shutdown_in_progress) {
    log_warn("C4 Server: Graceful shutdown already in progress, forcing immediate shutdown...");
    shutdown_requested = 1;
    if (instance && instance->loop) uv_stop(instance->loop);
    return;
  }

  graceful_shutdown_in_progress = 1;

  if (http_server.stats.open_requests == 0) {
    log_info("C4 Server: No open requests, shutting down immediately...");
    shutdown_requested = 1;
    if (instance && instance->loop) uv_stop(instance->loop);
  }
  else {
    log_info("C4 Server: %l open requests detected, waiting for completion...",
             (uint64_t) http_server.stats.open_requests);
    static uv_timer_t graceful_timer;
    uv_timer_init(instance->loop, &graceful_timer);
    graceful_timer.data = instance;
    uv_timer_start(&graceful_timer, on_graceful_shutdown_timer, 1000, 1000);
  }
}

int c4_server_start(server_instance_t* instance, int port) {
  if (!instance) {
    log_error("Error: NULL server instance");
    return 1;
  }

  memset(instance, 0, sizeof(server_instance_t));
  instance->port = port > 0 ? port : http_server.port;
  instance->loop = uv_default_loop();

  if (!instance->loop) {
    log_error("Error: Failed to initialize default uv loop");
    return 1;
  }

  struct sockaddr_in addr;
  uint64_t           cleanup_interval_ms = 3000; // 3 seconds

  // Register http-handlers
  c4_register_http_handler(c4_handle_config_ui);
  c4_register_http_handler(c4_handle_get_config);
  c4_register_http_handler(c4_handle_post_config);
  c4_register_http_handler(c4_handle_restart_server);
  c4_register_http_handler(c4_handle_openapi);
  c4_register_http_handler(c4_handle_verify_request);
  c4_register_http_handler(c4_handle_unverified_rpc_request);
  c4_register_http_handler(c4_handle_proof_request);
  c4_register_http_handler(c4_handle_metrics);
  c4_register_http_handler(c4_handle_status);

  UV_CHECK("TCP initialization", uv_tcp_init(instance->loop, &instance->server), instance);
  UV_CHECK("IP address parsing", uv_ip4_addr(http_server.host, instance->port, &addr), instance);
  UV_CHECK("TCP binding", uv_tcp_bind(&instance->server, (const struct sockaddr*) &addr, 0), instance);
  UV_CHECK("TCP listening", uv_listen((uv_stream_t*) &instance->server, 128, c4_on_new_connection), instance);

  // Initialize curl timer
  UV_CHECK("Curl Timer initialization", uv_timer_init(instance->loop, &instance->curl_timer), instance);

  // Initialize and start the prover cleanup timer
  UV_CHECK("Prover Cleanup Timer initialization",
           uv_timer_init(instance->loop, &instance->prover_cleanup_timer), instance);
  UV_CHECK("Prover Cleanup Timer start",
           uv_timer_start(&instance->prover_cleanup_timer, on_prover_cleanup_timer,
                          cleanup_interval_ms, cleanup_interval_ms),
           instance);

  log_info("C4 Server %s starting on %s:%d", c4_client_version, http_server.host, (uint32_t) instance->port);

  // Initialize curl
  c4_init_curl(&instance->curl_timer);

  // Setup signal handlers for graceful shutdown
  UV_CHECK("SIGTERM handler init", uv_signal_init(instance->loop, &instance->sigterm_handle), instance);
  instance->sigterm_handle.data = instance;
  UV_CHECK("SIGTERM start", uv_signal_start(&instance->sigterm_handle, on_signal, SIGTERM), instance);

  UV_CHECK("SIGINT handler init", uv_signal_init(instance->loop, &instance->sigint_handle), instance);
  instance->sigint_handle.data = instance;
  UV_CHECK("SIGINT start", uv_signal_start(&instance->sigint_handle, on_signal, SIGINT), instance);

  // Setup idle handle to initialize server handlers
  UV_CHECK("Init idle handle init", uv_idle_init(instance->loop, &instance->init_idle_handle), instance);
  UV_CHECK("Init idle handle start", uv_idle_start(&instance->init_idle_handle, on_init_idle), instance);

  instance->is_running = true;
  log_info("C4 Server %s running on %s:%d", c4_client_version, http_server.host, (uint32_t) instance->port);

  return 0;
}

void c4_server_run_once(server_instance_t* instance) {
  if (!instance || !instance->is_running || !instance->loop) {
    return;
  }
  uv_run(instance->loop, UV_RUN_NOWAIT);
}

// Helper data structure for uv_walk during shutdown
typedef struct {
  uv_handle_t*       server_handle;
  server_instance_t* instance;
} walk_data_t;

// Helper function to close active client connections during shutdown
static void close_active_clients(uv_handle_t* handle, void* arg) {
  walk_data_t* walk_data = (walk_data_t*) arg;

  // Only close TCP handles that are client connections (not the server socket or other handles)
  if (handle->type == UV_TCP && handle != walk_data->server_handle && !uv_is_closing(handle)) {
    // Verify this is an HTTP client by checking the magic number
    // This prevents us from closing memcache or other TCP handles with the wrong callback
    client_t* client = (client_t*) handle->data;
    if (client && client->magic == C4_CLIENT_MAGIC) {
      log_info("C4 Server: Closing active client connection 0x%lx", (uint64_t) (uintptr_t) handle);
      uv_close(handle, c4_http_server_on_close_callback); // Properly free client_t in callback
    }
    // Memcache and other handles will be closed by their respective cleanup functions
  }
}

void c4_server_stop(server_instance_t* instance) {
  if (!instance || !instance->is_running) {
    return;
  }

  log_info("C4 Server: Stopping server...");
  instance->is_running = false;

  c4_server_handlers_shutdown(&http_server);

  // Stop head poller to avoid lingering libuv handles
  c4_stop_rpc_head_poller();

  // Stop and close timers
  uv_timer_stop(&instance->prover_cleanup_timer);
  uv_close((uv_handle_t*) &instance->prover_cleanup_timer, NULL);

  uv_timer_stop(&instance->curl_timer);
  uv_close((uv_handle_t*) &instance->curl_timer, NULL);

  // Stop accepting new connections
  uv_close((uv_handle_t*) &instance->server, NULL);

  // Close signal handles
  uv_signal_stop(&instance->sigterm_handle);
  uv_close((uv_handle_t*) &instance->sigterm_handle, NULL);
  uv_signal_stop(&instance->sigint_handle);
  uv_close((uv_handle_t*) &instance->sigint_handle, NULL);

  // Close all active client connections by walking through all handles
  // This ensures clean shutdown and prevents memory leaks from active connections
  walk_data_t walk_data = {(uv_handle_t*) &instance->server, instance};

  uv_walk(instance->loop, close_active_clients, &walk_data);

  // Let libuv process pending close callbacks (quick check)
  for (int i = 0; i < 10; i++) {
    if (uv_run(instance->loop, UV_RUN_NOWAIT) == 0) {
      break;
    }
    uv_sleep(10);
  }

  // Run loop until ALL handles are closed (blocking until complete)
  // This ensures memcache handles are fully closed before cleanup
  uv_run(instance->loop, UV_RUN_DEFAULT);

  // Cleanup CURL after all libuv handles are closed
  // IMPORTANT: Must be AFTER uv_run(DEFAULT) to avoid use-after-free
  // (curl cleanup frees memcache which contains libuv handles)
  c4_cleanup_curl();

  log_info("C4 Server stopped.");
}
