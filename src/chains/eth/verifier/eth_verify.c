#include "eth_verify.h"
#include "beacon_types.h"
#include "chains.h"
#include "json.h"
#include "ssz.h"
#include "sync_committee.h"
#include "verify.h"
#include <string.h>

const ssz_def_t* c4_eth_get_request_type(chain_type_t chain_type) {
  return chain_type == C4_CHAIN_TYPE_ETHEREUM ? eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST) : NULL;
}

bool c4_eth_verify(verify_ctx_t* ctx) {
  if (ctx->chain_id != C4_CHAIN_MAINNET) return false;
  if (!c4_update_from_sync_data(ctx)) return true;

  if (ssz_is_type(&ctx->proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_BLOCK_HASH_PROOF)))
    verify_blockhash_proof(ctx);
#ifdef ETH_TX
  else if (ssz_is_type(&ctx->proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_TRANSACTION_PROOF)))
    verify_tx_proof(ctx);
#endif
#ifdef ETH_RECEIPT
  else if (ssz_is_type(&ctx->proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_RECEIPT_PROOF)))
    verify_receipt_proof(ctx);
#endif
#ifdef ETH_LOGS
  else if (ssz_is_type(&ctx->proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_LOGS_PROOF)))
    verify_logs_proof(ctx);
#endif
#ifdef ETH_ACCOUNT
  else if (ssz_is_type(&ctx->proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_ACCOUNT_PROOF)))
    verify_account_proof(ctx);
#endif
#ifdef ETH_CALL
  else if (ssz_is_type(&ctx->proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_CALL_PROOF)))
    verify_call_proof(ctx);
#endif
#ifdef ETH_BLOCK
  else if (ssz_is_type(&ctx->proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_BLOCK_PROOF)))
    verify_block_proof(ctx);
#endif
  else if (ctx->method == NULL && ctx->proof.def->type == SSZ_TYPE_NONE && ctx->sync_data.def->type != SSZ_TYPE_NONE && ctx->data.def->type == SSZ_TYPE_NONE)
    ctx->success = true; // if you only verify the sync data, this is ok
  else {
    ctx->state.error = strdup("proof is not a supported proof type");
    ctx->success     = false;
  }
  return true;
}
