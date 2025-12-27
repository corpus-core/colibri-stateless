/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "beacon.h"
#include "handler.h"
#include "logger.h"
#include "period_store.h"
#include "prover/prover.h"

// extracts query parameters as uint64
uint64_t c4_get_query(char* query, char* param) {
  char* found = strstr(query, param);
  if (!found) return 0;
  found += strlen(param);
  if (*found == '=')
    found++;
  else
    return 0;
  char tmp[20] = {0};
  for (int i = 0; i < sizeof(tmp); i++) {
    if (!found[i] || found[i] == '&') break;
    tmp[i] = found[i];
  }
  return (uint64_t) atoll(tmp);
}

static void handle_lcu_updates_cb(void* user_data, bytes_t updates, char* error) {
  client_t* client = (client_t*) user_data;
  if (error) {
    c4_write_error_response(client, 500, error);
    safe_free(error);
    return;
  }
  c4_http_respond(client, 200, "application/octet-stream", updates);
  safe_free(updates.data);
}

bool c4_handle_lcu(client_t* client) {

  const char* path = "/eth/v1/beacon/light_client/updates?";
  if (strncmp(client->request.path, path, strlen(path)) != 0) return false;
  char*    query = client->request.path + strlen(path);
  uint64_t start = c4_get_query(query, "start_period");
  uint64_t count = c4_get_query(query, "count");

  if (!start || !count) {
    c4_write_error_response(client, 500, "Invalid arguments");
    return true;
  }

  // Aus dem Period-Store lesen; fehlende Perioden werden automatisch nachgeladen
  c4_get_light_client_updates(client, start, (uint32_t) count, handle_lcu_updates_cb);

  return true;
}

static void handle_internal_lcu_updates_cb(void* user_data, bytes_t updates, char* error) {
  single_request_t* r = (single_request_t*) user_data;
  if (error)
    r->req->error = error;
  else
    r->req->response = updates;
  c4_internal_call_finish(r);
}

bool c4_handle_lcu_updates(single_request_t* r) {
  const char* path = "lcu_updates";
  if (strncmp(r->req->url, path, strlen(path)) != 0) return false;
  // parse query: expected r->req->url == "lcu_updates?start_period=...&count=..."
  char* query = strchr(r->req->url, '?');
  if (!query) {
    r->req->error = strdup("Missing query string for lcu_updates");
    c4_internal_call_finish(r);
    return true;
  }
  query++; // skip '?'
  uint64_t start = c4_get_query(query, "start_period");
  uint64_t count = c4_get_query(query, "count");
  if (!start || !count) {
    r->req->error = strdup("Invalid start_period or count");
    c4_internal_call_finish(r);
    return true;
  }
  c4_get_light_client_updates(r, start, (uint32_t) count, handle_internal_lcu_updates_cb);
  return true;
}