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

// Unit tests for src/chains/eth/prover/state_proofs.c.
// All expected descriptor byte vectors were cross-checked against the
// reference implementation at
//   https://github.com/ChainSafe/ssz/blob/main/packages/persistent-merkle-tree/src/proof/compactMulti.ts
// by tracing `computeDescriptor` on the same gindices.

#include "bytes.h"
#include "c4_assert.h"
#include "crypto.h"
#include "ssz.h"
#include "state_proofs.h"
#include "unity.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

// Helper: assert a computed descriptor matches an expected byte vector.
static void assert_descriptor(const gindex_t* indices, int count,
                              const uint8_t* expected, size_t expected_len,
                              const char* msg) {
  bytes_t desc = c4_ssz_compute_compact_descriptor(indices, count);
  TEST_ASSERT_EQUAL_MESSAGE(expected_len, desc.len, msg);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected, desc.data, expected_len, msg);
  safe_free(desc.data);
}

// :: Descriptor known vectors

void test_descriptor_single_root(void) {
  gindex_t g          = 1;
  uint8_t  expected[] = {0x80};
  assert_descriptor(&g, 1, expected, sizeof(expected), "gindex=1 descriptor");
}

void test_descriptor_single_left_child(void) {
  gindex_t g          = 2;
  uint8_t  expected[] = {0x60};
  assert_descriptor(&g, 1, expected, sizeof(expected), "gindex=2 descriptor");
}

void test_descriptor_single_right_child(void) {
  gindex_t g          = 3;
  uint8_t  expected[] = {0x60};
  assert_descriptor(&g, 1, expected, sizeof(expected), "gindex=3 descriptor");
}

void test_descriptor_deneb_current_sync_committee(void) {
  // BeaconState.current_sync_committee gindex in Deneb == 54.
  gindex_t g          = 54;
  uint8_t  expected[] = {0x4a, 0xe0};
  assert_descriptor(&g, 1, expected, sizeof(expected),
                    "gindex=54 (Deneb currentSyncCommittee) descriptor");
}

void test_descriptor_electra_current_sync_committee(void) {
  // BeaconState.current_sync_committee gindex in Electra == 86.
  gindex_t g          = 86;
  uint8_t  expected[] = {0x25, 0x78};
  assert_descriptor(&g, 1, expected, sizeof(expected),
                    "gindex=86 (Electra currentSyncCommittee) descriptor");
}

void test_descriptor_multi_gindex(void) {
  // Depth-3 tree: prove leaves at gindices 8 and 15.
  // Expected proof set = {8, 9, 5, 14, 15, 6}; sorted lex to
  // {1000, 1001, 101, 110, 1110, 1111}. Encoding yields "00011101011"
  // (11 bits) which pads to 0x1d 0x60.
  gindex_t indices[]  = {8, 15};
  uint8_t  expected[] = {0x1d, 0x60};
  assert_descriptor(indices, 2, expected, sizeof(expected),
                    "multi-gindex {8,15} descriptor");
}

void test_descriptor_three_gindices_mixed_parents(void) {
  // Depth-3 tree: prove leaves 8, 12, 15. After path-set removal the
  // combined proof set is {5, 8, 9, 12, 13, 14, 15}. Sorted lex:
  //   "1000" < "1001" < "101" < "1100" < "1101" < "1110" < "1111"
  // Encoding: 0001, 1, 1, 001, 1, 01, 1 -> "0001110011011" (13 bits)
  // Padded to 16 bits: 0x1c 0xd8.
  gindex_t indices[]  = {8, 12, 15};
  uint8_t  expected[] = {0x1c, 0xd8};
  assert_descriptor(indices, 3, expected, sizeof(expected),
                    "multi-gindex {8,12,15} descriptor");
}

void test_descriptor_ancestor_of_leaf_is_redundant(void) {
  // Adding ancestors of an already-included leaf must be a no-op because
  // path-set removal folds them away. Both inputs must yield the same
  // descriptor as `{8}` alone.
  gindex_t single[]         = {8};
  gindex_t with_ancestors[] = {2, 4, 8};
  uint8_t  expected[]       = {0x1e};
  assert_descriptor(single, 1, expected, sizeof(expected),
                    "descriptor for {8}");
  assert_descriptor(with_ancestors, 3, expected, sizeof(expected),
                    "adding ancestors of an existing leaf must not change descriptor");
}

void test_descriptor_duplicate_input_gindices(void) {
  gindex_t dup[]      = {8, 8, 8};
  uint8_t  expected[] = {0x1e};
  assert_descriptor(dup, 3, expected, sizeof(expected),
                    "duplicate input gindices must dedupe to single-gindex descriptor");
}

void test_descriptor_zero_gindex_rejected(void) {
  gindex_t g   = 0;
  bytes_t  res = c4_ssz_compute_compact_descriptor(&g, 1);
  TEST_ASSERT_EQUAL_MESSAGE(0, res.len, "gindex=0 must yield NULL_BYTES");
  TEST_ASSERT_NULL_MESSAGE(res.data, "gindex=0 must yield NULL_BYTES");
}

void test_descriptor_empty_input_rejected(void) {
  gindex_t g   = 1;
  bytes_t  res = c4_ssz_compute_compact_descriptor(&g, 0);
  TEST_ASSERT_EQUAL_MESSAGE(0, res.len, "count=0 must yield NULL_BYTES");
  TEST_ASSERT_NULL_MESSAGE(res.data, "count=0 must yield NULL_BYTES");
}

// :: compact_to_branch synthetic tree

