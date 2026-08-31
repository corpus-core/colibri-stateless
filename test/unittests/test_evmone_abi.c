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

#ifdef EVMONE
#include "evmone_c_wrapper.h"
#include <evmc/evmc.h>
#include <string.h>

typedef struct {
  uint64_t     nonce;
  uint32_t     nonce_calls;
  evmc_address last_nonce_addr;
  evmc_address last_create_dest;
  bool         saw_create;
  evmc_bytes32 blob_hashes[1];
} test_host_t;

static bool host_account_exists(void* context, const evmc_address* addr) {
  (void) context;
  (void) addr;
  return true;
}

static evmc_bytes32 host_get_storage(void* context, const evmc_address* addr, const evmc_bytes32* key) {
  (void) context;
  (void) addr;
  (void) key;
  return (evmc_bytes32){0};
}

static evmone_storage_status host_set_storage(void* context, const evmc_address* addr, const evmc_bytes32* key,
                                              const evmc_bytes32* value) {
  (void) context;
  (void) addr;
  (void) key;
  (void) value;
  return EVMONE_STORAGE_ASSIGNED;
}

static evmc_bytes32 host_get_balance(void* context, const evmc_address* addr) {
  (void) context;
  (void) addr;
  evmc_bytes32 bal = {0};
  bal.bytes[31]    = 1; // non-zero balance so CREATE with endowment can proceed if needed
  return bal;
}

static uint64_t host_get_nonce(void* context, const evmc_address* addr) {
  test_host_t* host     = (test_host_t*) context;
  host->nonce_calls++;
  host->last_nonce_addr = *addr;
  return host->nonce;
}

static size_t host_get_code_size(void* context, const evmc_address* addr) {
  (void) context;
  (void) addr;
  return 0;
}

static evmc_bytes32 host_get_code_hash(void* context, const evmc_address* addr) {
  (void) context;
  (void) addr;
  return (evmc_bytes32){0};
}

static size_t host_copy_code(void* context, const evmc_address* addr, size_t code_offset, uint8_t* buffer_data,
                             size_t buffer_size) {
  (void) context;
  (void) addr;
  (void) code_offset;
  (void) buffer_data;
  (void) buffer_size;
  return 0;
}

static void host_selfdestruct(void* context, const evmc_address* addr, const evmc_address* beneficiary) {
  (void) context;
  (void) addr;
  (void) beneficiary;
}

static void host_call(void* context, const struct evmone_message* msg, const uint8_t* code, size_t code_size,
                      struct evmone_result* result) {
  test_host_t* host = (test_host_t*) context;
  (void) code;
  (void) code_size;
  memset(result, 0, sizeof(*result));
  result->status_code = 0;
  result->gas_left    = msg->gas;

  if (msg->kind == EVMONE_CREATE || msg->kind == EVMONE_CREATE2) {
    host->saw_create       = true;
    host->last_create_dest = msg->destination;
    // EVMC ABI 18 host contract: bump creator nonce; not reverted on failure.
    host->nonce++;
  }
}

static void host_get_tx_context(void* context, struct evmone_tx_context* result) {
  test_host_t* host = (test_host_t*) context;
  memset(result, 0, sizeof(*result));
  result->block_gas_limit   = 30000000;
  result->blob_hashes       = host->blob_hashes;
  result->blob_hashes_count = 1;
  result->block_slot_number = 0;
}

static evmc_bytes32 host_get_block_hash(void* context, int64_t number) {
  (void) context;
  (void) number;
  return (evmc_bytes32){0};
}

static void host_emit_log(void* context, const evmc_address* addr, const uint8_t* data, size_t data_size,
                          const evmc_bytes32 topics[], size_t topic_count) {
  (void) context;
  (void) addr;
  (void) data;
  (void) data_size;
  (void) topics;
  (void) topic_count;
}

static int host_access_account(void* context, const evmc_address* addr) {
  (void) context;
  (void) addr;
  return EVMONE_ACCESS_COLD;
}

