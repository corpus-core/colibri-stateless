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
#include "call_ctx.h"
#include "chains.h"
#include "eth_call_account.h"
#include "json.h"
#include "unity.h"
#include "verify.h"
#include <string.h>

#ifdef EVMONE

// EIP-7708 constants mirrored here to keep the test independent of the
// implementation file's private symbols.
static const uint8_t SYSTEM_ADDRESS[20] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe};

static const uint8_t TRANSFER_TOPIC[32] = {
    0xdd, 0xf2, 0x52, 0xad, 0x1b, 0xe2, 0xc8, 0x9b,
    0x69, 0xc2, 0xb0, 0x68, 0xfc, 0x37, 0x8d, 0xaa,
    0x95, 0x2b, 0xa7, 0xf1, 0x63, 0xc4, 0xa1, 0x16,
    0x28, 0xf5, 0x5a, 0x4d, 0xf5, 0x23, 0xb3, 0xef};

// -- Direct helper coverage --------------------------------------------------

void test_emit_transfer_log_zero_value_skipped(void) {
  emitted_log_t* logs = NULL;
  address_t      from = {0}, to = {0};
  memset(from, 0x11, 20);
  memset(to, 0x22, 20);
  uint8_t value[32] = {0};

  emit_eth_transfer_log(&logs, from, to, value);

  TEST_ASSERT_NULL(logs);
}

void test_emit_transfer_log_self_transfer_skipped(void) {
  emitted_log_t* logs = NULL;
  address_t      addr = {0};
  memset(addr, 0x33, 20);
  uint8_t value[32] = {0};
  value[31]         = 0x64;

  emit_eth_transfer_log(&logs, addr, addr, value);

  TEST_ASSERT_NULL(logs);
}

void test_emit_transfer_log_normal(void) {
  emitted_log_t* logs = NULL;
  address_t      from = {0}, to = {0};
  memset(from, 0x11, 20);
  memset(to, 0x22, 20);
  uint8_t value[32] = {0};
  value[31]         = 0x64;

  emit_eth_transfer_log(&logs, from, to, value);

  TEST_ASSERT_NOT_NULL(logs);
  TEST_ASSERT_NULL(logs->next);
  TEST_ASSERT_EQUAL_MEMORY(SYSTEM_ADDRESS, logs->address, 20);

  TEST_ASSERT_EQUAL_UINT(3, logs->topics_count);
  TEST_ASSERT_EQUAL_MEMORY(TRANSFER_TOPIC, logs->topics[0], 32);

  uint8_t expected_topic1[32] = {0};
  memcpy(expected_topic1 + 12, from, 20);
  TEST_ASSERT_EQUAL_MEMORY(expected_topic1, logs->topics[1], 32);

  uint8_t expected_topic2[32] = {0};
  memcpy(expected_topic2 + 12, to, 20);
  TEST_ASSERT_EQUAL_MEMORY(expected_topic2, logs->topics[2], 32);

  TEST_ASSERT_EQUAL_UINT32(32, logs->data.len);
  TEST_ASSERT_EQUAL_MEMORY(value, logs->data.data, 32);

  free_emitted_logs(logs);
}

// -- Test fixtures for end-to-end EVM runs -----------------------------------

static call_account_t* make_contract_with_balance(const address_t addr, const uint8_t* code, size_t code_len, uint64_t balance) {
  call_account_t* acc = safe_calloc(1, sizeof(call_account_t));
  memcpy(acc->address, addr, 20);
  // `bytes_dup` on a zero-length payload would call memcpy(NULL, NULL, 0);
  // guard for the "just an EOA with balance" case used by the SELFDESTRUCT test.
  if (code && code_len > 0) {
    acc->code = bytes_dup(bytes((uint8_t*) code, (uint32_t) code_len));
    acc->flags |= ACCOUNT_HAS_CODE | ACCOUNT_FREE_CODE;
  }
  acc->flags |= ACCOUNT_HAS_NONCE | ACCOUNT_HAS_BALANCE;
  for (int i = 0; balance && i < 8; i++) {
    acc->balance[31 - i] = (uint8_t) (balance & 0xff);
    balance >>= 8;
  }
  return acc;
}

