#include "verify.h"
#include "../util/json.h"
#include "../util/ssz.h"
#include VERIFIERS_PATH
#include <stdlib.h>
#include <string.h>

const ssz_def_t* c4_get_request_type(chain_type_t chain_type) {
  return request_container(chain_type);
}

c4_status_t c4_verify_init(verify_ctx_t* ctx, bytes_t request, char* method, json_t args, chain_id_t chain_id) {
  chain_type_t chain_type = c4_chain_type(chain_id);
  memset(ctx, 0, sizeof(verify_ctx_t));
  if (request.len == 0) {
    method_type_t method_type = c4_get_method_type(chain_id, method);
    if (method_type == METHOD_UNDEFINED) {
      ctx->state.error = strdup("method not known");
      return C4_ERROR;
    }
    else if (method_type == METHOD_NOT_SUPPORTED) {
      ctx->state.error = strdup("method not supported");
      return C4_ERROR;
    }
    else if (method_type == METHOD_PROOFABLE) {
      ctx->state.error = strdup("missing proof!");
      return C4_ERROR;
    }
    ctx->data.def             = &ssz_none;
    ctx->data.bytes.data      = &ssz_none;
    ctx->proof.def            = &ssz_none;
    ctx->proof.bytes.data     = &ssz_none;
    ctx->sync_data.def        = &ssz_none;
    ctx->sync_data.bytes.data = &ssz_none;
  }
  else {
    if (chain_type != c4_get_chain_type_from_req(request)) {
      ctx->state.error = strdup("chain type does not match the proof");
      return C4_ERROR;
    }
    ssz_ob_t req = {.bytes = request, .def = request_container(chain_type)};
    if (!req.def) {
      ctx->state.error = strdup("chain not supported");
      return C4_ERROR;
    }
    if (!ssz_is_valid(req, true, &ctx->state)) return C4_ERROR;
    ctx->data      = ssz_get(&req, "data");
    ctx->proof     = ssz_get(&req, "proof");
    ctx->sync_data = ssz_get(&req, "sync_data");
  }
  ctx->chain_id = chain_id; // ssz_get_uint64(&req, "chainId");
  ctx->method   = method;
  ctx->args     = args;
  return C4_SUCCESS;
}

c4_status_t c4_verify_from_bytes(verify_ctx_t* ctx, bytes_t request, char* method, json_t args, chain_id_t chain_id) {
  TRY_ASYNC(c4_verify_init(ctx, request, method, args, chain_id));
  return c4_verify(ctx);
}

c4_status_t c4_verify(verify_ctx_t* ctx) {
  // make sure the state is clean
  if (ctx->state.error) return C4_ERROR;
  if (c4_state_get_pending_request(&ctx->state)) return C4_PENDING;

  // verify the proof
  if (!handle_verification(ctx))
    ctx->state.error = bprintf(NULL, "verification for proof of chain %l is not supported", ctx->chain_id);

  return (ctx->state.error ? C4_ERROR : (c4_state_get_pending_request(&ctx->state) ? C4_PENDING : C4_SUCCESS));
}

chain_type_t c4_get_chain_type_from_req(bytes_t request) {
  if (request.len < 4) return C4_CHAIN_TYPE_ETHEREUM;
  return (chain_type_t) request.data[0];
}

const ssz_def_t* c4_get_req_type_from_req(bytes_t request) {
  return c4_get_request_type(c4_get_chain_type_from_req(request));
}

void c4_verify_free_data(verify_ctx_t* ctx) {
  if (ctx->flags & VERIFY_FLAG_FREE_DATA) {
    safe_free(ctx->data.bytes.data);
    ctx->data.bytes.data = NULL;
  }
  c4_state_free(&ctx->state);
}
