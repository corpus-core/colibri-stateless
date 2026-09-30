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
  memset(key_a.src_value, 0x11, 32);
  key_a.accessed = true;
  key_a.next     = &key_skip;

  memset(key_skip.key, 0xee, 32);
  memset(key_skip.src_value, 0xee, 32);
  key_skip.next = &key_b;

  memset(key_b.key, 0xbb, 32);
  memset(key_b.src_value, 0x22, 32);
  key_b.accessed = true;
  acc.storage    = &key_a;

  uint8_t        preimage_bytes[] = {0x01, 0x02, 0x03, 0x04};
  keccak_entry_t preimage         = {0};
  memcpy(preimage.hash, key_a.key, 32);
  preimage.input = bytes(preimage_bytes, sizeof(preimage_bytes));

  ssz_ob_t result = eth_build_simulation_result_ssz(NULL_BYTES, NULL, true, 21000, NULL, &acc, &preimage, NULL);
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

  ssz_ob_t reads = ssz_get(&entry, "storage");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, ssz_len(reads), "storage reads must follow accessed keys");
  ssz_ob_t read_a = ssz_at(reads, 0);
  TEST_ASSERT_EQUAL_MEMORY(key_a.key, ssz_get(&read_a, "slot").bytes.data, 32);
  TEST_ASSERT_EQUAL_MEMORY(key_a.src_value, ssz_get(&read_a, "value").bytes.data, 32);
  ssz_ob_t source = ssz_get(&read_a, "slotSource");
  TEST_ASSERT_EQUAL_UINT32(sizeof(preimage_bytes), source.bytes.len);
  TEST_ASSERT_EQUAL_MEMORY(preimage_bytes, source.bytes.data, sizeof(preimage_bytes));

  ssz_ob_t read_b = ssz_at(reads, 1);
  TEST_ASSERT_EQUAL_MEMORY(key_b.key, ssz_get(&read_b, "slot").bytes.data, 32);
  TEST_ASSERT_EQUAL_MEMORY(key_b.src_value, ssz_get(&read_b, "value").bytes.data, 32);
  ssz_ob_t no_source = ssz_get(&read_b, "slotSource");
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_error(no_source) || no_source.bytes.len == 0,
                           "slot without a keccak preimage must not publish slotSource");

  safe_free(result.bytes.data);
}

// Regression for issue #381: an account materialised from the on-disk PAP
// cache must expose `src_balance == balance` and `src_nonce == nonce` before
// any simulation frame runs. Otherwise `verify_simulate.c` would ship
// `previousValue = 0` in the SSZ `stateChanges` even when the account already
// held a non-zero balance/nonce at the top of the transaction, and the
// explainer would print a "0 ETH -> N ETH" line whose delta contradicts the
// real value transfer.
void test_simulate_cache_deserialize_seeds_snapshot(void) {
  call_account_t seed = {0};
  memset(seed.address, 0x77, 20);
  seed.flags = ACCOUNT_HAS_BALANCE | ACCOUNT_HAS_CODE_HASH | ACCOUNT_HAS_STORAGE_ROOT | ACCOUNT_HAS_NONCE;
  seed.nonce = 0x123456;
  // 2.240151 ETH mirrors the WETH-deposit example from issue #381.
  seed.balance[24] = 0x1f; seed.balance[25] = 0x14; seed.balance[26] = 0x63;
  seed.balance[27] = 0xc6; seed.balance[28] = 0x1e; seed.balance[29] = 0xa3;
  seed.balance[30] = 0x60; seed.balance[31] = 0x00;
  memset(seed.storage_root, 0xaa, 32);
  memset(seed.code_hash, 0xbb, 32);
  seed.verified_at = 99;
  // `src_balance` / `src_nonce` are intentionally left zero: this mirrors an
  // in-flight account whose snapshot was not seeded before serialization.

  buffer_t buf = {0};
  eth_call_account_serialize(&buf, &seed);

  call_account_t out = {0};
  TEST_ASSERT_TRUE(eth_call_account_deserialize(buf.data, &out));

  TEST_ASSERT_EQUAL_MEMORY_MESSAGE(seed.balance, out.balance, 32, "cached balance must round-trip");
  TEST_ASSERT_EQUAL_MEMORY_MESSAGE(seed.balance, out.src_balance, 32,
                                   "src_balance must be seeded from balance on load (issue #381)");
  TEST_ASSERT_EQUAL_UINT64(seed.nonce, out.nonce);
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(seed.nonce, out.src_nonce,
                                   "src_nonce must be seeded from nonce on load (issue #381)");

  buffer_free(&buf);
  call_storage_free_list(out.storage);
}

