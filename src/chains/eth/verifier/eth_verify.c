#include "eth_verify.h"
#include "beacon_types.h"
#include "chains.h"
#include "json.h"
#include "ssz.h"
#include "sync_committee.h"
#include "verify.h"
#include <string.h>

static const char* proofable_methods[] = {
    "eth_call",
    "eth_getBalance",
    "eth_getBlockByHash",
    "eth_getBlockByNumber",
    "eth_getCode",
    "eth_getLogs",
    "eth_getTransactionCount",
    "eth_getStorageAt",
    "eth_getTransactionReceipt",
    "eth_getTransactionByHash",
    "eth_getTransactionByBlockHashAndIndex",
};
static const char* local_methods[] = {
    "eth_chainId",
};

method_type_t c4_eth_get_method_type(chain_id_t chain_id, char* method) {
  if (chain_id != C4_CHAIN_MAINNET) return METHOD_UNDEFINED;
  for (int i = 0; i < sizeof(proofable_methods) / sizeof(proofable_methods[0]); i++) {
    if (strcmp(method, proofable_methods[i]) == 0) return METHOD_PROOFABLE;
  }
  for (int i = 0; i < sizeof(local_methods) / sizeof(local_methods[0]); i++) {
    if (strcmp(method, local_methods[i]) == 0) return METHOD_LOCAL;
  }
  return METHOD_NOT_SUPPORTED;
}

const ssz_def_t* c4_eth_get_request_type(chain_type_t chain_type) {
  return chain_type == C4_CHAIN_TYPE_ETHEREUM ? eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST) : NULL;
}

bool c4_eth_verify(verify_ctx_t* ctx) {
  if (ctx->chain_id != C4_CHAIN_MAINNET) return false;
  if (!c4_update_from_sync_data(ctx)) return true;

#ifdef ETH_TX
  if (ssz_is_type(&ctx->proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_TRANSACTION_PROOF)))
    verify_tx_proof(ctx);
  else
#endif
#ifdef ETH_RECEIPT
      if (ssz_is_type(&ctx->proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_RECEIPT_PROOF)))
    verify_receipt_proof(ctx);
  else
#endif
#ifdef ETH_LOGS
      if (ssz_is_type(&ctx->proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_LOGS_PROOF)))
    verify_logs_proof(ctx);
  else
#endif
#ifdef ETH_ACCOUNT
      if (ssz_is_type(&ctx->proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_ACCOUNT_PROOF)))
    verify_account_proof(ctx);
  else
#endif
#ifdef ETH_CALL
      if (ssz_is_type(&ctx->proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_CALL_PROOF)))
    verify_call_proof(ctx);
  else
#endif
#ifdef ETH_BLOCK
      if (ssz_is_type(&ctx->proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_BLOCK_PROOF)))
    verify_block_proof(ctx);
  else
#endif
#ifdef ETH_UTIL
      if (c4_eth_get_method_type(ctx->chain_id, ctx->method) == METHOD_LOCAL)
    verify_eth_local(ctx);
  else
#endif
      if (ctx->method == NULL && ctx->proof.def->type == SSZ_TYPE_NONE && ctx->sync_data.def->type != SSZ_TYPE_NONE && ctx->data.def->type == SSZ_TYPE_NONE)
    ctx->success = true; // if you only verify the sync data, this is ok
  else {
    ctx->state.error = strdup("proof is not a supported proof type or not enabled");
    ctx->success     = false;
  }
  return true;
}
