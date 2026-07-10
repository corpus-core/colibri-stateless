/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */

#include "bytes.h"
#include "c4_assert.h" // Contains read_testdata and unity includes
#include "prover.h"
#include "ssz.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#ifdef ETH_ZKPROOF
#include "zk_verifier.h"

static bytes_t copy_bytes(bytes_t src) {
  bytes_t dst;
  dst.len  = src.len;
  dst.data = malloc(src.len);
  if (dst.data) {
    memcpy(dst.data, src.data, src.len);
  }
  return dst;
}

// Helper to validate a specific period proof
// If expected_anchor is not NULL, validates that current_keys_root matches it.
// If expected_anchor is NULL, returns the current_keys_root found (caller must free).
static uint8_t* verify_period_and_get_anchor(int period, const uint8_t* expected_anchor) {
  char proof_path[64];
  char pub_path[64];
  snprintf(proof_path, sizeof(proof_path), "zk_data/%d/zk_proof_g16.bin", period);
  snprintf(pub_path, sizeof(pub_path), "zk_data/%d/zk_pub.bin", period);

  bytes_t proof = read_testdata(proof_path);
  bytes_t pub   = read_testdata(pub_path);

  if (proof.data == NULL) {
    printf("Skipping period %d: proof not found\n", period);
    if (pub.data) free(pub.data);
    return NULL;
  }
  if (pub.data == NULL) {
    printf("Skipping period %d: public values not found\n", period);
    free(proof.data);
    return NULL;
  }

  // Verify Proof
  bool valid = verify_zk_proof(proof, pub);
  TEST_ASSERT_TRUE_MESSAGE(valid, "ZK Proof verification failed");

  // Verify Structure
  // [0..31]  = Current Keys Root (Anchor)
  // [32..63] = Next Keys Root
  // [64..71] = Next Period (LE)
  // [72..103] = Attested Header Root (BeaconBlockHeader hash_tree_root)
  // [104..135] = Domain (SigningData.domain)
  TEST_ASSERT_GREATER_OR_EQUAL_INT(136, pub.len);

  // Check Period
  uint64_t next_period = 0;
  memcpy(&next_period, pub.data + 64, 8);

  // proof_N produces next_period=N.
  TEST_ASSERT_EQUAL_UINT64(period, next_period);

  // Check Anchor
  if (expected_anchor != NULL) {
    int cmp = memcmp(pub.data, expected_anchor, 32);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, cmp, "Anchor hash mismatch (Aggregation broken?)");
  }

  uint8_t* anchor_out = malloc(32);
  memcpy(anchor_out, pub.data, 32);

  free(proof.data);
  free(pub.data);

  return anchor_out;
}

void test_verify_chain(void) {
  // Define the chain to test (SP1 v6 fixtures).
  // 1784 is the frozen v6 base proof; 1785 is produced by the server-side recursion and
  // is added to the fixtures after the roundtrip. Missing periods are skipped gracefully.
  // The recursive aggregation keeps `current_keys_root` pinned to the original trust
  // anchor across the whole chain, so every proof must share the same anchor.
  int periods[] = {1784, 1785};
  int count     = sizeof(periods) / sizeof(int);

  uint8_t* trust_anchor = NULL;

  for (int i = 0; i < count; i++) {
    int p = periods[i];
    printf("Verifying Period %d...\n", p);

    uint8_t* current_anchor = verify_period_and_get_anchor(p, trust_anchor);

    if (current_anchor) {
      if (trust_anchor == NULL) {
        // First proof establishes the anchor
        trust_anchor = current_anchor;
        printf("Trust Anchor established: ");
        for (int k = 0; k < 4; k++) printf("%02x", trust_anchor[k]);
        printf("...\n");
      }
      else {
        // Subsequent proofs must match the anchor (Aggregation)
        // verify_period_and_get_anchor already checked this if expected_anchor was set
        free(current_anchor);
      }
    }
  }

  if (trust_anchor) free(trust_anchor);
}

