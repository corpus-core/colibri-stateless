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

// Coverage for the PAP-mode EVM block context: when the EVM reads the tx/block
// context and no verified header is known, `call_lazy_fetch_block_header` must
// request `eth_getBlockByNumber(<tag>,false)`, rebuild the RLP header, check its
// hash and populate NUMBER / TIMESTAMP / COINBASE / GASLIMIT / PREVRANDAO.
// The block JSON fixtures are recorded mainnet responses from other tests.

#include "bytes.h"
#include "c4_assert.h"
#include "call_ctx.h"
#include "chains.h"
#include "crypto.h"
#include "el_header.h"
#include "eth_call_account.h"
#include "json.h"
#include "unity.h"
#include "verify.h"
#include <string.h>

#if defined(EVMONE) && defined(PAP)

#define ELECTRA_BLOCK_FILE "eth_getLogs_electra/eth_getBlockByNumber_0x1564967_false.json"
#define DENEB_BLOCK_FILE   "eth_getBlockByNumber1/eth_getBlockByNumber_0x152c765_false.json"
#define BLOCK_RPC_LATEST   "{\"jsonrpc\":\"2.0\",\"method\":\"eth_getBlockByNumber\",\"params\":[\"latest\",false],\"id\":1}"

// NUMBER, TIMESTAMP, COINBASE, GASLIMIT, PREVRANDAO, BASEFEE each stored as one
// 32-byte word, then RETURN 192 bytes.
static const uint8_t BLOCK_OPCODES_CODE[] = {
    0x43, 0x60, 0x00, 0x52, // NUMBER     -> mem[0x00]
    0x42, 0x60, 0x20, 0x52, // TIMESTAMP  -> mem[0x20]
    0x41, 0x60, 0x40, 0x52, // COINBASE   -> mem[0x40]
    0x45, 0x60, 0x60, 0x52, // GASLIMIT   -> mem[0x60]
    0x44, 0x60, 0x80, 0x52, // PREVRANDAO -> mem[0x80]
    0x48, 0x60, 0xa0, 0x52, // BASEFEE    -> mem[0xa0]
    0x60, 0xc0, 0x60, 0x00, 0xf3};

// PUSH1 42 / PUSH1 0 / MSTORE / PUSH1 32 / PUSH1 0 / RETURN (no block opcodes)
static const uint8_t NO_BLOCK_OPCODES_CODE[] = {
    0x60, 0x2a, 0x60, 0x00, 0x52, 0x60, 0x20, 0x60, 0x00, 0xf3};

static const char* CALL_ARGS_LATEST =
    "[{\"from\":\"0x2222222222222222222222222222222222222222\","
    "\"to\":\"0x1111111111111111111111111111111111111111\","
    "\"gas\":\"0xf4240\"},\"latest\"]";

/**
 * Creates a contract account with known code, so no code fetch is triggered.
 *
 * @param code contract bytecode
 * @param code_len length of `code`
 * @return heap-allocated account (owned by the `evm_call_ctx_t` it is attached to)
 */
static call_account_t* make_contract(const uint8_t* code, size_t code_len) {
  call_account_t* acc = safe_calloc(1, sizeof(call_account_t));
  memset(acc->address, 0x11, 20);
  acc->code  = bytes_dup(bytes((uint8_t*) code, (uint32_t) code_len));
  acc->flags = ACCOUNT_HAS_CODE | ACCOUNT_FREE_CODE | ACCOUNT_HAS_NONCE;
  return acc;
}

/**
 * Prepares a PAP verification context and an EVM call context for `code`.
 *
 * @param ctx verification context to initialize
 * @param evm call context to initialize
 * @param args JSON-RPC params of the `eth_call` (must stay alive during the test)
 * @param code contract bytecode
 * @param code_len length of `code`
 */
static void setup_pap_call(verify_ctx_t* ctx, evm_call_ctx_t* evm, const char* args, const uint8_t* code, size_t code_len) {
  memset(ctx, 0, sizeof(*ctx));
  memset(evm, 0, sizeof(*evm));
  ctx->chain_id = C4_CHAIN_MAINNET;
  ctx->flags    = VERIFY_FLAG_PAP;
  ctx->args     = json_parse(args);
  evm->pap_mode = true;
  evm->accounts = make_contract(code, code_len);
}

/**
 * Counts all requests (pending or answered) of the state.
 *
 * @param state state to inspect
 * @return number of requests in the linked list
 */