void test_simulation_access_storage_uses_src_value_and_caps_preimage(void) {
  call_account_t acc        = {0};
  call_storage_t plain      = {0};
  call_storage_t capped     = {0};
  call_storage_t oversized  = {0};
  call_storage_t empty_slot = {0};

  memset(acc.address, 0x66, 20);
  acc.flags = ACCOUNT_ACCESSED | ACCOUNT_HAS_CODE_HASH;
  memset(acc.code_hash, 0xab, 32);

  memset(plain.key, 0x10, 32);
  memset(plain.src_value, 0x11, 32);
  memset(plain.post_value, 0x99, 32);
  plain.accessed = true;
  plain.next     = &capped;

  memset(capped.key, 0x20, 32);
  memset(capped.src_value, 0x21, 32);
  capped.accessed = true;
  capped.next     = &oversized;

  memset(oversized.key, 0x30, 32);
  memset(oversized.src_value, 0x31, 32);
  oversized.accessed = true;
  oversized.next     = &empty_slot;

  memset(empty_slot.key, 0x40, 32);
  memset(empty_slot.src_value, 0x41, 32);
  empty_slot.accessed = true;
  acc.storage          = &plain;

  uint8_t at_cap[1024];
  uint8_t over_cap[1025];
  uint8_t mismatch_bytes[4] = {0x01, 0x02, 0x03, 0x04};
  memset(at_cap, 0x5a, sizeof(at_cap));
  memset(over_cap, 0x5b, sizeof(over_cap));

  keccak_entry_t mismatch = {0};
  memset(mismatch.hash, 0xcc, 32);
  mismatch.input = bytes(mismatch_bytes, sizeof(mismatch_bytes));

  keccak_entry_t empty = {0};
  memcpy(empty.hash, empty_slot.key, 32);
  empty.input = bytes(NULL, 0);
  empty.next  = &mismatch;

  keccak_entry_t over = {0};
  memcpy(over.hash, oversized.key, 32);
  over.input = bytes(over_cap, sizeof(over_cap));
  over.next  = &empty;

  keccak_entry_t cap = {0};
  memcpy(cap.hash, capped.key, 32);
  cap.input = bytes(at_cap, sizeof(at_cap));
  cap.next  = &over;

  ssz_ob_t result = eth_build_simulation_result_ssz(NULL_BYTES, NULL, true, 21000, NULL, &acc, &cap, NULL);
  ssz_ob_t list   = ssz_get(&result, "accessList");
  ssz_ob_t entry  = ssz_at(list, 0);
  ssz_ob_t reads  = ssz_get(&entry, "storage");
  TEST_ASSERT_EQUAL_UINT32(4, ssz_len(reads));

  ssz_ob_t read_plain = ssz_at(reads, 0);
  TEST_ASSERT_EQUAL_MEMORY(plain.src_value, ssz_get(&read_plain, "value").bytes.data, 32);
  TEST_ASSERT_TRUE(memcmp(ssz_get(&read_plain, "value").bytes.data, plain.post_value, 32) != 0);
  ssz_ob_t plain_source = ssz_get(&read_plain, "slotSource");
  TEST_ASSERT_TRUE(ssz_is_error(plain_source) || plain_source.bytes.len == 0);

  ssz_ob_t read_cap = ssz_at(reads, 1);
  ssz_ob_t source   = ssz_get(&read_cap, "slotSource");
  TEST_ASSERT_EQUAL_UINT32(1024, source.bytes.len);
  TEST_ASSERT_EQUAL_MEMORY(at_cap, source.bytes.data, 1024);

  ssz_ob_t read_over   = ssz_at(reads, 2);
  ssz_ob_t over_source = ssz_get(&read_over, "slotSource");
  TEST_ASSERT_TRUE(ssz_is_error(over_source) || over_source.bytes.len == 0);

  ssz_ob_t read_empty   = ssz_at(reads, 3);
  ssz_ob_t empty_source = ssz_get(&read_empty, "slotSource");
  TEST_ASSERT_TRUE(ssz_is_error(empty_source) || empty_source.bytes.len == 0);

  safe_free(result.bytes.data);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_simulate_simple);
  RUN_TEST(test_simulate_weth_deposit);
  RUN_TEST(test_simulation_result_access_list_includes_code_hash);
  RUN_TEST(test_simulation_result_omits_access_list_when_nothing_accessed);
  RUN_TEST(test_simulation_result_access_list_skips_unaccessed_keys_without_code_hash);
  RUN_TEST(test_simulation_access_storage_uses_src_value_and_caps_preimage);
  RUN_TEST(test_simulate_cache_deserialize_seeds_snapshot);
  return UNITY_END();
}
