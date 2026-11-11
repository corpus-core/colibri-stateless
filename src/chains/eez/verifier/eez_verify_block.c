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

#include "bytes.h"
#include "crypto.h"
#include "eez_chains_conf.h"
#include "eez_verify.h"
#include "eip2935.h"
#include "el_header.h"
#include "eth_account.h"
#include "eth_verify.h"
#include "header_cache.h"
#include "ssz.h"
#include "state.h"
#include <string.h>

c4_status_t eez_get_l1_storage_value(c4_state_t* state, const eez_chain_config_t* conf, ssz_ob_t account, bytes_t l1_state_root, bytes32_t value) {
  bytes32_t slot = {0};
  memset(value, 0, 32);
  if (!conf) return c4_state_add_error(state, "unsupported EEZ chain!");
  eez_root_slot(conf->rollup_id, slot);
  return eth_verify_storage_slot(state, account, conf->proxy, slot, l1_state_root, value);
}

c4_status_t eez_verify_l1_proof(verify_ctx_t* ctx, ssz_ob_t block, bytes_t* el_header, bytes32_t block_hash) {
  if (!block.def || !block.def->name || strcmp(block.def->name, "eezL1Proof") != 0)
    THROW_ERROR("invalid block proof for an EEZ chain, expected eezL1Proof!");

  const eez_chain_config_t* conf = eez_get_chain_config(ctx->chain_id);
  if (!conf) THROW_ERROR("unsupported EEZ chain!");

  bytes_t  l2_header = ssz_get(&block, "elHeader").bytes;
  ssz_ob_t account   = ssz_get(&block, "account");
  if (!account.def) THROW_ERROR("missing account proof in the EEZ proof!");
  ssz_ob_t l1_proof = ssz_get(&account, "elProof");
  if (!l2_header.data || !l2_header.len) THROW_ERROR("missing L2 header in the EEZ proof!");

  // Only a full consensus proof is accepted for the L1 block. In particular `blockHash`
  // is rejected: verified headers are looked up by hash, and a hash of the L2 chain must
  // never be resolved as an L1 header.
  if (!l1_proof.def || !l1_proof.def->name || strcmp(l1_proof.def->name, "clProof") != 0)
    THROW_ERROR("the L1 block of an EEZ proof must be proven by a clProof!");

  // The L1 block is verified as a block of the L1 chain (chain spec, sync committee,
  // header cache). The L2 chain id is restored on every path, including PENDING and ERROR.
  bytes_t    l1_header  = NULL_BYTES;
  bytes32_t  l1_hash    = {0};
  chain_id_t l2_chain   = ctx->chain_id;
  ctx->chain_id         = conf->l1_chain_id;
  c4_status_t l1_status = c4_verify_block(ctx, l1_proof, &l1_header, l1_hash);
  ctx->chain_id         = l2_chain;
  if (l1_status != C4_SUCCESS) return l1_status;

  bytes32_t anchor = {0};
  TRY_ASYNC(eez_get_l1_storage_value(&ctx->state, conf, account, eth_el_header_get(l1_header, EL_STATE_ROOT), anchor));
  if (bytes_all_zero(bytes(anchor, 32))) THROW_ERROR("no L2 block hash is stored in the EEZ proxy for this rollup!");

  keccak(l2_header, block_hash);
  if (memcmp(anchor, block_hash, 32) != 0) THROW_ERROR("L2 header does not match the block hash stored on L1!");
  if (!eth_el_header_is_valid(l2_header)) THROW_ERROR("the block anchored on L1 is not a valid execution header!");

  // The header is now verified: cache it under the L2 chain id so follow-up proofs can
  // reference this block by hash only (blockHash variant of ETH_EL_PROOF_UNION).
  // A block number of 0 indicates an unparsable header field, do not key the cache on it.
  uint64_t block_number = eth_el_header_get_uint64(l2_header, EL_BLOCK_NUMBER);
  if (block_number) c4_header_cache_put(ctx->chain_id, block_number, block_hash, l2_header, NULL);

  *el_header = l2_header; // borrowed from the proof, valid for the lifetime of the ctx
  return C4_SUCCESS;
}

c4_status_t eez_verify_block_proof(verify_ctx_t* ctx, ssz_ob_t block, bytes_t* el_header, bytes32_t block_hash) {
  if (!block.def || !block.def->name) THROW_ERROR("invalid block type!");
  if (strcmp(block.def->name, "eezL1Proof") == 0) return eez_verify_l1_proof(ctx, block, el_header, block_hash);
  // A block older than the anchor: its hash is read from the EIP-2935 history contract in
  // the state of a newer block of this chain. That newer block is verified through
  // `c4_verify_block`, which comes back here for the `eezL1Proof` of the anchor.
  if (strcmp(block.def->name, "eip2935") == 0) return c4_verify_eip2935_block(ctx, block, el_header, block_hash);
  THROW_ERROR("invalid block proof for an EEZ chain, expected eezL1Proof or eip2935!");
}

void eez_register_block_proof_verify(void) {
  c4_register_block_proof_verify(C4_CHAIN_TYPE_EEZ, eez_verify_block_proof);
}
