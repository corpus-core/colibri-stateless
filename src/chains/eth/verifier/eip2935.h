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

#ifndef eip2935_h__
#define eip2935_h__

#include "bytes.h"
#include "crypto.h"
#include "ssz.h"
#include "state.h"
#include "verify.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Number of block hashes the EIP-2935 history contract keeps (`HISTORY_SERVE_WINDOW`).
 * The hash of block `n` can be read from the state of every block in `n + 1 .. n + 8191`.
 */
#define ETH_EIP2935_HISTORY_WINDOW 8191

/**
 * Address of the EIP-2935 history contract (`HISTORY_STORAGE_ADDRESS`),
 * `0x0000F90827F1C53a10cb7A02335B175320002935` on every chain that activated EIP-2935.
 */
extern const address_t ETH_EIP2935_HISTORY_ADDRESS;

/**
 * Computes the storage key under which the EIP-2935 history contract stores the hash
 * of a block: `block_number % 8191` as a 32-byte big-endian word.
 *
 * @param block_number number of the block whose hash is stored
 * @param slot receives the 32-byte storage key
 */
void eth_eip2935_slot(uint64_t block_number, bytes32_t slot);

/**
 * Verifies the `eip2935` variant of `ETH_EL_PROOF_UNION`: the header of an older block,
 * proven through the EIP-2935 history contract in the state of a newer block.
 *
 * The header is accepted when all of the following hold:
 *
 * 1. `account.elProof` verifies through `c4_verify_block` as a block of `ctx->chain_id`
 *    (the newer block). It must not be an `eip2935` proof itself.
 * 2. `account.address` is the history contract and `account.storageProof` holds exactly
 *    one entry.
 * 3. The account proof and the storage proof verify against the `stateRoot` of the
 *    newer block.
 * 4. The proven value is non-zero and equals `keccak256(elHeader)`, and `elHeader` has
 *    the shape of an execution header.
 * 5. The proven storage key is the slot of the block number of `elHeader`
 *    (`eth_eip2935_slot`).
 * 6. That number lies in the history window of the newer block:
 *    `newer - 8191 <= number < newer`.
 *
 * On success the header is stored in the header cache of `ctx->chain_id`, so follow-up
 * proofs can reference the block by hash only (`blockHash` variant).
 *
 * The function does not decide whether a chain may use this variant. `c4_verify_block`
 * does not call it; a chain module that accepts `eip2935` proofs calls it from its
 * block-proof hook (`c4_register_block_proof_verify`).
 *
 * @param ctx verification context
 * @param block selected `eip2935` union member
 * @param el_header receives the verified RLP header (borrowed from the proof)
 * @param block_hash receives `keccak256(el_header)`
 * @return `C4_SUCCESS`, `C4_PENDING` (the newer block needs more data) or `C4_ERROR`
 */
c4_status_t c4_verify_eip2935_block(verify_ctx_t* ctx, ssz_ob_t block, bytes_t* el_header, bytes32_t block_hash);

#ifdef __cplusplus
}
#endif

#endif