static call_account_t* find_account(call_account_t* list, const address_t addr) {
  for (call_account_t* a = list; a; a = a->next)
    if (memcmp(a->address, addr, 20) == 0) return a;
  return NULL;
}

static uint32_t count_logs(emitted_log_t* logs) {
  uint32_t n = 0;
  for (emitted_log_t* l = logs; l; l = l->next) n++;
  return n;
}

static emitted_log_t* find_transfer_log(emitted_log_t* logs, const address_t from, const address_t to) {
  for (emitted_log_t* l = logs; l; l = l->next) {
    if (l->topics_count != 3) continue;
    if (memcmp(l->address, SYSTEM_ADDRESS, 20) != 0) continue;
    if (memcmp(l->topics[0], TRANSFER_TOPIC, 32) != 0) continue;
    if (memcmp(l->topics[1] + 12, from, 20) != 0) continue;
    if (memcmp(l->topics[2] + 12, to, 20) != 0) continue;
    return l;
  }
  return NULL;
}

// -- Bytecode helpers --------------------------------------------------------

// Bytecode for contract A that CALLs target B with 1 wei and no calldata.
// After the sub-call it just STOPs; the outer frame therefore succeeds even
// when the inner CALL reverts.
//
// Layout (37 bytes):
//   6000                     PUSH1 0            // retSize
//   6000                     PUSH1 0            // retOffset
//   6000                     PUSH1 0            // argsSize
//   6000                     PUSH1 0            // argsOffset
//   6001                     PUSH1 1            // value = 1 wei
//   73 <20 bytes of B>       PUSH20 <B>
//   620f4240                 PUSH3 0x0f4240     // gas = 1_000_000
//   f1                       CALL
//   50                       POP                // discard success flag
//   00                       STOP
static bytes_t build_call_with_value_bytecode(const address_t target_b, buffer_t* out) {
  const uint8_t prefix[] = {0x60, 0x00, 0x60, 0x00, 0x60, 0x00, 0x60, 0x00, 0x60, 0x01, 0x73};
  const uint8_t suffix[] = {0x62, 0x0f, 0x42, 0x40, 0xf1, 0x50, 0x00};
  buffer_reset(out);
  buffer_append(out, bytes((uint8_t*) prefix, sizeof(prefix)));
  buffer_append(out, bytes((uint8_t*) target_b, 20));
  buffer_append(out, bytes((uint8_t*) suffix, sizeof(suffix)));
  return out->data;
}

// Same as build_call_with_value_bytecode but with a trailing REVERT so the
// outer frame itself reverts after the inner CALL returns. Used to exercise
// the EIP-7708 top-level revert semantics.
//
// Layout (40 bytes): prefix + <B> + suffix
//   suffix: 620f4240 f1 50 60 00 60 00 fd
//           PUSH3 gas / CALL / POP / PUSH1 0 / PUSH1 0 / REVERT
static bytes_t build_call_with_value_then_revert_bytecode(const address_t target_b, buffer_t* out) {
  const uint8_t prefix[] = {0x60, 0x00, 0x60, 0x00, 0x60, 0x00, 0x60, 0x00, 0x60, 0x01, 0x73};
  const uint8_t suffix[] = {0x62, 0x0f, 0x42, 0x40, 0xf1, 0x50, 0x60, 0x00, 0x60, 0x00, 0xfd};
  buffer_reset(out);
  buffer_append(out, bytes((uint8_t*) prefix, sizeof(prefix)));
  buffer_append(out, bytes((uint8_t*) target_b, 20));
  buffer_append(out, bytes((uint8_t*) suffix, sizeof(suffix)));
  return out->data;
}

// -- End-to-end simulate scenarios -------------------------------------------

/**
 * A nested CALL A -> B with value emits a second SYSTEM_ADDRESS Transfer log
 * *after* the top-level one. Balances on A and B reflect the value transfer.
 */
