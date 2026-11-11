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

#ifndef eez_prover_h__
#define eez_prover_h__

#include "beacon.h"
#include "eth_tools.h"
#include "prover.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Resolves an L2 block for an EEZ chain. Registered as the GET_BLOCK hook.
 *
 * Every proof of an L2 block starts at the anchor: the block whose hash the L1
 * knows in the storage of the EEZ proxy. The function
 *
 * 1. asks the prover of the L1 chain (`C4_DATA_TYPE_PROVER` request with the L1 chain id)
 *    for an `eth_getStorageAt` proof of the root slot. The request carries the client
 *    state, so the response holds the sync data the verifier needs for the L1 block.
 *    A host that builds the proof locally (`c4_rpc_ctx_t`, see `colibri_common.h`) answers
 *    this request with a local prover of the L1 chain instead of a remote one.
 * 2. reads the anchor hash from the storage proof of that response,
 * 3. fetches the L2 header (and body) of the anchor from the L2 execution node and
 *    checks that it hashes to the anchor.
 * 4. for a block of the 8191 blocks before the anchor: reads the hash of that block from
 *    the EIP-2935 history contract in the state of the anchor (`eth_getProof` on the L2
 *    node) and fetches the header (and body) of that block instead. A block requested by
 *    hash is looked up on the L2 node first (`eth_getBlockByHash`): its number selects
 *    the history slot. The anchor header and
 *    the history proof are handed out in `out->eez` for the `eip2935` proof.
 *
 * Block identifiers:
 *
 * | `block`                | L1 block tag | Result                                                    |
 * |------------------------|--------------|-----------------------------------------------------------|
 * | `latest` or missing    | `latest`     | the anchor                                                |
 * | `safe`, `finalized`    | same tag     | the anchor known by that L1 block                         |
 * | block hash or number   | `latest`     | the anchor if it is that block; a block of the 8191 blocks before the anchor; otherwise an error |
 *
 * The error for another block names the reason: newer than the anchor, or older than
 * the EIP-2935 window before it. A block hash that is not on the chain anchored on L1
 * (the history contract holds another hash for its number) is an error as well.
 *
 * @param ctx prover context of the L2 request
 * @param block JSON block identifier
 * @param out output block (`proof_type` EEZ, EL fields, `eez.account` and `eez.sync_data`; `eez.history` (header of the anchor and history proof) for a block before the anchor)
 * @param with_body if true, `el_body` contains transactions and withdrawals
 * @return `C4_SUCCESS`, `C4_PENDING`, or `C4_ERROR`
 */
c4_status_t eez_get_el_block(prover_ctx_t* ctx, json_t block, eth_block_t* out, bool with_body);

/**
 * Appends the block proof of an EEZ block to `ETH_EL_PROOF_UNION`. Registered as the
 * ADD_BLOCK_PROOF hook.
 *
 * The anchor gets an `eezL1Proof`. A block before the anchor (`eez.history` is set) gets
 * an `eip2935` proof that wraps the `eezL1Proof` of the anchor; it is built by `c4_eth_add_eip2935_proof` of the Ethereum module. Blocks
 * that need another variant extend `eth_block_eez_t` and get their own branch here.
 *
 * @param ctx prover context
 * @param builder parent proof builder (`elProof` union field)
 * @param block_data block produced by `eez_get_el_block`
 * @param historic unused (the consensus proof is part of the L1 account proof)
 * @return true if the variant was written
 */
bool eez_add_block_proof(prover_ctx_t* ctx, ssz_builder_t* builder, eth_block_t* block_data, blockroot_proof_t* historic);

/**
 * Registers the EEZ prover block-proof hooks. Idempotent.
 */
void eez_register_block_proof_prover(void);

/**
 * Creates the proof for a request of an EEZ chain (registered via CMake `PROOF`).
 *
 * @param ctx prover context
 * @return `false` if `ctx->chain_id` is not a configured EEZ chain, otherwise `true` (result in `ctx`)
 */
bool eez_prover_execute(prover_ctx_t* ctx);

#ifdef __cplusplus
}
#endif

#endif // eez_prover_h__
