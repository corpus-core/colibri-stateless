#include "../../src/proofer/proofer.h"
#include <emscripten.h>
#include <string.h>
#include <time.h>

proofer_ctx_t* EMSCRIPTEN_KEEPALIVE c4w_create_proof_ctx(char* method, char* args, uint64_t chain_id) {
  return c4_proofer_create(method, args, chain_id);
}

void EMSCRIPTEN_KEEPALIVE c4w_free_proof_ctx(proofer_ctx_t* ctx) {
  c4_proofer_free(ctx);
}
static const char* status_to_string(c4_proofer_status_t status) {
  switch (status) {
    case C4_PROOFER_SUCCESS:
      return "success";
    case C4_PROOFER_ERROR:
      return "error";
    case C4_PROOFER_WAITING:
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
  bprintf(result, "\"method\": \"%s\",", method_to_string(data_request->method));
  bprintf(result, "\"url\": \"%s\",", data_request->url);
  if (data_request->payload.data)
    bprintf(result, "\"payload\": %j,", (json_t) {.len = data_request->payload.len, .start = (char*) data_request->payload.data, .type = JSON_TYPE_OBJECT});
  bprintf(result, "\"type\": \"%s\"}", data_request_type_to_string(data_request->type));
}

char* EMSCRIPTEN_KEEPALIVE c4w_execute_proof_ctx(proofer_ctx_t* ctx) {
  buffer_t            result = {0};
  c4_proofer_status_t status = c4_proofer_execute(ctx);
  bprintf(&result, "{\"status\": \"%s\",", status_to_string(status));
  switch (status) {
    case C4_PROOFER_SUCCESS:
      bprintf(&result, "\"result\": %l, \"result_len\": %d", (uint64_t) ctx->proof.data, ctx->proof.len);
      break;
    case C4_PROOFER_ERROR:
      bprintf(&result, "\"error\": %s", ctx->error);
      break;
    case C4_PROOFER_WAITING: {
      bprintf(&result, "\"requests\": [");
      data_request_t* data_request = c4_proofer_get_pending_data_request(ctx);
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
  return (char*) result.data.data;
}

void EMSCRIPTEN_KEEPALIVE c4w_req_set_response(data_request_t* ctx, void* data, size_t len) {
  ctx->response = bytes(data, len);
}

void EMSCRIPTEN_KEEPALIVE c4w_req_set_error(data_request_t* ctx, char* error) {
  ctx->error = strdup(error);
}
