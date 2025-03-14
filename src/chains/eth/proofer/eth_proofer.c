#include "eth_proofer.h"
#include "json.h"
#include "state.h"
#include <stdlib.h>
#include <string.h>

static const char* eth_account_methods[] = {
    "eth_getBalance",
    "eth_getCode",
    "eth_getNonce",
    "eth_getProof",
    "eth_getStorageAt",
    NULL};

static const bool includes(const char** methods, const char* method) {
  for (int i = 0; methods[i] != NULL; i++) {
    if (strcmp(methods[i], method) == 0) {
      return true;
    }
  }
  return false;
}

bool eth_proofer_execute(proofer_ctx_t* ctx) {
  if (ctx->chain_id != C4_CHAIN_MAINNET) return false;

  if (includes(eth_account_methods, ctx->method))
    c4_proof_account(ctx);
  else if (strcmp(ctx->method, "eth_getTransactionByHash") == 0)
    c4_proof_transaction(ctx);
  else if (strcmp(ctx->method, "eth_getTransactionReceipt") == 0)
    c4_proof_receipt(ctx);
  else if (strcmp(ctx->method, "eth_getLogs") == 0)
    c4_proof_logs(ctx);
  else if (strcmp(ctx->method, "eth_call") == 0)
    c4_proof_call(ctx);
  else
    ctx->state.error = strdup("Unsupported method");

  return true;
}
