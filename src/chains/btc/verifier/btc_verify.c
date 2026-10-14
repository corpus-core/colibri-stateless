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
#include "ssz.h"
#include <stdlib.h>
#include <string.h>

// Index 0 is frozen as NONE. Proof and data variants are appended in later steps.
static const ssz_def_t BTC_REQUEST_DATA_UNION[] = {
    SSZ_NONE,
};

static const ssz_def_t BTC_REQUEST_PROOF_UNION[] = {
    SSZ_NONE,
};

static const ssz_def_t BTC_REQUEST_SYNCDATA_UNION[] = {
    SSZ_NONE,
};

static const ssz_def_t BTC_REQUEST[] = {
    SSZ_BYTE_VECTOR("version", 4),
    SSZ_UNION("data", BTC_REQUEST_DATA_UNION),
    SSZ_UNION("proof", BTC_REQUEST_PROOF_UNION),
    SSZ_UNION("sync_data", BTC_REQUEST_SYNCDATA_UNION),
};

static const ssz_def_t BTC_REQUEST_CONTAINER = SSZ_CONTAINER("C4Request", BTC_REQUEST);

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
  return chain_type == C4_CHAIN_TYPE_BITCOIN ? &BTC_REQUEST_CONTAINER : NULL;
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

bool c4_btc_verify(verify_ctx_t* ctx) {
  if (!ctx || c4_chain_type(ctx->chain_id) != C4_CHAIN_TYPE_BITCOIN) return false;
  if (!ctx->state.error) ctx->state.error = strdup("bitcoin verification is not implemented");
  ctx->success = false;
  return true;
}
