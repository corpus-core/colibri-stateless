#include "colibri.h"
#include "../src/proofer/proofer.h"
#include "../src/util/plugin.h"
#include "../src/util/ssz.h"
#include "../src/verifier/sync_committee.h"
#include "../src/verifier/types_verify.h"
#include "../src/verifier/verify.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  json_t       trusted_blocks;
  verify_ctx_t ctx;
  bool         initialised;
} c4_verify_ctx_t;

proofer_t* create_proofer_ctx(char* method, char* params, uint64_t chain_id) {
  return (void*) c4_proofer_create(method, params, chain_id);
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
  ssz_ob_t         req = ssz_ob(C4_REQUEST_CONTAINER, bytes_dup(proof));
  ctx->ctx.chain_id    = chain_id;
  ctx->ctx.data        = ssz_get(&req, "data");
  ctx->ctx.proof       = ssz_get(&req, "proof");
  ctx->ctx.sync_data   = ssz_get(&req, "sync_data");
  ctx->ctx.method      = method ? strdup(method) : NULL;
  ctx->ctx.args        = args ? json_parse(strdup(args)) : ((json_t) {0});
  ctx->trusted_blocks  = trusted_block_hashes ? json_parse(strdup(trusted_block_hashes)) : ((json_t) {0});
  return (void*) ctx;
}

char* verify_execute_json_status(void* ptr) {
  buffer_t         buf    = {0};
  c4_verify_ctx_t* ctx    = (c4_verify_ctx_t*) ptr;
  data_request_t*  req    = c4_state_get_pending_request(&(ctx->ctx.state));
  c4_status_t      status = ctx->ctx.state.error ? C4_ERROR : (req ? C4_PENDING : C4_SUCCESS);

  // initialise the trusted blocks
  if (status == C4_SUCCESS && !ctx->initialised) {
    status = c4_set_trusted_blocks(&(ctx->ctx.state), ctx->trusted_blocks, ctx->ctx.chain_id);
    if (status == C4_SUCCESS) ctx->initialised = true;
  }

  // do we have some client updates to handle?
  if (status == C4_SUCCESS && ctx->ctx.first_missing_period) {
    buffer_t url = {0};
    bprintf(&url, "eth/v1/beacon/light_client/updates?start_period=%d&count=%d", (uint32_t) ctx->ctx.first_missing_period - 1, (uint32_t) (ctx->ctx.last_missing_period - ctx->ctx.first_missing_period + 1));
    data_request_t* req = c4_state_get_data_request_by_url(&(ctx->ctx.state), (char*) url.data.data);
    buffer_free(&url);
    if (req) {
      if (req->error) {
        buffer_t error       = {0};
        ctx->ctx.state.error = bprintf(&buf, "Error fetching the client updates: %s", req->error);
      }
      else if (!c4_handle_client_updates(req->response, ctx->ctx.chain_id, NULL))
        ctx->ctx.state.error = strdup("Error handling the client updates");

      ctx->ctx.first_missing_period = 0;
    }
    else
      ctx->ctx.state.error = strdup("No response to client update handle");

    status = ctx->ctx.state.error ? C4_ERROR : C4_SUCCESS;
  }

  // verify the proof
  if (status == C4_SUCCESS) {
    c4_verify(&ctx->ctx);
    if (!ctx->ctx.success) {
      if (ctx->ctx.first_missing_period) {
        buffer_t url = {0};
        bprintf(&url, "eth/v1/beacon/light_client/updates?start_period=%d&count=%d", (uint32_t) ctx->ctx.first_missing_period - 1, (uint32_t) (ctx->ctx.last_missing_period - ctx->ctx.first_missing_period + 1));

        data_request_t* req = malloc(sizeof(data_request_t));
        *req                = (data_request_t) {
                           .encoding = C4_DATA_ENCODING_SSZ,
                           .error    = NULL,
                           .id       = {0},
                           .method   = C4_DATA_METHOD_GET,
                           .payload  = {0},
                           .response = {0},
                           .type     = C4_DATA_TYPE_BEACON_API,
                           .url      = (char*) url.data.data,
                           .chain_id = ctx->ctx.chain_id};
        c4_state_add_request(&(ctx->ctx.state), req);
        if (ctx->ctx.state.error) {
          free(ctx->ctx.state.error);
          ctx->ctx.state.error = NULL;
        }
        status = C4_PENDING;
      }
      else
        status = C4_ERROR;
    }
  }

  bprintf(&buf, "{\"status\": \"%s\",", status_to_string(status));
  switch (status) {
    case C4_SUCCESS:
      bprintf(&buf, "\"result\": %Z,", ctx->ctx.data);
      break;
    case C4_ERROR:
      bprintf(&buf, "\"error\": %\"s\"", ctx->ctx.state.error);
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
  if (ctx->trusted_blocks.start) free((char*) ctx->trusted_blocks.start);
  if (ctx->ctx.method) free((char*) ctx->ctx.method);
  if (ctx->ctx.args.start) free((char*) ctx->ctx.args.start);
  c4_state_free(&(ctx->ctx.state));
  free(ctx);
}