// Builds a 4-leaf compact tree that mirrors a real 4-leaf tree at gindices
// [8, 9, 10, 11] where leaves 12..15 are represented as a single opaque
// subtree hash at gindex=3. Returns the tree root and populates the compact
// proof leaves that would be sent by a Lodestar node for target gindex=8.
//
// Layout (compact tree leaves in preorder = descriptor decoding order):
//   [gindex=8, gindex=9, gindex=5 (opaque subtree), gindex=3 (opaque subtree)]
//
// Descriptor bit-encoding for {8, 9, 5, 3} sorted lex to
// {"1000","1001","101","11"} gives "0001111" (7 bits) padded to 0x1e.
static void build_compact_proof_target_8(bytes32_t leaf_at_8,
                                         bytes32_t leaf_at_9,
                                         bytes32_t subtree_at_5,
                                         bytes32_t subtree_at_3,
                                         uint8_t   compact_leaves[128], // 4 * 32
                                         uint8_t   descriptor_out[1],
                                         bytes32_t root_out) {
  memcpy(compact_leaves + 0 * 32, leaf_at_8, 32);
  memcpy(compact_leaves + 1 * 32, leaf_at_9, 32);
  memcpy(compact_leaves + 2 * 32, subtree_at_5, 32);
  memcpy(compact_leaves + 3 * 32, subtree_at_3, 32);
  descriptor_out[0] = 0x1e;

  // Compute the root manually so the assertion doesn't smuggle bugs in
  // via reusing the code under test.
  bytes32_t hash_4 = {0};
  sha256_merkle(bytes(leaf_at_8, 32), bytes(leaf_at_9, 32), hash_4);
  bytes32_t hash_2 = {0};
  sha256_merkle(bytes(hash_4, 32), bytes(subtree_at_5, 32), hash_2);
  sha256_merkle(bytes(hash_2, 32), bytes(subtree_at_3, 32), root_out);
}

void test_descriptor_target_8_matches_expected_bytes(void) {
  gindex_t g          = 8;
  uint8_t  expected[] = {0x1e};
  assert_descriptor(&g, 1, expected, sizeof(expected), "gindex=8 descriptor");
}

void test_compact_to_branch_roundtrip_target_8(void) {
  bytes32_t leaf_8 = {0}, leaf_9 = {0}, sub_5 = {0}, sub_3 = {0};
  memset(leaf_8, 0xaa, 32);
  memset(leaf_9, 0xbb, 32);
  memset(sub_5, 0xcc, 32);
  memset(sub_3, 0xdd, 32);

  uint8_t   compact_leaves[128] = {0};
  uint8_t   descriptor[1]       = {0};
  bytes32_t root                = {0};
  build_compact_proof_target_8(leaf_8, leaf_9, sub_5, sub_3,
                               compact_leaves, descriptor, root);

  bytes_t branch = NULL_BYTES;
  bool    ok     = c4_ssz_compact_to_branch(
      bytes(compact_leaves, 128), bytes(descriptor, 1),
      /* gindex */ 8, root, &branch);
  TEST_ASSERT_TRUE_MESSAGE(ok, "compact_to_branch must succeed for gindex=8");
  TEST_ASSERT_EQUAL_MESSAGE(3 * 32, branch.len,
                            "branch depth for gindex=8 must be 3 siblings");

  // Expected branch (leaf-to-root): sibling of 8 = leaf_9, sibling of 4 = sub_5,
  // sibling of 2 = sub_3.
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(leaf_9, branch.data + 0 * 32, 32,
                                        "branch[0] must be leaf at gindex=9");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(sub_5, branch.data + 1 * 32, 32,
                                        "branch[1] must be subtree hash at gindex=5");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(sub_3, branch.data + 2 * 32, 32,
                                        "branch[2] must be subtree hash at gindex=3");

  // Verify the branch actually reconstructs the root using the shared
  // Colibri verifier (defense against reversed / off-by-one packing).
  bytes32_t computed = {0};
  ssz_verify_single_merkle_proof(branch, leaf_8, /* gindex */ 8, computed);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(root, computed, 32,
                                        "extracted branch must reconstruct root");

  safe_free(branch.data);
}

void test_compact_to_branch_root_mismatch_rejected(void) {
  bytes32_t leaf_8 = {0}, leaf_9 = {0}, sub_5 = {0}, sub_3 = {0};
  memset(leaf_8, 0x11, 32);
  memset(leaf_9, 0x22, 32);
  memset(sub_5, 0x33, 32);
  memset(sub_3, 0x44, 32);

  uint8_t   compact_leaves[128] = {0};
  uint8_t   descriptor[1]       = {0};
  bytes32_t real_root           = {0};
  build_compact_proof_target_8(leaf_8, leaf_9, sub_5, sub_3,
                               compact_leaves, descriptor, real_root);

  bytes32_t wrong_root = {0};
  memset(wrong_root, 0x55, 32);

  bytes_t branch = NULL_BYTES;
  bool    ok     = c4_ssz_compact_to_branch(
      bytes(compact_leaves, 128), bytes(descriptor, 1),
      8, wrong_root, &branch);
  TEST_ASSERT_FALSE_MESSAGE(ok, "root mismatch must be rejected");
  TEST_ASSERT_NULL_MESSAGE(branch.data,
                           "no branch allocation on root mismatch");
}

void test_compact_to_branch_target_not_in_compact_tree(void) {
  // Same tree as above but target gindex=11: real leaf that is folded into
  // the opaque subtree at gindex=5 in the compact proof. Reconstruction
  // reaches gindex=5 as a leaf and cannot descend further; the branch
  // length check must reject this.
  bytes32_t leaf_8 = {0}, leaf_9 = {0}, sub_5 = {0}, sub_3 = {0};
  memset(leaf_8, 0x01, 32);
  memset(leaf_9, 0x02, 32);
  memset(sub_5, 0x03, 32);
  memset(sub_3, 0x04, 32);

  uint8_t   compact_leaves[128] = {0};
  uint8_t   descriptor[1]       = {0};
  bytes32_t root                = {0};
  build_compact_proof_target_8(leaf_8, leaf_9, sub_5, sub_3,
                               compact_leaves, descriptor, root);

  bytes_t branch = NULL_BYTES;
  bool    ok     = c4_ssz_compact_to_branch(
      bytes(compact_leaves, 128), bytes(descriptor, 1),
      /* gindex */ 11, root, &branch);
  TEST_ASSERT_FALSE_MESSAGE(ok,
                            "target that is not a compact-tree leaf must be rejected");
  TEST_ASSERT_NULL_MESSAGE(branch.data,
                           "no branch allocation for out-of-tree target");
}

