/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "beacon.h"
#include "logger.h"
#include "server.h"

bool c4_handle_status(client_t* client) {
  buffer_t data = {0};
  uint64_t now  = current_ms();
  bprintf(&data,
          "{\"status\":\"ok\", "
          "\"stats\":{" // Start of stats object
          "\"total_requests\":%l, "
          "\"total_errors\":%l, "
          "\"last_sync_event\":%l, "
          "\"last_request_time\":%l, "
          "\"open_requests\":%l "
          "}", // End of stats object and main object
          http_server.stats.total_requests, http_server.stats.total_errors,
          (now - http_server.stats.last_sync_event) / 1000,
          (now - http_server.stats.last_request_time) / 1000,
          http_server.stats.open_requests);
  c4_http_respond(client, 200, "application/json", data.data);
  buffer_free(&data);
  return true;
}
