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
 * Nitro feed digest for a message that carries a block hash.
 *
 * ```
 * keccak256( "Arbitrum Nitro Feed:" | be64(chain_id) | be64(sequence) | block_hash | signed_tail )
 * ```
 *
 * `signed_tail` is everything after `blockHash` in Nitro's `SignatureHash`. A message
 * signed without a block hash cannot match, because `block_hash` is always inserted.
 *
 * @param chain_id numeric chain id (42161 for Arbitrum One), not a packed `chain_id_t`
 * @param sequence feed sequence, `block number - nitro genesis`
 * @param block_hash 32-byte `keccak256(elHeader)`
 * @param signed_tail uncompressed tail
 * @param out 32-byte digest
 */
void arb_feed_digest(uint64_t chain_id, uint64_t sequence, bytes32_t block_hash, bytes_t signed_tail, bytes32_t out);

/**
 * Verifies the `arbFeedProof` variant of `ETH_EL_PROOF_UNION`.
 * Registered via `c4_register_block_proof_verify` for `C4_CHAIN_TYPE_ARBITRUM`.
 *
 * The recovered signer must be the Arbitrum One batch poster
 * `0xC1b634853Cb333D3aD8663715b08f41A3Aec47cc`. The L1 proof of that address
 * is not checked yet. The digest uses `ctx->chain_id`; a chain reaches this
 * function only when `c4_chain_type` is `C4_CHAIN_TYPE_ARBITRUM`.
 *
 * @param ctx verification context
 * @param block selected `arbFeedProof` union member
 * @param el_header receives the RLP header (borrowed from the proof)
 * @param block_hash receives `keccak(el_header)`
 * @return `C4_SUCCESS` or `C4_ERROR`
 */
c4_status_t arb_verify_feed_proof(verify_ctx_t* ctx, ssz_ob_t block, bytes_t* el_header, bytes32_t block_hash);

/**
 * Registers the Arbitrum `arbFeedProof` verify hook. Idempotent.
 */
void arb_register_block_proof_verify(void);

/**
 * Returns the ETH `C4Request` type for Arbitrum (same wire format as Ethereum).
 * Block proofs use `arbFeedProof` (index 6 of `ETH_EL_PROOF_UNION`).
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
 * Verifies a request of an Arbitrum chain (registered via CMake `VERIFY`).
 *
 * Registers the feed-proof hook first. The hook slot depends only on
 * `C4_CHAIN_TYPE_ARBITRUM`, so registration runs before the chain-type check.
 * Sync data and the proof are then handled by `c4_eth_verify_request`.
 *
 * @param ctx verification context
 * @return `false` if `ctx->chain_id` is not Arbitrum, otherwise `true` (result in `ctx`)
 */
bool c4_arb_verify(verify_ctx_t* ctx);

/**
 * Chain-specific RPC-context init hook (registered via CMake `INIT_RPC_CTX`).
 * Registers the feed-proof verify handler for `C4_CHAIN_TYPE_ARBITRUM`.
 * The slot depends only on that chain type, so the hook is registered for every
 * context, including chains this module does not verify.
 *
 * @param ctx initialization context (unused; the hook does not read it)
 */
void arb_init_rpc_ctx(c4_init_ctx_t* ctx);

#ifdef __cplusplus
}
#endif

#endif