void test_compact_to_branch_gindex_rejects_zero_and_one(void) {
  uint8_t   descriptor[]  = {0x80}; // valid single-leaf descriptor
  uint8_t   leaves[32]    = {0};
  bytes_t   branch        = NULL_BYTES;
  bytes32_t expected_root = {0};

  TEST_ASSERT_FALSE_MESSAGE(
      c4_ssz_compact_to_branch(bytes(leaves, 32), bytes(descriptor, 1),
                               0, expected_root, &branch),
      "gindex=0 must be rejected");
  TEST_ASSERT_NULL(branch.data);

  TEST_ASSERT_FALSE_MESSAGE(
      c4_ssz_compact_to_branch(bytes(leaves, 32), bytes(descriptor, 1),
                               1, expected_root, &branch),
      "gindex=1 must be rejected");
  TEST_ASSERT_NULL(branch.data);
}

// :: Descriptor validation edge cases

void test_compact_to_branch_all_zero_descriptor_rejected(void) {
  // Descriptor with no `1` bit cannot terminate the tree.
  uint8_t   descriptor[] = {0x00};
  uint8_t   leaves[32]   = {0};
  bytes32_t expected     = {0};
  bytes_t   branch       = NULL_BYTES;
  TEST_ASSERT_FALSE(c4_ssz_compact_to_branch(bytes(leaves, 32),
                                             bytes(descriptor, 1),
                                             2, expected, &branch));
  TEST_ASSERT_NULL(branch.data);
}

void test_compact_to_branch_padding_bit_set_rejected(void) {
  // Valid single-leaf descriptor is 0x80. Setting a padding bit (any bit
  // after position 0) must be rejected.
  uint8_t   descriptor[]     = {0x81};
  uint8_t   leaves[32]       = {0}; // one leaf
  bytes32_t root_of_one_leaf = {0};
  memcpy(root_of_one_leaf, leaves, 32);
  bytes_t branch = NULL_BYTES;
  TEST_ASSERT_FALSE_MESSAGE(
      c4_ssz_compact_to_branch(bytes(leaves, 32), bytes(descriptor, 1),
                               /* gindex */ 2, root_of_one_leaf, &branch),
      "padding bit set must be rejected");
  TEST_ASSERT_NULL(branch.data);
}

void test_compact_to_branch_extra_trailing_bytes_rejected(void) {
  // Valid descriptor 0x60 (single-node proof) followed by an extra zero byte
  // must be rejected (JS reference: "Invalid descriptor: too many bytes").
  uint8_t   descriptor[] = {0x60, 0x00};
  bytes32_t leafA        = {0};
  bytes32_t leafB        = {0};
  memset(leafA, 0xa5, 32);
  memset(leafB, 0x5a, 32);
  uint8_t leaves[64] = {0};
  memcpy(leaves, leafA, 32);
  memcpy(leaves + 32, leafB, 32);
  bytes32_t root = {0};
  sha256_merkle(bytes(leafA, 32), bytes(leafB, 32), root);

  bytes_t branch = NULL_BYTES;
  TEST_ASSERT_FALSE_MESSAGE(
      c4_ssz_compact_to_branch(bytes(leaves, 64), bytes(descriptor, 2),
                               /* gindex */ 2, root, &branch),
      "trailing padding byte must be rejected");
  TEST_ASSERT_NULL(branch.data);
}

void test_compact_to_branch_null_inputs_rejected(void) {
  bytes32_t root     = {0};
  bytes_t   branch   = NULL_BYTES;
  uint8_t   desc[]   = {0x80};
  uint8_t   leaf[32] = {0};

  // NULL branch_out pointer
  TEST_ASSERT_FALSE(c4_ssz_compact_to_branch(bytes(leaf, 32), bytes(desc, 1),
                                             2, root, NULL));

  // NULL leaves buffer with non-zero length is malformed
  bytes_t bad_leaves = {.data = NULL, .len = 32};
  TEST_ASSERT_FALSE(c4_ssz_compact_to_branch(bad_leaves, bytes(desc, 1),
                                             2, root, &branch));
  TEST_ASSERT_NULL(branch.data);

  // NULL descriptor buffer
  bytes_t bad_desc = {.data = NULL, .len = 1};
  TEST_ASSERT_FALSE(c4_ssz_compact_to_branch(bytes(leaf, 32), bad_desc,
                                             2, root, &branch));
  TEST_ASSERT_NULL(branch.data);
}

void test_compact_to_branch_unaligned_leaves_rejected(void) {
  uint8_t   descriptor[] = {0x80};
  uint8_t   leaves[33]   = {0}; // not a multiple of 32
  bytes32_t root         = {0};
  bytes_t   branch       = NULL_BYTES;
  TEST_ASSERT_FALSE_MESSAGE(
      c4_ssz_compact_to_branch(bytes(leaves, 33), bytes(descriptor, 1),
                               2, root, &branch),
      "leaves buffer length must be a multiple of 32");
  TEST_ASSERT_NULL(branch.data);
}

