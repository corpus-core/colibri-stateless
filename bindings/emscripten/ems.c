#include "plugin.h"
#include "proofer.h"
#include "sync_committee.h"
#include "verify.h"
#include <emscripten.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

proofer_ctx_t* EMSCRIPTEN_KEEPALIVE c4w_create_proof_ctx(char* method, char* args, uint64_t chain_id) {
  return c4_proofer_create(method, args, chain_id);
}

void EMSCRIPTEN_KEEPALIVE c4w_free_proof_ctx(proofer_ctx_t* ctx) {
  c4_proofer_free(ctx);
}
static const char* status_to_string(c4_status_t status) {
  switch (status) {
    case C4_SUCCESS:
      return "success";
    case C4_ERROR:
      return "error";
    case C4_PENDING:
      return "waiting";
  }
}

static const char* encoding_to_string(data_request_encoding_t encoding) {
  switch (encoding) {
    case C4_DATA_ENCODING_SSZ:
      return "ssz";
    case C4_DATA_ENCODING_JSON:
      return "json";
  }
}

static const char* method_to_string(data_request_method_t method) {
  switch (method) {
    case C4_DATA_METHOD_GET:
      return "get";
    case C4_DATA_METHOD_POST:
      return "post";
    case C4_DATA_METHOD_PUT:
      return "put";
    case C4_DATA_METHOD_DELETE:
      return "delete";
  }
}

static const char* data_request_type_to_string(data_request_type_t type) {
  switch (type) {
    case C4_DATA_TYPE_BEACON_API:
      return "beacon_api";
    case C4_DATA_TYPE_ETH_RPC:
      return "eth_rpc";
    case C4_DATA_TYPE_REST_API:
      return "rest_api";
  }
}
static void add_data_request(buffer_t* result, data_request_t* data_request) {
  bprintf(result, "{\"req_ptr\": %l,", (uint64_t) data_request);
  bprintf(result, "\"chain_id\": %d,", (uint32_t) data_request->chain_id);
  bprintf(result, "\"encoding\": \"%s\",", encoding_to_string(data_request->encoding));
  bprintf(result, "\"exclude_mask\": \"%d\",", (uint32_t) data_request->node_exclude_mask);
  bprintf(result, "\"method\": \"%s\",", method_to_string(data_request->method));
  bprintf(result, "\"url\": \"%s\",", data_request->url);
  if (data_request->payload.data)
    bprintf(result, "\"payload\": %j,", (json_t) {.len = data_request->payload.len, .start = (char*) data_request->payload.data, .type = JSON_TYPE_OBJECT});
  bprintf(result, "\"type\": \"%s\"}", data_request_type_to_string(data_request->type));
}

char* EMSCRIPTEN_KEEPALIVE c4w_execute_proof_ctx(proofer_ctx_t* ctx) {
  buffer_t    result = {0};
  c4_status_t status = c4_proofer_execute(ctx);
  bprintf(&result, "{\"status\": \"%s\",", status_to_string(status));
  switch (status) {
    case C4_SUCCESS:
      bprintf(&result, "\"result\": %l, \"result_len\": %d", (uint64_t) ctx->proof.data, ctx->proof.len);
      break;
    case C4_ERROR:
      bprintf(&result, "\"error\": %\"s\"", ctx->state.error);
      break;
    case C4_PENDING: {
      bprintf(&result, "\"requests\": [");
      data_request_t* data_request = c4_state_get_pending_request(&ctx->state);
      while (data_request) {
        if (!data_request->response.data && !data_request->error) {
          if (result.data.data[result.data.len - 1] != '[') bprintf(&result, ",");
          add_data_request(&result, data_request);
        }
        data_request = data_request->next;
      }

      bprintf(&result, "]");
      break;
    }
  }
  bprintf(&result, "}");
  return buffer_as_string(result);
}

void EMSCRIPTEN_KEEPALIVE c4w_req_set_response(data_request_t* ctx, void* data, size_t len, uint16_t node_index) {
  ctx->response            = bytes(data, len);
  ctx->response_node_index = node_index;
}

void EMSCRIPTEN_KEEPALIVE c4w_req_set_error(data_request_t* ctx, char* error, uint16_t node_index) {
  ctx->error               = strdup(error);
  ctx->response_node_index = node_index;
}

