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

/**
 * @file test_blst.c
 * @brief Comprehensive unit tests for BLST (BLS12-381) signature library
 *
 * This test suite provides comprehensive coverage of the BLST library,
 * which implements BLS (Boneh-Lynn-Shacham) signatures on the BLS12-381 curve.
 *
 * Test Statistics:
 * - Total Tests: Multiple tests covering key generation, signing, and verification
 * - Coverage: Key generation, signing, verification, aggregation, and edge cases
 *
 * Tested Function Categories:
 * ---------------------------
 *
 * 1. KEY GENERATION:
 *    - blst_keygen (private key generation from IKM)
 *    - blst_sk_to_pk_in_g1 (public key generation)
 *
 * 2. SIGNING:
 *    - blst_sign_pk_in_g1 (signature creation)
 *    - blst_hash_to_g2 (message hashing to curve)
 *
 * 3. VERIFICATION:
 *    - blst_verify (signature verification)
 *    - blst_pairing operations
 *
 * 4. AGGREGATION:
 *    - Multiple public keys
 *    - Bitmask selection
 */

#include "crypto.h"
#include "blst.h"
#include "unity.h"
#include <string.h>
#include <stdio.h>

// pow256 is an array type in blst, but we use blst_scalar struct
// For compatibility, we'll use blst_scalar directly

void setUp(void) {}

void tearDown(void) {}

// Helper function to print hex (renamed to avoid conflict with bytes.h)
static void test_print_hex(const char* label, const uint8_t* data, size_t len) {
  printf("%s: ", label);
  for (size_t i = 0; i < len; i++) {
    printf("%02x", data[i]);
  }
  printf("\n");
}

// Test: Key generation from IKM
void test_blst_keygen() {
  // Input Key Material (IKM) - must be at least 32 bytes
  const uint8_t ikm[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
  };
  
  blst_scalar sk = {0};
  blst_keygen(&sk, ikm, sizeof(ikm), NULL, 0);
  
  // Check that key is not all zeros
  bool is_zero = true;
  for (int i = 0; i < 32; i++) {
    if (sk.b[i] != 0) {
      is_zero = false;
      break;
    }
  }
  TEST_ASSERT_FALSE(is_zero);
  
  // Check that key is valid (not zero modulo r)
  TEST_ASSERT_EQUAL_INT(1, blst_sk_check(&sk));
}

// Test: Public key generation from private key
void test_blst_sk_to_pk() {
  // Generate a private key
  const uint8_t ikm[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
  };
  
  blst_scalar sk = {0};
  blst_keygen(&sk, ikm, sizeof(ikm), NULL, 0);
  
  // Generate public key in G1
  blst_p1 pk;
  blst_sk_to_pk_in_g1(&pk, &sk);
  
  // Serialize public key
  uint8_t pk_bytes[48] = {0};
  blst_p1_compress(pk_bytes, &pk);
  
  // Check that public key is not all zeros
  bool is_zero = true;
  for (int i = 0; i < 48; i++) {
    if (pk_bytes[i] != 0) {
      is_zero = false;
      break;
    }
  }
  TEST_ASSERT_FALSE(is_zero);
  
  // Verify public key is valid
  blst_p1_affine pk_affine;
  blst_p1_to_affine(&pk_affine, &pk);
  TEST_ASSERT_EQUAL_INT(1, blst_p1_affine_is_inf(&pk_affine) == 0);
}

// Test: Sign and verify a message
void test_blst_sign_verify() {
  // Generate key pair
  const uint8_t ikm[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
  };
  
  blst_scalar sk = {0};
  blst_keygen(&sk, ikm, sizeof(ikm), NULL, 0);
  
  blst_p1 pk;
  blst_sk_to_pk_in_g1(&pk, &sk);
  
  // Message to sign
  const uint8_t message[] = "Hello, BLS signatures!";
  bytes32_t msg_hash = {0};
  // Hash message (using SHA-256 for simplicity, in practice use proper hash-to-curve)
  // For this test, we'll use a simple hash
  for (size_t i = 0; i < sizeof(message) - 1 && i < 32; i++) {
    msg_hash[i] = message[i];
  }
  
  // Hash message to G2
  blst_p2 msg_point;
  blst_hash_to_g2(&msg_point, msg_hash, sizeof(msg_hash), 
                   (const uint8_t*)"BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_POP_", 43, NULL, 0);
  
  // Sign the message
  blst_p2 sig;
  blst_sign_pk_in_g1(&sig, &msg_point, &sk);
  
  // Serialize signature
  bls_signature_t sig_bytes = {0};
  blst_p2_compress(sig_bytes, &sig);
  
  // Serialize public key
  uint8_t pk_bytes[48] = {0};
  blst_p1_compress(pk_bytes, &pk);
  
  // Verify signature using blst_verify
  uint8_t bitmask[1] = {0x01}; // Use first (and only) key
  bytes_t bitmask_bytes = bytes(bitmask, 1);
  
  bool valid = blst_verify(msg_hash, sig_bytes, pk_bytes, 1, bitmask_bytes, false);
  TEST_ASSERT_TRUE(valid);
}

