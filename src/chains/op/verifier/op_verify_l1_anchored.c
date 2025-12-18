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

#include "op_verify_l1_anchored.h"
#include "op_output_root.h"
#include "op_chains_conf.h"
#include "eth_account.h"
#include "crypto.h"
#include "patricia.h"
#include "rlp.h"
#include "logger.h"
#include "intx_c_api.h"
#include <string.h>

bool op_verify_l1_anchored_proof(verify_ctx_t* ctx, ssz_ob_t l1_anchored_proof) {
  // Get chain configuration to retrieve L2OutputOracle address
  const op_chain_config_t* config = op_get_chain_config(ctx->chain_id);
  if (config == NULL) {
    c4_state_add_error(&ctx->state, "chain not supported");
    return false;
  }

  if (config->l2_output_oracle_address[0] == '\0' ||
      memcmp(config->l2_output_oracle_address, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 20) == 0) {
    c4_state_add_error(&ctx->state, "L2OutputOracle address not configured for this chain");
    return false;
  }

  ssz_ob_t version_ob                   = ssz_get(&l1_anchored_proof, "version");
  ssz_ob_t state_root_ob                = ssz_get(&l1_anchored_proof, "stateRoot");
  ssz_ob_t message_passer_storage_ob    = ssz_get(&l1_anchored_proof, "messagePasserStorageRoot");
  ssz_ob_t latest_block_hash_ob         = ssz_get(&l1_anchored_proof, "latestBlockHash");
  ssz_ob_t l2_output_index_ob           = ssz_get(&l1_anchored_proof, "l2OutputIndex");

  // Extract L1 proofs
  ssz_ob_t l1_account_proof_ob          = ssz_get(&l1_anchored_proof, "l1AccountProof");
  ssz_ob_t l1_storage_proof_ob          = ssz_get(&l1_anchored_proof, "l1StorageProof");
  ssz_ob_t l1_state_proof_ob            = ssz_get(&l1_anchored_proof, "l1StateProof");

  if (version_ob.bytes.len != 32 || state_root_ob.bytes.len != 32 ||
      message_passer_storage_ob.bytes.len != 32 || latest_block_hash_ob.bytes.len != 32) {
    c4_state_add_error(&ctx->state, "invalid L2 component sizes in proof");
    return false;
  }

  // Reconstruct OutputRoot from L2 components
  bytes32_t reconstructed_output_root;
  op_reconstruct_output_root(
      version_ob.bytes.data,
      state_root_ob.bytes.data,
      message_passer_storage_ob.bytes.data,
      latest_block_hash_ob.bytes.data,
      reconstructed_output_root
  );

  // Calculate storage slot for the output index
  uint256_t output_index;
  intx_from_bytes(&output_index, l2_output_index_ob.bytes);

  uint256_t mapping_slot;
  intx_init_value(&mapping_slot, config->l2_outputs_mapping_slot);

  bytes32_t storage_slot;
  op_calculate_output_storage_slot(&output_index, &mapping_slot, storage_slot);

  ssz_ob_t l1_header = ssz_get(&l1_state_proof_ob, "header");
  ssz_ob_t l1_state_root_ob = ssz_get(&l1_header, "stateRoot");

  if (l1_state_root_ob.bytes.len != 32) {
    c4_state_add_error(&ctx->state, "invalid L1 state root");
    return false;
  }

  bytes32_t l1_state_root;
  memcpy(l1_state_root, l1_state_root_ob.bytes.data, 32);

  // Verify the L1 state proof
  if (!eth_verify_state_proof(ctx, l1_state_proof_ob, l1_state_root)) {
    c4_state_add_error(&ctx->state, "L1 state proof verification failed");
    return false;
  }

  // Verify L1 account proof for L2OutputOracle contract
  bytes32_t address_hash = {0};
  bytes_t oracle_address = bytes((uint8_t*)config->l2_output_oracle_address, 20);
  keccak(oracle_address, address_hash);

  bytes_t rlp_account = {0};
  bytes32_t root_check = {0};
  if (patricia_verify(root_check, bytes(address_hash, 32), l1_account_proof_ob, &rlp_account) == PATRICIA_INVALID) {
    c4_state_add_error(&ctx->state, "L1 account proof Patricia verification failed");
    return false;
  }

  if (memcmp(root_check, l1_state_root, 32) != 0) {
    c4_state_add_error(&ctx->state, "L1 account proof root mismatch");
    return false;
  }

  // Decode RLP account to extract storage root
  bytes_t account_field = {0};
  bytes_t remaining = rlp_account;

  if (rlp_decode(&remaining, 0, &account_field) != RLP_LIST) {
    c4_state_add_error(&ctx->state, "invalid RLP account encoding");
    return false;
  }

  rlp_decode(&remaining, 0, &account_field);
  rlp_decode(&remaining, 0, &account_field);

  bytes32_t storage_root = {0};
  memcpy(storage_root, account_field.data, 32);

  // Verify L1 storage proof contains the reconstructed OutputRoot
  bytes32_t storage_path = {0};
  keccak(bytes(storage_slot, 32), storage_path);

  // Verify Patricia trie proof for storage
  bytes_t storage_value_rlp = {0};
  bytes32_t storage_root_check = {0};
  if (patricia_verify(storage_root_check, bytes(storage_path, 32), l1_storage_proof_ob, &storage_value_rlp) == PATRICIA_INVALID) {
    c4_state_add_error(&ctx->state, "L1 storage proof Patricia verification failed");
    return false;
  }

  if (memcmp(storage_root_check, storage_root, 32) != 0) {
    c4_state_add_error(&ctx->state, "L1 storage proof root mismatch");
    return false;
  }

  // Decode RLP to get the actual storage value
  bytes_t storage_value = {0};
  if (rlp_decode(&storage_value_rlp, 0, &storage_value) != RLP_ITEM) {
    c4_state_add_error(&ctx->state, "invalid RLP storage value encoding");
    return false;
  }

  // Extract the OutputRoot from the storage value
  bytes32_t stored_output_root;
  if (!op_extract_output_root_from_storage(storage_value, stored_output_root)) {
    c4_state_add_error(&ctx->state, "failed to extract OutputRoot from storage");
    return false;
  }

  // Compare reconstructed OutputRoot with stored OutputRoot
  if (memcmp(reconstructed_output_root, stored_output_root, 32) != 0) {
    c4_state_add_error(&ctx->state, "OutputRoot mismatch: reconstructed != stored");
    return false;
  }

  return true;
}