void test_compact_to_branch_leaf_count_mismatch_rejected(void) {
  // Descriptor 0x60 implies a 2-leaf tree (3 bits: "011"). Passing a single
  // leaf must fail the `bits == leaves*2 - 1` invariant.
  uint8_t   descriptor[] = {0x60};
  uint8_t   leaves[32]   = {0};
  bytes32_t root         = {0};
  bytes_t   branch       = NULL_BYTES;
  TEST_ASSERT_FALSE_MESSAGE(
      c4_ssz_compact_to_branch(bytes(leaves, 32), bytes(descriptor, 1),
                               /* gindex */ 2, root, &branch),
      "leaf count mismatch must be rejected");
  TEST_ASSERT_NULL(branch.data);
}

void test_compact_to_branch_two_leaf_right_target(void) {
  // Same 2-leaf tree as the "two_leaf_success" case, but target the right
  // leaf (gindex=3). Exercises the right_on_path branch in reconstruct().
  uint8_t   descriptor[] = {0x60};
  bytes32_t leafA        = {0};
  bytes32_t leafB        = {0};
  memset(leafA, 0x71, 32);
  memset(leafB, 0x8e, 32);
  uint8_t leaves[64] = {0};
  memcpy(leaves, leafA, 32);
  memcpy(leaves + 32, leafB, 32);
  bytes32_t root = {0};
  sha256_merkle(bytes(leafA, 32), bytes(leafB, 32), root);

  bytes_t branch = NULL_BYTES;
  bool    ok     = c4_ssz_compact_to_branch(bytes(leaves, 64),
                                            bytes(descriptor, 1),
                                            /* gindex */ 3, root, &branch);
  TEST_ASSERT_TRUE_MESSAGE(ok, "right-side target must succeed");
  TEST_ASSERT_EQUAL_MESSAGE(32, branch.len,
                            "two-leaf right target has depth 1");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(leafA, branch.data, 32,
                                        "sibling of right leaf is left leaf");

  bytes32_t computed = {0};
  ssz_verify_single_merkle_proof(branch, leafB, /* gindex */ 3, computed);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(root, computed, 32,
                                        "extracted branch must reconstruct root");
  safe_free(branch.data);
}

void test_compact_to_branch_right_side_opaque_subtree_target(void) {
  // Reuse the 4-leaf compact tree from build_compact_proof_target_8, but
  // request the opaque subtree at gindex=3 as target. The compact tree
  // represents that gindex as a leaf, so the branch has a single sibling
  // = hash of the whole left subtree (gindex=2).
  bytes32_t leaf_8 = {0}, leaf_9 = {0}, sub_5 = {0}, sub_3 = {0};
  memset(leaf_8, 0xa1, 32);
  memset(leaf_9, 0xa2, 32);
  memset(sub_5, 0xa3, 32);
  memset(sub_3, 0xa4, 32);

  uint8_t   compact_leaves[128] = {0};
  uint8_t   descriptor[1]       = {0};
  bytes32_t root                = {0};
  build_compact_proof_target_8(leaf_8, leaf_9, sub_5, sub_3,
                               compact_leaves, descriptor, root);

  bytes_t branch = NULL_BYTES;
  bool    ok     = c4_ssz_compact_to_branch(bytes(compact_leaves, 128),
                                            bytes(descriptor, 1),
                                            /* gindex */ 3, root, &branch);
  TEST_ASSERT_TRUE_MESSAGE(ok, "right-side opaque subtree target must succeed");
  TEST_ASSERT_EQUAL_MESSAGE(32, branch.len, "gindex=3 branch depth is 1");

  // Expected sibling = hash at gindex=2 = merkle(merkle(leaf_8, leaf_9), sub_5)
  bytes32_t hash_4 = {0}, hash_2 = {0};
  sha256_merkle(bytes(leaf_8, 32), bytes(leaf_9, 32), hash_4);
  sha256_merkle(bytes(hash_4, 32), bytes(sub_5, 32), hash_2);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(hash_2, branch.data, 32,
                                        "sibling of gindex=3 is hash at gindex=2");

  bytes32_t computed = {0};
  ssz_verify_single_merkle_proof(branch, sub_3, /* gindex */ 3, computed);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(root, computed, 32,
                                        "branch reconstructs root");
  safe_free(branch.data);
}

void test_compact_to_branch_single_leaf_tree_rejects_deeper_gindex(void) {
  // A 1-leaf compact tree (descriptor 0x80, one leaf, root == that leaf)
  // has no depth to offer. Requesting any gindex >= 2 must fail because
  // reconstruction never descends and the branch stays empty.
  uint8_t   descriptor[] = {0x80};
  bytes32_t leaf         = {0};
  memset(leaf, 0x42, 32);
  uint8_t leaves[32] = {0};
  memcpy(leaves, leaf, 32);
  bytes32_t root_matches_leaf = {0};
  memcpy(root_matches_leaf, leaf, 32);

  bytes_t branch = NULL_BYTES;
  TEST_ASSERT_FALSE_MESSAGE(
      c4_ssz_compact_to_branch(bytes(leaves, 32), bytes(descriptor, 1),
                               /* gindex */ 2, root_matches_leaf, &branch),
      "single-leaf compact tree cannot serve a gindex-2 target");
  TEST_ASSERT_NULL(branch.data);
}

void test_compact_to_branch_all_ones_byte_rejected(void) {
  // Body is a single "1" bit; the remaining 7 padding bits are all set.
  uint8_t   descriptor[] = {0xFF};
  uint8_t   leaves[32]   = {0};
  bytes32_t root         = {0};
  bytes_t   branch       = NULL_BYTES;
  TEST_ASSERT_FALSE_MESSAGE(
      c4_ssz_compact_to_branch(bytes(leaves, 32), bytes(descriptor, 1),
                               2, root, &branch),
      "descriptor 0xFF must be rejected (padding bits set)");
  TEST_ASSERT_NULL(branch.data);
}