static int count_requests(c4_state_t* state) {
  int n = 0;
  for (data_request_t* r = state->requests; r; r = r->next) n++;
  return n;
}

/**
 * Returns the single pending eth_getBlockByNumber request and asserts its payload.
 *
 * @param state state holding the request
 * @param expected_payload expected JSON-RPC payload
 * @return the pending request
 */
static data_request_t* expect_block_request(c4_state_t* state, const char* expected_payload) {
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_requests(state), "expected exactly one request");
  data_request_t* req = c4_state_get_pending_request(state);
  TEST_ASSERT_NOT_NULL(req);
  TEST_ASSERT_EQUAL_INT(C4_DATA_TYPE_ETH_RPC, req->type);
  TEST_ASSERT_EQUAL_INT(C4_DATA_METHOD_POST, req->method);
  TEST_ASSERT_EQUAL_INT(C4_DATA_ENCODING_JSON, req->encoding);
  TEST_ASSERT_NOT_NULL(req->payload.data);
  TEST_ASSERT_EQUAL_STRING_LEN(expected_payload, (char*) req->payload.data, req->payload.len);
  TEST_ASSERT_EQUAL_UINT32(strlen(expected_payload), req->payload.len);

  bytes32_t id = {0};
  keccak(bytes((uint8_t*) expected_payload, (uint32_t) strlen(expected_payload)), id);
  TEST_ASSERT_EQUAL_MEMORY_MESSAGE(id, req->id, 32, "request id must be keccak(payload)");
  return req;
}

/**
 * Checks whether `data` contains the string `needle`.
 *
 * @param data bytes to search (may be empty)
 * @param needle NUL-terminated string to find
 * @return true if `needle` occurs in `data`
 */
static bool bytes_contains(bytes_t data, const char* needle) {
  size_t n = strlen(needle);
  if (!data.data || data.len < n) return false;
  for (size_t i = 0; i + n <= data.len; i++)
    if (memcmp(data.data + i, needle, n) == 0) return true;
  return false;
}

/**
 * Asserts that a 32-byte EVM word equals the right-aligned big-endian `value`.
 *
 * @param word 32-byte word returned by the EVM
 * @param value expected bytes (right aligned)
 * @param len length of `value`
 * @param message failure message
 */
