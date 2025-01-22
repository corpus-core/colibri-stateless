#include "verify.h"
#include "../util/ssz.h"
#include "types_verify.h"
#include <string.h>
void c4_verify_from_bytes(verify_ctx_t* ctx, bytes_t request) {
  ssz_ob_t req = ssz_ob(C4_REQUEST_CONTAINER, request);
  memset(ctx, 0, sizeof(verify_ctx_t));

  ctx->data      = ssz_get(&req, "data");
  ctx->proof     = ssz_get(&req, "proof");
  ctx->sync_data = ssz_get(&req, "sync_data");
  c4_verify(ctx);
}

void c4_verify(verify_ctx_t* ctx) {

  if (ssz_is_type(&ctx->proof, BLOCK_HASH_PROOF)) {
    ctx->type = PROOF_TYPE_BEACON_HEADER;
    verify_blockhash_proof(ctx);
  }
  else {
    ctx->error   = "proof is not a supported proof type";
    ctx->success = false;
  }
}