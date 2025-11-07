/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "server.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>

// Callback for proxied requests
static void rpc_callback(client_t* client, void* data, data_request_t* req) {
  // Check if client is still valid before responding
  if (!client || client->being_closed) {
    log_warn("Client is no longer valid or is being closed - discarding proxy response");
    // Clean up resources
    if (req) {
      safe_free(req->response.data);
      safe_free(req->error);
      safe_free(req);
    }
    return;
  }

  if (req->response.data)
    c4_http_respond(client, 200, "application/json", req->response);
  else
    c4_write_error_response(client, 500, req->error);
  safe_free(req->response.data);
  safe_free(req->error);
  safe_free(req);
}

bool c4_handle_unverified_rpc_request(client_t* client) {
  if (client->request.method != C4_DATA_METHOD_POST || strncmp(client->request.path, "/unverified_rpc", 16) != 0) return false;
  data_request_t* req = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
  req->chain_id       = http_server.chain_id;
  req->method         = C4_DATA_METHOD_POST;
  req->type           = C4_DATA_TYPE_ETH_RPC;
  req->encoding       = C4_DATA_ENCODING_JSON;
  req->payload        = bytes(client->request.payload, client->request.payload_len);

  c4_add_request(client, req, NULL, rpc_callback);
  return true;
}
