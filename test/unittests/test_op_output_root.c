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
#include "chains/op/verifier/op_output_root.h"
#include "crypto.h"
#include "bytes.h"
#include "intx_c_api.h"
#include <string.h>

void setUp(void) {
}

void tearDown(void) {
}

/**
 * Test OutputRoot reconstruction with zero values
 */
void test_op_reconstruct_output_root_zeros(void) {
  bytes32_t version                     = {0};
  bytes32_t state_root                  = {0};
  bytes32_t message_passer_storage_root = {0};
  bytes32_t latest_block_hash           = {0};

  bytes32_t output_root;
  op_reconstruct_output_root(version, state_root, message_passer_storage_root, latest_block_hash, output_root);

  // Calculate expected: keccak256(128 zero bytes)
  uint8_t   zero_concat[128] = {0};
  bytes32_t expected;
  keccak(bytes(zero_concat, 128), expected);

  TEST_ASSERT_EQUAL_MEMORY(expected, output_root, 32);
}

/**
 * Test OutputRoot reconstruction with non-zero values
 */
void test_op_reconstruct_output_root_nonzero(void) {
  bytes32_t version = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };
  bytes32_t state_root = {
      0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
      0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
      0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
      0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11
  };
  bytes32_t message_passer_storage_root = {
      0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
      0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
      0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
      0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22
  };
  bytes32_t latest_block_hash = {
      0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
      0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
      0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
      0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33
  };

  bytes32_t output_root;
  op_reconstruct_output_root(version, state_root, message_passer_storage_root, latest_block_hash, output_root);

  // Calculate expected manually
  uint8_t concat[128];
  memcpy(concat + 0, version, 32);
  memcpy(concat + 32, state_root, 32);
  memcpy(concat + 64, message_passer_storage_root, 32);
  memcpy(concat + 96, latest_block_hash, 32);

  bytes32_t expected;
  keccak(bytes(concat, 128), expected);

  TEST_ASSERT_EQUAL_MEMORY(expected, output_root, 32);
}

/**
 * Test that different inputs produce different OutputRoots
 */
void test_op_output_root_uniqueness(void) {
  bytes32_t version                     = {0x01};
  bytes32_t state_root1                 = {0x02};
  bytes32_t state_root2                 = {0x03};
  bytes32_t message_passer_storage_root = {0x04};
  bytes32_t latest_block_hash           = {0x05};

  bytes32_t output_root1, output_root2;

  op_reconstruct_output_root(version, state_root1, message_passer_storage_root, latest_block_hash, output_root1);
  op_reconstruct_output_root(version, state_root2, message_passer_storage_root, latest_block_hash, output_root2);

  // Should produce different results
  TEST_ASSERT_FALSE(memcmp(output_root1, output_root2, 32) == 0);
}


/**
 * Test storage slot calculation
 */
void test_op_calculate_storage_slot(void) {
  uint256_t output_index;
  intx_init_value(&output_index, 42);

  uint256_t mapping_slot;
  intx_init_value(&mapping_slot, 0);

  bytes32_t storage_slot;
  op_calculate_output_storage_slot(&output_index, &mapping_slot, storage_slot);

  // Calculate expected
  uint8_t concat[64] = {0};
  memcpy(concat, output_index.bytes, 32);
  memcpy(concat + 32, mapping_slot.bytes, 32);

  bytes32_t expected;
  keccak(bytes(concat, 64), expected);

  TEST_ASSERT_EQUAL_MEMORY(expected, storage_slot, 32);
}

/**
 * Test OutputRoot extraction from valid storage value
 */
void test_op_extract_output_root_valid(void) {
  // Create a mock storage value with 32-byte OutputRoot
  bytes32_t expected_output_root = {
      0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22,
      0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00,
      0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
      0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00
  };

  bytes_t   storage_value = bytes((uint8_t*)expected_output_root, 32);
  bytes32_t extracted_output_root;

  bool result = op_extract_output_root_from_storage(storage_value, extracted_output_root);

  TEST_ASSERT_TRUE(result);
  TEST_ASSERT_EQUAL_MEMORY(expected_output_root, extracted_output_root, 32);
}

/**
 * Test OutputRoot extraction with empty storage
 */
void test_op_extract_output_root_empty(void) {
  bytes_t   empty_storage = bytes(NULL, 0);
  bytes32_t output_root;

  bool result = op_extract_output_root_from_storage(empty_storage, output_root);

  TEST_ASSERT_FALSE(result);
}

/**
 * Test OutputRoot extraction with insufficient data
 */
void test_op_extract_output_root_too_short(void) {
  uint8_t short_data[16] = {0};
  bytes_t storage_value  = bytes(short_data, 16);
  bytes32_t output_root;

  bool result = op_extract_output_root_from_storage(storage_value, output_root);

  TEST_ASSERT_FALSE(result);
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_op_reconstruct_output_root_zeros);
  RUN_TEST(test_op_reconstruct_output_root_nonzero);
  RUN_TEST(test_op_output_root_uniqueness);
  RUN_TEST(test_op_calculate_storage_slot);
  RUN_TEST(test_op_extract_output_root_valid);
  RUN_TEST(test_op_extract_output_root_empty);
  RUN_TEST(test_op_extract_output_root_too_short);

  return UNITY_END();
}