char* EMSCRIPTEN_KEEPALIVE c4w_verify_proof(uint8_t* proof, size_t proof_len, char* method, char* args, uint64_t chain_id) {
  verify_ctx_t ctx = {0};
  buffer_t     buf = {0};
  c4_verify_from_bytes(&ctx, bytes(proof, proof_len), method, json_parse(args), chain_id);

  if (ctx.success)
    bprintf(&buf, "{\"result\": %z}", ctx.data);
  else if (ctx.first_missing_period) {
    char url[200] = {0};
    sprintf(url, "eth/v1/beacon/light_client/updates?start_period=%d&count=%d", (uint32_t) ctx.first_missing_period - 1, (uint32_t) (ctx.last_missing_period - ctx.first_missing_period + 1));
    data_request_t* req = malloc(sizeof(data_request_t));
    *req                = (data_request_t) {
                       .encoding = C4_DATA_ENCODING_SSZ,
                       .error    = NULL,
                       .id       = {0},
                       .method   = C4_DATA_METHOD_GET,
                       .payload  = {0},
                       .response = {0},
                       .type     = C4_DATA_TYPE_BEACON_API,
                       .url      = url,
                       .chain_id = chain_id};
    sha256(bytes((uint8_t*) url, strlen(url)), req->id);
    bprintf(&buf, "{\"error\": \"%s\", \"client_updates\": [", ctx.state.error);
    add_data_request(&buf, req);
    bprintf(&buf, "]}");
  }
  else
    bprintf(&buf, "{\"error\": \"%s\"}", ctx.state.error);

  if (ctx.state.error) free(ctx.state.error);

  return (char*) buf.data.data;
}

bool EMSCRIPTEN_KEEPALIVE c4w_handle_client_updates(data_request_t* client_update, uint64_t chain_id) {
  return c4_handle_client_updates(client_update->response, chain_id, NULL);
}

void EMSCRIPTEN_KEEPALIVE c4w_req_free(data_request_t* client_update) {
  if (client_update->error) free(client_update->error);
  if (client_update->response.data) free(client_update->response.data);
  free(client_update);
}

uint8_t* EMSCRIPTEN_KEEPALIVE c4w_buffer_alloc(buffer_t* buf, size_t len) {
  buffer_grow(buf, len + 1);
  buf->data.len = len;
  return buf->data.data;
}

char* EMSCRIPTEN_KEEPALIVE c4w_init_chain(uint64_t chain_id, char* trusted_block_hashes, data_request_t* requests) {
  buffer_t   buf    = {0};
  json_t     blocks = json_parse(trusted_block_hashes ? trusted_block_hashes : "[]");
  c4_state_t state  = {0};
  state.requests    = requests;

  c4_status_t status = c4_set_trusted_blocks(&state, blocks, chain_id);
  if (state.error) {
    bprintf(&buf, "{\"error\": \"%s\"}", state.error);
    c4_state_free(&state);
    return (char*) buf.data.data;
  }

  bprintf(&buf, "{\"req_ptr\": %d, \"requests\": [", (uint32_t) state.requests);
  requests = state.requests;
  while (requests) {
    if (!requests->error && !requests->response.data) {
      if (buf.data.data[buf.data.len - 1] != '[') bprintf(&buf, ",");
      add_data_request(&buf, requests);
    }
    requests = requests->next;
  }

  if (!c4_state_get_pending_request(&state)) c4_state_free(&state);
  return bprintf(&buf, "]}");
}

static bool file_get(char* key, buffer_t* buffer) {
  return EM_ASM_INT({
    var keyStr = UTF8ToString($0);
    var data = Module.storage.get(keyStr);
    if (data) {
      var bufferPtr =  _c4w_buffer_alloc($1,data.length);
      HEAPU8.set(data, bufferPtr);
      return 1;
    }
    return 0; }, key, buffer);
}

static void file_set(char* key, bytes_t data) {
  EM_ASM({
    var keyStr = UTF8ToString($0);
    var array = new Uint8Array($2);
    array.set(HEAPU8.subarray($1, $1 + $2));
    Module.storage.set(keyStr,array ); }, key, data.data, data.len);
}

static void file_delete(char* key) {
  EM_ASM({
    var keyStr = UTF8ToString($0);
    Module.storage.del(keyStr); }, key);
}

void EMSCRIPTEN_KEEPALIVE init_storage(void* ptr) {
  storage_plugin_t plgn = {
      .del             = file_delete,
      .get             = file_get,
      .set             = file_set,
      .max_sync_states = 3};
  c4_set_storage_config(&plgn);
}
