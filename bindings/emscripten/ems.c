#include "plugin.h"
#include "proofer.h"
#include "sync_committee.h"
#include "verify.h"
#include <emscripten.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct {
  bytes_t      proof;
  verify_ctx_t verify;
} c4w_verify_ctx_t;

proofer_ctx_t* EMSCRIPTEN_KEEPALIVE c4w_create_proof_ctx(char* method, char* args, uint64_t chain_id, uint32_t flags) {
  return c4_proofer_create(method, args, chain_id, flags);
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

void EMSCRIPTEN_KEEPALIVE c4w_set_trusted_blockhashes(uint64_t chain_id, uint8_t* blockhashes, int len) {
  c4_eth_set_trusted_blockhashes(chain_id, bytes(blockhashes, len));
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

void* EMSCRIPTEN_KEEPALIVE c4w_create_verify_ctx(uint8_t* proof, size_t proof_len, char* method, char* args, uint64_t chain_id) {
  c4w_verify_ctx_t* ctx = calloc(1, sizeof(c4w_verify_ctx_t));
  ctx->proof            = bytes_dup(bytes(proof, proof_len));
  c4_verify_init(&ctx->verify, ctx->proof, strdup(method), args ? json_parse(strdup(args)) : ((json_t) {.len = 0, .start = "[]", .type = JSON_TYPE_ARRAY}), (chain_id_t) chain_id);

  return (void*) ctx;
}
void EMSCRIPTEN_KEEPALIVE c4w_free_verify_ctx(void* ptr) {
  c4w_verify_ctx_t* ctx = (c4w_verify_ctx_t*) ptr;
  if (ctx->verify.method) free((char*) ctx->verify.method);
  if (ctx->verify.args.len) free((char*) ctx->verify.args.start);
  if (ctx->proof.data) free(ctx->proof.data);
  c4_verify_free_data(&ctx->verify);
  free(ctx);
}
method_type_t EMSCRIPTEN_KEEPALIVE c4w_get_method_type(uint64_t chain_id, char* method) {
  return c4_get_method_type((chain_id_t) chain_id, method);
}

char* EMSCRIPTEN_KEEPALIVE c4w_verify_proof(void* ptr) {
  verify_ctx_t* ctx    = &((c4w_verify_ctx_t*) ptr)->verify;
  buffer_t      result = {0};
  c4_status_t   status = c4_verify(ctx);
  bprintf(&result, "{\"status\": \"%s\",", status_to_string(status));
  switch (status) {
    case C4_SUCCESS:
      bprintf(&result, "\"result\": %Z", ctx->data);
      break;
    case C4_ERROR:
      bprintf(&result, "\"error\": \"%s\"", ctx->state.error);
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