// Keep the tampering test for robustness (using 1785 as target if available, else 1784)
void test_verify_tampered(void) {
  int  period = 1785;
  char proof_path[64];
  snprintf(proof_path, sizeof(proof_path), "zk_data/%d/zk_proof_g16.bin", period);

  // Fallback to 1784 if 1785 not yet built
  bytes_t check = read_testdata(proof_path);
  if (check.data == NULL) {
    period = 1784;
    snprintf(proof_path, sizeof(proof_path), "zk_data/%d/zk_proof_g16.bin", period);
  }
  else {
    free(check.data);
  }

  char pub_path[64];
  snprintf(pub_path, sizeof(pub_path), "zk_data/%d/zk_pub.bin", period);

  bytes_t proof    = read_testdata(proof_path);
  bytes_t pub_orig = read_testdata(pub_path);

  if (proof.data == NULL || pub_orig.data == NULL) {
    TEST_IGNORE_MESSAGE("Skipping tampering test: No proof files found");
    return;
  }

  printf("Running Tampering Tests on Period %d\n", period);

  // 0. Baseline Check (MUST PASS)
  TEST_ASSERT_TRUE_MESSAGE(verify_zk_proof(proof, pub_orig), "Baseline verification failed! Cannot run tampering tests.");

  // 1. Tamper Current Keys Root
  {
    bytes_t pub = copy_bytes(pub_orig);
    pub.data[0] ^= 0xFF;
    TEST_ASSERT_FALSE(verify_zk_proof(proof, pub));
    free(pub.data);
  }
  // 2. Tamper Next Keys Root
  {
    bytes_t pub = copy_bytes(pub_orig);
    pub.data[32] ^= 0xFF;
    TEST_ASSERT_FALSE(verify_zk_proof(proof, pub));
    free(pub.data);
  }
  // 3. Tamper Period
  {
    bytes_t pub = copy_bytes(pub_orig);
    pub.data[64] ^= 0xFF;
    TEST_ASSERT_FALSE(verify_zk_proof(proof, pub));
    free(pub.data);
  }
  // 4. Tamper Proof
  {
    bytes_t proof_mod = copy_bytes(proof);
    if (proof_mod.len > 64)
      proof_mod.data[64] ^= 0xFF;
    else
      proof_mod.data[0] ^= 0xFF;
    TEST_ASSERT_FALSE(verify_zk_proof(proof_mod, pub_orig));
    free(proof_mod.data);
  }

  free(proof.data);
  free(pub_orig.data);
}

// Locks the public-output layout and the known values of the v6 base proof (period 1784).
// current_keys_root must equal the trust anchor (period 1783 next_keys), next_keys_root the
// proven committee, and the proof must be the 356-byte SP1 v6 Groth16 format.
void test_verify_v6_anchor_values(void) {
  bytes_t proof = read_testdata("zk_data/1784/zk_proof_g16.bin");
  bytes_t pub   = read_testdata("zk_data/1784/zk_pub.bin");

  if (proof.data == NULL || pub.data == NULL) {
    if (proof.data) free(proof.data);
    if (pub.data) free(pub.data);
    TEST_IGNORE_MESSAGE("Skipping v6 anchor test: 1784 fixture not found");
    return;
  }

  // SP1 v6 Groth16 proof is 356 bytes (v5 was 260).
  TEST_ASSERT_EQUAL_INT_MESSAGE(356, proof.len, "Expected 356-byte SP1 v6 Groth16 proof");
  TEST_ASSERT_GREATER_OR_EQUAL_INT(136, pub.len);

  TEST_ASSERT_TRUE_MESSAGE(verify_zk_proof(proof, pub), "v6 base proof (1784) failed to verify");

  const uint8_t expected_current[32] = {0xc0, 0x23, 0x61, 0xcb, 0x34, 0xfe, 0xce, 0x1e, 0xae, 0x2c, 0x74, 0xbd, 0x67, 0x5d, 0x38, 0x76, 0xc5, 0x3b, 0x93, 0xa7, 0xe8, 0x00, 0x15, 0x74, 0xf5, 0x49, 0xd2, 0x8c, 0xa8, 0x9c, 0xfb, 0x9b};
  const uint8_t expected_next[32]    = {0x59, 0xd1, 0xe4, 0xec, 0x47, 0x79, 0x51, 0x24, 0xfc, 0x89, 0xe3, 0xb0, 0x9a, 0xf7, 0x24, 0x35, 0xc7, 0x21, 0x54, 0x5b, 0x80, 0x7c, 0xfe, 0xfa, 0x4d, 0xb5, 0x6e, 0xd2, 0x1b, 0xa6, 0x92, 0x94};

  TEST_ASSERT_EQUAL_MEMORY_MESSAGE(expected_current, pub.data, 32, "current_keys_root != trust anchor");
  TEST_ASSERT_EQUAL_MEMORY_MESSAGE(expected_next, pub.data + 32, 32, "next_keys_root mismatch");

  uint64_t next_period = 0;
  memcpy(&next_period, pub.data + 64, 8);
  TEST_ASSERT_EQUAL_UINT64(1784, next_period);

  free(proof.data);
  free(pub.data);
}

#endif

#ifndef ETH_ZKPROOF
void test_skipped(void) {
  TEST_IGNORE_MESSAGE("ETH_ZKPROOF is disabled");
}
#endif

int main(void) {
  UNITY_BEGIN();

#ifdef ETH_ZKPROOF
  RUN_TEST(test_verify_chain);
  RUN_TEST(test_verify_tampered);
  RUN_TEST(test_verify_v6_anchor_values);
#else
  RUN_TEST(test_skipped);
#endif

  return UNITY_END();
}
