#include "eth_proofer.h"
#include "json.h"
#include "state.h"
#include <stdlib.h>
#include <string.h>

bool eth_proofer_execute(proofer_ctx_t* ctx) {
  if (ctx->chain_id != C4_CHAIN_MAINNET) return false;

  if (strcmp(ctx->method, "eth_getBalance") == 0 || strcmp(ctx->method, "eth_getCode") == 0 || strcmp(ctx->method, "eth_getNonce") == 0 || strcmp(ctx->method, "eth_getProof") == 0 || strcmp(ctx->method, "eth_getStorageAt") == 0)
    c4_proof_account(ctx);
  else if (strcmp(ctx->method, "eth_getTransactionByHash") == 0)
    c4_proof_transaction(ctx);
  else if (strcmp(ctx->method, "eth_getTransactionReceipt") == 0)
    c4_proof_receipt(ctx);
  else if (strcmp(ctx->method, "eth_getLogs") == 0)
    c4_proof_logs(ctx);
  else
    ctx->state.error = strdup("Unsupported method");

  return true;
}
