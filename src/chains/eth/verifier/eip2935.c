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

#include "eip2935.h"
#include "el_header.h"
#include "eth_account.h"
#include "eth_verify.h"
#include "header_cache.h"
#include <string.h>

const address_t ETH_EIP2935_HISTORY_ADDRESS = {0x00, 0x00, 0xF9, 0x08, 0x27, 0xF1, 0xC5, 0x3a, 0x10, 0xcb, 0x7A, 0x02, 0x33, 0x5B, 0x17, 0x53, 0x20, 0x00, 0x29, 0x35};

void eth_eip2935_slot(uint64_t block_number, bytes32_t slot) {
  memset(slot, 0, 32);
  uint64_to_be(slot + 24, block_number % ETH_EIP2935_HISTORY_WINDOW);
}

c4_status_t c4_verify_eip2935_block(verify_ctx_t* ctx, ssz_ob_t block, bytes_t* el_header, bytes32_t block_hash) {
  if (!block.def || !block.def->name || strcmp(block.def->name, "eip2935") != 0)
    THROW_ERROR("invalid block proof, expected eip2935!");

  bytes_t  header  = ssz_get(&block, "elHeader").bytes;
  ssz_ob_t account = ssz_get(&block, "account");
  if (!account.def) THROW_ERROR("missing account proof in the eip2935 proof!");
  ssz_ob_t newer_proof = ssz_get(&account, "elProof");
  if (!header.data || !header.len) THROW_ERROR("missing header in the eip2935 proof!");
  if (!newer_proof.def || !newer_proof.def->name) THROW_ERROR("missing proof of the newer block in the eip2935 proof!");

  // One level only: the newer block must be proven by itself. A chain of history proofs
  // would let the proof (and the recursion below) grow without bound.
  if (strcmp(newer_proof.def->name, "eip2935") == 0)
    THROW_ERROR("an eip2935 proof must not be nested in another eip2935 proof!");

  // 1. the newer block, verified as a block of this chain
  bytes_t   newer_header = NULL_BYTES;
  bytes32_t newer_hash   = {0};
  TRY_ASYNC(c4_verify_block(ctx, newer_proof, &newer_header, newer_hash));

  // 2. + 3. the history contract (exactly one storage proof) in the state of the newer
  //    block. The slot is taken from the proof here and bound to the block number in
  //    step 5: the header is not parsed before its hash is authenticated.
  bytes32_t slot     = {0};
  bytes32_t expected = {0};
  bytes32_t value    = {0};
  TRY_ASYNC(eth_verify_storage_proof(&ctx->state, account, ETH_EIP2935_HISTORY_ADDRESS, eth_el_header_get(newer_header, EL_STATE_ROOT), slot, value));

  // 4. the header is the one whose hash the history contract stores
  keccak(header, block_hash);
  if (memcmp(value, block_hash, 32) != 0) THROW_ERROR("header does not match the block hash stored in the EIP-2935 history contract!");
  if (!eth_el_header_is_valid(header)) THROW_ERROR("the block stored in the EIP-2935 history contract is not a valid execution header!");

  // 5. + 6. the slot is the one of this block number, and the newer block still serves it.
  //    A slot is overwritten every 8191 blocks, so outside the window it holds another block.
  uint64_t number       = eth_el_header_get_uint64(header, EL_BLOCK_NUMBER);
  uint64_t newer_number = eth_el_header_get_uint64(newer_header, EL_BLOCK_NUMBER);
  eth_eip2935_slot(number, expected);
  if (memcmp(slot, expected, 32) != 0) THROW_ERROR("eip2935 proof is not about the history slot of the block!");
  if (number >= newer_number || newer_number - number > ETH_EIP2935_HISTORY_WINDOW)
    THROW_ERROR("block is outside the EIP-2935 history window of the newer block!");

  // The header is now verified: cache it so follow-up proofs can reference this block by
  // hash only (blockHash variant of ETH_EL_PROOF_UNION).
  // A block number of 0 indicates an unparsable header field, do not key the cache on it.
  if (number) c4_header_cache_put(ctx->chain_id, number, block_hash, header, NULL);

  *el_header = header; // borrowed from the proof, valid for the lifetime of the ctx
  return C4_SUCCESS;
}
