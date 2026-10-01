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
#include "chains.h"
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

  ssz_ob_t result = eth_build_simulation_result_ssz(NULL_BYTES, NULL, true, 21000, NULL, &eoa, NULL, NULL, 0, NULL);
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
  TEST_ASSERT_NULL_MESSAGE(strstr(json, "\"storage\":"),
                           "storage values stay hidden unless state_values is set");

  safe_free(json);
  safe_free(result.bytes.data);
}

void test_simulation_result_omits_access_list_when_nothing_accessed(void) {
  call_account_t loaded = {0};
  memset(loaded.address, 0x44, 20);

  ssz_ob_t result = eth_build_simulation_result_ssz(NULL_BYTES, NULL, true, 21000, NULL, &loaded, NULL, NULL, 0, NULL);
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

  ssz_ob_t result = eth_build_simulation_result_ssz(NULL_BYTES, NULL, true, 21000, NULL, &acc, &preimage, NULL, EVM_SIM_STATE_VALUES, NULL);
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
  seed.balance[24] = 0x1f;
  seed.balance[25] = 0x14;
  seed.balance[26] = 0x63;
  seed.balance[27] = 0xc6;
  seed.balance[28] = 0x1e;
  seed.balance[29] = 0xa3;
  seed.balance[30] = 0x60;
  seed.balance[31] = 0x00;
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
  acc.storage         = &plain;

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

  ssz_ob_t result = eth_build_simulation_result_ssz(NULL_BYTES, NULL, true, 21000, NULL, &acc, &cap, NULL, EVM_SIM_STATE_VALUES, NULL);
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

static void assert_simulate_schema(const char* json, bool ok) {
  const char* err = json_validate(json_parse(json), C4_SIMULATE_TX_PARAMS, "");
  if (ok)
    TEST_ASSERT_NULL_MESSAGE(err, json);
  else
    TEST_ASSERT_NOT_NULL_MESSAGE(err, json);
  safe_free((char*) err);
}

void test_simulate_config_schema_and_flags(void) {
  const char* tx =
      "[{\"to\":\"0x1111111111111111111111111111111111111111\",\"data\":\"0x00\"},\"latest\"";

  char* two = bprintf(NULL, "%s]", tx);
  char* nul = bprintf(NULL, "%s,null]", tx);
  char* cfg = bprintf(NULL, "%s,null,{\"positions\":true,\"state_values\":true}]", tx);
  char* off = bprintf(NULL, "%s,null,{\"positions\":false,\"state_values\":false}]", tx);
  char* bad = bprintf(NULL, "%s,null,{\"positions\":\"yes\"}]", tx);

  assert_simulate_schema(two, true);
  assert_simulate_schema(nul, true);
  assert_simulate_schema(cfg, true);
  assert_simulate_schema(off, true);
  assert_simulate_schema(bad, false);

  TEST_ASSERT_EQUAL_UINT32(0, c4_eth_sim_flags_from_args(json_parse(two)));
  TEST_ASSERT_EQUAL_UINT32(0, c4_eth_sim_flags_from_args(json_parse(nul)));
  TEST_ASSERT_EQUAL_UINT32(0, c4_eth_sim_flags_from_args(json_parse(off)));
  TEST_ASSERT_EQUAL_UINT32(EVM_SIM_POSITIONS | EVM_SIM_STATE_VALUES, c4_eth_sim_flags_from_args(json_parse(cfg)));

  char* only_values = bprintf(NULL, "%s,null,{\"state_values\":true}]", tx);
  char* only_pos    = bprintf(NULL, "%s,null,{\"positions\":true}]", tx);
  TEST_ASSERT_EQUAL_UINT32(EVM_SIM_STATE_VALUES, c4_eth_sim_flags_from_args(json_parse(only_values)));
  TEST_ASSERT_EQUAL_UINT32(EVM_SIM_POSITIONS, c4_eth_sim_flags_from_args(json_parse(only_pos)));
  assert_simulate_schema(only_values, true);
  assert_simulate_schema(only_pos, true);

  safe_free(two);
  safe_free(nul);
  safe_free(cfg);
  safe_free(off);
  safe_free(bad);
  safe_free(only_values);
  safe_free(only_pos);
}

void test_simulation_positions_are_emitted_sorted(void) {
  uint8_t* bits = safe_calloc(EVM_JUMPDEST_PC_LIMIT / 8, 1);
  bits[2 >> 3] |= (uint8_t) (1u << (2 & 7));
  bits[100 >> 3] |= (uint8_t) (1u << (100 & 7));

  jumpdest_set_t set = {0};
  memset(set.address, 0x11, 20);
  set.bits  = bits;
  set.count = 2;

  ssz_ob_t result    = eth_build_simulation_result_ssz(NULL_BYTES, NULL, true, 0, NULL, NULL, NULL, NULL, EVM_SIM_POSITIONS, &set);
  ssz_ob_t positions = ssz_get(&result, "positions");
  ssz_ob_t entry     = ssz_at(positions, 0);
  ssz_ob_t pcs       = ssz_get(&entry, "pcs");
  TEST_ASSERT_EQUAL_UINT32(2, ssz_len(pcs));
  TEST_ASSERT_EQUAL_UINT32(2, ssz_uint32(ssz_at(pcs, 0)));
  TEST_ASSERT_EQUAL_UINT32(100, ssz_uint32(ssz_at(pcs, 1)));

  safe_free(bits);
  safe_free(result.bytes.data);
}

#ifdef EVMONE
static call_account_t* make_runtime(const address_t addr, const uint8_t* code, size_t code_len) {
  call_account_t* acc = safe_calloc(1, sizeof(call_account_t));
  memcpy(acc->address, addr, 20);
  acc->code  = bytes_dup(bytes((uint8_t*) code, (uint32_t) code_len));
  acc->flags = ACCOUNT_HAS_CODE | ACCOUNT_FREE_CODE;
  return acc;
}

void test_simulation_positions_records_each_jumpdest_once(void) {
  // Counter starts at 2 and jumps back to the JUMPDEST at pc 2 while it is
  // non-zero, so that destination runs twice. The 0x5b after STOP is never
  // executed, and the 0x5b inside PUSH1 is immediate data.
  const uint8_t code[] = {
      0x60, 0x02,
      0x5b,
      0x60, 0x01,
      0x90,
      0x03,
      0x80,
      0x60, 0x02,
      0x57,
      0x50,
      0x60, 0x5b,
      0x50,
      0x00,
      0x5b};

  address_t contract = {0};
  memset(contract, 0x11, 20);

  verify_ctx_t ctx = {0};
  ctx.chain_id     = C4_CHAIN_MAINNET;
  ctx.args         = json_parse(
      "[{\"from\":\"0x2222222222222222222222222222222222222222\","
              "\"to\":\"0x1111111111111111111111111111111111111111\","
              "\"gas\":\"0xf4240\"},\"latest\"]");

  evm_call_ctx_t evm = {0};
  evm.sim_flags      = EVM_SIM_POSITIONS;
  evm.accounts       = make_runtime(contract, code, sizeof(code));

  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eth_run_call_evmone_with_events(&ctx, &evm, false));
  TEST_ASSERT_FALSE(evm.reverted);
  TEST_ASSERT_NULL(ctx.state.error);

  ssz_ob_t result    = eth_build_simulation_result_ssz(evm.call_result, NULL, true, evm.gas_used, NULL, evm.accounts, NULL, NULL, evm.sim_flags, evm.positions);
  ssz_ob_t positions = ssz_get(&result, "positions");
  TEST_ASSERT_EQUAL_UINT32(1, ssz_len(positions));

  ssz_ob_t entry = ssz_at(positions, 0);
  TEST_ASSERT_EQUAL_MEMORY(contract, ssz_get(&entry, "address").bytes.data, 20);
  ssz_ob_t pcs = ssz_get(&entry, "pcs");
  TEST_ASSERT_EQUAL_UINT32(1, ssz_len(pcs));
  TEST_ASSERT_EQUAL_UINT32(2, ssz_uint32(ssz_at(pcs, 0)));

  char* json = ssz_dump_to_str(result, false, true);
  TEST_ASSERT_NOT_NULL(strstr(json, "\"positions\""));
  TEST_ASSERT_NULL_MESSAGE(strstr(json, "\"storage\":"), "state_values stays off unless requested");
  safe_free(json);

  ssz_ob_t with_values = eth_build_simulation_result_ssz(evm.call_result, NULL, true, evm.gas_used, NULL, evm.accounts, NULL, NULL, EVM_SIM_STATE_VALUES | EVM_SIM_POSITIONS, evm.positions);
  char*    values_json = ssz_dump_to_str(with_values, false, true);
  TEST_ASSERT_NOT_NULL(strstr(values_json, "\"storage\":"));
  safe_free(values_json);
  safe_free(with_values.bytes.data);

  ssz_ob_t hidden      = eth_build_simulation_result_ssz(evm.call_result, NULL, true, evm.gas_used, NULL, evm.accounts, NULL, NULL, 0, evm.positions);
  char*    hidden_json = ssz_dump_to_str(hidden, false, true);
  TEST_ASSERT_NULL(strstr(hidden_json, "\"positions\""));
  safe_free(hidden_json);
  safe_free(hidden.bytes.data);

  safe_free(result.bytes.data);
  evm_call_ctx_free(&evm);
}