void test_compact_to_branch_balanced_bits_never_terminate(void) {
  // 0x55 == 01010101 keeps #1 == #0 throughout the byte. The termination
  // condition `#1 > #0` is never reached, so validation must fail.
  uint8_t   descriptor[] = {0x55};
  uint8_t   leaves[32]   = {0};
  bytes32_t root         = {0};
  bytes_t   branch       = NULL_BYTES;
  TEST_ASSERT_FALSE_MESSAGE(
      c4_ssz_compact_to_branch(bytes(leaves, 32), bytes(descriptor, 1),
                               2, root, &branch),
      "descriptor whose #1 never exceeds #0 must be rejected");
  TEST_ASSERT_NULL(branch.data);
}

void test_compact_to_branch_descriptor_over_max_size_rejected(void) {
  // MAX_DESCRIPTOR_BYTES (2048) matches the SSZ container limit. A
  // 2049-byte descriptor must be rejected before any bit parsing.
  const size_t oversize   = 2049;
  uint8_t*     descriptor = (uint8_t*) safe_calloc(oversize, 1);
  descriptor[0]           = 0x80;
  uint8_t   leaves[32]    = {0};
  bytes32_t root          = {0};
  bytes_t   branch        = NULL_BYTES;
  TEST_ASSERT_FALSE_MESSAGE(
      c4_ssz_compact_to_branch(bytes(leaves, 32), bytes(descriptor, oversize),
                               2, root, &branch),
      "descriptor over MAX_DESCRIPTOR_BYTES must be rejected");
  TEST_ASSERT_NULL(branch.data);
  safe_free(descriptor);
}

void test_compact_to_branch_two_leaf_success(void) {
  // Simplest non-trivial roundtrip: tree of two leaves at gindices 2 and 3.
  // Descriptor = 0x60, target = 2.
  uint8_t   descriptor[] = {0x60};
  bytes32_t leafA        = {0};
  bytes32_t leafB        = {0};
  memset(leafA, 0x71, 32);
  memset(leafB, 0x8e, 32);
  uint8_t leaves[64] = {0};
  memcpy(leaves, leafA, 32);
  memcpy(leaves + 32, leafB, 32);
  bytes32_t root = {0};
  sha256_merkle(bytes(leafA, 32), bytes(leafB, 32), root);

  bytes_t branch = NULL_BYTES;
  bool    ok     = c4_ssz_compact_to_branch(bytes(leaves, 64),
                                            bytes(descriptor, 1),
                                            /* gindex */ 2, root, &branch);
  TEST_ASSERT_TRUE_MESSAGE(ok, "two-leaf compact proof must succeed");
  TEST_ASSERT_EQUAL_MESSAGE(32, branch.len,
                            "two-leaf proof branch has depth 1");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(leafB, branch.data, 32,
                                        "branch[0] must be sibling leafB");

  bytes32_t computed = {0};
  ssz_verify_single_merkle_proof(branch, leafA, 2, computed);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(root, computed, 32,
                                        "extracted branch must reconstruct root");
  safe_free(branch.data);
}

// :: Deeper synthetic tree to cover branch length > 3

void test_compact_to_branch_depth_5(void) {
  // Depth-5 tree; target = gindex 32. Compact tree leaves in lex-sorted
  // preorder (shorter prefix < longer with '0' < '1'):
  //   "100000" (32) < "100001" (33) < "10001" (17) < "1001" (9)
  //     < "101" (5) < "11" (3)
  // Encoding = trailing-zeros run + terminator "1":
  //   000001 . 1 . 1 . 1 . 1 . 1 . 1 = 0000011111111  (11 bits = 2*6-1)
  // Padded to 16 bits: 0x07 0xe0.
  gindex_t g               = 32;
  uint8_t  expected_desc[] = {0x07, 0xe0};
  assert_descriptor(&g, 1, expected_desc, sizeof(expected_desc),
                    "gindex=32 descriptor");

  bytes32_t l32 = {0}, l33 = {0}, s17 = {0}, s9 = {0}, s5 = {0}, s3 = {0};
  memset(l32, 0x11, 32);
  memset(l33, 0x22, 32);
  memset(s17, 0x33, 32);
  memset(s9, 0x44, 32);
  memset(s5, 0x55, 32);
  memset(s3, 0x66, 32);

  // Reconstruct expected root
  bytes32_t h16 = {0}, h8 = {0}, h4 = {0}, h2 = {0}, root = {0};
  sha256_merkle(bytes(l32, 32), bytes(l33, 32), h16);
  sha256_merkle(bytes(h16, 32), bytes(s17, 32), h8);
  sha256_merkle(bytes(h8, 32), bytes(s9, 32), h4);
  sha256_merkle(bytes(h4, 32), bytes(s5, 32), h2);
  sha256_merkle(bytes(h2, 32), bytes(s3, 32), root);

  uint8_t leaves[6 * 32] = {0};
  memcpy(leaves + 0 * 32, l32, 32);
  memcpy(leaves + 1 * 32, l33, 32);
  memcpy(leaves + 2 * 32, s17, 32);
  memcpy(leaves + 3 * 32, s9, 32);
  memcpy(leaves + 4 * 32, s5, 32);
  memcpy(leaves + 5 * 32, s3, 32);

  bytes_t branch = NULL_BYTES;
  bool    ok     = c4_ssz_compact_to_branch(bytes(leaves, sizeof(leaves)),
                                            bytes(expected_desc, sizeof(expected_desc)),
                                            /* gindex */ 32, root, &branch);
  TEST_ASSERT_TRUE_MESSAGE(ok, "depth-5 compact proof must succeed");
  TEST_ASSERT_EQUAL_MESSAGE(5 * 32, branch.len,
                            "gindex=32 branch depth is 5");

  // leaf-to-root ordering: [l33, s17, s9, s5, s3]
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(l33, branch.data + 0 * 32, 32, "branch[0]");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(s17, branch.data + 1 * 32, 32, "branch[1]");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(s9, branch.data + 2 * 32, 32, "branch[2]");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(s5, branch.data + 3 * 32, 32, "branch[3]");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(s3, branch.data + 4 * 32, 32, "branch[4]");

  bytes32_t computed = {0};
  ssz_verify_single_merkle_proof(branch, l32, 32, computed);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(root, computed, 32,
                                        "verifier must reconstruct the root");
  safe_free(branch.data);
}

