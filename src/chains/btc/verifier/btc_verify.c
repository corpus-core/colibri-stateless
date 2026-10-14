/*
 * Copyright (c) 2025 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include "btc_verify.h"
#include "btc_proof.h"
#include "btc_target.h"
#include "btc_types.h"
#include <stdlib.h>
#include <string.h>

static const char* btc_methods[] = {
    "getblock",
    "getblockheader",
    "getrawtransaction",
    "getblockhash",
    "getbestblockhash",
    "getblockcount",
    "getdifficulty",
    "decoderawtransaction",
    "decodescript",
};

const ssz_def_t* c4_btc_get_request_type(chain_type_t chain_type) {
  return chain_type == C4_CHAIN_TYPE_BITCOIN ? btc_ssz_verification_type(BTC_SSZ_VERIFY_REQUEST) : NULL;
}

method_type_t c4_btc_get_method_type(chain_id_t chain_id, char* method, json_t params, verify_flags_t flags) {
  (void) params;
  (void) flags;
  if (!method || c4_chain_type(chain_id) != C4_CHAIN_TYPE_BITCOIN) return METHOD_UNDEFINED;
  for (size_t i = 0; i < sizeof(btc_methods) / sizeof(btc_methods[0]); i++) {
    if (strcmp(method, btc_methods[i]) == 0) return METHOD_NOT_SUPPORTED;
  }
  return METHOD_UNDEFINED;
}

/**
 * Reports whether `ob` carries a selected SSZ value.
 *
 * @param ob object from the verification context
 * @return false when the object is missing or `NONE`
 */
static bool btc_ssz_present(ssz_ob_t ob) {
  return ob.def && ob.def->type != SSZ_TYPE_NONE;
}

bool c4_btc_verify(verify_ctx_t* ctx) {
  btc_target_cache_t cache;
  bool               has_sync;
  if (!ctx || c4_chain_type(ctx->chain_id) != C4_CHAIN_TYPE_BITCOIN) return false;
  has_sync = btc_ssz_present(ctx->sync_data);
  btc_target_cache_load(ctx->chain_id, &cache);
  if (!btc_sync_accept_epoch_path(ctx->sync_data, &cache, NULL)) {
    if (!ctx->state.error) ctx->state.error = strdup("invalid bitcoin sync data");
    ctx->success = false;
    return true;
  }
  if (has_sync && !btc_target_cache_store(ctx->chain_id, &cache)) {
    if (!ctx->state.error) ctx->state.error = strdup("failed to store bitcoin target cache");
    ctx->success = false;
    return true;
  }
  if (has_sync && !btc_ssz_present(ctx->proof) && !btc_ssz_present(ctx->data)) {
    ctx->success = true;
    return true;
  }
  if (!ctx->state.error) ctx->state.error = strdup("bitcoin verification is not implemented");
  ctx->success = false;
  return true;
}
