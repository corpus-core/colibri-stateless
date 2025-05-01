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
  const char* path = "eth/period_store?";
  if (strncmp(r->req->url, path, strlen(path))) return false;
  char*    query  = r->req->url + strlen(path);
  uint64_t period = c4_get_query(query, "period");
  uint64_t type   = c4_get_query(query, "type");
  uint32_t slot   = c4_get_query(query, "slot");
  if (period == 0 || type == 0)
    throw_error(r, "Invalid period or type", false);
  else
    c4_get_from_store(http_server.chain_id, period, type, slot, r, call_store_cb);
  return true;
}

static call_handler call_handlers[] = {
    call_store,
};

void c4_handle_internal_request(single_request_t* r) {
  for (size_t i = 0; i < sizeof(call_handlers) / sizeof(call_handler); i++) {
    if (call_handlers[i](r)) return;
  }
  throw_error(r, bprintf(NULL, "Unsupported path for internal request: %s", r->req->url), true);
}
