/*
 * Copyright (c) 2025,2026 corpus.core GmbH
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

#ifndef arb_verify_h__
#define arb_verify_h__

#include "eth_verify.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns the ETH `C4Request` type for Arbitrum (same wire format as Ethereum
 * until `arbFeedProof` is added).
 *
 * @param chain_type must be `C4_CHAIN_TYPE_ARBITRUM`
 * @return ETH request SSZ def, or NULL
 */
const ssz_def_t* c4_arb_get_request_type(chain_type_t chain_type);

/**
 * Classifies an RPC method for Arbitrum with the Ethereum catalog
 * (`c4_eth_classify_method`).
 *
 * @param chain_id chain of the request
 * @param method RPC method name
 * @param params RPC params (used to detect block tags that cannot be proven)
 * @param flags verify flags, forwarded to the Ethereum classifier
 * @return method type, or `METHOD_UNDEFINED` when `chain_id` is not Arbitrum or the method is unknown
 */
method_type_t c4_arb_get_method_type(chain_id_t chain_id, char* method, json_t params, verify_flags_t flags);

/**
 * Verifies a request of an Arbitrum chain by delegating to the Ethereum verifier.
 *
 * No Arbitrum block-proof hook is registered yet. Sync data and the proof are
 * handled by `c4_eth_verify_request`.
 *
 * @param ctx verification context
 * @return `false` if `ctx->chain_id` is not Arbitrum, otherwise `true` (result in `ctx`)
 */
bool c4_arb_verify(verify_ctx_t* ctx);

/**
 * Chain-specific RPC-context init hook (registered via CMake `INIT_RPC_CTX`).
 *
 * Nothing to register until the feed proof has its own verify hook.
 *
 * @param ctx initialization context (unused)
 */
void arb_init_rpc_ctx(c4_init_ctx_t* ctx);

#ifdef __cplusplus
}
#endif

#endif
