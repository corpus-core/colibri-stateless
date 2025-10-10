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
  int close_rc = uv_loop_close(instance.loop);
  if (close_rc != 0) {
    uv_run(instance.loop, UV_RUN_DEFAULT);
    uv_loop_close(instance.loop);
  }

  return 0;
}