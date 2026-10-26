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
#include "crypto.h"
#include "eth_account.h"
#include "eth_call_account.h"
#include "json.h"
#include "state.h"
#include "unity.h"
#include "verify.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#ifdef EVMONE

// PUSH1 0 / SLOAD / PUSH1 0 / MSTORE / PUSH1 32 / PUSH1 0 / RETURN
static const uint8_t sload_code[] = {0x60, 0x00, 0x54, 0x60, 0x00, 0x52, 0x60, 0x20, 0x60, 0x00, 0xf3};

static call_account_t* make_account(const address_t addr, const uint8_t* code, size_t code_len, uint32_t flags) {
  call_account_t* acc = safe_calloc(1, sizeof(call_account_t));
  memcpy(acc->address, addr, 20);
  acc->code  = bytes_dup(bytes((uint8_t*) code, (uint32_t) code_len));
  acc->flags = flags | ACCOUNT_HAS_CODE | ACCOUNT_FREE_CODE;
  return acc;
}

static void run_sload(call_account_t* account, bool pap, c4_status_t* status, verify_ctx_t* ctx, evm_call_ctx_t* evm) {
  memset(ctx, 0, sizeof(*ctx));
  memset(evm, 0, sizeof(*evm));
  ctx->chain_id = C4_CHAIN_MAINNET;
  ctx->args     = json_parse(
      "[{\"to\":\"0x1111111111111111111111111111111111111111\",\"gas\":\"0xf4240\"},\"latest\"]");
  evm->accounts = account;
  evm->pap_mode = pap;
  *status       = eth_run_call_evmone_with_events(ctx, evm, false);
}

void test_unproven_slot_is_rejected(void) {
  address_t       addr = {0};
  verify_ctx_t    ctx  = {0};
  evm_call_ctx_t  evm  = {0};
  c4_status_t     status;
  call_account_t* acc = make_account(addr, sload_code, sizeof(sload_code), ACCOUNT_HAS_STORAGE_ROOT);
  memset(addr, 0x11, 20);
  memcpy(acc->address, addr, 20);
  acc->storage_root[0] = 0xab;

  run_sload(acc, false, &status, &ctx, &evm);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, status);
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "unproven storage slot"));
  TEST_ASSERT_NULL(evm.call_result.data);

  c4_state_free(&ctx.state);
  evm_call_ctx_free(&evm);
}

void test_proven_slot_is_returned(void) {
  address_t       addr = {0};
  verify_ctx_t    ctx  = {0};
  evm_call_ctx_t  evm  = {0};
  c4_status_t     status;
  call_account_t* acc = make_account(addr, sload_code, sizeof(sload_code), ACCOUNT_HAS_STORAGE_ROOT);
  memset(addr, 0x11, 20);
  memcpy(acc->address, addr, 20);
  acc->storage_root[0] = 0xab;
  call_storage_t* slot = safe_calloc(1, sizeof(call_storage_t));
  slot->post_value[31] = 3;
  slot->src_value[31]  = 3;
  acc->storage         = slot;

  run_sload(acc, false, &status, &ctx, &evm);
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, status);
  TEST_ASSERT_NULL(ctx.state.error);
  TEST_ASSERT_EQUAL_UINT32(32, evm.call_result.len);
  TEST_ASSERT_EQUAL_UINT8(3, evm.call_result.data[31]);

  c4_state_free(&ctx.state);
  evm_call_ctx_free(&evm);
}

void test_empty_storage_root_reads_zero(void) {
  address_t       addr = {0};
  verify_ctx_t    ctx  = {0};
  evm_call_ctx_t  evm  = {0};
  c4_status_t     status;
  call_account_t* acc = make_account(addr, sload_code, sizeof(sload_code), ACCOUNT_HAS_STORAGE_ROOT);
  memset(addr, 0x11, 20);
  memcpy(acc->address, addr, 20);
  memcpy(acc->storage_root, EMPTY_ROOT_HASH, 32);

  run_sload(acc, false, &status, &ctx, &evm);
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, status);
  TEST_ASSERT_NULL(ctx.state.error);
  TEST_ASSERT_EQUAL_UINT32(32, evm.call_result.len);
  TEST_ASSERT_TRUE(bytes_all_zero(evm.call_result));

  c4_state_free(&ctx.state);
  evm_call_ctx_free(&evm);
}

