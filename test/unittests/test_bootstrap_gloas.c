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

// Unit tests for src/chains/eth/prover/bootstrap_gloas.c -- specifically the
// synchronous helpers (chunk reassembly + SSZ assembly). The async orchestrator
// c4_create_gloas_bootstrap needs data-request fixtures and is exercised
// separately once real Lodestar fixtures are captured.

#include "beacon_types.h"
#include "bootstrap_gloas.h"
#include "bytes.h"
#include "c4_assert.h"
#include "crypto.h"
#include "ssz.h"
#include "unity.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

// -----------------------------------------------------------------------------
// Chunk reassembly
// -----------------------------------------------------------------------------

// Build a 24624-byte SyncCommittee raw SSZ blob and split it into 1026 chunks
// exactly the way Lodestar does (48-byte pubkey -> 2 chunks with zero padding).
static void build_deterministic_sync_committee(uint8_t sc_bytes[24624],
                                               uint8_t chunk_leaves[1026 * 32]) {
  memset(sc_bytes, 0, 24624);
  memset(chunk_leaves, 0, 1026 * 32);
  for (uint32_t i = 0; i < 512; i++) {
    // Fill each pubkey with a deterministic pattern so a wrong reassembly is
    // caught (and pubkeys stay distinguishable across the 512 slots).
    for (uint32_t j = 0; j < 48; j++) {
      sc_bytes[i * 48 + j] = (uint8_t) ((i * 7 + j * 31 + 1) & 0xFF);
    }
  }
  // Aggregate pubkey (bytes 24576..24624)
  for (uint32_t j = 0; j < 48; j++) {
    sc_bytes[24576 + j] = (uint8_t) ((j * 5 + 17) & 0xFF);
  }

  // Chunk layout: chunk0 = bytes[0..32], chunk1 = bytes[32..48] || 16 * 0x00.
  for (uint32_t i = 0; i < 512; i++) {
    memcpy(chunk_leaves + (2 * i) * 32u, sc_bytes + i * 48u, 32);
    memcpy(chunk_leaves + (2 * i + 1) * 32u, sc_bytes + i * 48u + 32u, 16);
    // trailing 16 bytes stay zero (memset above)
  }
  memcpy(chunk_leaves + 1024u * 32u, sc_bytes + 24576u, 32);
  memcpy(chunk_leaves + 1025u * 32u, sc_bytes + 24576u + 32u, 16);
}

void test_gloas_bootstrap_chunks_to_sync_committee_roundtrip(void) {
  uint8_t sc_expected[24624]      = {0};
  uint8_t chunk_leaves[1026 * 32] = {0};
  build_deterministic_sync_committee(sc_expected, chunk_leaves);

  uint8_t sc_out[24624] = {0};
  bool    ok            = c4_gloas_bootstrap_chunks_to_sync_committee(
      bytes(chunk_leaves, sizeof(chunk_leaves)),
      bytes(sc_out, sizeof(sc_out)));
  TEST_ASSERT_TRUE_MESSAGE(ok, "well-formed chunks must reassemble");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(sc_expected, sc_out, 24624,
                                        "reassembled SyncCommittee must match the source bytes");
}

void test_gloas_bootstrap_chunks_padding_rejected(void) {
  uint8_t sc_expected[24624]      = {0};
  uint8_t chunk_leaves[1026 * 32] = {0};
  build_deterministic_sync_committee(sc_expected, chunk_leaves);

  // Corrupt one byte of the SSZ padding region of pubkey 42, chunk 1.
  // The 16 tail bytes of chunk 1 (offsets [43*32 + 16 .. 43*32 + 32) inside
  // chunk_leaves) MUST be zero for a valid ByteVector[uint8, 48] leaf.
  chunk_leaves[(2 * 42 + 1) * 32u + 20u] = 0x01;

  uint8_t sc_out[24624] = {0};
  bool    ok            = c4_gloas_bootstrap_chunks_to_sync_committee(
      bytes(chunk_leaves, sizeof(chunk_leaves)),
      bytes(sc_out, sizeof(sc_out)));
  TEST_ASSERT_FALSE_MESSAGE(ok, "non-canonical pubkey padding must be rejected");
}

void test_gloas_bootstrap_chunks_aggregate_padding_rejected(void) {
  uint8_t sc_expected[24624]      = {0};
  uint8_t chunk_leaves[1026 * 32] = {0};
  build_deterministic_sync_committee(sc_expected, chunk_leaves);

  // Corrupt the aggregate pubkey's chunk-1 padding.
  chunk_leaves[1025u * 32u + 24u] = 0x77;

  uint8_t sc_out[24624] = {0};
  bool    ok            = c4_gloas_bootstrap_chunks_to_sync_committee(
      bytes(chunk_leaves, sizeof(chunk_leaves)),
      bytes(sc_out, sizeof(sc_out)));
  TEST_ASSERT_FALSE_MESSAGE(ok, "non-canonical aggregate-pubkey padding must be rejected");
}

