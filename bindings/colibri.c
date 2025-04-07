#include "colibri.h"
#include "beacon_types.h"
#include "plugin.h"
#include "proofer.h"
#include "ssz.h"
#include "sync_committee.h"
#include "verify.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  json_t       trusted_blocks;
  verify_ctx_t ctx;
  bytes_t      proof;
  bool         initialised;
} c4_verify_ctx_t;

proofer_t* create_proofer_ctx(char* method, char* params, uint64_t chain_id, uint32_t flags) {
  return (void*) c4_proofer_create(method, params, chain_id, flags);
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

char* proofer_execute_json_status(proofer_t* proofer) {
  buffer_t       result = {0};
  proofer_ctx_t* ctx    = (proofer_ctx_t*) proofer;
  c4_status_t    status = c4_proofer_execute(ctx);
  bprintf(&result, "{\"status\": \"%s\",", status_to_string(status));
  switch (status) {
    case C4_SUCCESS:
      bprintf(&result, "\"result\": \"0x%lx\", \"result_len\": %d", (uint64_t) ctx->proof.data, ctx->proof.len);
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

void free_proofer_ctx(proofer_t* ctx) {
  c4_proofer_free((proofer_ctx_t*) ctx);
}
void req_set_response(void* req_ptr, bytes_t data, uint16_t node_index) {
  data_request_t* ctx      = (data_request_t*) req_ptr;
  ctx->response            = bytes(data.data, data.len);
  ctx->response_node_index = node_index;
}

void req_set_error(void* req_ptr, char* error, uint16_t node_index) {
  data_request_t* ctx      = (data_request_t*) req_ptr;
  ctx->error               = strdup(error);
  ctx->response_node_index = node_index;
}

bytes_t proofer_get_proof(proofer_t* proofer) {
  proofer_ctx_t* ctx = (proofer_ctx_t*) proofer;
  return ctx->proof;
}

void* verify_create_ctx(bytes_t proof, char* method, char* args, uint64_t chain_id, char* trusted_block_hashes) {
  c4_verify_ctx_t* ctx = calloc(1, sizeof(c4_verify_ctx_t));
  ctx->proof           = bytes_dup(proof);
  c4_status_t status   = c4_verify_init(&ctx->ctx, ctx->proof, method ? strdup(method) : NULL, args ? json_parse(strdup(args)) : ((json_t) {0}), (chain_id_t) chain_id);
  ctx->trusted_blocks  = trusted_block_hashes ? json_parse(strdup(trusted_block_hashes)) : ((json_t) {0});
  return (void*) ctx;
}

char* verify_execute_json_status(void* ptr) {
  buffer_t         buf    = {0};
  c4_verify_ctx_t* ctx    = (c4_verify_ctx_t*) ptr;
  c4_status_t      status = c4_verify(&ctx->ctx);

  bprintf(&buf, "{\"status\": \"%s\",", status_to_string(status));
  switch (status) {
    case C4_SUCCESS:
      bprintf(&buf, "\"result\": %Z,", ctx->ctx.data);
      break;
    case C4_ERROR:
      bprintf(&buf, "\"error\": \"%s\"", ctx->ctx.state.error);
      break;
    case C4_PENDING: {
      bprintf(&buf, "\"requests\": [");
      data_request_t* data_request = c4_state_get_pending_request(&(ctx->ctx.state));
      while (data_request) {
        if (!data_request->response.data && !data_request->error) {
          if (buf.data.data[buf.data.len - 1] != '[') bprintf(&buf, ",");
          add_data_request(&buf, data_request);
        }
        data_request = data_request->next;
      }

      bprintf(&buf, "]");
      break;
    }
  }
  bprintf(&buf, "}");
  return buffer_as_string(buf);
}

void verify_free_ctx(void* ptr) {
  c4_verify_ctx_t* ctx = (c4_verify_ctx_t*) ptr;
  if (ctx->proof.data) free(ctx->proof.data);
  if (ctx->trusted_blocks.start) free((char*) ctx->trusted_blocks.start);
  if (ctx->ctx.method) free((char*) ctx->ctx.method);
  if (ctx->ctx.args.start) free((char*) ctx->ctx.args.start);
  c4_verify_free_data(&(ctx->ctx));
  free(ctx);
}