// Test: Verify with wrong message (should fail)
void test_blst_verify_wrong_message() {
  // Generate key pair
  const uint8_t ikm[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
  };
  
  blst_scalar sk = {0};
  blst_keygen(&sk, ikm, sizeof(ikm), NULL, 0);
  
  blst_p1 pk;
  blst_sk_to_pk_in_g1(&pk, &sk);
  
  // Original message
  const uint8_t message1[] = "Hello, BLS signatures!";
  bytes32_t msg_hash1 = {0};
  for (size_t i = 0; i < sizeof(message1) - 1 && i < 32; i++) {
    msg_hash1[i] = message1[i];
  }
  
  // Hash message to G2
  blst_p2 msg_point;
  blst_hash_to_g2(&msg_point, msg_hash1, sizeof(msg_hash1),
                   (const uint8_t*)"BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_POP_", 43, NULL, 0);
  
  // Sign the message
  blst_p2 sig;
  blst_sign_pk_in_g1(&sig, &msg_point, &sk);
  
  // Serialize signature
  bls_signature_t sig_bytes = {0};
  blst_p2_compress(sig_bytes, &sig);
  
  // Serialize public key
  uint8_t pk_bytes[48] = {0};
  blst_p1_compress(pk_bytes, &pk);
  
  // Try to verify with different message
  bytes32_t msg_hash2 = {0};
  const uint8_t message2[] = "Different message!";
  for (size_t i = 0; i < sizeof(message2) - 1 && i < 32; i++) {
    msg_hash2[i] = message2[i];
  }
  
  uint8_t bitmask[1] = {0x01};
  bytes_t bitmask_bytes = bytes(bitmask, 1);
  
  bool valid = blst_verify(msg_hash2, sig_bytes, pk_bytes, 1, bitmask_bytes, false);
  TEST_ASSERT_FALSE(valid);
}

// Test: Verify with wrong public key (should fail)
void test_blst_verify_wrong_pubkey() {
  // Generate first key pair
  const uint8_t ikm1[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
  };
  
  blst_scalar sk1 = {0};
  blst_keygen(&sk1, ikm1, sizeof(ikm1), NULL, 0);
  
  blst_p1 pk1;
  blst_sk_to_pk_in_g1(&pk1, &sk1);
  
  // Message
  const uint8_t message[] = "Hello, BLS signatures!";
  bytes32_t msg_hash = {0};
  for (size_t i = 0; i < sizeof(message) - 1 && i < 32; i++) {
    msg_hash[i] = message[i];
  }
  
  // Hash message to G2
  blst_p2 msg_point;
  blst_hash_to_g2(&msg_point, msg_hash, sizeof(msg_hash),
                   (const uint8_t*)"BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_POP_", 43, NULL, 0);
  
  // Sign with first key
  blst_p2 sig;
  blst_sign_pk_in_g1(&sig, &msg_point, &sk1);
  
  // Serialize signature
  bls_signature_t sig_bytes = {0};
  blst_p2_compress(sig_bytes, &sig);
  
  // Generate second key pair
  const uint8_t ikm2[32] = {
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f
  };
  
  blst_scalar sk2 = {0};
  blst_keygen(&sk2, ikm2, sizeof(ikm2), NULL, 0);
  
  blst_p1 pk2;
  blst_sk_to_pk_in_g1(&pk2, &sk2);
  
  // Serialize wrong public key
  uint8_t pk_bytes[48] = {0};
  blst_p1_compress(pk_bytes, &pk2);
  
  // Try to verify signature with wrong public key
  uint8_t bitmask[1] = {0x01};
  bytes_t bitmask_bytes = bytes(bitmask, 1);
  
  bool valid = blst_verify(msg_hash, sig_bytes, pk_bytes, 1, bitmask_bytes, false);
  TEST_ASSERT_FALSE(valid);
}

