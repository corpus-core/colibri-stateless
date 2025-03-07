#include "chains.h"
#include "json.h"
#include "ssz.h"
#include "sync_committee.h"
#include "types_verify.h"
#include "verifiers.h"
#include "verify.h"
#include <string.h>

ssz_def_t* c4_eth_get_request_type(chain_id_t chain_id) {
  if (chain_id == C4_CHAIN_MAINNET)
    return &C4_REQUEST_CONTAINER;
  return NULL;
}

bool c4_eth_verify(verify_ctx_t* ctx) {
  if (ctx->chain_id != C4_CHAIN_MAINNET) return false;
  if (!c4_update_from_sync_data(ctx)) return true;

  if (ssz_is_type(&ctx->proof, BLOCK_HASH_PROOF)) {
    ctx->type = PROOF_TYPE_BEACON_HEADER;
    verify_blockhash_proof(ctx);
  }
  else if (ssz_is_type(&ctx->proof, ETH_TRANSACTION_PROOF))
    verify_tx_proof(ctx);
  else if (ssz_is_type(&ctx->proof, ETH_RECEIPT_PROOF))
    verify_receipt_proof(ctx);
  else if (ssz_is_type(&ctx->proof, &ETH_LOGS_BLOCK_CONTAINER))
    verify_logs_proof(ctx);
  else if (ssz_is_type(&ctx->proof, ETH_ACCOUNT_PROOF))
    verify_account_proof(ctx);
  else if (ctx->method == NULL && ctx->proof.def->type == SSZ_TYPE_NONE && ctx->sync_data.def->type != SSZ_TYPE_NONE && ctx->data.def->type == SSZ_TYPE_NONE)
    ctx->success = true; // if you only verify the sync data, this is ok
  else {
    ctx->state.error = strdup("proof is not a supported proof type");
    ctx->success     = false;
  }
}
