#include "verify.h"
#include "../util/json.h"
#include "../util/ssz.h"
#include VERIFIERS_PATH
#include <string.h>

const ssz_def_t* c4_get_request_type(chain_type_t chain_type) {
  return request_container(chain_type);
}

c4_status_t c4_verify_from_bytes(verify_ctx_t* ctx, bytes_t request, char* method, json_t args, chain_id_t chain_id) {
  chain_type_t chain_type = c4_chain_type(chain_id);
  if (chain_type != c4_get_chain_type_from_req(request)) {
    ctx->state.error = strdup("chain type does not match the proof");
    return C4_ERROR;
  }
  ssz_ob_t req = {.bytes = request, .def = request_container(chain_type)};
  memset(ctx, 0, sizeof(verify_ctx_t));
  if (!req.def) {
    ctx->state.error = strdup("chain not supported");
    return C4_ERROR;
  }
  if (!ssz_is_valid(req, true, &ctx->state)) return C4_ERROR;
  ctx->chain_id  = chain_id; // ssz_get_uint64(&req, "chainId");
  ctx->data      = ssz_get(&req, "data");
  ctx->proof     = ssz_get(&req, "proof");
  ctx->sync_data = ssz_get(&req, "sync_data");
  ctx->method    = method;
  ctx->args      = args;
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