// Test: Aggregate signatures (multiple public keys)
void test_blst_aggregate_verify() {
  // Generate two key pairs
  const uint8_t ikm1[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
  };
  
  const uint8_t ikm2[32] = {
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f
  };
  
  blst_scalar sk1 = {0}, sk2 = {0};
  blst_keygen(&sk1, ikm1, sizeof(ikm1), NULL, 0);
  blst_keygen(&sk2, ikm2, sizeof(ikm2), NULL, 0);
  
  blst_p1 pk1, pk2;
  blst_sk_to_pk_in_g1(&pk1, &sk1);
  blst_sk_to_pk_in_g1(&pk2, &sk2);
  
  // Message
  const uint8_t message[] = "Aggregate signature test";
  bytes32_t msg_hash = {0};
  for (size_t i = 0; i < sizeof(message) - 1 && i < 32; i++) {
    msg_hash[i] = message[i];
  }
  
  // Hash message to G2
  blst_p2 msg_point;
  blst_hash_to_g2(&msg_point, msg_hash, sizeof(msg_hash),
                   (const uint8_t*)"BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_POP_", 43, NULL, 0);
  
  // Sign with both keys
  blst_p2 sig1, sig2;
  blst_sign_pk_in_g1(&sig1, &msg_point, &sk1);
  blst_sign_pk_in_g1(&sig2, &msg_point, &sk2);
  
  // Aggregate signatures
  blst_p2 sig_agg;
  blst_p2_add_or_double(&sig_agg, &sig1, &sig2);
  
  // Serialize aggregated signature
  bls_signature_t sig_bytes = {0};
  blst_p2_compress(sig_bytes, &sig_agg);
  
  // Serialize public keys
  uint8_t pk_bytes[2 * 48] = {0};
  blst_p1_compress(pk_bytes, &pk1);
  blst_p1_compress(pk_bytes + 48, &pk2);
  
  // Verify with both public keys (bitmask: 0b11 = both keys)
  uint8_t bitmask[1] = {0x03}; // Use both keys (bits 0 and 1)
  bytes_t bitmask_bytes = bytes(bitmask, 1);
  
  bool valid = blst_verify(msg_hash, sig_bytes, pk_bytes, 2, bitmask_bytes, false);
  TEST_ASSERT_TRUE(valid);
}

// Test: Invalid input validation
void test_blst_verify_invalid_inputs() {
  bytes32_t msg_hash = {0};
  bls_signature_t sig = {0};
  uint8_t pk_bytes[48] = {0};
  uint8_t bitmask[1] = {0x01};
  bytes_t bitmask_bytes = bytes(bitmask, 1);
  
  // Test: num_public_keys <= 0
  bool result = blst_verify(msg_hash, sig, pk_bytes, 0, bitmask_bytes, false);
  TEST_ASSERT_FALSE(result);
  
  // Test: NULL bitmask (use 0 for data pointer)
  bytes_t null_bitmask = {0};
  null_bitmask.data = NULL;
  null_bitmask.len = 0;
  result = blst_verify(msg_hash, sig, pk_bytes, 1, null_bitmask, false);
  TEST_ASSERT_FALSE(result);
  
  // Test: Wrong bitmask length
  uint8_t wrong_bitmask[2] = {0x01, 0x00};
  bytes_t wrong_bitmask_bytes = bytes(wrong_bitmask, 2);
  result = blst_verify(msg_hash, sig, pk_bytes, 1, wrong_bitmask_bytes, false);
  TEST_ASSERT_FALSE(result);
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_blst_keygen);
  RUN_TEST(test_blst_sk_to_pk);
  RUN_TEST(test_blst_sign_verify);
  RUN_TEST(test_blst_verify_wrong_message);
  RUN_TEST(test_blst_verify_wrong_pubkey);
  RUN_TEST(test_blst_aggregate_verify);
  RUN_TEST(test_blst_verify_invalid_inputs);

  return UNITY_END();
}