static int host_access_storage(void* context, const evmc_address* addr, const evmc_bytes32* key) {
  (void) context;
  (void) addr;
  (void) key;
  return EVMONE_ACCESS_COLD;
}

static evmc_bytes32 host_get_transient_storage(void* context, const evmc_address* addr, const evmc_bytes32* key) {
  (void) context;
  (void) addr;
  (void) key;
  return (evmc_bytes32){0};
}

static void host_set_transient_storage(void* context, const evmc_address* addr, const evmc_bytes32* key,
                                       const evmc_bytes32* value) {
  (void) context;
  (void) addr;
  (void) key;
  (void) value;
}

static const evmone_host_interface g_host = {
    .account_exists        = host_account_exists,
    .get_storage           = host_get_storage,
    .set_storage           = host_set_storage,
    .get_balance           = host_get_balance,
    .get_nonce             = host_get_nonce,
    .get_code_size         = host_get_code_size,
    .get_code_hash         = host_get_code_hash,
    .copy_code             = host_copy_code,
    .selfdestruct          = host_selfdestruct,
    .call                  = host_call,
    .get_tx_context        = host_get_tx_context,
    .get_block_hash        = host_get_block_hash,
    .emit_log              = host_emit_log,
    .access_account        = host_access_account,
    .access_storage        = host_access_storage,
    .get_transient_storage = host_get_transient_storage,
    .set_transient_storage = host_set_transient_storage,
};

void test_evmone_abi_version_and_executor(void) {
  TEST_ASSERT_EQUAL_INT(18, EVMC_ABI_VERSION);

  void* executor = evmone_create_executor();
  TEST_ASSERT_NOT_NULL(executor);

  struct evmc_vm* vm = (struct evmc_vm*) executor;
  TEST_ASSERT_EQUAL_INT(EVMC_ABI_VERSION, vm->abi_version);

  evmone_destroy_executor(executor);
}

void test_evmone_osaka_stop(void) {
  void*      executor = evmone_create_executor();
  test_host_t host     = {.nonce = 0};
  TEST_ASSERT_NOT_NULL(executor);

  uint8_t        code[] = {0x00}; // STOP
  evmone_message msg    = {0};
  msg.kind              = EVMONE_CALL;
  msg.gas               = 100000;

  evmone_result result = evmone_execute(executor, &g_host, &host, EVMONE_REV_OSAKA, &msg, code, sizeof(code));
  TEST_ASSERT_EQUAL_INT(0, result.status_code);

  evmone_release_result(&result);
  evmone_destroy_executor(executor);
}

/**
 * EVMONE_REV_OSAKA must still gate out Amsterdam-introduced opcodes; the
 * mapping is independent of what production uses, so this test guards the
 * revision boundary itself.
 */
void test_evmone_osaka_rev_rejects_amsterdam_opcodes(void) {
  void*       executor = evmone_create_executor();
  test_host_t host     = {0};
  TEST_ASSERT_NOT_NULL(executor);

  // CLZ is active in Osaka, while SLOTNUM is introduced in Amsterdam.
  const uint8_t osaka_code[]     = {0x60, 0x00, 0x1e, 0x50, 0x00};
  const uint8_t amsterdam_code[] = {0x4b, 0x50, 0x00};
  evmone_message msg             = {0};
  msg.kind                       = EVMONE_CALL;
  msg.gas                        = 100000;

  evmone_result osaka_result =
      evmone_execute(executor, &g_host, &host, EVMONE_REV_OSAKA, &msg, osaka_code, sizeof(osaka_code));
  TEST_ASSERT_EQUAL_INT(0, osaka_result.status_code);
  evmone_release_result(&osaka_result);

  evmone_result amsterdam_result =
      evmone_execute(executor, &g_host, &host, EVMONE_REV_OSAKA, &msg, amsterdam_code, sizeof(amsterdam_code));
  TEST_ASSERT_EQUAL_INT(EVMC_UNDEFINED_INSTRUCTION, amsterdam_result.status_code);
  evmone_release_result(&amsterdam_result);

  evmone_destroy_executor(executor);
}

