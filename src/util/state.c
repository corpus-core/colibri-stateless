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
#include <stdlib.h>
#include <string.h>
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
    if (memcmp(data_request->id, id, 32) == 0) return data_request;
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

void c4_state_add_request(c4_state_t* state, data_request_t* data_request) {
  if (bytes_all_zero(bytes(data_request->id, 32))) {
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

c4_status_t c4_state_add_error(c4_state_t* state, const char* error) {
  if (state->error)
    state->error = bprintf(NULL, "%s\n%s", state->error, error);
  else
    state->error = strdup(error);
  return C4_ERROR;
}

#ifdef TEST
char* c4_req_mockname(data_request_t* req) {
  buffer_t buf = {0};
  if (req->url) {
    bprintf(&buf, "%s", req->url);
  }
  else if (req->payload.data) {
    json_t t = json_parse((char*) req->payload.data);
    bprintf(&buf, "%j", json_get(t, "method"));
    json_t params = json_get(t, "params");
    for (int i = 0; i < json_len(params); i++)
      bprintf(&buf, "_%j", json_at(params, i));
  }

  for (int i = 0; i < buf.data.len; i++) {
    switch (buf.data.data[i]) {
      case '/':
      case '.':
      case ',':
      case ' ':
      case ':':
      case '=':
      case '?':
      case '"':
      case '&':
      case '[':
      case ']':
      case '{':
      case '}':
        buf.data.data[i] = '_';
        break;
      default:
        break;
    }
  }
  if (buf.data.len > 100) buf.data.len = 100;
  bprintf(&buf, ".%s", req->encoding == C4_DATA_ENCODING_SSZ ? "ssz" : "json");
  return (char*) buf.data.data;
}
#endif