// :: c4_ssz_compact_multi_extract

void test_compact_multi_extract_two_leaves_plus_subroot(void) {
  // 4-leaf compact tree at gindices [8, 9, 5, 3]. Extract the two real leaves
  // 8 and 9, plus the internal subroot at gindex 4 (parent of 8, 9). The
  // subroot's leaf-to-root branch is 2 siblings: sub_5 (at gindex 5) and
  // sub_3 (at gindex 3).
  bytes32_t leaf_8 = {0}, leaf_9 = {0}, sub_5 = {0}, sub_3 = {0};
  memset(leaf_8, 0xa5, 32);
  memset(leaf_9, 0x5a, 32);
  memset(sub_5, 0xcc, 32);
  memset(sub_3, 0x33, 32);

  uint8_t   compact_leaves[128] = {0};
  uint8_t   descriptor[1]       = {0};
  bytes32_t root                = {0};
  build_compact_proof_target_8(leaf_8, leaf_9, sub_5, sub_3,
                               compact_leaves, descriptor, root);

  gindex_t  caller_gindices[2] = {8, 9};
  uint8_t   leaves_out[2 * 32] = {0};
  bytes32_t subroot_hash       = {0};
  bytes_t   subroot_branch     = NULL_BYTES;

  bool ok = c4_ssz_compact_multi_extract(
      bytes(compact_leaves, 128), bytes(descriptor, 1),
      caller_gindices, 2, root,
      bytes(leaves_out, sizeof(leaves_out)),
      /* subroot_gindex */ 4, subroot_hash, &subroot_branch);
  TEST_ASSERT_TRUE_MESSAGE(ok, "multi-extract with 2 leaves + subroot must succeed");

  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(leaf_8, leaves_out + 0, 32,
                                        "leaves_out[0] must equal the leaf at gindex 8");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(leaf_9, leaves_out + 32, 32,
                                        "leaves_out[1] must equal the leaf at gindex 9");

  bytes32_t expected_hash_4 = {0};
  sha256_merkle(bytes(leaf_8, 32), bytes(leaf_9, 32), expected_hash_4);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected_hash_4, subroot_hash, 32,
                                        "subroot hash at gindex 4 must be merkle(leaf_8, leaf_9)");

  TEST_ASSERT_EQUAL_MESSAGE(2 * 32, subroot_branch.len,
                            "subroot branch depth must equal bitlen(4)-1 == 2");
  // Leaf-to-root: [sibling of 4 = sub_5, sibling of 2 = sub_3]
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(sub_5, subroot_branch.data + 0, 32,
                                        "branch[0] is sub_5 (sibling of gindex 4)");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(sub_3, subroot_branch.data + 32, 32,
                                        "branch[1] is sub_3 (sibling of gindex 2)");

  // Verify the branch reconstructs the root using the shared merkle verifier.
  bytes32_t computed = {0};
  ssz_verify_single_merkle_proof(subroot_branch, subroot_hash, /* gindex */ 4, computed);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(root, computed, 32,
                                        "captured subroot branch must reconstruct root");

  safe_free(subroot_branch.data);
}

void test_compact_multi_extract_subroot_not_in_tree_rejected(void) {
  // Same 4-leaf compact tree. Subroot gindex 11 lives inside the opaque
  // subtree at gindex 5 -- reconstruction terminates at gindex 5 as a leaf
  // and never visits gindex 11. multi_extract must reject.
  bytes32_t leaf_8 = {0}, leaf_9 = {0}, sub_5 = {0}, sub_3 = {0};
  memset(leaf_8, 0x01, 32);
  memset(leaf_9, 0x02, 32);
  memset(sub_5, 0x03, 32);
  memset(sub_3, 0x04, 32);

  uint8_t   compact_leaves[128] = {0};
  uint8_t   descriptor[1]       = {0};
  bytes32_t root                = {0};
  build_compact_proof_target_8(leaf_8, leaf_9, sub_5, sub_3,
                               compact_leaves, descriptor, root);

  bytes32_t subroot_hash   = {0};
  bytes_t   subroot_branch = NULL_BYTES;
  bool      ok             = c4_ssz_compact_multi_extract(
      bytes(compact_leaves, 128), bytes(descriptor, 1),
      /* gindices */ NULL, /* count */ 0, root,
      /* leaves_out */ NULL_BYTES,
      /* subroot_gindex */ 11, subroot_hash, &subroot_branch);
  TEST_ASSERT_FALSE_MESSAGE(ok, "subroot inside an opaque subtree must be rejected");
  TEST_ASSERT_NULL_MESSAGE(subroot_branch.data,
                           "no branch allocation for missing subroot");
}