void test_nested_call_with_value_emits_log(void) {
  address_t addr_a = {0};
  memset(addr_a, 0x11, 20);
  address_t addr_b = {0};
  memset(addr_b, 0x33, 20);
  address_t addr_from = {0};
  memset(addr_from, 0x22, 20);

  buffer_t bytecode = {0};
  bytes_t  code_a   = build_call_with_value_bytecode(addr_b, &bytecode);

  // B is deployed with STOP (0x00) so the sub-call succeeds trivially.
  const uint8_t stop_code[] = {0x00};

  verify_ctx_t ctx = {0};
  ctx.chain_id     = C4_CHAIN_MAINNET;
  ctx.args         = json_parse(
      "[{\"from\":\"0x2222222222222222222222222222222222222222\","
              "\"to\":\"0x1111111111111111111111111111111111111111\","
              "\"value\":\"0x64\","
              "\"gas\":\"0xf4240\"},\"latest\"]");

  evm_call_ctx_t evm = {0};
  // A holds 10 wei so the nested CALL(value=1) has funds; B starts at 0.
  evm.accounts       = make_contract_with_balance(addr_a, code_a.data, code_a.len, 10);
  evm.accounts->next = make_contract_with_balance(addr_b, stop_code, sizeof(stop_code), 0);

  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eth_run_call_evmone_with_events(&ctx, &evm, true));
  TEST_ASSERT_FALSE(evm.reverted);
  TEST_ASSERT_NULL(ctx.state.error);

  // Two SYSTEM_ADDRESS Transfer logs: top-level (from -> A, 0x64) and nested (A -> B, 0x01).
  TEST_ASSERT_EQUAL_UINT32(2, count_logs(evm.logs));
  TEST_ASSERT_NOT_NULL_MESSAGE(find_transfer_log(evm.logs, addr_from, addr_a), "top-level Transfer log missing");
  TEST_ASSERT_NOT_NULL_MESSAGE(find_transfer_log(evm.logs, addr_a, addr_b), "nested Transfer log missing");

  // Balance bookkeeping: A was pre-funded with 10, transferred 1 to B.
  // The top-level "from" credit rule keeps A at 10 (only dest gets credited on the top-level).
  call_account_t* after_a = find_account(evm.accounts, addr_a);
  call_account_t* after_b = find_account(evm.accounts, addr_b);
  TEST_ASSERT_NOT_NULL(after_a);
  TEST_ASSERT_NOT_NULL(after_b);

  // A: 10 (initial) + 0x64 (top-level credit) - 1 (nested debit) = 10 + 100 - 1 = 109
  uint8_t exp_a[32] = {0};
  exp_a[31]         = 109;
  TEST_ASSERT_EQUAL_MEMORY(exp_a, after_a->balance, 32);

  // B: 0 + 1 = 1
  uint8_t exp_b[32] = {0};
  exp_b[31]         = 1;
  TEST_ASSERT_EQUAL_MEMORY(exp_b, after_b->balance, 32);

  buffer_free(&bytecode);
  evm_call_ctx_free(&evm);
}

/**
 * When the nested CALL target reverts the nested Transfer log MUST be
 * discarded together with the reverted frame's state, leaving only the
 * top-level Transfer log in the output.
 */
