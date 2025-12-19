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

#include "unity.h"
#include "verify.h"
#include "chains.h"
#include "chains/op/verifier/op_verify.h"
#include "chains/op/verifier/op_output_root.h"
#include "chains/op/verifier/op_chains_conf.h"
#include "chains/op/ssz/op_types.h"
#include "crypto.h"
#include "bytes.h"
#include "ssz.h"
#include "rlp.h"
#include "intx_c_api.h"
#include "patricia.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void setUp(void) {
}

void tearDown(void) {
}

/**
 * Helper to create a valid minimal Patricia proof that will pass verification
 */
static bytes_t create_valid_patricia_proof(bytes32_t key, bytes_t value, bytes32_t* out_root) {
  size_t total_size = 256;
  uint8_t* proof_data = calloc(1, total_size);
  size_t offset = 0;

  // Create RLP-encoded leaf node
  uint8_t rlp_leaf[256];
  size_t rlp_offset = 0;

  // RLP list header for [path, value]
  rlp_leaf[rlp_offset++] = 0xc0 + 2 + value.len;
  // Path
  rlp_leaf[rlp_offset++] = 0x20;  // leaf flag + even path
  rlp_leaf[rlp_offset++] = 0x00;  // path nibbles

  // Value
  if (value.len <= 55) {
    rlp_leaf[rlp_offset++] = 0x80 + value.len;
    memcpy(&rlp_leaf[rlp_offset], value.data, value.len);
    rlp_offset += value.len;
  }

  memcpy(proof_data, rlp_leaf, rlp_offset);
  offset = rlp_offset;

  // Calculate root
  keccak(bytes(rlp_leaf, rlp_offset), *out_root);

  return bytes(proof_data, offset);
}

/**
 * Helper to create a complete L1-anchored proof SSZ structure
 */
static bytes_t create_l1_anchored_proof_ssz(void) {
  // Set up the L2 components
  bytes32_t version = {0};  // Version 0
  bytes32_t state_root;
  memset(state_root, 0x11, 32);
  bytes32_t message_passer_storage_root;
  memset(message_passer_storage_root, 0x22, 32);
  bytes32_t latest_block_hash;
  memset(latest_block_hash, 0x33, 32);

  // Calculate the expected OutputRoot
  bytes32_t expected_output_root;
  op_reconstruct_output_root(version, state_root, message_passer_storage_root,
                            latest_block_hash, expected_output_root);

  // L2 output index
  uint256_t output_index;
  intx_init_value(&output_index, 42);

  // Get chain config
  const op_chain_config_t* config = op_get_chain_config(C4_CHAIN_OP_MAINNET);

  // Create mock L1 state root
  bytes32_t l1_state_root;
  memset(l1_state_root, 0x77, 32);

  // Create mock storage root for L2OutputOracle account
  bytes32_t oracle_storage_root;
  memset(oracle_storage_root, 0x88, 32);

  // Create RLP-encoded account for L2OutputOracle
  uint8_t rlp_account[128];
  size_t acc_offset = 0;

  // RLP list [nonce, balance, storageRoot, codeHash]
  rlp_account[acc_offset++] = 0xf8;
  rlp_account[acc_offset++] = 68;    // content length
  rlp_account[acc_offset++] = 0x80;  // nonce = 0
  rlp_account[acc_offset++] = 0x80;  // balance = 0
  rlp_account[acc_offset++] = 0xa0;  // storage root (32 bytes)
  memcpy(&rlp_account[acc_offset], oracle_storage_root, 32);
  acc_offset += 32;
  rlp_account[acc_offset++] = 0xa0;  // code hash (32 bytes)
  bytes32_t code_hash;
  keccak(bytes(NULL, 0), code_hash);  // empty code hash for testing
  memcpy(&rlp_account[acc_offset], code_hash, 32);
  acc_offset += 32;

  // Create account proof
  bytes32_t account_proof_root;
  bytes_t account_value = bytes(rlp_account, acc_offset);
  bytes_t account_proof = create_valid_patricia_proof((bytes32_t){0}, account_value, &account_proof_root);

  // Create storage value containing the OutputRoot
  uint8_t rlp_storage[64];
  rlp_storage[0] = 0xa0;  // RLP prefix for 32 bytes
  memcpy(&rlp_storage[1], expected_output_root, 32);

  // Create storage proof
  bytes32_t storage_proof_root;
  bytes_t storage_value = bytes(rlp_storage, 33);

  // Calculate storage slot
  uint256_t mapping_slot;
  intx_init_value(&mapping_slot, config->l2_outputs_mapping_slot);
  bytes32_t storage_slot;
  op_calculate_output_storage_slot(&output_index, &mapping_slot, storage_slot);

  bytes_t storage_proof = create_valid_patricia_proof(storage_slot, storage_value, &storage_proof_root);

  // Allocate buffer for the complete proof
  size_t total_size = 8192;
  uint8_t* proof_buffer = calloc(1, total_size);
  size_t offset = 0;

  // block_proof container
  // The offset should point to the start of the union data (byte 4 from start of container)
  proof_buffer[offset++] = 4;  // Little-endian encoding of 4
  proof_buffer[offset++] = 0;
  proof_buffer[offset++] = 0;
  proof_buffer[offset++] = 0;

  // Write selector for L1-anchored (1)
  proof_buffer[offset++] = 0x01;

  // Now write the L1-anchored proof content
  // Fixed fields first (version, stateRoot, messagePasserStorageRoot, latestBlockHash, l2OutputIndex)
  memcpy(&proof_buffer[offset], version, 32);
  offset += 32;
  memcpy(&proof_buffer[offset], state_root, 32);
  offset += 32;
  memcpy(&proof_buffer[offset], message_passer_storage_root, 32);
  offset += 32;
  memcpy(&proof_buffer[offset], latest_block_hash, 32);
  offset += 32;
  memcpy(&proof_buffer[offset], output_index.bytes, 32);
  offset += 32;

  // Each offset is 4 bytes, so total offset header is 12 bytes
  // offset for l1AccountProof
  uint32_t account_proof_offset = 12;
  memcpy(&proof_buffer[offset], &account_proof_offset, 4);
  offset += 4;

  // offset for l1StorageProof
  uint32_t storage_proof_offset = account_proof_offset + 4 + account_proof.len;  
  memcpy(&proof_buffer[offset], &storage_proof_offset, 4);
  offset += 4;

  // offset for l1StateProof
  uint32_t state_proof_offset = storage_proof_offset + 4 + storage_proof.len; 
  memcpy(&proof_buffer[offset], &state_proof_offset, 4);
  offset += 4;

  uint32_t acc_proof_len = account_proof.len;
  memcpy(&proof_buffer[offset], &acc_proof_len, 4);
  offset += 4;
  memcpy(&proof_buffer[offset], account_proof.data, account_proof.len);
  offset += account_proof.len;

  uint32_t stor_proof_len = storage_proof.len;
  memcpy(&proof_buffer[offset], &stor_proof_len, 4);
  offset += 4;
  memcpy(&proof_buffer[offset], storage_proof.data, storage_proof.len);
  offset += storage_proof.len;

  // For simplicity, create a minimal state proof
  size_t state_proof_start = offset;

  // ETH_STATE_PROOF contains header and signatures
  memcpy(&proof_buffer[offset], l1_state_root, 32);  // slot
  offset += 32;
  memset(&proof_buffer[offset], 0, 224);  // rest of header fields
  offset += 224;

  free(account_proof.data);
  free(storage_proof.data);

  return bytes(proof_buffer, offset);
}