void test_created_account_storage_starts_at_zero(void) {
  // PUSH9 initcode / PUSH1 0 / MSTORE / CREATE(size=9, offset=23) / PUSH1 0 / MSTORE / PUSH1 32 / PUSH1 0 / RETURN
  // initcode: PUSH1 0 / SLOAD / POP / PUSH1 0 / PUSH1 0 / RETURN
  const uint8_t code[] = {
      0x68, 0x60, 0x00, 0x54, 0x50, 0x60, 0x00, 0x60, 0x00, 0xf3,
      0x60, 0x00, 0x52,
      0x60, 0x09, 0x60, 0x17, 0x60, 0x00, 0xf0,
      0x60, 0x00, 0x52,
      0x60, 0x20, 0x60, 0x00, 0xf3};

  address_t       addr = {0};
  verify_ctx_t    ctx  = {0};
  evm_call_ctx_t  evm  = {0};
  c4_status_t     status;
  call_account_t* acc = make_account(addr, code, sizeof(code), ACCOUNT_HAS_STORAGE_ROOT | ACCOUNT_HAS_NONCE);
  memset(addr, 0x11, 20);
  memcpy(acc->address, addr, 20);
  memcpy(acc->storage_root, EMPTY_ROOT_HASH, 32);

  run_sload(acc, false, &status, &ctx, &evm);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, status, ctx.state.error ? ctx.state.error : "no error");
  TEST_ASSERT_NULL(ctx.state.error);
  TEST_ASSERT_EQUAL_UINT32(32, evm.call_result.len);
  TEST_ASSERT_FALSE(bytes_all_zero(bytes(evm.call_result.data + 12, 20)));

  c4_state_free(&ctx.state);
  evm_call_ctx_free(&evm);
}

void test_pap_missing_slot_requests_storage(void) {
  address_t       addr = {0};
  verify_ctx_t    ctx  = {0};
  evm_call_ctx_t  evm  = {0};
  c4_status_t     status;
  call_account_t* acc = make_account(addr, sload_code, sizeof(sload_code), ACCOUNT_HAS_STORAGE_ROOT);
  memset(addr, 0x11, 20);
  memcpy(acc->address, addr, 20);
  acc->storage_root[0] = 0xab;

  run_sload(acc, true, &status, &ctx, &evm);
  TEST_ASSERT_EQUAL_INT(C4_PENDING, status);
  TEST_ASSERT_NOT_NULL(c4_state_get_pending_request(&ctx.state));

  c4_state_free(&ctx.state);
  evm_call_ctx_free(&evm);
}

// CREATE address of `sender` with nonce 0: keccak(rlp([sender, 0]))[12:].
static void address_created_by(const uint8_t sender[20], uint8_t out[20]) {
  uint8_t rlp[23];
  rlp[0] = 0xd6;
  rlp[1] = 0x94;
  memcpy(rlp + 2, sender, 20);
  rlp[22] = 0x80;
  uint8_t hash[32];
  keccak(bytes(rlp, 23), hash);
  memcpy(out, hash + 12, 20);
}

void test_create_collision_does_not_read_storage(void) {
  // PUSH1 0 / PUSH1 0 / MSTORE8 / PUSH1 1 / PUSH1 0 / PUSH1 0 / CREATE / PUSH1 0 / MSTORE / PUSH1 32 / PUSH1 0 / RETURN
  const uint8_t code[] = {0x60, 0x00, 0x60, 0x00, 0x53, 0x60, 0x01, 0x60, 0x00, 0x60, 0x00, 0xf0, 0x60, 0x00, 0x52, 0x60, 0x20, 0x60, 0x00, 0xf3};
  uint8_t       sender[20];
  uint8_t       created[20];
  memset(sender, 0x11, 20);
  address_created_by(sender, created);

  verify_ctx_t    ctx = {0};
  evm_call_ctx_t  evm = {0};
  c4_status_t     status;
  call_account_t* factory = make_account(sender, code, sizeof(code), ACCOUNT_HAS_NONCE | ACCOUNT_HAS_STORAGE_ROOT);
  memcpy(factory->storage_root, EMPTY_ROOT_HASH, 32);
  const uint8_t   stop[] = {0x00};
  call_account_t* prior  = make_account(created, stop, sizeof(stop), ACCOUNT_HAS_NONCE | ACCOUNT_HAS_STORAGE_ROOT);
  prior->nonce           = 1;
  prior->storage_root[0] = 0xab;
  factory->next          = prior;

  run_sload(factory, false, &status, &ctx, &evm);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, status, ctx.state.error ? ctx.state.error : "no error");
  TEST_ASSERT_NULL(ctx.state.error);
  TEST_ASSERT_EQUAL_UINT32(32, evm.call_result.len);
  TEST_ASSERT_TRUE(bytes_all_zero(evm.call_result));

  c4_state_free(&ctx.state);
  evm_call_ctx_free(&evm);
}

