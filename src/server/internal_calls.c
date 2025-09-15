/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "server.h"

typedef bool (*call_handler)(single_request_t*);

static void finish(request_t* req) {
  for (size_t i = 0; i < req->request_count; i++) {
    if (c4_state_is_pending(req->requests[i].req)) return;
  }
  req->cb(req);
}

static void throw_error(single_request_t* r, const char* error, bool must_free) {
  r->req->error = must_free ? error : strdup(error);
  finish(r->parent);
}

static void call_store_cb(void* u_ptr, uint64_t period, bytes_t data, char* error) {
  single_request_t* r = u_ptr;
  r->req->response    = bytes_dup(data);
  finish(r->parent);
}

static bool call_store(single_request_t* r) {
  const char* path = "chain_store/";
  if (strncmp(r->req->url, path, strlen(path))) return false;
  char* query = r->req->url + strlen(path);
  c4_get_from_store(r->req->url + strlen(path), r, call_store_cb);
  return true;
}

// Callback for preconf data loading
static void call_preconf_cb(void* u_ptr, uint64_t block_number, bytes_t data, char* error) {
  single_request_t* r = u_ptr;
  if (error) {
    r->req->error = strdup(error);
  }
  else {
    r->req->response = bytes_dup(data);
  }
  finish(r->parent);
}

// Handler for preconf/{block_number} and preconf/latest requests
static bool call_preconf(single_request_t* r) {
  const char* path = "preconf/";
  if (strncmp(r->req->url, path, strlen(path))) return false;

  // Extract block identifier from URL: preconf/{block_number} or preconf/latest
  char* block_identifier = r->req->url + strlen(path);

  // Handle "latest" request
  if (strcmp(block_identifier, "latest") == 0 || strcmp(block_identifier, "pre_latest") == 0) {
    c4_get_preconf(http_server.chain_id, 0, block_identifier, r, call_preconf_cb);
    return true;
  }

  // Handle specific block number (support both hex and decimal)
  uint64_t block_number;
  if (strncmp(block_identifier, "0x", 2) == 0 || strncmp(block_identifier, "0X", 2) == 0) {
    // Parse as hex (skip 0x prefix)
    block_number = strtoull(block_identifier + 2, NULL, 16);
  }
  else {
    // Parse as decimal
    block_number = strtoull(block_identifier, NULL, 10);
  }

  if (block_number == 0) {
    throw_error(r, "Invalid block number in preconf request", false);
    return true;
  }

  // Use chain_id from http_server and load optimized preconf file
  c4_get_preconf(http_server.chain_id, block_number, NULL, r, call_preconf_cb);
  return true;
}

static call_handler call_handlers[] = {
    call_store,
    call_preconf,
};

void c4_handle_internal_request(single_request_t* r) {
  for (size_t i = 0; i < sizeof(call_handlers) / sizeof(call_handler); i++) {
    if (call_handlers[i](r)) return;
  }
  throw_error(r, bprintf(NULL, "Unsupported path for internal request: %s", r->req->url), true);
}