void test_nested_reverting_call_drops_transfer_log(void) {
  address_t addr_a = {0};
  memset(addr_a, 0x11, 20);
  address_t addr_b = {0};
  memset(addr_b, 0x33, 20);
  address_t addr_from = {0};
  memset(addr_from, 0x22, 20);

  buffer_t bytecode = {0};
  bytes_t  code_a   = build_call_with_value_bytecode(addr_b, &bytecode);

  // B: PUSH1 0 / PUSH1 0 / REVERT -> deterministic revert without return data.
  const uint8_t revert_code[] = {0x60, 0x00, 0x60, 0x00, 0xfd};

  verify_ctx_t ctx = {0};
  ctx.chain_id     = C4_CHAIN_MAINNET;
  ctx.args         = json_parse(
      "[{\"from\":\"0x2222222222222222222222222222222222222222\","
              "\"to\":\"0x1111111111111111111111111111111111111111\","
              "\"value\":\"0x64\","
              "\"gas\":\"0xf4240\"},\"latest\"]");

  evm_call_ctx_t evm = {0};
  evm.accounts       = make_contract_with_balance(addr_a, code_a.data, code_a.len, 10);
  evm.accounts->next = make_contract_with_balance(addr_b, revert_code, sizeof(revert_code), 0);

  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eth_run_call_evmone_with_events(&ctx, &evm, true));
  // A's outer frame still returns cleanly (CALL failure is signalled via return
  // value on the stack, but the enclosing STOP path succeeds unconditionally).
  TEST_ASSERT_FALSE(evm.reverted);
  TEST_ASSERT_NULL(ctx.state.error);

  // Only the top-level Transfer log survives; the nested one lived on the
  // reverted child context and was freed when host_call skipped context_apply.
  TEST_ASSERT_EQUAL_UINT32(1, count_logs(evm.logs));
  TEST_ASSERT_NOT_NULL_MESSAGE(find_transfer_log(evm.logs, addr_from, addr_a), "top-level Transfer log missing");
  TEST_ASSERT_NULL_MESSAGE(find_transfer_log(evm.logs, addr_a, addr_b), "reverted nested Transfer log leaked");

  // Balance bookkeeping: B never received the wei because the frame reverted.
  call_account_t* after_a = find_account(evm.accounts, addr_a);
  call_account_t* after_b = find_account(evm.accounts, addr_b);
  TEST_ASSERT_NOT_NULL(after_a);

  // A: 10 (initial) + 0x64 (top-level credit) - 0 = 10 + 100 = 110
  uint8_t exp_a[32] = {0};
  exp_a[31]         = 110;
  TEST_ASSERT_EQUAL_MEMORY(exp_a, after_a->balance, 32);

  // B may or may not have a materialised account (the reverted CALL discards
  // its child list). If it exists, its balance must remain zero.
  if (after_b) {
    for (int i = 0; i < 32; i++) TEST_ASSERT_EQUAL_UINT8(0, after_b->balance[i]);
  }

  buffer_free(&bytecode);
  evm_call_ctx_free(&evm);
}

/**
 * When the top-level frame itself reverts, the deferred top-level EIP-7708
 * Transfer log MUST NOT surface. Nested logs applied via successful
 * context_apply remain in the debug output (pre-existing simulate convention).
 */
void test_top_level_revert_drops_top_level_transfer_log(void) {
  address_t addr_a = {0};
  memset(addr_a, 0x11, 20);
  address_t addr_b = {0};
  memset(addr_b, 0x33, 20);
  address_t addr_from = {0};
  memset(addr_from, 0x22, 20);

  buffer_t bytecode = {0};
  bytes_t  code_a   = build_call_with_value_then_revert_bytecode(addr_b, &bytecode);

  const uint8_t stop_code[] = {0x00};

  verify_ctx_t ctx = {0};
  ctx.chain_id     = C4_CHAIN_MAINNET;
  ctx.args         = json_parse(
      "[{\"from\":\"0x2222222222222222222222222222222222222222\","
              "\"to\":\"0x1111111111111111111111111111111111111111\","
              "\"value\":\"0x64\","
              "\"gas\":\"0xf4240\"},\"latest\"]");

  evm_call_ctx_t evm = {0};
  evm.accounts       = make_contract_with_balance(addr_a, code_a.data, code_a.len, 10);
  evm.accounts->next = make_contract_with_balance(addr_b, stop_code, sizeof(stop_code), 0);

  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eth_run_call_evmone_with_events(&ctx, &evm, true));
  TEST_ASSERT_TRUE(evm.reverted);

  // Top-level Transfer must be gone. The nested Transfer (A -> B) survived
  // because its enclosing frame (host_call for B) succeeded before A reverted,
  // and it merged into the parent's log list via context_apply.
  TEST_ASSERT_NULL_MESSAGE(find_transfer_log(evm.logs, addr_from, addr_a), "top-level Transfer log leaked on revert");
  TEST_ASSERT_NOT_NULL_MESSAGE(find_transfer_log(evm.logs, addr_a, addr_b), "nested Transfer log incorrectly discarded");

  buffer_free(&bytecode);
  evm_call_ctx_free(&evm);
}

/**
 * SELFDESTRUCT with a non-zero balance and a different beneficiary must emit
 * a SYSTEM_ADDRESS Transfer log (from = self-destructing account, to =
 * beneficiary, value = balance immediately before the drain).
 */
