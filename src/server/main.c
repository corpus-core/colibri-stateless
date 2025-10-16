/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "server.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
  c4_configure(argc, argv);

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