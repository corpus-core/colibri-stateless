#include "verify.h"

#include "beacon_types.h"

void c4_verify(verify_ctx_t* ctx) {
  if (ctx->type == PROOF_TYPE_BEACON_HEADER) {
    if (ctx->proof.def->type != SSZ_TYPE_CONTAINER) {
      ctx->error   = "proof is not a container";
      ctx->success = false;
      return;
    }
    if (ctx->proof.def->def.container.elements == BLOCK_HASH_PROOF)
      verify_blockhash_proof(ctx);
    else {
      ctx->error   = "proof is not a supported proof type";
      ctx->success = false;
      return;
    }
  }
}