void test_gloas_bootstrap_chunks_size_mismatch_rejected(void) {
  uint8_t chunk_leaves[1026 * 32 - 1] = {0};
  uint8_t sc_out[24624]               = {0};
  TEST_ASSERT_FALSE(c4_gloas_bootstrap_chunks_to_sync_committee(
      bytes(chunk_leaves, sizeof(chunk_leaves)),
      bytes(sc_out, sizeof(sc_out))));

  uint8_t chunk_leaves2[1026 * 32] = {0};
  uint8_t sc_out2[24623]           = {0};
  TEST_ASSERT_FALSE(c4_gloas_bootstrap_chunks_to_sync_committee(
      bytes(chunk_leaves2, sizeof(chunk_leaves2)),
      bytes(sc_out2, sizeof(sc_out2))));
}

// -----------------------------------------------------------------------------
// Fixed-size SSZ assembly
// -----------------------------------------------------------------------------

// BeaconBlockHeader: slot(8) + proposerIndex(8) + parentRoot(32) + stateRoot(32) + bodyRoot(32)
static void build_dummy_cl_header(uint8_t       out[112],
                                  uint64_t      slot,
                                  uint64_t      proposer_index,
                                  const uint8_t parent_root[32],
                                  const uint8_t state_root[32],
                                  const uint8_t body_root[32]) {
  memcpy(out + 0, &slot, 8);
  memcpy(out + 8, &proposer_index, 8);
  memcpy(out + 16, parent_root, 32);
  memcpy(out + 48, state_root, 32);
  memcpy(out + 80, body_root, 32);
}

void test_gloas_bootstrap_assemble_layout_and_validity(void) {
  // Header
  bytes32_t parent_root = {0}, state_root = {0}, body_root = {0};
  memset(parent_root, 0x11, 32);
  memset(state_root, 0x22, 32);
  memset(body_root, 0x33, 32);
  uint8_t cl_header[112] = {0};
  build_dummy_cl_header(cl_header, /* slot */ 0x1234, /* proposer_index */ 0x99,
                        parent_root, state_root, body_root);

  bytes32_t exec_block_hash = {0};
  memset(exec_block_hash, 0x44, 32);
  uint8_t exec_branch[11 * 32] = {0};
  for (uint32_t i = 0; i < 11; i++) memset(exec_branch + i * 32, (uint8_t) (0x50 + i), 32);

  // SyncCommittee
  uint8_t sc_bytes[24624]   = {0};
  uint8_t chunks[1026 * 32] = {0};
  build_deterministic_sync_committee(sc_bytes, chunks);

  // sync_committee_branch: 11 arbitrary siblings
  uint8_t sync_branch[11 * 32] = {0};
  for (uint32_t i = 0; i < 11; i++) memset(sync_branch + i * 32, (uint8_t) (0x60 + i), 32);

  bytes_t bootstrap = NULL_BYTES;
  bool    ok        = c4_gloas_bootstrap_assemble(
      bytes(cl_header, 112),
      bytes(exec_block_hash, 32),
      bytes(exec_branch, sizeof(exec_branch)),
      bytes(sc_bytes, sizeof(sc_bytes)),
      bytes(sync_branch, sizeof(sync_branch)),
      &bootstrap);
  TEST_ASSERT_TRUE_MESSAGE(ok, "assemble must succeed with correctly-sized inputs");
  TEST_ASSERT_EQUAL_MESSAGE(C4_GLOAS_BOOTSTRAP_SIZE, bootstrap.len,
                            "assembled bootstrap is 25472 bytes fixed");

  // Layout check: the concatenation is [header (112) | exec_hash (32) | exec_branch (352) |
  // sync_committee (24624) | sync_branch (352)] with no offsets/gaps.
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(cl_header, bootstrap.data + 0, 112,
                                        "cl_header at offset 0");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(exec_block_hash, bootstrap.data + 112, 32,
                                        "executionBlockHash at offset 112");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(exec_branch, bootstrap.data + 144, 352,
                                        "executionBranch at offset 144");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(sc_bytes, bootstrap.data + 496, 24624,
                                        "SyncCommittee at offset 496");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(sync_branch, bootstrap.data + 25120, 352,
                                        "currentSyncCommitteeBranch at offset 25120");

  // SSZ validation against the canonical GLOAS_LIGHT_CLIENT_BOOTSTRAP def.
  const ssz_def_t* def = eth_get_light_client_bootstrap(C4_FORK_GLOAS);
  TEST_ASSERT_NOT_NULL_MESSAGE(def, "GLOAS_LIGHT_CLIENT_BOOTSTRAP def must be defined");
  ssz_ob_t   ob    = {.def = def, .bytes = bootstrap};
  c4_state_t state = {0};
  bool       valid = ssz_is_valid(ob, true, &state);
  if (!valid && state.error) {
    TEST_MESSAGE(state.error);
  }
  TEST_ASSERT_TRUE_MESSAGE(valid, "assembled bytes must validate against Gloas SSZ def");
  c4_state_free(&state);

  // hash_tree_root of the SyncCommittee field must be independently reproducible:
  // build the same 24624 bytes standalone and hash both.
  static const ssz_def_t SYNC_COMMITTEE_CONTAINER =
      SSZ_CONTAINER("SyncCommittee", SYNC_COMMITTEE);
  ssz_ob_t  sc_standalone   = {.def = &SYNC_COMMITTEE_CONTAINER, .bytes = bytes(sc_bytes, 24624)};
  bytes32_t root_standalone = {0};
  ssz_hash_tree_root(sc_standalone, root_standalone);

  ssz_ob_t  sc_embedded   = ssz_get(&ob, "currentSyncCommittee");
  bytes32_t root_embedded = {0};
  ssz_hash_tree_root(sc_embedded, root_embedded);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(root_standalone, root_embedded, 32,
                                        "embedded SyncCommittee root must match standalone");

  safe_free(bootstrap.data);
}

