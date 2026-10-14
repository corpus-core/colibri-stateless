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

#ifndef btc_verify_h__
#define btc_verify_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "verify.h"

/**
 * Returns the SSZ request container for Bitcoin proofs.
 *
 * The container is `C4Request` (`version`, `data`, `proof`, `sync_data`).
 * Union index 0 of each union is `NONE`. Later proof variants are appended
 * and existing indices stay frozen.
 *
 * @param chain_type chain family to resolve
 * @return the Bitcoin request definition when `chain_type` is `C4_CHAIN_TYPE_BITCOIN`, otherwise NULL
 */
const ssz_def_t* c4_btc_get_request_type(chain_type_t chain_type);

/**
 * Verifies a Bitcoin proof.
 *
 * Returns false when `ctx` belongs to another chain so the dispatcher can
 * continue. `sync_data` is applied first and updates the cached epoch targets.
 * A request that contains only that sync data succeeds. `BtcHeaderProof` verifies
 * `getblockheader` without verbose output. Other proofs are rejected.
 *
 * @param ctx verification context
 * @return true when this module handled the context, false when the chain is not Bitcoin
 */
bool c4_btc_verify(verify_ctx_t* ctx);

/**
 * Classifies a Bitcoin Core RPC method.
 *
 * `getblockheader` on mainnet is `METHOD_PROOFABLE` only when verbosity is explicitly
 * `false` or `0`. Bitcoin Core defaults that argument to the JSON object, which this
 * proof cannot return. The object, other networks, and the other known methods are
 * `METHOD_NOT_SUPPORTED` until their proofs exist. Unknown names and non-Bitcoin
 * chains return `METHOD_UNDEFINED`.
 *
 * @param chain_id chain the call is aimed at
 * @param method RPC method name
 * @param params RPC parameter array (unused until method-specific checks exist)
 * @param flags verify flags (unused until method-specific checks exist)
 * @return `METHOD_NOT_SUPPORTED` for a known Bitcoin method, otherwise `METHOD_UNDEFINED`
 */
method_type_t c4_btc_get_method_type(chain_id_t chain_id, char* method, json_t params, verify_flags_t flags);

/**
 * Fills chain properties for a Bitcoin network.
 *
 * Block time is the 10-minute target (600000 ms). Names follow
 * `C4_CHAIN_BTC_MAINNET` (0), `C4_CHAIN_BTC_TESTNET` (1) and
 * `C4_CHAIN_BTC_DEVNET` (2).
 *
 * @param chain_id packed chain id
 * @param props output; left untouched when this module does not own the chain
 * @return true when `props` was filled for a Bitcoin chain
 */
bool c4_btc_chain_props(chain_id_t chain_id, chain_properties_t* props);

#ifdef __cplusplus
}
#endif

#endif
