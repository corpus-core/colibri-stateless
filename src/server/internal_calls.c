/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "server.h"

static void finish(request_t* req) {
  for (size_t i = 0; i < req->request_count; i++) {
    if (c4_state_is_pending(req->requests[i].req)) return;
  }
  req->cb(req);
}

void c4_internal_call_finish(single_request_t* r) {
  finish(r->parent);
}

static void throw_error(single_request_t* r, const char* error, bool must_free) {
  r->req->error = must_free ? (char*) error : strdup(error);
  finish(r->parent);
}

static call_handler* call_handlers       = NULL;
static size_t        call_handlers_count = 0;

void c4_register_internal_handler(call_handler handler) {
  call_handlers                        = safe_realloc(call_handlers, (call_handlers_count + 1) * sizeof(call_handler));
  call_handlers[call_handlers_count++] = handler;
}

void c4_handle_internal_request(single_request_t* r) {
  for (size_t i = 0; i < call_handlers_count; i++) {
    if (call_handlers[i](r)) return;
  }
  throw_error(r, bprintf(NULL, "Unsupported path for internal request: %s", r->req->url), true);
}
