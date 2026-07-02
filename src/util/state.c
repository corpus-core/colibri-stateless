/*
 * Copyright (c) 2025 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include "state.h"
#include "logger.h"
#include "plugin.h"
#include "version.h"
#include <stdlib.h>
#include <string.h>

void c4_append_prover_request_props(buffer_t* payload, bytes_t client_state, chain_id_t chain_id, uint32_t flags, bytes_t witness_key) {
  if (!payload) return;
  bprintf(payload, ",\"version\":%d", c4_current_version_number());

  // Prefer the supplied snapshot; fall back to a fresh storage read if the caller passed NULL_BYTES.
  bytes_t cs       = client_state;
  bool    cs_owned = false;
  if (!cs.data || !cs.len) {
    cs       = c4_get_client_state(chain_id);
    cs_owned = true;
  }
  if (cs.data && cs.len)
    bprintf(payload, ",\"c4\":\"0x%x\"", cs);
  if (cs_owned && cs.data) safe_free(cs.data);

  if (flags & C4_PROVER_REQ_FLAG_ZK_PROOF)
    bprintf(payload, ",\"zk_proof\":true");
  if (flags & C4_PROVER_REQ_FLAG_INCLUDE_CODE)
    bprintf(payload, ",\"include_code\":true");
  if (witness_key.data && witness_key.len)
    bprintf(payload, ",\"signers\":\"0x%x\"", witness_key);
}

void c4_request_free(data_request_t* req) {
  if (!req) return;
  if (req->url) safe_free(req->url);
  if (req->error) safe_free(req->error);
  if (req->payload.data) safe_free(req->payload.data);
  if (req->response.data) safe_free(req->response.data);
  safe_free(req);
}

void c4_state_free(c4_state_t* state) {
  data_request_t* data_request = state->requests;
  while (data_request) {
    data_request_t* next = data_request->next;
    if (data_request->url) safe_free(data_request->url);
    if (data_request->error) safe_free(data_request->error);
    if (data_request->payload.data) safe_free(data_request->payload.data);
    if (data_request->response.data) safe_free(data_request->response.data);
    safe_free(data_request);
    data_request = next;
  }
  if (state->error) safe_free(state->error);
}

data_request_t* c4_state_get_data_request_by_id(c4_state_t* state, bytes32_t id) {
  data_request_t* data_request = state->requests;
  while (data_request) {
    if (memcmp(data_request->id, id, C4_BYTES32_SIZE) == 0) return data_request;
    data_request = data_request->next;
  }
  return NULL;
}

data_request_t* c4_state_get_data_request_by_url(c4_state_t* state, char* url) {
  data_request_t* data_request = state->requests;
  while (data_request) {
    if (data_request->url && strcmp(data_request->url, url) == 0) return data_request;
    data_request = data_request->next;
  }
  return NULL;
}

bool c4_state_is_pending(data_request_t* req) {
  return !req->error && !req->response.data;
}

bool c4_state_retry_after(data_request_t* req, uint32_t delay_ms, uint16_t max_retries) {
  if (!req || req->retry_count >= max_retries) return false;
  if (req->response.data) {
    safe_free(req->response.data);
    req->response = NULL_BYTES;
  }
  if (req->error) {
    safe_free(req->error);
    req->error = NULL;
  }
  // Retry on the SAME node: reset the host-side node selection. Hosts (e.g. the
  // curl host) resume from `response_node_index` and skip already-tried nodes;
  // without this reset the only oblivious node is skipped ("no more nodes to
  // try"). Unlike `RETRY_REQUEST`, we explicitly do NOT exclude the node.
  req->response_node_index = 0;
  req->node_exclude_mask   = 0;
  req->delay               = delay_ms;
  req->retry_count++;
  return true;
}

void c4_state_add_request(c4_state_t* state, data_request_t* data_request) {
  log_debug("adding request type %d : %s %r", data_request->type, data_request->url ? "url" : "rpc", data_request->url ? bytes(data_request->url, strlen(data_request->url)) : data_request->payload);
  if (bytes_all_zero(bytes(data_request->id, C4_BYTES32_SIZE))) {
    if (data_request->payload.len)
      sha256(data_request->payload, data_request->id);
    else
      sha256(bytes(data_request->url, strlen(data_request->url)), data_request->id);
  }
  data_request->next = state->requests;
  state->requests    = data_request;
}

data_request_t* c4_state_get_pending_request(c4_state_t* state) {
  data_request_t* data_request = state->requests;
  while (data_request) {
    if (c4_state_is_pending(data_request)) return data_request;
    data_request = data_request->next;
  }
  return NULL;
}

void c4_state_take_requests(c4_state_t* dst, c4_state_t* src) {
  if (!src || !src->requests) return;
  if (!dst) return;
  data_request_t* tail = src->requests;
  while (tail->next) tail = tail->next;
  tail->next    = dst->requests;
  dst->requests = src->requests;
  src->requests = NULL;
}

c4_status_t c4_state_add_error(c4_state_t* state, const char* error) {
  // NULL-Check: Use generic message if error is NULL
  if (!error) error = "Unknown error";
  if (state->error) {
    // Store old error pointer to free after creating new concatenated string
    char* old_error = state->error;
    state->error    = bprintf(NULL, "%s\n%s", old_error, error);
    safe_free(old_error); // Fix memory leak: free old error message
  }
  else {
    state->error = strdup(error);
  }
  return C4_ERROR;
}

#ifdef TEST
char* c4_req_mockname(data_request_t* req) {
  buffer_t buf = {0};

  // Generate base name from URL or RPC method/params
  if (req->url) {
    // Cache-friendly proof URLs of the form `proof/<method>/<block>/<version>/<zk|std>/<c4>`
    // contain fields that change independently of the underlying proof content: the client
    // version bumps with every release, the `zk|std` segment reflects a build flag, and `<c4>`
    // is the caller's local sync-committee snapshot. To keep test fixtures stable across those
    // dimensions we compress the URL to `proof/<method>/<block>` before sanitization.
    if (strncmp(req->url, "proof/", 6) == 0) {
      const char* p_method = req->url + 6;
      const char* p_block  = strchr(p_method, '/');
      const char* p_after  = p_block ? strchr(p_block + 1, '/') : NULL;
      if (p_block && p_after) {
        buffer_append(&buf, bytes((uint8_t*) "proof/", 6));
        buffer_append(&buf, bytes((uint8_t*) p_method, (uint32_t) (p_after - p_method)));
      }
      else {
        bprintf(&buf, "%s", req->url);
      }
    }
    else {
      bprintf(&buf, "%s", req->url);
    }
  }
  else if (req->payload.data) {
    // For RPC requests, use method name and parameters
    json_t t = json_parse((char*) req->payload.data);
    bprintf(&buf, "%j", json_get(t, "method"));
    json_t params = json_get(t, "params");
    for (int i = 0; i < json_len(params); i++)
      bprintf(&buf, "_%j", json_at(params, i));
  }

  // Sanitize filename: replace characters that are invalid or problematic in filenames
  // This ensures the mock filename can be safely used across different filesystems
  for (int i = 0; i < buf.data.len; i++) {
    switch (buf.data.data[i]) {
      case '/': // Path separator
      case '.': // Extension separator
      case ',': // Common separator
      case ' ': // Whitespace
      case ':': // Windows invalid char
      case '=': // Query string
      case '?': // Query string
      case '"': // Quote
      case '&': // Query string
      case '[': // Bracket
      case ']': // Bracket
      case '{': // Brace
      case '}': // Brace
        buf.data.data[i] = '_';
        break;
      default:
        break;
    }
  }

  // Truncate to maximum length to keep filenames manageable
  if (buf.data.len > C4_MAX_MOCKNAME_LEN) buf.data.len = C4_MAX_MOCKNAME_LEN;

  // Add file extension based on encoding type
  bprintf(&buf, ".%s", req->encoding == C4_DATA_ENCODING_SSZ ? "ssz" : "json");
  return (char*) buf.data.data;
}
#endif
