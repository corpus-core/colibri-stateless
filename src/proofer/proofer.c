#include "proofer.h"
#include "../util/json.h"
#include "../util/state.h"
#include PROOFERS_PATH
#include <stdlib.h>
#include <string.h>

proofer_ctx_t* c4_proofer_create(char* method, char* params, chain_id_t chain_id, proofer_flags_t flags) {
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
  ctx->flags                  = flags;
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

  proofer_execute(ctx);

  return c4_proofer_status(ctx);
}