static void assert_word(const uint8_t* word, const uint8_t* value, uint32_t len, const char* message) {
  uint8_t expected[32] = {0};
  memcpy(expected + 32 - len, value, len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(expected, word, 32, message);
}

/**
 * Asserts a 32-byte EVM word equals a uint64 value.
 *
 * @param word 32-byte word returned by the EVM
 * @param value expected value
 * @param message failure message
 */
static void assert_word_u64(const uint8_t* word, uint64_t value, const char* message) {
  uint8_t be[8];
  for (int i = 0; i < 8; i++) be[i] = (uint8_t) (value >> (56 - 8 * i));
  assert_word(word, be, 8, message);
}

/**
 * Reads a recorded `eth_getBlockByNumber` response and returns the JSON `result`.
 *
 * @param file test data file (relative to TESTDATA_DIR)
 * @param raw receives the owned file content (caller frees)
 * @return the parsed block object (references `raw`)
 */
static json_t read_block(const char* file, bytes_t* raw) {
  *raw = read_testdata(file);
  TEST_ASSERT_NOT_NULL_MESSAGE(raw->data, file);
  json_t block = json_get(json_parse((char*) raw->data), "result");
  TEST_ASSERT_EQUAL_INT(JSON_TYPE_OBJECT, block.type);
  return block;
}

/**
 * Overwrites the first hex digit after `"<key>":"0x` in a JSON string.
 *
 * @param json mutable JSON text
 * @param key field name to tamper with
 */
static void tamper_hex_field(char* json, const char* key) {
  char pattern[64];
  snprintf(pattern, sizeof(pattern), "\"%s\":\"0x", key);
  char* p = strstr(json, pattern);
  TEST_ASSERT_NOT_NULL_MESSAGE(p, pattern);
  p += strlen(pattern);
  *p = (*p == '1') ? '2' : '1';
}

/**
 * Runs the block-opcode contract twice in PAP mode (first run emits the block
 * request, second run uses the provided response) and asserts the result.
 *
 * @param file recorded block response used as RPC answer
 */
static void run_lazy_fetch_success(const char* file) {
  verify_ctx_t   ctx = {0};
  evm_call_ctx_t evm = {0};
  setup_pap_call(&ctx, &evm, CALL_ARGS_LATEST, BLOCK_OPCODES_CODE, sizeof(BLOCK_OPCODES_CODE));

  // first run: no header known -> eth_getBlockByNumber is requested
  TEST_ASSERT_EQUAL_INT(C4_PENDING, eth_run_call_evmone_with_events(&ctx, &evm, false));
  TEST_ASSERT_NULL(ctx.state.error);
  TEST_ASSERT_TRUE_MESSAGE(evm.block_ctx_used, "block context must be marked as used");
  TEST_ASSERT_NULL_MESSAGE(evm.block_header.data, "header must not be set while pending");
  data_request_t* req = expect_block_request(&ctx.state, BLOCK_RPC_LATEST);
  req->response       = read_testdata(file);
  TEST_ASSERT_NOT_NULL(req->response.data);

  // second run: header is built, hash checked and applied to the block context
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, eth_run_call_evmone_with_events(&ctx, &evm, false),
                                ctx.state.error ? ctx.state.error : "unexpected status");
  TEST_ASSERT_NULL(ctx.state.error);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_requests(&ctx.state), "the header must not be requested again");
  TEST_ASSERT_TRUE(evm.block_ctx_used);
  TEST_ASSERT_FALSE_MESSAGE(evm.block_header_verified, "an RPC header must be marked as unverified");
  TEST_ASSERT_NOT_NULL(evm.block_header.data);

  bytes_t   raw   = {0};
  json_t    block = read_block(file, &raw);
  bytes32_t expected_hash, header_hash;
  buffer_t  hb = stack_buffer(expected_hash);
  json_get_bytes(block, "hash", &hb);
  keccak(evm.block_header, header_hash);
  TEST_ASSERT_EQUAL_MEMORY_MESSAGE(expected_hash, header_hash, 32, "stored header must match the block hash");

  TEST_ASSERT_EQUAL_UINT32(192, evm.call_result.len);
  uint8_t* w = evm.call_result.data;
  assert_word_u64(w + 0x00, json_get_uint64(block, "number"), "NUMBER");
  assert_word_u64(w + 0x20, json_get_uint64(block, "timestamp"), "TIMESTAMP");
  assert_word_u64(w + 0x60, json_get_uint64(block, "gasLimit"), "GASLIMIT");

  uint8_t  tmp[32];
  buffer_t buf = stack_buffer(tmp);
  bytes_t  miner = json_get_bytes(block, "miner", &buf);
  TEST_ASSERT_EQUAL_UINT32(20, miner.len);
  assert_word(w + 0x40, miner.data, miner.len, "COINBASE");

  buf             = stack_buffer(tmp);
  bytes_t mixhash = json_get_bytes(block, "mixHash", &buf);
  TEST_ASSERT_EQUAL_UINT32(32, mixhash.len);
  assert_word(w + 0x80, mixhash.data, mixhash.len, "PREVRANDAO");

  safe_free(raw.data);
  evm_call_ctx_free(&evm);
  c4_state_free(&ctx.state);
}

/**
 * Electra block (has `requestsHash`): header rebuilt and context populated.
 */
void test_pap_lazy_block_header_electra(void) {
  run_lazy_fetch_success(ELECTRA_BLOCK_FILE);
}

/**
 * Deneb block (no `requestsHash`): fork detection must pick the Deneb layout,
 * otherwise the rebuilt header would not match the block hash.
 */
void test_pap_lazy_block_header_deneb(void) {
  run_lazy_fetch_success(DENEB_BLOCK_FILE);
}

/**
 * BASEFEE must reflect `baseFeePerGas` of the header used as block context.
 */
void test_pap_lazy_block_header_basefee(void) {
  verify_ctx_t   ctx = {0};
  evm_call_ctx_t evm = {0};
  setup_pap_call(&ctx, &evm, CALL_ARGS_LATEST, BLOCK_OPCODES_CODE, sizeof(BLOCK_OPCODES_CODE));
  TEST_ASSERT_EQUAL_INT(C4_PENDING, eth_run_call_evmone_with_events(&ctx, &evm, false));
  data_request_t* req = expect_block_request(&ctx.state, BLOCK_RPC_LATEST);
  req->response       = read_testdata(ELECTRA_BLOCK_FILE);
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eth_run_call_evmone_with_events(&ctx, &evm, false));
  TEST_ASSERT_EQUAL_UINT32(192, evm.call_result.len);

  bytes_t raw   = {0};
  json_t  block = read_block(ELECTRA_BLOCK_FILE, &raw);
  assert_word_u64(evm.call_result.data + 0xa0, json_get_uint64(block, "baseFeePerGas"), "BASEFEE");

  safe_free(raw.data);
  evm_call_ctx_free(&evm);
  c4_state_free(&ctx.state);
}

