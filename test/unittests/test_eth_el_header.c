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

#include "bytes.h"
#include "el_header.h"
#include "unity.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

// keccak256(rlp.encode([])) = keccak256(0xc0)
static const uint8_t empty_rlp_list_hash[32] = {
    0x1d, 0xcc, 0x4d, 0xe8, 0xde, 0xc7, 0x5d, 0x7a,
    0xab, 0x85, 0xb5, 0x67, 0xb6, 0xcc, 0xd4, 0x1a,
    0xd3, 0x12, 0x45, 0x1b, 0x94, 0x8a, 0x74, 0x13,
    0xf0, 0xa1, 0x42, 0xfd, 0x40, 0xd4, 0x93, 0x47};

void test_empty_bal_hashes_as_rlp_empty_list(void) {
  bytes32_t hash = {0};
  eth_get_block_access_list_hash(hash, NULL_BYTES);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(empty_rlp_list_hash, hash, 32);
}

void test_explicit_empty_rlp_list_matches_empty_bal(void) {
  uint8_t   empty_rlp  = 0xc0;
  bytes32_t from_c0    = {0};
  bytes32_t from_empty = {0};
  eth_get_block_access_list_hash(from_c0, bytes(&empty_rlp, 1));
  eth_get_block_access_list_hash(from_empty, NULL_BYTES);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(empty_rlp_list_hash, from_c0, 32);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(from_c0, from_empty, 32);
}

void test_non_empty_bal_differs_from_empty_hash(void) {
  uint8_t   payload[] = {0xc1, 0x80}; // rlp list with one empty item, not []
  bytes32_t hash      = {0};
  eth_get_block_access_list_hash(hash, bytes(payload, sizeof(payload)));
  TEST_ASSERT_TRUE(memcmp(hash, empty_rlp_list_hash, 32) != 0);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_bal_hashes_as_rlp_empty_list);
  RUN_TEST(test_explicit_empty_rlp_list_matches_empty_bal);
  RUN_TEST(test_non_empty_bal_differs_from_empty_hash);
  return UNITY_END();
}