void test_compact_multi_extract_gindex_order_preserved(void) {
  // Passing caller gindices out of lex order must still copy leaves back
  // into caller order (permutation invariant).
  bytes32_t leaf_8 = {0}, leaf_9 = {0}, sub_5 = {0}, sub_3 = {0};
  memset(leaf_8, 0xaa, 32);
  memset(leaf_9, 0xbb, 32);
  memset(sub_5, 0xcc, 32);
  memset(sub_3, 0xdd, 32);

  uint8_t   compact_leaves[128] = {0};
  uint8_t   descriptor[1]       = {0};
  bytes32_t root                = {0};
  build_compact_proof_target_8(leaf_8, leaf_9, sub_5, sub_3,
                               compact_leaves, descriptor, root);

  // Reverse order: caller_gindices[0] = 9, caller_gindices[1] = 8.
  gindex_t caller_gindices[2] = {9, 8};
  uint8_t  leaves_out[2 * 32] = {0};

  bool ok = c4_ssz_compact_multi_extract(
      bytes(compact_leaves, 128), bytes(descriptor, 1),
      caller_gindices, 2, root,
      bytes(leaves_out, sizeof(leaves_out)),
      /* subroot_gindex */ 0, /* subroot_hash_out */ NULL, /* subroot_branch_out */ NULL);
  TEST_ASSERT_TRUE_MESSAGE(ok, "reverse-order gindices must still succeed");

  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(leaf_9, leaves_out + 0, 32,
                                        "leaves_out[0] must map to caller_gindices[0] = 9");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(leaf_8, leaves_out + 32, 32,
                                        "leaves_out[1] must map to caller_gindices[1] = 8");
}

void test_compact_multi_extract_subroot_is_caller_leaf(void) {
  // Subroot IS one of the caller gindices. Both output paths (leaves_out
  // and subroot_hash_out) must be populated from the same leaf value.
  bytes32_t leaf_8 = {0}, leaf_9 = {0}, sub_5 = {0}, sub_3 = {0};
  memset(leaf_8, 0xa1, 32);
  memset(leaf_9, 0xa2, 32);
  memset(sub_5, 0xa3, 32);
  memset(sub_3, 0xa4, 32);

  uint8_t   compact_leaves[128] = {0};
  uint8_t   descriptor[1]       = {0};
  bytes32_t root                = {0};
  build_compact_proof_target_8(leaf_8, leaf_9, sub_5, sub_3,
                               compact_leaves, descriptor, root);

  gindex_t  caller_gindices[2] = {8, 9};
  uint8_t   leaves_out[2 * 32] = {0};
  bytes32_t subroot_hash       = {0};
  bytes_t   subroot_branch     = NULL_BYTES;

  bool ok = c4_ssz_compact_multi_extract(
      bytes(compact_leaves, 128), bytes(descriptor, 1),
      caller_gindices, 2, root,
      bytes(leaves_out, sizeof(leaves_out)),
      /* subroot_gindex */ 8, subroot_hash, &subroot_branch);
  TEST_ASSERT_TRUE_MESSAGE(ok, "subroot at a caller leaf must succeed");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(leaf_8, leaves_out + 0, 32,
                                        "leaves_out[0] populated from leaf 8");
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(leaf_8, subroot_hash, 32,
                                        "subroot_hash also captures the leaf 8 value");
  TEST_ASSERT_EQUAL_MESSAGE(3 * 32, subroot_branch.len,
                            "gindex 8 branch depth is 3");
  safe_free(subroot_branch.data);
}

void test_compact_multi_extract_root_only_verification(void) {
  // count == 0 AND subroot_gindex == 0: pure root-verification mode.
  uint8_t   descriptor[] = {0x60};
  bytes32_t leafA        = {0};
  bytes32_t leafB        = {0};
  memset(leafA, 0x71, 32);
  memset(leafB, 0x8e, 32);
  uint8_t leaves[64] = {0};
  memcpy(leaves, leafA, 32);
  memcpy(leaves + 32, leafB, 32);
  bytes32_t root = {0};
  sha256_merkle(bytes(leafA, 32), bytes(leafB, 32), root);

  bytes_t branch = NULL_BYTES;
  TEST_ASSERT_TRUE_MESSAGE(
      c4_ssz_compact_multi_extract(
          bytes(leaves, 64), bytes(descriptor, 1),
          NULL, 0, root,
          NULL_BYTES,
          /* subroot_gindex */ 0, NULL, &branch),
      "root-only mode must succeed when descriptor + leaves reconstruct root");
  TEST_ASSERT_NULL_MESSAGE(branch.data,
                           "no branch allocated in root-only mode");

  bytes32_t wrong = {0};
  memset(wrong, 0x99, 32);
  TEST_ASSERT_FALSE_MESSAGE(
      c4_ssz_compact_multi_extract(
          bytes(leaves, 64), bytes(descriptor, 1),
          NULL, 0, wrong,
          NULL_BYTES,
          /* subroot_gindex */ 0, NULL, &branch),
      "root-only mode must reject a wrong expected_root");
  TEST_ASSERT_NULL(branch.data);
}

void test_compact_multi_extract_leaves_out_too_small(void) {
  bytes32_t leaf_8 = {0}, leaf_9 = {0}, sub_5 = {0}, sub_3 = {0};
  uint8_t   compact_leaves[128] = {0};
  uint8_t   descriptor[1]       = {0};
  bytes32_t root                = {0};
  build_compact_proof_target_8(leaf_8, leaf_9, sub_5, sub_3,
                               compact_leaves, descriptor, root);

  gindex_t caller_gindices[2] = {8, 9};
  uint8_t  small_out[32]      = {0}; // one leaf worth; too small for count=2
  TEST_ASSERT_FALSE_MESSAGE(
      c4_ssz_compact_multi_extract(
          bytes(compact_leaves, 128), bytes(descriptor, 1),
          caller_gindices, 2, root,
          bytes(small_out, sizeof(small_out)),
          /* subroot_gindex */ 0, NULL, NULL),
      "leaves_out too small must be rejected");
}

