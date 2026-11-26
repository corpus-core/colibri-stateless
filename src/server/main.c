/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "../util/version.h"
#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[]) {
  // Check for --version before configure
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
      c4_print_version(stdout, "colibri-server");
      return 0;
    }
  }

  // libuv creates its thread pool lazily on first use with UV_THREADPOOL_SIZE threads (default: 4), shared by
  // the proof workers (uv_queue_work) and all file io of the period store. With only 4 threads cpu-heavy proofs
  // queue up while most cores stay idle, so unless the operator set the variable we size it to the available cores.
  // This must happen before anything touches the pool.
  char   threadpool_size[16] = {0};
  size_t threadpool_len      = sizeof(threadpool_size);
  if (uv_os_getenv("UV_THREADPOOL_SIZE", threadpool_size, &threadpool_len) == UV_ENOENT) {
    unsigned int cores = uv_available_parallelism();
    if (cores < 4) cores = 4;
    if (cores > 64) cores = 64;
    snprintf(threadpool_size, sizeof(threadpool_size), "%u", cores);
    uv_os_setenv("UV_THREADPOOL_SIZE", threadpool_size);
  }

  // c4_configure returns true when --help/-h was handled; it does not call
  // exit() so tests can keep the process alive. In production we terminate
  // here, otherwise the server would start despite the user asking for usage.
  if (c4_configure(argc, argv)) return 0;

  server_instance_t instance;
  int               result = c4_server_start(&instance, http_server.port);
  if (result != 0) {
    return result;
  }

  // Run event loop - blocks until server stops
  uv_run(instance.loop, UV_RUN_DEFAULT);

  c4_server_stop(&instance);

  // Attempt to close the loop
  // After c4_server_stop, all handles should be closed, so this should succeed
  uv_loop_close(instance.loop);
  //  if (close_rc != 0) {
  //    fprintf(stderr, "Warning: uv_loop_close failed (code: %d) - some handles may still be open\n", close_rc);
  // DO NOT run uv_run again here - memcache has already been freed by c4_cleanup_curl()
  // Running the loop again would cause use-after-free errors
  // If this happens, it indicates a bug in the shutdown sequence
  //  }

  return 0;
}