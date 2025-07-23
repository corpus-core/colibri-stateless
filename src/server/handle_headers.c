/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */



#include "logger.h"
#include "server.h"

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
    c4_http_respond(client, 200, "application/json", req->response);
  else {
    buffer_t buf = {0};
    bprintf(&buf, "{\"error\":\"%s\"}", req->error);
    c4_http_respond(client, 500, "application/json", buf.data);
    buffer_free(&buf);
  }
  safe_free(req->url);
  safe_free(req->response.data);
  safe_free(req->error);
  safe_free(req);
}

bool c4_proxy(client_t* client) {
  const char* path = "/eth/v1/beacon/headers/";
  if (strncmp(client->request.path, path, strlen(path)) != 0) return false;
  data_request_t* req = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
  req->url            = strdup(client->request.path + 1);
  req->method         = C4_DATA_METHOD_GET;
  req->chain_id       = C4_CHAIN_MAINNET;
  req->type           = C4_DATA_TYPE_BEACON_API;
  req->encoding       = C4_DATA_ENCODING_JSON;
  c4_add_request(client, req, NULL, c4_proxy_callback);
  return true;
}
