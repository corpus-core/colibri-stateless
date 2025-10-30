/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "handler.h"
#include "util/logger.h"
#include <stdlib.h>
#include <string.h>

// Callback for proxied requests
static void c4_proxy_callback(client_t* client, void* data, data_request_t* req) {
  // Check if client is still valid before responding
  if (!client || client->being_closed) {
    fprintf(stderr, "WARNING: Client is no longer valid or is being closed - discarding proxy response\n");
    // Clean up resources
    if (req) {
      safe_free(req->url);
      safe_free(req->response.data);
      safe_free(req->error);
      safe_free(req);
    }
    return;
  }

  if (req->response.data)
    c4_http_respond(client, 200, req->encoding == C4_DATA_ENCODING_SSZ ? "application/octet-stream" : "application/json", req->response);
  else
    c4_write_error_response(client, 500, req->error);
  safe_free(req->url);
  safe_free(req->response.data);
  safe_free(req->error);
  safe_free(req);
}

bool c4_proxy(client_t* client) {
  const char* path_headers              = "/eth/v1/beacon/headers/";
  const char* path_lightclient          = "/eth/v1/beacon/light_client";
  const char* path_finality_checkpoints = "/eth/v1/beacon/states/head/finality_checkpoints";

  if (strncmp(client->request.path, path_headers, strlen(path_headers)) != 0 && strncmp(client->request.path, path_lightclient, strlen(path_lightclient)) != 0 && strncmp(client->request.path, path_finality_checkpoints, strlen(path_finality_checkpoints)) != 0) return false;
  data_request_t* req = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
  req->url            = strdup(client->request.path + 1);
  req->method         = C4_DATA_METHOD_GET;
  req->chain_id       = http_server.chain_id;
  req->type           = C4_DATA_TYPE_BEACON_API;
  req->encoding       = C4_DATA_ENCODING_JSON;

  if (client->request.accept && strncmp(client->request.accept, "application/octet-stream", strlen("application/octet-stream")) == 0)
    req->encoding = C4_DATA_ENCODING_SSZ;
  c4_add_request(client, req, NULL, c4_proxy_callback);
  return true;
}