/**
 * The block tag of the call is forwarded to eth_getBlockByNumber; a missing
 * tag falls back to "latest".
 */
void test_pap_lazy_block_header_request_params(void) {
  verify_ctx_t   ctx = {0};
  evm_call_ctx_t evm = {0};

  setup_pap_call(&ctx, &evm,
                 "[{\"from\":\"0x2222222222222222222222222222222222222222\","
                 "\"to\":\"0x1111111111111111111111111111111111111111\"},\"0x1564967\"]",
                 BLOCK_OPCODES_CODE, sizeof(BLOCK_OPCODES_CODE));
  TEST_ASSERT_EQUAL_INT(C4_PENDING, eth_run_call_evmone_with_events(&ctx, &evm, false));
  expect_block_request(&ctx.state,
                       "{\"jsonrpc\":\"2.0\",\"method\":\"eth_getBlockByNumber\",\"params\":[\"0x1564967\",false],\"id\":1}");
  evm_call_ctx_free(&evm);
  c4_state_free(&ctx.state);

  setup_pap_call(&ctx, &evm,
                 "[{\"from\":\"0x2222222222222222222222222222222222222222\","
                 "\"to\":\"0x1111111111111111111111111111111111111111\"}]",
                 BLOCK_OPCODES_CODE, sizeof(BLOCK_OPCODES_CODE));
  TEST_ASSERT_EQUAL_INT(C4_PENDING, eth_run_call_evmone_with_events(&ctx, &evm, false));
  expect_block_request(&ctx.state, BLOCK_RPC_LATEST);
  evm_call_ctx_free(&evm);
  c4_state_free(&ctx.state);
}

/**
 * Runs the block-opcode contract against a manipulated block response and
 * expects the hash check to reject it.
 *
 * @param key JSON field to tamper with in the recorded response
 */
static void run_tampered_block(const char* key) {
  verify_ctx_t   ctx = {0};
  evm_call_ctx_t evm = {0};
  setup_pap_call(&ctx, &evm, CALL_ARGS_LATEST, BLOCK_OPCODES_CODE, sizeof(BLOCK_OPCODES_CODE));

  TEST_ASSERT_EQUAL_INT(C4_PENDING, eth_run_call_evmone_with_events(&ctx, &evm, false));
  data_request_t* req = expect_block_request(&ctx.state, BLOCK_RPC_LATEST);
  req->response       = read_testdata(ELECTRA_BLOCK_FILE);
  TEST_ASSERT_NOT_NULL(req->response.data);
  tamper_hex_field((char*) req->response.data, key);

  TEST_ASSERT_EQUAL_INT(C4_ERROR, eth_run_call_evmone_with_events(&ctx, &evm, false));
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx.state.error, "not matching its block hash"), ctx.state.error);
  TEST_ASSERT_NULL_MESSAGE(evm.block_header.data, "a rejected header must not be stored");
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_requests(&ctx.state), "no retry request expected");

  evm_call_ctx_free(&evm);
  c4_state_free(&ctx.state);
}

/**
 * A response whose `hash` does not match the rebuilt header is rejected.
 */
void test_pap_lazy_block_header_wrong_hash(void) {
  run_tampered_block("hash");
}

/**
 * A response with a manipulated header field (but original hash) is rejected.
 */
void test_pap_lazy_block_header_tampered_field(void) {
  run_tampered_block("timestamp");
}

/**
 * A `null` block (unknown tag) must yield an error, never a zero block context.
 */
void test_pap_lazy_block_header_null_result(void) {
  verify_ctx_t   ctx = {0};
  evm_call_ctx_t evm = {0};
  setup_pap_call(&ctx, &evm, CALL_ARGS_LATEST, BLOCK_OPCODES_CODE, sizeof(BLOCK_OPCODES_CODE));

  TEST_ASSERT_EQUAL_INT(C4_PENDING, eth_run_call_evmone_with_events(&ctx, &evm, false));
  data_request_t* req = expect_block_request(&ctx.state, BLOCK_RPC_LATEST);
  const char*     res = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":null}";
  req->response       = bytes_dup(bytes((uint8_t*) res, (uint32_t) strlen(res) + 1));

  TEST_ASSERT_EQUAL_INT(C4_ERROR, eth_run_call_evmone_with_events(&ctx, &evm, false));
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  TEST_ASSERT_NULL(evm.block_header.data);

  evm_call_ctx_free(&evm);
  c4_state_free(&ctx.state);
}