void test_simulation_positions_follow_delegatecall_code(void) {
  // Implementation jumps to pc 6 first, then back to pc 3. Output must be
  // sorted [3, 6] and attributed to the implementation, not the proxy.
  const uint8_t impl_code[] = {
      0x60, 0x06,
      0x56,
      0x5b,
      0x00,
      0x00,
      0x5b,
      0x60, 0x03,
      0x56};
  address_t impl = {0};
  memset(impl, 0x22, 20);

  // retSize, retOffset, argsSize, argsOffset, address, gas, DELEGATECALL, POP, JUMPDEST, STOP
  uint8_t proxy_code[36] = {
      0x60, 0x00,
      0x60, 0x00,
      0x60, 0x00,
      0x60, 0x00,
      0x73};
  memcpy(proxy_code + 9, impl, 20);
  proxy_code[29] = 0x61;
  proxy_code[30] = 0xff;
  proxy_code[31] = 0xff;
  proxy_code[32] = 0xf4;
  proxy_code[33] = 0x50;
  proxy_code[34] = 0x5b;
  proxy_code[35] = 0x00;

  address_t proxy = {0};
  memset(proxy, 0x11, 20);

  verify_ctx_t ctx = {0};
  ctx.chain_id     = C4_CHAIN_MAINNET;
  ctx.args         = json_parse(
      "[{\"from\":\"0x3333333333333333333333333333333333333333\","
              "\"to\":\"0x1111111111111111111111111111111111111111\","
              "\"gas\":\"0xf4240\"},\"latest\"]");

  evm_call_ctx_t evm = {0};
  evm.sim_flags      = EVM_SIM_POSITIONS;
  evm.accounts       = make_runtime(proxy, proxy_code, sizeof(proxy_code));
  evm.accounts->next = make_runtime(impl, impl_code, sizeof(impl_code));

  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eth_run_call_evmone_with_events(&ctx, &evm, false));
  TEST_ASSERT_FALSE(evm.reverted);
  TEST_ASSERT_NULL(ctx.state.error);

  ssz_ob_t result    = eth_build_simulation_result_ssz(evm.call_result, NULL, true, evm.gas_used, NULL, evm.accounts, NULL, NULL, evm.sim_flags, evm.positions);
  ssz_ob_t positions = ssz_get(&result, "positions");
  TEST_ASSERT_EQUAL_UINT32(2, ssz_len(positions));

  ssz_ob_t impl_entry = ssz_at(positions, 0);
  TEST_ASSERT_EQUAL_MEMORY(impl, ssz_get(&impl_entry, "address").bytes.data, 20);
  ssz_ob_t impl_pcs = ssz_get(&impl_entry, "pcs");
  TEST_ASSERT_EQUAL_UINT32(2, ssz_len(impl_pcs));
  TEST_ASSERT_EQUAL_UINT32(3, ssz_uint32(ssz_at(impl_pcs, 0)));
  TEST_ASSERT_EQUAL_UINT32(6, ssz_uint32(ssz_at(impl_pcs, 1)));

  ssz_ob_t proxy_entry = ssz_at(positions, 1);
  TEST_ASSERT_EQUAL_MEMORY(proxy, ssz_get(&proxy_entry, "address").bytes.data, 20);
  ssz_ob_t proxy_pcs = ssz_get(&proxy_entry, "pcs");
  TEST_ASSERT_EQUAL_UINT32(1, ssz_len(proxy_pcs));
  TEST_ASSERT_EQUAL_UINT32(34, ssz_uint32(ssz_at(proxy_pcs, 0)));

  safe_free(result.bytes.data);
  evm_call_ctx_free(&evm);
}
#endif

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_simulate_simple);
  RUN_TEST(test_simulate_weth_deposit);
  RUN_TEST(test_simulation_result_access_list_includes_code_hash);
  RUN_TEST(test_simulation_result_omits_access_list_when_nothing_accessed);
  RUN_TEST(test_simulation_result_access_list_skips_unaccessed_keys_without_code_hash);
  RUN_TEST(test_simulation_access_storage_uses_src_value_and_caps_preimage);
  RUN_TEST(test_simulate_cache_deserialize_seeds_snapshot);
  RUN_TEST(test_simulate_config_schema_and_flags);
  RUN_TEST(test_simulation_positions_are_emitted_sorted);
#ifdef EVMONE
  RUN_TEST(test_simulation_positions_records_each_jumpdest_once);
  RUN_TEST(test_simulation_positions_follow_delegatecall_code);
#endif
  return UNITY_END();
}