void test_create_drops_preexisting_slot(void) {
  // CODECOPY 18 bytes of initcode from offset 22, CREATE, return the address.
  // initcode: SLOAD 0; revert when the value is not zero, otherwise return empty.
  const uint8_t code[] = {
      0x60, 0x12, 0x60, 0x16, 0x60, 0x00, 0x39,
      0x60, 0x12, 0x60, 0x00, 0x60, 0x00, 0xf0,
      0x60, 0x00, 0x52, 0x60, 0x20, 0x60, 0x00, 0xf3,
      0x60, 0x00, 0x54, 0x15, 0x60, 0x0c, 0x57,
      0x60, 0x00, 0x60, 0x00, 0xfd,
      0x5b, 0x60, 0x00, 0x60, 0x00, 0xf3};
  uint8_t sender[20];
  uint8_t created[20];
  memset(sender, 0x11, 20);
  address_created_by(sender, created);

  verify_ctx_t    ctx = {0};
  evm_call_ctx_t  evm = {0};
  c4_status_t     status;
  call_account_t* factory = make_account(sender, code, sizeof(code), ACCOUNT_HAS_NONCE | ACCOUNT_HAS_STORAGE_ROOT);
  memcpy(factory->storage_root, EMPTY_ROOT_HASH, 32);
  call_account_t* prior = make_account(created, NULL, 0, ACCOUNT_HAS_NONCE | ACCOUNT_HAS_STORAGE_ROOT);
  prior->flags &= ~ACCOUNT_HAS_CODE;
  prior->storage_root[0] = 0xab;
  call_storage_t* slot    = safe_calloc(1, sizeof(call_storage_t));
  slot->post_value[31]    = 7;
  slot->src_value[31]     = 7;
  prior->storage          = slot;
  factory->next           = prior;

  run_sload(factory, false, &status, &ctx, &evm);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, status, ctx.state.error ? ctx.state.error : "no error");
  TEST_ASSERT_NULL(ctx.state.error);
  TEST_ASSERT_EQUAL_UINT32(32, evm.call_result.len);
  TEST_ASSERT_FALSE(bytes_all_zero(bytes(evm.call_result.data + 12, 20)));

  c4_state_free(&ctx.state);
  evm_call_ctx_free(&evm);
}

void test_nested_pap_sload_requests_storage(void) {
  // CALL 0x2222… with the sload bytecode, then return the 32-byte returndata.
  const uint8_t code[] = {
      0x60, 0x20, 0x60, 0x00, 0x60, 0x00, 0x60, 0x00, 0x60, 0x00,
      0x73, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
      0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
      0x61, 0xff, 0xff, 0xf1,
      0x60, 0x20, 0x60, 0x00, 0xf3};
  uint8_t caller[20];
  uint8_t callee[20];
  memset(caller, 0x11, 20);
  memset(callee, 0x22, 20);

  verify_ctx_t    ctx = {0};
  evm_call_ctx_t  evm = {0};
  c4_status_t     status;
  call_account_t* factory = make_account(caller, code, sizeof(code), ACCOUNT_HAS_STORAGE_ROOT);
  memcpy(factory->storage_root, EMPTY_ROOT_HASH, 32);
  call_account_t* other = make_account(callee, sload_code, sizeof(sload_code), ACCOUNT_HAS_STORAGE_ROOT);
  other->storage_root[0] = 0xab;
  factory->next          = other;

  run_sload(factory, true, &status, &ctx, &evm);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_PENDING, status, ctx.state.error ? ctx.state.error : "no error");
  TEST_ASSERT_NOT_NULL(c4_state_get_pending_request(&ctx.state));

  c4_state_free(&ctx.state);
  evm_call_ctx_free(&evm);
}

#endif

void test_created_flag_is_not_cached(void) {
  call_account_t account = {0};
  account.flags          = ACCOUNT_CREATED | ACCOUNT_HAS_NONCE;
  buffer_t out           = {0};
  eth_call_account_serialize(&out, &account);
  TEST_ASSERT_TRUE(out.data.len >= 5);
  uint32_t flags = (uint32_t) out.data.data[1] | ((uint32_t) out.data.data[2] << 8) | ((uint32_t) out.data.data[3] << 16) | ((uint32_t) out.data.data[4] << 24);
  TEST_ASSERT_EQUAL_UINT32(0, flags & ACCOUNT_CREATED);
  TEST_ASSERT_NOT_EQUAL(0, flags & ACCOUNT_HAS_NONCE);
  buffer_free(&out);
}

int main(void) {
  UNITY_BEGIN();
#ifdef EVMONE
  RUN_TEST(test_unproven_slot_is_rejected);
  RUN_TEST(test_proven_slot_is_returned);
  RUN_TEST(test_empty_storage_root_reads_zero);
  RUN_TEST(test_created_account_storage_starts_at_zero);
  RUN_TEST(test_pap_missing_slot_requests_storage);
  RUN_TEST(test_create_collision_does_not_read_storage);
  RUN_TEST(test_create_drops_preexisting_slot);
  RUN_TEST(test_nested_pap_sload_requests_storage);
#endif
  RUN_TEST(test_created_flag_is_not_cached);
  return UNITY_END();
}