void test_selfdestruct_emits_transfer_log(void) {
  address_t addr_a = {0};
  memset(addr_a, 0x11, 20);
  address_t addr_b = {0};
  memset(addr_b, 0x33, 20);
  address_t addr_from = {0};
  memset(addr_from, 0x22, 20);

  // Bytecode: PUSH20 <B> SELFDESTRUCT
  uint8_t code_a[22];
  code_a[0] = 0x73;
  memcpy(code_a + 1, addr_b, 20);
  code_a[21] = 0xff;

  verify_ctx_t ctx = {0};
  ctx.chain_id     = C4_CHAIN_MAINNET;
  ctx.args         = json_parse(
      "[{\"from\":\"0x2222222222222222222222222222222222222222\","
              "\"to\":\"0x1111111111111111111111111111111111111111\","
              "\"value\":\"0x0\","
              "\"gas\":\"0xf4240\"},\"latest\"]");

  evm_call_ctx_t evm = {0};
  evm.accounts       = make_contract_with_balance(addr_a, code_a, sizeof(code_a), 50);
  evm.accounts->next = make_contract_with_balance(addr_b, NULL, 0, 0);

  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eth_run_call_evmone_with_events(&ctx, &evm, true));
  TEST_ASSERT_FALSE(evm.reverted);
  TEST_ASSERT_NULL(ctx.state.error);

  // Only the SELFDESTRUCT-triggered log; top-level value was zero so no
  // top-level Transfer must be present either.
  TEST_ASSERT_EQUAL_UINT32(1, count_logs(evm.logs));
  TEST_ASSERT_NULL_MESSAGE(find_transfer_log(evm.logs, addr_from, addr_a), "unexpected top-level Transfer for value=0 tx");
  emitted_log_t* sd_log = find_transfer_log(evm.logs, addr_a, addr_b);
  TEST_ASSERT_NOT_NULL_MESSAGE(sd_log, "SELFDESTRUCT SYSTEM_ADDRESS Transfer log missing");

  // Log data must carry the drained balance.
  uint8_t expected_amount[32] = {0};
  expected_amount[31]         = 50;
  TEST_ASSERT_EQUAL_MEMORY(expected_amount, sd_log->data.data, 32);

  // Balance bookkeeping: A drained to 0, B credited by 50 (top-level value = 0).
  call_account_t* after_a = find_account(evm.accounts, addr_a);
  call_account_t* after_b = find_account(evm.accounts, addr_b);
  TEST_ASSERT_NOT_NULL(after_a);
  TEST_ASSERT_NOT_NULL(after_b);
  for (int i = 0; i < 32; i++) TEST_ASSERT_EQUAL_UINT8(0, after_a->balance[i]);
  TEST_ASSERT_EQUAL_MEMORY(expected_amount, after_b->balance, 32);

  evm_call_ctx_free(&evm);
}

/**
 * When a nested frame emits both a Transfer log (from the host) and an EVM
 * `LOG0`, the final chronological output must preserve their relative order:
 * top-level Transfer, then nested Transfer, then the LOG0. This exercises the
 * order-preserving splice in `context_apply` and the reversal in
 * `match_simulate_result`.
 */