/**
 * A transport error on the header request must surface as an error.
 */
void test_pap_lazy_block_header_request_error(void) {
  verify_ctx_t   ctx = {0};
  evm_call_ctx_t evm = {0};
  setup_pap_call(&ctx, &evm, CALL_ARGS_LATEST, BLOCK_OPCODES_CODE, sizeof(BLOCK_OPCODES_CODE));

  TEST_ASSERT_EQUAL_INT(C4_PENDING, eth_run_call_evmone_with_events(&ctx, &evm, false));
  data_request_t* req = expect_block_request(&ctx.state, BLOCK_RPC_LATEST);
  req->error          = strdup("connection refused");

  TEST_ASSERT_EQUAL_INT(C4_ERROR, eth_run_call_evmone_with_events(&ctx, &evm, false));
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx.state.error, "connection refused"), ctx.state.error);
  TEST_ASSERT_NULL(evm.block_header.data);

  evm_call_ctx_free(&evm);
  c4_state_free(&ctx.state);
}

/**
 * Regression: a PAP call without block opcodes must not request the header.
 */
void test_pap_no_block_opcodes_no_header_request(void) {
  verify_ctx_t   ctx = {0};
  evm_call_ctx_t evm = {0};
  setup_pap_call(&ctx, &evm, CALL_ARGS_LATEST, NO_BLOCK_OPCODES_CODE, sizeof(NO_BLOCK_OPCODES_CODE));

  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, eth_run_call_evmone_with_events(&ctx, &evm, false),
                                ctx.state.error ? ctx.state.error : "unexpected status");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, count_requests(&ctx.state), "no request expected");
  TEST_ASSERT_FALSE(evm.block_ctx_used);
  TEST_ASSERT_NULL(evm.block_header.data);
  TEST_ASSERT_EQUAL_UINT32(32, evm.call_result.len);
  TEST_ASSERT_EQUAL_HEX8(0x2a, evm.call_result.data[31]);

  evm_call_ctx_free(&evm);
  c4_state_free(&ctx.state);
}

/**
 * A header already present in `evm->block_header` (e.g. verified by a previous
 * proofCall) takes precedence and suppresses the RPC request.
 */
void test_pap_known_block_header_no_request(void) {
  verify_ctx_t   ctx = {0};
  evm_call_ctx_t evm = {0};
  setup_pap_call(&ctx, &evm, CALL_ARGS_LATEST, BLOCK_OPCODES_CODE, sizeof(BLOCK_OPCODES_CODE));

  bytes_t raw   = {0};
  json_t  block = read_block(ELECTRA_BLOCK_FILE, &raw);
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eth_el_header_build_from_json(&ctx.state, &evm.block_header, C4_FORK_ELECTRA, block));
  evm.block_header_verified = true;

  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, eth_run_call_evmone_with_events(&ctx, &evm, false),
                                ctx.state.error ? ctx.state.error : "unexpected status");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, count_requests(&ctx.state), "no request expected for a known header");
  TEST_ASSERT_TRUE(evm.block_ctx_used);
  TEST_ASSERT_TRUE(evm.block_header_verified);
  TEST_ASSERT_EQUAL_UINT32(192, evm.call_result.len);
  assert_word_u64(evm.call_result.data, json_get_uint64(block, "number"), "NUMBER");
  assert_word_u64(evm.call_result.data + 0x20, json_get_uint64(block, "timestamp"), "TIMESTAMP");

  safe_free(raw.data);
  evm_call_ctx_free(&evm);
  c4_state_free(&ctx.state);
}

/**
 * Without PAP the block context stays at its defaults and nothing is requested.
 */
