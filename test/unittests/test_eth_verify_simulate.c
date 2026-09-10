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
#include "c4_assert.h"
#include "call_ctx.h"
#include "eth_account.h"
#include "eth_call_account.h"
#include "ssz.h"
#include "unity.h"
#include <string.h>

void setUp(void) {
  reset_local_filecache();
}

void tearDown(void) {
  reset_local_filecache();
}

// Test 1: Simple transaction simulation (no events)
void test_simulate_simple() {
  // Fixture recorded with debug_traceCall; opt into the legacy path.
  run_rpc_test("simulate_simple", C4_PROVER_FLAG_USE_DEBUG_TRACE, 0);
}

// Test 2: WETH deposit simulation with events
void test_simulate_weth_deposit() {
  // INCLUDE_CODE because contract code is in the fixture; USE_DEBUG_TRACE for the recorded prestate.
  run_rpc_test("simulate_weth", C4_PROVER_FLAG_INCLUDE_CODE | C4_PROVER_FLAG_USE_DEBUG_TRACE, 0);
}

void test_simulation_result_access_list_includes_code_hash(void) {
  call_account_t eoa      = {0};
  call_account_t contract = {0};
  call_account_t ignored  = {0};
  call_storage_t slot     = {0};

  memset(eoa.address, 0x11, 20);
  eoa.flags = ACCOUNT_ACCESSED | ACCOUNT_HAS_CODE_HASH;
  memcpy(eoa.code_hash, EMPTY_HASH, 32);
  eoa.next = &contract;

  memset(contract.address, 0x22, 20);
  contract.flags = ACCOUNT_ACCESSED | ACCOUNT_HAS_CODE_HASH;
  memset(contract.code_hash, 0xab, 32);
  memset(slot.key, 0xcd, 32);
  slot.accessed    = true;
  contract.storage = &slot;
  contract.next    = &ignored;

  memset(ignored.address, 0x33, 20);

  ssz_ob_t result = eth_build_simulation_result_ssz(NULL_BYTES, NULL, true, 21000, NULL, &eoa, NULL, NULL);
  char*    json   = ssz_dump_to_str(result, false, true);

  TEST_ASSERT_NOT_NULL(json);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json, "\"accessList\""), "accessList must be present in JSON");
  TEST_ASSERT_NOT_NULL(strstr(json, "0x1111111111111111111111111111111111111111"));
  TEST_ASSERT_NOT_NULL(strstr(json, "0x2222222222222222222222222222222222222222"));
  TEST_ASSERT_NULL_MESSAGE(strstr(json, "0x3333333333333333333333333333333333333333"),
                           "accounts without ACCOUNT_ACCESSED must be omitted");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json, "0xabababababababababababababababababababababababababababababababab"),
                               "contract codeHash missing");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json, "0xcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"),
                               "accessed storage key missing");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json, "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470"),
                               "EOA must carry EMPTY_HASH as codeHash");

  safe_free(json);
  safe_free(result.bytes.data);
}

void test_simulation_result_omits_access_list_when_nothing_accessed(void) {
  call_account_t loaded = {0};
  memset(loaded.address, 0x44, 20);

  ssz_ob_t result = eth_build_simulation_result_ssz(NULL_BYTES, NULL, true, 21000, NULL, &loaded, NULL, NULL);
  char*    json   = ssz_dump_to_str(result, false, true);

  TEST_ASSERT_NOT_NULL(json);
  TEST_ASSERT_NULL_MESSAGE(strstr(json, "\"accessList\""),
                           "accessList must be hidden when no account was accessed");
  TEST_ASSERT_NULL(strstr(json, "0x4444444444444444444444444444444444444444"));

  safe_free(json);
  safe_free(result.bytes.data);
}

void test_simulation_result_access_list_skips_unaccessed_keys_without_code_hash(void) {
  call_account_t acc      = {0};
  call_storage_t key_a    = {0};
  call_storage_t key_skip = {0};
  call_storage_t key_b    = {0};

  memset(acc.address, 0x55, 20);
  acc.flags = ACCOUNT_ACCESSED; /* no ACCOUNT_HAS_CODE_HASH */
  memset(acc.code_hash, 0xff, 32);

  memset(key_a.key, 0xaa, 32);
  key_a.accessed = true;
  key_a.next     = &key_skip;

  memset(key_skip.key, 0xee, 32);
  key_skip.next = &key_b;

  memset(key_b.key, 0xbb, 32);
  key_b.accessed = true;
  acc.storage    = &key_a;

  ssz_ob_t result = eth_build_simulation_result_ssz(NULL_BYTES, NULL, true, 21000, NULL, &acc, NULL, NULL);
  ssz_ob_t list   = ssz_get(&result, "accessList");

  TEST_ASSERT_FALSE(ssz_is_error(list));
  TEST_ASSERT_EQUAL_UINT32(1, ssz_len(list));

  ssz_ob_t entry = ssz_at(list, 0);
  TEST_ASSERT_EQUAL_MEMORY(acc.address, ssz_get(&entry, "address").bytes.data, 20);
  TEST_ASSERT_EQUAL_MEMORY_MESSAGE(EMPTY_HASH, ssz_get(&entry, "codeHash").bytes.data, 32,
                                   "missing ACCOUNT_HAS_CODE_HASH must fall back to EMPTY_HASH");

  ssz_ob_t keys = ssz_get(&entry, "storageKeys");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, ssz_len(keys), "unaccessed storage keys must be omitted");
  TEST_ASSERT_EQUAL_MEMORY(key_a.key, ssz_at(keys, 0).bytes.data, 32);
  TEST_ASSERT_EQUAL_MEMORY(key_b.key, ssz_at(keys, 1).bytes.data, 32);

  safe_free(result.bytes.data);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_simulate_simple);
  RUN_TEST(test_simulate_weth_deposit);
  RUN_TEST(test_simulation_result_access_list_includes_code_hash);
  RUN_TEST(test_simulation_result_omits_access_list_when_nothing_accessed);
  RUN_TEST(test_simulation_result_access_list_skips_unaccessed_keys_without_code_hash);
  return UNITY_END();
}