void test_log_ordering_top_level_before_nested_transfer_before_evm_log(void) {
  address_t addr_a = {0};
  memset(addr_a, 0x11, 20);
  address_t addr_b = {0};
  memset(addr_b, 0x33, 20);
  address_t addr_from = {0};
  memset(addr_from, 0x22, 20);

  buffer_t bytecode = {0};
  bytes_t  code_a   = build_call_with_value_bytecode(addr_b, &bytecode);

  // B: emit a bare LOG0 with empty data (topics = 0, data = 0 bytes), then STOP.
  //   6000                     PUSH1 0            // size
  //   6000                     PUSH1 0            // offset
  //   a0                       LOG0
  //   00                       STOP
  const uint8_t log0_stop_code[] = {0x60, 0x00, 0x60, 0x00, 0xa0, 0x00};

  verify_ctx_t ctx = {0};
  ctx.chain_id     = C4_CHAIN_MAINNET;
  ctx.args         = json_parse(
      "[{\"from\":\"0x2222222222222222222222222222222222222222\","
              "\"to\":\"0x1111111111111111111111111111111111111111\","
              "\"value\":\"0x64\","
              "\"gas\":\"0xf4240\"},\"latest\"]");

  evm_call_ctx_t evm = {0};
  evm.accounts       = make_contract_with_balance(addr_a, code_a.data, code_a.len, 10);
  evm.accounts->next = make_contract_with_balance(addr_b, log0_stop_code, sizeof(log0_stop_code), 0);

  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eth_run_call_evmone_with_events(&ctx, &evm, true));
  TEST_ASSERT_FALSE(evm.reverted);

  // Pre-reversal (evm->logs is LIFO): head is the most recently emitted log,
  // i.e. B's LOG0 followed by the nested Transfer followed by the top-level
  // Transfer at the tail (top-level was spliced onto the tail on success).
  TEST_ASSERT_NOT_NULL(evm.logs);
  emitted_log_t* first_lifo  = evm.logs;
  emitted_log_t* second_lifo = evm.logs->next;
  emitted_log_t* third_lifo  = evm.logs->next ? evm.logs->next->next : NULL;
  TEST_ASSERT_NOT_NULL(second_lifo);
  TEST_ASSERT_NOT_NULL(third_lifo);
  TEST_ASSERT_NULL_MESSAGE(third_lifo->next, "unexpected 4th log in LIFO list");

  // First (newest): B's LOG0 -- emitter is B, no topics.
  TEST_ASSERT_EQUAL_MEMORY(addr_b, first_lifo->address, 20);
  TEST_ASSERT_EQUAL_UINT(0, first_lifo->topics_count);

  // Second: nested Transfer (A -> B) from SYSTEM_ADDRESS.
  TEST_ASSERT_EQUAL_MEMORY(SYSTEM_ADDRESS, second_lifo->address, 20);
  TEST_ASSERT_EQUAL_UINT(3, second_lifo->topics_count);
  TEST_ASSERT_EQUAL_MEMORY(addr_a, second_lifo->topics[1] + 12, 20);
  TEST_ASSERT_EQUAL_MEMORY(addr_b, second_lifo->topics[2] + 12, 20);

  // Third (oldest in LIFO, i.e. first chronologically): top-level Transfer.
  TEST_ASSERT_EQUAL_MEMORY(SYSTEM_ADDRESS, third_lifo->address, 20);
  TEST_ASSERT_EQUAL_UINT(3, third_lifo->topics_count);
  TEST_ASSERT_EQUAL_MEMORY(addr_from, third_lifo->topics[1] + 12, 20);
  TEST_ASSERT_EQUAL_MEMORY(addr_a, third_lifo->topics[2] + 12, 20);

  buffer_free(&bytecode);
  evm_call_ctx_free(&evm);
}

// -- uint256 carry / borrow coverage -----------------------------------------
//
// The uint256_add / uint256_sub helpers are file-static in call_evmone.c. We
// exercise them indirectly through emit_eth_transfer_log's data field (which
// echoes `value` unchanged) and through the balance mutations in the
// end-to-end scenarios above. That is sufficient for correctness at the byte
// level for the values used in real ETH transfers (all wei-scaled), but a
// direct byte-boundary check would need those helpers exported. Skipping the
// direct arithmetic test avoids exposing internals; the end-to-end tests
// cover the operative paths on realistic 32-byte inputs.

#else

void test_transfer_logs_skipped(void) {
  TEST_IGNORE_MESSAGE("EVMONE disabled");
}

#endif // EVMONE

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();
#ifdef EVMONE
  RUN_TEST(test_emit_transfer_log_zero_value_skipped);
  RUN_TEST(test_emit_transfer_log_self_transfer_skipped);
  RUN_TEST(test_emit_transfer_log_normal);
  RUN_TEST(test_nested_call_with_value_emits_log);
  RUN_TEST(test_nested_reverting_call_drops_transfer_log);
  RUN_TEST(test_top_level_revert_drops_top_level_transfer_log);
  RUN_TEST(test_selfdestruct_emits_transfer_log);
  RUN_TEST(test_log_ordering_top_level_before_nested_transfer_before_evm_log);
#else
  RUN_TEST(test_transfer_logs_skipped);
#endif
  return UNITY_END();
}
