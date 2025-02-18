#include "proofer.h"
#include "../util/json.h"
#include "../util/state.h"
#include <stdlib.h>
#include <string.h>

proofer_ctx_t* c4_proofer_create(char* method, char* params, chain_id_t chain_id) {
  json_t params_json = json_parse(params);
  if (params_json.type != JSON_TYPE_ARRAY) return NULL;
  char* params_str = malloc(params_json.len + 1);
  memcpy(params_str, params_json.start, params_json.len);
  params_str[params_json.len] = 0;
  params_json.start           = params_str;
  proofer_ctx_t* ctx          = calloc(1, sizeof(proofer_ctx_t));
  ctx->method                 = strdup(method);
  ctx->params                 = params_json;
  ctx->chain_id               = chain_id;
  return ctx;
}

void c4_proofer_free(proofer_ctx_t* ctx) {
  c4_state_free(&ctx->state);
  if (ctx->method) free(ctx->method);
  if (ctx->params.start) free((void*) ctx->params.start);
  if (ctx->proof.data) free(ctx->proof.data);
  free(ctx);
}

c4_status_t c4_proofer_status(proofer_ctx_t* ctx) {
  if (ctx->state.error) return C4_ERROR;
  if (ctx->proof.data) return C4_SUCCESS;
  if (c4_state_get_pending_request(&ctx->state)) return C4_PENDING;
  return C4_PENDING;
}

c4_status_t c4_proofer_execute(proofer_ctx_t* ctx) {
  if (c4_state_get_pending_request(&ctx->state)) return C4_PENDING;
  if (ctx->state.error) return C4_ERROR;

  if (strcmp(ctx->method, "eth_getBalance") == 0 || strcmp(ctx->method, "eth_getCode") == 0 || strcmp(ctx->method, "eth_getNonce") == 0 || strcmp(ctx->method, "eth_getProof") == 0 || strcmp(ctx->method, "eth_getStorageAt") == 0)
    c4_proof_account(ctx);
  else if (strcmp(ctx->method, "eth_getTransactionByHash") == 0)
    c4_proof_transaction(ctx);
  else if (strcmp(ctx->method, "eth_getTransactionReceipt") == 0)
    c4_proof_receipt(ctx);
  else if (strcmp(ctx->method, "eth_getLogs") == 0)
    c4_proof_logs(ctx);
  else
    THROW_ERROR("Unsupported method");

  return c4_proofer_status(ctx);
}
