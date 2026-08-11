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

static call_account_t* make_contract(const address_t addr, const uint8_t* code, size_t code_len, uint64_t nonce) {
  call_account_t* acc = safe_calloc(1, sizeof(call_account_t));
  memcpy(acc->address, addr, 20);
  acc->nonce     = nonce;
  acc->src_nonce = nonce;
  acc->code      = bytes_dup(bytes((uint8_t*) code, (uint32_t) code_len));
  acc->flags     = ACCOUNT_HAS_CODE | ACCOUNT_FREE_CODE | ACCOUNT_HAS_NONCE;
  return acc;
}

static call_account_t* find_account(call_account_t* list, const address_t addr) {
  for (call_account_t* a = list; a; a = a->next)
    if (memcmp(a->address, addr, 20) == 0) return a;
  return NULL;
}

/**
 * Two empty CREATEs must bump the creator nonce twice and produce distinct addresses.
 */
void test_colibri_host_create_bumps_nonce(void) {
  // PUSH1 0 / PUSH1 0 / PUSH1 0 / CREATE /
  // PUSH1 0 / MSTORE /
  // PUSH1 0 / PUSH1 0 / PUSH1 0 / CREATE /
  // PUSH1 32 / MSTORE /
  // PUSH1 64 / PUSH1 0 / RETURN
  const uint8_t code[] = {
      0x60, 0x00, 0x60, 0x00, 0x60, 0x00, 0xf0,
      0x60, 0x00, 0x52,
      0x60, 0x00, 0x60, 0x00, 0x60, 0x00, 0xf0,
      0x60, 0x20, 0x52,
      0x60, 0x40, 0x60, 0x00, 0xf3};

  address_t contract = {0};
  memset(contract, 0x11, 20);

  // cast compute-address --nonce 0/1 0x1111...11
  const uint8_t expect0[20] = {
      0x8f, 0x7a, 0x45, 0xeb, 0xde, 0x05, 0x93, 0x92, 0xe4, 0x6a,
      0x46, 0xdc, 0xc1, 0x4a, 0xb2, 0x46, 0x81, 0xa9, 0x61, 0xea};
  const uint8_t expect1[20] = {
      0x15, 0x45, 0x2e, 0xc0, 0x16, 0xc4, 0xdc, 0x8c, 0x54, 0x9e,
      0x7f, 0xe6, 0xff, 0x4b, 0x26, 0x32, 0x4e, 0xa8, 0xb7, 0xa4};

  verify_ctx_t ctx = {0};
  ctx.chain_id     = C4_CHAIN_MAINNET;
  ctx.args         = json_parse(
      "[{\"from\":\"0x2222222222222222222222222222222222222222\","
      "\"to\":\"0x1111111111111111111111111111111111111111\","
      "\"gas\":\"0xf4240\"},\"latest\"]");

  evm_call_ctx_t evm = {0};
  evm.accounts       = make_contract(contract, code, sizeof(code), 0);

  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eth_run_call_evmone_with_events(&ctx, &evm, false));
  TEST_ASSERT_FALSE(evm.reverted);
  TEST_ASSERT_NULL(ctx.state.error);

  call_account_t* after = find_account(evm.accounts, contract);
  TEST_ASSERT_NOT_NULL(after);
  TEST_ASSERT_EQUAL_UINT64(2, after->nonce);

  TEST_ASSERT_EQUAL_UINT32(64, evm.call_result.len);
  // Addresses are right-aligned in the 32-byte words returned by MSTORE of CREATE results.
  TEST_ASSERT_EQUAL_MEMORY(expect0, evm.call_result.data + 12, 20);
  TEST_ASSERT_EQUAL_MEMORY(expect1, evm.call_result.data + 32 + 12, 20);

  evm_call_ctx_free(&evm);
}

/**
 * A failed CREATE (reverting initcode) must still bump the creator nonce.
 */
void test_colibri_host_failed_create_still_bumps_nonce(void) {
  // The reverting initcode `PUSH1 0 / PUSH1 0 / REVERT` is stored via MSTORE, which
  // right-aligns it in the 32-byte word, so it ends up at mem[27..31].
  const uint8_t code[] = {
      0x64, 0x60, 0x00, 0x60, 0x00, 0xfd,       // PUSH5 initcode
      0x60, 0x00, 0x52,                         // MSTORE at 0
      0x60, 0x05, 0x60, 0x1b, 0x60, 0x00, 0xf0, // CREATE size=5 offset=27 value=0 -> reverts
      0x60, 0x00, 0x60, 0x00, 0x60, 0x00, 0xf0, // CREATE with empty initcode
      0x60, 0x00, 0x52,                         // MSTORE second address
      0x60, 0x20, 0x60, 0x00, 0xf3              // RETURN 32 bytes
  };

  address_t contract = {0};
  memset(contract, 0x11, 20);

  const uint8_t expect1[20] = {
      0x15, 0x45, 0x2e, 0xc0, 0x16, 0xc4, 0xdc, 0x8c, 0x54, 0x9e,
      0x7f, 0xe6, 0xff, 0x4b, 0x26, 0x32, 0x4e, 0xa8, 0xb7, 0xa4};

  verify_ctx_t ctx = {0};
  ctx.chain_id     = C4_CHAIN_MAINNET;
  ctx.args         = json_parse(
      "[{\"from\":\"0x2222222222222222222222222222222222222222\","
      "\"to\":\"0x1111111111111111111111111111111111111111\","
      "\"gas\":\"0xf4240\"},\"latest\"]");

  evm_call_ctx_t evm = {0};
  evm.accounts       = make_contract(contract, code, sizeof(code), 0);

  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eth_run_call_evmone_with_events(&ctx, &evm, false));
  TEST_ASSERT_FALSE(evm.reverted);
  TEST_ASSERT_NULL(ctx.state.error);

  call_account_t* after = find_account(evm.accounts, contract);
  TEST_ASSERT_NOT_NULL(after);
  TEST_ASSERT_EQUAL_UINT64(2, after->nonce);

  TEST_ASSERT_EQUAL_UINT32(32, evm.call_result.len);
  TEST_ASSERT_EQUAL_MEMORY(expect1, evm.call_result.data + 12, 20);

  evm_call_ctx_free(&evm);
}

#else

void test_evmone_create_nonce_skipped(void) {
  TEST_IGNORE_MESSAGE("EVMONE disabled");
}

#endif

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();
#ifdef EVMONE
  RUN_TEST(test_colibri_host_create_bumps_nonce);
  RUN_TEST(test_colibri_host_failed_create_still_bumps_nonce);
#else
  RUN_TEST(test_evmone_create_nonce_skipped);
#endif
  return UNITY_END();
}