void test_compact_multi_extract_zero_gindex_rejected(void) {
  // A zero in the caller gindices array is invalid (root has gindex 1).
  bytes32_t leaf_8 = {0}, leaf_9 = {0}, sub_5 = {0}, sub_3 = {0};
  uint8_t   compact_leaves[128] = {0};
  uint8_t   descriptor[1]       = {0};
  bytes32_t root                = {0};
  build_compact_proof_target_8(leaf_8, leaf_9, sub_5, sub_3,
                               compact_leaves, descriptor, root);

  gindex_t caller_gindices[2] = {8, 0};
  uint8_t  leaves_out[2 * 32] = {0};
  TEST_ASSERT_FALSE_MESSAGE(
      c4_ssz_compact_multi_extract(
          bytes(compact_leaves, 128), bytes(descriptor, 1),
          caller_gindices, 2, root,
          bytes(leaves_out, sizeof(leaves_out)),
          /* subroot_gindex */ 0, NULL, NULL),
      "gindex == 0 in caller list must be rejected");
}

void test_compact_multi_extract_subroot_hash_out_required(void) {
  // subroot_gindex != 0 requires BOTH subroot_hash_out and subroot_branch_out.
  bytes32_t leaf_8 = {0}, leaf_9 = {0}, sub_5 = {0}, sub_3 = {0};
  uint8_t   compact_leaves[128] = {0};
  uint8_t   descriptor[1]       = {0};
  bytes32_t root                = {0};
  build_compact_proof_target_8(leaf_8, leaf_9, sub_5, sub_3,
                               compact_leaves, descriptor, root);

  bytes_t branch = NULL_BYTES;
  TEST_ASSERT_FALSE_MESSAGE(
      c4_ssz_compact_multi_extract(
          bytes(compact_leaves, 128), bytes(descriptor, 1),
          NULL, 0, root,
          NULL_BYTES,
          /* subroot_gindex */ 4, /* subroot_hash_out */ NULL, &branch),
      "subroot_gindex != 0 requires subroot_hash_out");
}

void test_compact_multi_extract_duplicate_gindices_rejected(void) {
  // Two callers gindices with the same value are ambiguous for leaves_out
  // (which output slot should receive the value?). Reject upfront.
  bytes32_t leafA = {0}, leafB = {0};
  memset(leafA, 0x71, 32);
  memset(leafB, 0x8e, 32);
  uint8_t leaves[64] = {0};
  memcpy(leaves, leafA, 32);
  memcpy(leaves + 32, leafB, 32);
  bytes32_t root = {0};
  sha256_merkle(bytes(leafA, 32), bytes(leafB, 32), root);
  uint8_t descriptor[] = {0x60};

  gindex_t caller[2]          = {2, 2};
  uint8_t  leaves_out[2 * 32] = {0};

  bool ok = c4_ssz_compact_multi_extract(
      bytes(leaves, 64), bytes(descriptor, 1),
      caller, 2, root,
      bytes(leaves_out, sizeof(leaves_out)),
      /* subroot_gindex */ 0, NULL, NULL);
  TEST_ASSERT_FALSE_MESSAGE(ok, "duplicate caller gindices must be rejected");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_descriptor_single_root);
  RUN_TEST(test_descriptor_single_left_child);
  RUN_TEST(test_descriptor_single_right_child);
  RUN_TEST(test_descriptor_deneb_current_sync_committee);
  RUN_TEST(test_descriptor_electra_current_sync_committee);
  RUN_TEST(test_descriptor_multi_gindex);
  RUN_TEST(test_descriptor_three_gindices_mixed_parents);
  RUN_TEST(test_descriptor_ancestor_of_leaf_is_redundant);
  RUN_TEST(test_descriptor_duplicate_input_gindices);
  RUN_TEST(test_descriptor_zero_gindex_rejected);
  RUN_TEST(test_descriptor_empty_input_rejected);
  RUN_TEST(test_descriptor_target_8_matches_expected_bytes);
  RUN_TEST(test_compact_to_branch_roundtrip_target_8);
  RUN_TEST(test_compact_to_branch_root_mismatch_rejected);
  RUN_TEST(test_compact_to_branch_target_not_in_compact_tree);
  RUN_TEST(test_compact_to_branch_gindex_rejects_zero_and_one);
  RUN_TEST(test_compact_to_branch_all_zero_descriptor_rejected);
  RUN_TEST(test_compact_to_branch_padding_bit_set_rejected);
  RUN_TEST(test_compact_to_branch_extra_trailing_bytes_rejected);
  RUN_TEST(test_compact_to_branch_null_inputs_rejected);
  RUN_TEST(test_compact_to_branch_unaligned_leaves_rejected);
  RUN_TEST(test_compact_to_branch_leaf_count_mismatch_rejected);
  RUN_TEST(test_compact_to_branch_two_leaf_success);
  RUN_TEST(test_compact_to_branch_two_leaf_right_target);
  RUN_TEST(test_compact_to_branch_right_side_opaque_subtree_target);
  RUN_TEST(test_compact_to_branch_single_leaf_tree_rejects_deeper_gindex);
  RUN_TEST(test_compact_to_branch_all_ones_byte_rejected);
  RUN_TEST(test_compact_to_branch_balanced_bits_never_terminate);
  RUN_TEST(test_compact_to_branch_descriptor_over_max_size_rejected);
  RUN_TEST(test_compact_to_branch_depth_5);
  RUN_TEST(test_compact_multi_extract_two_leaves_plus_subroot);
  RUN_TEST(test_compact_multi_extract_subroot_not_in_tree_rejected);
  RUN_TEST(test_compact_multi_extract_gindex_order_preserved);
  RUN_TEST(test_compact_multi_extract_subroot_is_caller_leaf);
  RUN_TEST(test_compact_multi_extract_root_only_verification);
  RUN_TEST(test_compact_multi_extract_leaves_out_too_small);
  RUN_TEST(test_compact_multi_extract_zero_gindex_rejected);
  RUN_TEST(test_compact_multi_extract_subroot_hash_out_required);
  RUN_TEST(test_compact_multi_extract_duplicate_gindices_rejected);
  return UNITY_END();
}