/**
 * EVMONE_REV_AMSTERDAM must accept the Amsterdam-introduced SLOTNUM opcode.
 * This guards against regressions in the OSAKA -> AMSTERDAM revision switch.
 */
void test_evmone_amsterdam_rev_accepts_slotnum(void) {
  void*       executor = evmone_create_executor();
  test_host_t host     = {0};
  TEST_ASSERT_NOT_NULL(executor);

  // PUSH0 style pattern would need Shanghai; use SLOTNUM (0x4b) then POP then STOP.
  const uint8_t amsterdam_code[] = {0x4b, 0x50, 0x00};
  evmone_message msg             = {0};
  msg.kind                       = EVMONE_CALL;
  msg.gas                        = 100000;

  evmone_result result =
      evmone_execute(executor, &g_host, &host, EVMONE_REV_AMSTERDAM, &msg, amsterdam_code, sizeof(amsterdam_code));
  TEST_ASSERT_EQUAL_INT(0, result.status_code);
  evmone_release_result(&result);

  evmone_destroy_executor(executor);
}

void test_evmone_create_uses_get_nonce(void) {
  // CREATE with empty initcode: value=0, offset=0, size=0
  // PUSH1 0 / PUSH1 0 / PUSH1 0 / CREATE / STOP
  const uint8_t code[] = {0x60, 0x00, 0x60, 0x00, 0x60, 0x00, 0xf0, 0x00};

  // CREATE address for sender=0x00..00 and nonce=1.
  const uint8_t expected[20] = {
      0x5a, 0x44, 0x37, 0x04, 0xdd, 0x4b, 0x59, 0x4b, 0x38, 0x2c,
      0x22, 0xa0, 0x83, 0xe2, 0xbd, 0x30, 0x90, 0xa6, 0xfe, 0xf3};
  const uint8_t zero_address[20] = {0};

  void*       executor = evmone_create_executor();
  test_host_t host     = {.nonce = 1};
  TEST_ASSERT_NOT_NULL(executor);

  evmone_message msg = {0};
  msg.kind           = EVMONE_CALL;
  msg.gas            = 1000000;

  evmone_result result = evmone_execute(executor, &g_host, &host, EVMONE_REV_OSAKA, &msg, code, sizeof(code));
  TEST_ASSERT_EQUAL_INT(0, result.status_code);
  TEST_ASSERT_TRUE(host.saw_create);
  TEST_ASSERT_EQUAL_UINT32(1, host.nonce_calls);
  TEST_ASSERT_EQUAL_UINT64(2, host.nonce); // host bumped after pre-bump read of 1
  TEST_ASSERT_EQUAL_MEMORY(zero_address, host.last_nonce_addr.bytes, sizeof(zero_address));
  TEST_ASSERT_EQUAL_MEMORY(expected, host.last_create_dest.bytes, 20);

  evmone_release_result(&result);
  evmone_destroy_executor(executor);
}