void test_non_pap_no_header_request(void) {
  verify_ctx_t   ctx = {0};
  evm_call_ctx_t evm = {0};
  setup_pap_call(&ctx, &evm, CALL_ARGS_LATEST, BLOCK_OPCODES_CODE, sizeof(BLOCK_OPCODES_CODE));
  ctx.flags    = 0;
  evm.pap_mode = false;

  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, eth_run_call_evmone_with_events(&ctx, &evm, false),
                                ctx.state.error ? ctx.state.error : "unexpected status");
  TEST_ASSERT_EQUAL_INT(0, count_requests(&ctx.state));
  TEST_ASSERT_NULL(evm.block_header.data);
  TEST_ASSERT_EQUAL_UINT32(192, evm.call_result.len);
  assert_word_u64(evm.call_result.data, 0, "NUMBER");

  evm_call_ctx_free(&evm);
  c4_state_free(&ctx.state);
}

/**
 * Regression on the full RPC pipeline: the recorded PAP balanceOf call does
 * not read the block context and must not emit an eth_getBlockByNumber request.
 */
void test_pap_cached_call_no_block_request(void) {
  const char* dirname = "eth_call_pap_cached";
  char        test_file[256];
  snprintf(test_file, sizeof(test_file), "%s/test.json", dirname);
  bytes_t test_content = read_testdata(test_file);
  TEST_ASSERT_NOT_NULL(test_content.data);
  json_t test     = json_parse((char*) test_content.data);
  char*  method   = bprintf(NULL, "%j", json_get(test, "method"));
  char*  args     = json_new_string(json_get(test, "params"));
  char*  expected = bprintf(NULL, "%J", json_get(test, "expected_result"));

  set_state(C4_CHAIN_MAINNET, (char*) dirname);
  c4_rpc_ctx_t* rpc_ctx = c4_rpc_ctx_create(method, args, C4_CHAIN_MAINNET, C4_PROVER_FLAG_INCLUDE_CODE,
                                            VERIFY_FLAG_PAP, C4_PROVER_MODE_REMOTE);
  c4_status_t   status  = C4_PENDING;
  while ((status = c4_rpc_execute(rpc_ctx)) == C4_PENDING) {
    data_request_t* req;
    while ((req = c4_state_get_pending_request(c4_rpc_get_state(rpc_ctx)))) {
      TEST_ASSERT_FALSE_MESSAGE(bytes_contains(req->payload, "eth_getBlockByNumber"),
                                "unexpected eth_getBlockByNumber request");
      char  tmp[1024];
      char* filename = c4_req_mockname(req);
      snprintf(tmp, sizeof(tmp), "%s/%s", dirname, filename);
      safe_free(filename);
      req->response = read_testdata(tmp);
      TEST_ASSERT_NOT_NULL_MESSAGE(req->response.data, tmp);
    }
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, status,
                                rpc_ctx->error ? rpc_ctx->error : (rpc_ctx->verifier.state.error ? rpc_ctx->verifier.state.error : "error"));

  char* result     = ssz_dump_to_str(rpc_ctx->verifier.data, false, true);
  char* norm_res   = normalize_newlines(result);
  char* norm_exp   = normalize_newlines(expected);
  TEST_ASSERT_EQUAL_STRING(norm_exp, norm_res);

  safe_free(result);
  safe_free(norm_res);
  safe_free(norm_exp);
  c4_rpc_ctx_free(rpc_ctx);
  safe_free(method);
  safe_free(args);
  safe_free(expected);
  safe_free(test_content.data);
}

#else

void test_evmone_pap_block_header_skipped(void) {
  TEST_IGNORE_MESSAGE("EVMONE or PAP disabled");
}

#endif

void setUp(void) { reset_local_filecache(); }
void tearDown(void) { reset_local_filecache(); }

int main(void) {
  UNITY_BEGIN();
#if defined(EVMONE) && defined(PAP)
  RUN_TEST(test_pap_lazy_block_header_electra);
  RUN_TEST(test_pap_lazy_block_header_deneb);
  RUN_TEST(test_pap_lazy_block_header_basefee);
  RUN_TEST(test_pap_lazy_block_header_request_params);
  RUN_TEST(test_pap_lazy_block_header_wrong_hash);
  RUN_TEST(test_pap_lazy_block_header_tampered_field);
  RUN_TEST(test_pap_lazy_block_header_null_result);
  RUN_TEST(test_pap_lazy_block_header_request_error);
  RUN_TEST(test_pap_no_block_opcodes_no_header_request);
  RUN_TEST(test_pap_known_block_header_no_request);
  RUN_TEST(test_non_pap_no_header_request);
  RUN_TEST(test_pap_cached_call_no_block_request);
#else
  RUN_TEST(test_evmone_pap_block_header_skipped);
#endif
  return UNITY_END();
}
