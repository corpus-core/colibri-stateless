#include "state.h"
#include <stdlib.h>
#include <string.h>
void c4_state_free(c4_state_t* state) {
  data_request_t* data_request = state->requests;
  while (data_request) {
    data_request_t* next = data_request->next;
    if (data_request->url) free(data_request->url);
    if (data_request->error) free(data_request->error);
    if (data_request->payload.data) free(data_request->payload.data);
    if (data_request->response.data) free(data_request->response.data);
    free(data_request);
    data_request = next;
  }
  if (state->error) free(state->error);
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
    if (strcmp(data_request->url, url) == 0) return data_request;
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