void test_evmone_create_bumps_nonce_between_creates(void) {
  // Two empty CREATEs; second address must use bumped nonce.
  // PUSH1 0 / PUSH1 0 / PUSH1 0 / CREATE / PUSH1 0 / PUSH1 0 / PUSH1 0 / CREATE / STOP
  const uint8_t code[] = {
      0x60, 0x00, 0x60, 0x00, 0x60, 0x00, 0xf0,
      0x60, 0x00, 0x60, 0x00, 0x60, 0x00, 0xf0,
      0x00};

  // create(0x00..00, nonce=0) and create(0x00..00, nonce=1)
  const uint8_t addr_nonce0[20] = {
      0xbd, 0x77, 0x04, 0x16, 0xa3, 0x34, 0x5f, 0x91, 0xe4, 0xb3,
      0x45, 0x76, 0xcb, 0x80, 0x4a, 0x57, 0x6f, 0xa4, 0x8e, 0xb1};
  const uint8_t addr_nonce1[20] = {
      0x5a, 0x44, 0x37, 0x04, 0xdd, 0x4b, 0x59, 0x4b, 0x38, 0x2c,
      0x22, 0xa0, 0x83, 0xe2, 0xbd, 0x30, 0x90, 0xa6, 0xfe, 0xf3};

  void*       executor = evmone_create_executor();
  test_host_t host     = {.nonce = 0};
  TEST_ASSERT_NOT_NULL(executor);

  evmone_message msg = {0};
  msg.kind           = EVMONE_CALL;
  msg.gas            = 1000000;

  evmone_result result = evmone_execute(executor, &g_host, &host, EVMONE_REV_OSAKA, &msg, code, sizeof(code));
  TEST_ASSERT_EQUAL_INT(0, result.status_code);
  TEST_ASSERT_EQUAL_UINT32(2, host.nonce_calls);
  TEST_ASSERT_EQUAL_UINT64(2, host.nonce);

  // Without the host-side bump the second CREATE would reuse the nonce=0 address.
  TEST_ASSERT_TRUE(memcmp(addr_nonce0, addr_nonce1, 20) != 0);
  TEST_ASSERT_EQUAL_MEMORY(addr_nonce1, host.last_create_dest.bytes, 20);

  evmone_release_result(&result);
  evmone_destroy_executor(executor);
}

void test_evmone_tx_context_blob_hashes(void) {
  void*       executor = evmone_create_executor();
  test_host_t host     = {.nonce = 0};
  memset(host.blob_hashes[0].bytes, 0xab, 32);
  TEST_ASSERT_NOT_NULL(executor);

  // PUSH1 0 / BLOBHASH / PUSH1 0 / MSTORE / PUSH1 32 / PUSH1 0 / RETURN
  const uint8_t code[] = {0x60, 0x00, 0x49, 0x60, 0x00, 0x52, 0x60, 0x20, 0x60, 0x00, 0xf3};

  evmone_message msg = {0};
  msg.kind           = EVMONE_CALL;
  msg.gas            = 100000;

  evmone_result result = evmone_execute(executor, &g_host, &host, EVMONE_REV_OSAKA, &msg, code, sizeof(code));
  TEST_ASSERT_EQUAL_INT(0, result.status_code);
  TEST_ASSERT_EQUAL_UINT32(32, (uint32_t) result.output_size);
  TEST_ASSERT_EQUAL_MEMORY(host.blob_hashes[0].bytes, result.output_data, 32);

  evmone_release_result(&result);

  // Unknown Colibri revision IDs must not silently map to Osaka.
  evmone_result bad = evmone_execute(executor, &g_host, &host, 0, &msg, code, sizeof(code));
  TEST_ASSERT_EQUAL_INT(EVMC_INTERNAL_ERROR, bad.status_code);
  evmone_release_result(&bad);

  evmone_destroy_executor(executor);
}

#else

void test_evmone_skipped(void) {
  TEST_IGNORE_MESSAGE("EVMONE disabled");
}

#endif

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();
#ifdef EVMONE
  RUN_TEST(test_evmone_abi_version_and_executor);
  RUN_TEST(test_evmone_osaka_stop);
  RUN_TEST(test_evmone_osaka_rev_rejects_amsterdam_opcodes);
  RUN_TEST(test_evmone_amsterdam_rev_accepts_slotnum);
  RUN_TEST(test_evmone_create_uses_get_nonce);
  RUN_TEST(test_evmone_create_bumps_nonce_between_creates);
  RUN_TEST(test_evmone_tx_context_blob_hashes);
#else
  RUN_TEST(test_evmone_skipped);
#endif
  return UNITY_END();
}