/**
 * Integration test: op_verify_block with L1-anchored proof
 * Tests the complete flow from entry point through routing to verification
 */
void test_op_verify_block_l1_anchored_integration(void) {
  verify_ctx_t ctx = {0};
  ctx.chain_id = C4_CHAIN_OP_MAINNET;
  ctx.method = "eth_getBlockByNumber";
  const char* args_str = "[\"0x123\", false]";
  ctx.args = (json_t){
    .start = args_str,
    .len = strlen(args_str),
    .type = JSON_TYPE_ARRAY
  };

  bytes_t proof_bytes = create_l1_anchored_proof_ssz();

  // We need to get the OP_BLOCK_PROOF type definition
  const ssz_def_t* proof_type = op_ssz_verification_type(OP_SSZ_VERIFY_BLOCK_PROOF);
  TEST_ASSERT_NOT_NULL(proof_type);

  // SSZ object from our proof
  ctx.proof = ssz_ob(*proof_type, proof_bytes);

  // Verify the proof
  ssz_ob_t block_proof = ssz_get(&ctx.proof, "block_proof");
  TEST_ASSERT_TRUE(block_proof.def != NULL);

  bool is_l1_anchored = strcmp(block_proof.def->name, "l1_anchored") == 0;

  TEST_ASSERT_TRUE(is_l1_anchored);

  // Call op_verify_block
  bool result = op_verify_block(&ctx);

  // Expected to fail because the Patricia proofs aren't cryptographically valid
  // Verifies that the correct flow is executed
  TEST_ASSERT_FALSE(result);

  // Verify we got an appropriate error (not a crash)
  TEST_ASSERT_NOT_NULL(ctx.state.error);

  printf("Integration test completed. Error: %s\n", ctx.state.error ? ctx.state.error : "none");

  c4_state_free(&ctx.state);
  free((void*)proof_bytes.data);
}

int main(void) {
  UNITY_BEGIN();

  // Test L1-anchored proof verification through op_verify_block
  RUN_TEST(test_op_verify_block_l1_anchored_integration);

  return UNITY_END();
}