#include "proofer.h"

bool eth_proofer_execute(proofer_ctx_t* ctx);

static void proofer_execute(proofer_ctx_t* ctx) {
  if (eth_proofer_execute(ctx)) return;
  ctx->state.error = strdup("Unsupported chain");
}
