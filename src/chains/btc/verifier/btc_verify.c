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
#include "btc_header.h"
#include "btc_proof.h"
#include "btc_target.h"
#include "btc_types.h"
#include <string.h>

// : Bitcoin
//
// Bitcoin proofs verify mainnet block headers and the values taken from them.
// A header is accepted after proof-of-work, the cached difficulty target, and at
// least six following headers.

// :: Supported RPC-Methods
//
// The following table shows the supported RPC methods for Bitcoin.
// `getblockheader` is proofable only when verbosity is explicitly `false` or `0`.
// Bitcoin Core defaults that argument to the JSON object, which this proof cannot
// return. `getblockhash` is proofable for one numeric height. Other networks and
// the remaining known methods stay unsupported until their proofs exist.

static const char* proofable_methods[] = {
    RPC_METHOD("getblockheader", BtcHeader, BtcHeaderProof), // raw header only, verbosity false or 0
    RPC_METHOD("getblockhash", BtcBlockHash, BtcHeaderProof),
};

static const char* not_verifieable_yet_methods[] = {
    RPC_METHOD("getblock", Void, Void),
    RPC_METHOD("getrawtransaction", Void, Void),
    RPC_METHOD("getbestblockhash", Void, Void),
    RPC_METHOD("getblockcount", Void, Void),
    RPC_METHOD("getdifficulty", Void, Void),
    RPC_METHOD("decoderawtransaction", Void, Void),
    RPC_METHOD("decodescript", Void, Void),
};

const ssz_def_t* c4_btc_get_request_type(chain_type_t chain_type) {
  return chain_type == C4_CHAIN_TYPE_BITCOIN ? btc_ssz_verification_type(BTC_SSZ_VERIFY_REQUEST) : NULL;
}

method_type_t c4_btc_get_method_type(chain_id_t chain_id, char* method, json_t params, verify_flags_t flags) {
  size_t i;
  (void) flags;
  if (!method || c4_chain_type(chain_id) != C4_CHAIN_TYPE_BITCOIN) return METHOD_UNDEFINED;
  for (i = 0; i < sizeof(proofable_methods) / sizeof(proofable_methods[0]); i++) {
    if (strcmp(method, proofable_methods[i]) != 0) continue;
    if (chain_id != C4_CHAIN_BTC_MAINNET) return METHOD_NOT_SUPPORTED;
    if (strcmp(method, "getblockheader") == 0)
      return btc_header_raw_requested(params) ? METHOD_PROOFABLE : METHOD_NOT_SUPPORTED;
    if (strcmp(method, "getblockhash") == 0)
      return btc_blockhash_height(params, NULL) ? METHOD_PROOFABLE : METHOD_NOT_SUPPORTED;
    return METHOD_NOT_SUPPORTED;
  }
  for (i = 0; i < sizeof(not_verifieable_yet_methods) / sizeof(not_verifieable_yet_methods[0]); i++) {
    if (strcmp(method, not_verifieable_yet_methods[i]) == 0) return METHOD_NOT_SUPPORTED;
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

/**
 * Checks `BtcEpochPath`, then stores the updated target cache.
 *
 * @param ctx verification context
 * @return `C4_SUCCESS` when the path was accepted and stored, otherwise `C4_ERROR`
 */
static c4_status_t btc_apply_epoch_path(verify_ctx_t* ctx) {
  btc_target_cache_t cache;
  btc_target_cache_load(ctx->chain_id, &cache);
  if (!btc_sync_accept_epoch_path(ctx->sync_data, &cache, NULL)) THROW_ERROR("invalid bitcoin sync data");
  if (!btc_target_cache_store(ctx->chain_id, &cache)) THROW_ERROR("failed to store bitcoin target cache");
  return C4_SUCCESS;
}

/**
 * Brings the cached epoch targets forward from `ctx->sync_data`.
 *
 * Absent sync data is success. `BtcEpochPath` is checked and stored before any block proof.
 * Further variants, including a ZK checkpoint of a long header span, are dispatched here and
 * may return `C4_PENDING` when witness bytes are still missing. `C4_ERROR` always leaves an
 * error on `ctx->state`. After a successful apply, `ctx->sync_data` is set to `NONE` so a
 * later call, for example after `C4_PENDING`, does not apply the same update again.
 *
 * @param ctx verification context
 * @return `C4_SUCCESS` when sync data was absent or applied, otherwise `C4_ERROR` or `C4_PENDING`
 */
static c4_status_t btc_handle_sync_data(verify_ctx_t* ctx) {
  if (!btc_ssz_present(ctx->sync_data)) return C4_SUCCESS;
  // A later ZK checkpoint variant is selected by its SSZ type and verified here, before the block proof.
  if (ssz_is_type(&ctx->sync_data, btc_ssz_verification_type(BTC_SSZ_VERIFY_EPOCH_PATH)))
    TRY_ASYNC(btc_apply_epoch_path(ctx));
  else
    THROW_ERROR("unknown bitcoin sync data");
  // Applied. A later re-entry must not apply the same sync data again.
  ctx->sync_data.def = &ssz_none;
  return C4_SUCCESS;
}

/**
 * Selects the proof verifier after sync data has been applied.
 *
 * `had_sync` is the state from before `btc_handle_sync_data` cleared `ctx->sync_data`. A request
 * that contained only that sync data succeeds. Each proof type has its own function, and that
 * function checks the RPC method and arguments before the witness.
 *
 * @param ctx verification context
 * @param had_sync true when the request carried sync data
 * @return true when this module handled the context
 */
static bool btc_dispatch_proof(verify_ctx_t* ctx, bool had_sync) {
  if (ssz_is_type(&ctx->proof, btc_ssz_verification_type(BTC_SSZ_VERIFY_HEADER_PROOF)))
    btc_verify_header_proof(ctx);
  else if (had_sync && !btc_ssz_present(ctx->proof) && !btc_ssz_present(ctx->data))
    ctx->success = true;
  else if (btc_ssz_present(ctx->proof))
    c4_state_add_error(&ctx->state, "proof is not a supported proof type");
  else
    c4_state_add_error(&ctx->state, "bitcoin verification is not implemented");
  return true;
}

bool c4_btc_verify(verify_ctx_t* ctx) {
  bool has_sync;
  if (!ctx || c4_chain_type(ctx->chain_id) != C4_CHAIN_TYPE_BITCOIN) return false;
  if (ctx->chain_id != C4_CHAIN_BTC_MAINNET) {
    c4_state_add_error(&ctx->state, "bitcoin network is not supported");
    return true;
  }
  has_sync = btc_ssz_present(ctx->sync_data);
  if (btc_handle_sync_data(ctx) != C4_SUCCESS) return true;
  return btc_dispatch_proof(ctx, has_sync);
}
