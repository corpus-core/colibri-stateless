#include "verify.h"
#include "../util/json.h"
#include "../util/ssz.h"
#include VERIFIERS_PATH
#include <string.h>

void c4_verify_from_bytes(verify_ctx_t* ctx, bytes_t request, char* method, json_t args, chain_id_t chain_id) {
  ssz_def_t* container_type = request_container(chain_id);
  ssz_ob_t   req            = {.bytes = request, .def = container_type};
  memset(ctx, 0, sizeof(verify_ctx_t));
  if (!req.def) {
    ctx->state.error = strdup("chain not supported");
    return;
  }
  if (!ssz_is_valid(req, true, &ctx->state)) return;
  ctx->chain_id  = chain_id; // ssz_get_uint64(&req, "chainId");
  ctx->data      = ssz_get(&req, "data");
  ctx->proof     = ssz_get(&req, "proof");
  ctx->sync_data = ssz_get(&req, "sync_data");
  ctx->method    = method;
  ctx->args      = args;
  c4_verify(ctx);
}

void c4_verify(verify_ctx_t* ctx) {
  if (ctx->state.error) return;
  // check if there are sync_datat, we should use to update the state
}