void test_gloas_bootstrap_assemble_size_mismatches_rejected(void) {
  uint8_t   cl_header[111]       = {0}; // one byte short
  bytes32_t exec_block_hash      = {0};
  uint8_t   exec_branch[11 * 32] = {0};
  uint8_t   sc_bytes[24624]      = {0};
  uint8_t   sync_branch[11 * 32] = {0};
  bytes_t   out                  = NULL_BYTES;

  TEST_ASSERT_FALSE_MESSAGE(
      c4_gloas_bootstrap_assemble(
          bytes(cl_header, sizeof(cl_header)),
          bytes(exec_block_hash, 32),
          bytes(exec_branch, sizeof(exec_branch)),
          bytes(sc_bytes, sizeof(sc_bytes)),
          bytes(sync_branch, sizeof(sync_branch)),
          &out),
      "short cl_header must be rejected");
  TEST_ASSERT_NULL(out.data);

  uint8_t cl_header_ok[112]         = {0};
  uint8_t short_branch[11 * 32 - 1] = {0};
  TEST_ASSERT_FALSE_MESSAGE(
      c4_gloas_bootstrap_assemble(
          bytes(cl_header_ok, 112),
          bytes(exec_block_hash, 32),
          bytes(short_branch, sizeof(short_branch)),
          bytes(sc_bytes, sizeof(sc_bytes)),
          bytes(sync_branch, sizeof(sync_branch)),
          &out),
      "short execution branch must be rejected");
  TEST_ASSERT_NULL(out.data);

  uint8_t short_sc[24623] = {0};
  TEST_ASSERT_FALSE_MESSAGE(
      c4_gloas_bootstrap_assemble(
          bytes(cl_header_ok, 112),
          bytes(exec_block_hash, 32),
          bytes(exec_branch, sizeof(exec_branch)),
          bytes(short_sc, sizeof(short_sc)),
          bytes(sync_branch, sizeof(sync_branch)),
          &out),
      "short SyncCommittee must be rejected");
  TEST_ASSERT_NULL(out.data);

  uint8_t sc_ok[24624]      = {0};
  uint8_t bad_exec_hash[31] = {0};
  TEST_ASSERT_FALSE_MESSAGE(
      c4_gloas_bootstrap_assemble(
          bytes(cl_header_ok, 112),
          bytes(bad_exec_hash, sizeof(bad_exec_hash)),
          bytes(exec_branch, sizeof(exec_branch)),
          bytes(sc_ok, sizeof(sc_ok)),
          bytes(sync_branch, sizeof(sync_branch)),
          &out),
      "wrong executionBlockHash size must be rejected");
  TEST_ASSERT_NULL(out.data);

  uint8_t short_sync_branch[11 * 32 - 1] = {0};
  TEST_ASSERT_FALSE_MESSAGE(
      c4_gloas_bootstrap_assemble(
          bytes(cl_header_ok, 112),
          bytes(exec_block_hash, 32),
          bytes(exec_branch, sizeof(exec_branch)),
          bytes(sc_ok, sizeof(sc_ok)),
          bytes(short_sync_branch, sizeof(short_sync_branch)),
          &out),
      "short currentSyncCommitteeBranch must be rejected");
  TEST_ASSERT_NULL(out.data);

  // NULL out_bytes must be rejected without crash.
  TEST_ASSERT_FALSE_MESSAGE(
      c4_gloas_bootstrap_assemble(
          bytes(cl_header_ok, 112),
          bytes(exec_block_hash, 32),
          bytes(exec_branch, sizeof(exec_branch)),
          bytes(sc_ok, sizeof(sc_ok)),
          bytes(sync_branch, sizeof(sync_branch)),
          NULL),
      "NULL out_bytes must be rejected");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_gloas_bootstrap_chunks_to_sync_committee_roundtrip);
  RUN_TEST(test_gloas_bootstrap_chunks_padding_rejected);
  RUN_TEST(test_gloas_bootstrap_chunks_aggregate_padding_rejected);
  RUN_TEST(test_gloas_bootstrap_chunks_size_mismatch_rejected);
  RUN_TEST(test_gloas_bootstrap_assemble_layout_and_validity);
  RUN_TEST(test_gloas_bootstrap_assemble_size_mismatches_rejected);
  return UNITY_END();
}
