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
// request an `eth_getBlockHeader(<tag>)` proof from the prover, verify it via
// `c4_verify_block` and populate NUMBER / TIMESTAMP / COINBASE / GASLIMIT /
// PREVRANDAO / BASEFEE from the verified header.
//
// The prover response is an `eth_getBlockHeader ["latest"]` C4Request (mainnet)
// created in-process by the local prover from the recorded beacon data in
// `eth_getBlockHeader1/` (the recorded `proof.ssz` there uses an outdated SSZ
// layout). The sync committee state of that directory is loaded into the file
// cache so the clProof verifies offline.

#include "bytes.h"
#include "c4_assert.h"
#include "call_ctx.h"
#include "chains.h"
#include "crypto.h"
#include "el_header.h"
#include "eth_call_account.h"
#ifdef EL_HEADER_CACHE
#include "../../src/chains/eth/verifier/header_cache.h"
#endif
#include "json.h"
#include "unity.h"
#include "verify.h"
#include <string.h>

#if defined(EVMONE) && defined(PAP)

#define HEADER_DIR         "eth_getBlockHeader1"
#define HEADER_PROOF_FILE  HEADER_DIR "/proof.ssz" // marker only: the proof is generated, see create_header_proof()
#define HEADER_REQ_LATEST  "{\"method\":\"eth_getBlockHeader\",\"params\":[\"latest\"]}"
#define ELECTRA_BLOCK_FILE "eth_getLogs_electra/eth_getBlockByNumber_0x1564967_false.json"
#define CALL_PROOF_FILE    "eth_call_pap_cached/colibri_proofCall___accessList_____address___0xdac17f958d2ee523a2206206994597c13d831ec7___storageKey.ssz"

// expected values of the header in HEADER_PROOF_FILE (see eth_getBlockHeader1/test.json)
#define HEADER_NUMBER    0x175d764ULL
#define HEADER_TIMESTAMP 0x6998b1c3ULL
#define HEADER_GASLIMIT  0x3938700ULL
#define HEADER_BASEFEE   0x2fbf493ULL
#define HEADER_COINBASE  "4838b106fce9647bdf1e7877bf73ce8b0bad5f97"
#define HEADER_HASH      "8172dcd8215f02003cd251864652a00bf9b011861a15324ac6b48281a912253f"

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
 * Prepares a PAP verification context and an EVM call context for `code` and
 * loads the sync committee state needed to verify the header proof.
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
  set_state(C4_CHAIN_MAINNET, HEADER_DIR);
}

/**
 * Counts all requests (pending or answered) of the given type.
 *
 * @param state state to inspect
 * @param type request type to count
 * @return number of matching requests
 */
static int count_requests_of_type(c4_state_t* state, data_request_type_t type) {
  int n = 0;
  for (data_request_t* r = state->requests; r; r = r->next)
    if (r->type == type) n++;
  return n;
}

/**
 * Returns the single pending prover request and asserts its payload.
 *
 * @param state state holding the request
 * @param expected_payload expected JSON payload
 * @return the pending prover request
 */
static data_request_t* expect_header_request(c4_state_t* state, const char* expected_payload) {
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_requests_of_type(state, C4_DATA_TYPE_PROVER), "expected exactly one prover request");
  data_request_t* req = NULL;
  for (data_request_t* r = state->requests; r; r = r->next)
    if (r->type == C4_DATA_TYPE_PROVER) req = r;
  TEST_ASSERT_NOT_NULL(req);
  TEST_ASSERT_NULL_MESSAGE(req->response.data, "prover request must still be pending");
  TEST_ASSERT_EQUAL_INT(C4_DATA_METHOD_POST, req->method);
  TEST_ASSERT_EQUAL_INT(C4_DATA_ENCODING_SSZ, req->encoding);
  TEST_ASSERT_NOT_NULL(req->payload.data);
  TEST_ASSERT_EQUAL_UINT32(strlen(expected_payload), req->payload.len);
  TEST_ASSERT_EQUAL_STRING_LEN(expected_payload, (char*) req->payload.data, req->payload.len);

  bytes32_t id = {0};
  keccak(bytes((uint8_t*) expected_payload, (uint32_t) strlen(expected_payload)), id);
  TEST_ASSERT_EQUAL_MEMORY_MESSAGE(id, req->id, 32, "request id must be keccak(payload)");
  return req;
}

/**
 * Creates a fresh `eth_getBlockHeader ["latest"]` proof with the local prover
 * from the recorded beacon data in HEADER_DIR, so the proof always matches the
 * current SSZ layout.
 *
 * @return heap-allocated C4Request bytes (caller frees)
 */
static bytes_t create_header_proof(void) {
#ifdef PROVER_CACHE
  c4_prover_cache_cleanup(0xffffffffffffffffULL, 0);
#endif
  prover_ctx_t* prover = c4_prover_create("eth_getBlockHeader", "[\"latest\"]", C4_CHAIN_MAINNET, 0);
  TEST_ASSERT_NOT_NULL(prover);
  c4_status_t status;
  while ((status = c4_prover_execute(prover)) == C4_PENDING) {
    data_request_t* req;
    while ((req = c4_state_get_pending_request(&prover->state))) {
      char  tmp[1024];
      char* filename = c4_req_mockname(req);
      snprintf(tmp, sizeof(tmp), "%s/%s", HEADER_DIR, filename);
      safe_free(filename);
      req->response = read_testdata(tmp);
      TEST_ASSERT_NOT_NULL_MESSAGE(req->response.data, tmp);
    }
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, status, prover->state.error ? prover->state.error : "prover failed");
  bytes_t proof = bytes_dup(prover->proof);
  c4_prover_free(prover);
  return proof;
}

/**
 * Returns the prover response for `proof_file`; HEADER_PROOF_FILE is generated.
 *
 * @param proof_file test data file or HEADER_PROOF_FILE
 * @return heap-allocated response bytes (ownership goes to the request)
 */
static bytes_t load_prover_response(const char* proof_file) {
  if (strcmp(proof_file, HEADER_PROOF_FILE) == 0) return create_header_proof();
  bytes_t data = read_testdata(proof_file);
  TEST_ASSERT_NOT_NULL_MESSAGE(data.data, proof_file);
  return data;
}

/**
 * Answers all pending requests: prover requests with `proof_file`, any other
 * request (e.g. beacon data needed for sync) from the recorded HEADER_DIR.
 *
 * @param state state with pending requests
 * @param proof_file prover response (HEADER_PROOF_FILE or a test data file)
 */
static void serve_pending(c4_state_t* state, const char* proof_file) {
  data_request_t* req;
  while ((req = c4_state_get_pending_request(state))) {
    if (req->type == C4_DATA_TYPE_PROVER) {
      req->response = load_prover_response(proof_file);
      continue;
    }
    char  tmp[1024];
    char* filename = c4_req_mockname(req);
    snprintf(tmp, sizeof(tmp), "%s/%s", HEADER_DIR, filename);
    safe_free(filename);
    req->response = read_testdata(tmp);
    TEST_ASSERT_NOT_NULL_MESSAGE(req->response.data, bprintf(NULL, "Did not find the testdata: %s", tmp));
  }
}

/**
 * Runs the EVM, answering pending requests (also those left by a previous run)
 * until the run is no longer pending.
 *
 * @param ctx verification context
 * @param evm call context
 * @param proof_file prover response (HEADER_PROOF_FILE or a test data file)
 * @return final status of the EVM run
 */
static c4_status_t run_until_done(verify_ctx_t* ctx, evm_call_ctx_t* evm, const char* proof_file) {
  for (int i = 0; i < 10; i++) {
    serve_pending(&ctx->state, proof_file);
    c4_status_t status = eth_run_call_evmone_with_events(ctx, evm, false);
    if (status != C4_PENDING) return status;
  }
  TEST_FAIL_MESSAGE("EVM run still pending after 10 rounds");
  return C4_ERROR;
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
 * Returns the single prover request of the state (pending or answered).
 *
 * @param state state to inspect
 * @return the prover request or NULL
 */
static data_request_t* find_prover_request(c4_state_t* state) {
  for (data_request_t* r = state->requests; r; r = r->next)
    if (r->type == C4_DATA_TYPE_PROVER) return r;
  return NULL;
}

/**
 * Success path: the first run emits exactly one eth_getBlockHeader prover
 * request, the second run verifies the proof, uses the header as block context
 * and borrows it from the response buffer.
 */
void test_pap_lazy_block_header_success(void) {
  verify_ctx_t   ctx = {0};
  evm_call_ctx_t evm = {0};
  setup_pap_call(&ctx, &evm, CALL_ARGS_LATEST, BLOCK_OPCODES_CODE, sizeof(BLOCK_OPCODES_CODE));

  // first run: no header known -> eth_getBlockHeader proof is requested
  TEST_ASSERT_EQUAL_INT(C4_PENDING, eth_run_call_evmone_with_events(&ctx, &evm, false));
  TEST_ASSERT_NULL(ctx.state.error);
  TEST_ASSERT_TRUE_MESSAGE(evm.block_ctx_used, "block context must be marked as used");
  TEST_ASSERT_NULL_MESSAGE(evm.block_header.data, "header must not be set while pending");
  expect_header_request(&ctx.state, HEADER_REQ_LATEST);
  serve_pending(&ctx.state, HEADER_PROOF_FILE);

  // following runs: proof verified, header applied to the block context
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, run_until_done(&ctx, &evm, HEADER_PROOF_FILE),
                                ctx.state.error ? ctx.state.error : "unexpected status");
  TEST_ASSERT_NULL(ctx.state.error);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_requests_of_type(&ctx.state, C4_DATA_TYPE_PROVER), "the header must not be requested again");
  TEST_ASSERT_TRUE(evm.block_ctx_used);
  TEST_ASSERT_NOT_NULL(evm.block_header.data);

  // the header is borrowed from the prover response, not copied
  data_request_t* req = find_prover_request(&ctx.state);
  TEST_ASSERT_NOT_NULL(req);
  TEST_ASSERT_TRUE_MESSAGE(evm.block_header.data >= req->response.data &&
                               evm.block_header.data + evm.block_header.len <= req->response.data + req->response.len,
                           "block_header must point into the prover response");

  bytes32_t header_hash;
  keccak(evm.block_header, header_hash);
  ASSERT_HEX_STRING_EQUAL(HEADER_HASH, header_hash, 32, "header hash");

  TEST_ASSERT_EQUAL_UINT32(192, evm.call_result.len);
  uint8_t* w = evm.call_result.data;
  assert_word_u64(w + 0x00, HEADER_NUMBER, "NUMBER");
  assert_word_u64(w + 0x20, HEADER_TIMESTAMP, "TIMESTAMP");
  uint8_t coinbase[20];
  hex_to_bytes(HEADER_COINBASE, -1, bytes(coinbase, 20));
  assert_word(w + 0x40, coinbase, 20, "COINBASE");
  assert_word_u64(w + 0x60, HEADER_GASLIMIT, "GASLIMIT");
  bytes_t randao = eth_el_header_get(evm.block_header, EL_PREV_RANDAO);
  TEST_ASSERT_EQUAL_UINT32(32, randao.len);
  assert_word(w + 0x80, randao.data, 32, "PREVRANDAO");
  assert_word_u64(w + 0xa0, HEADER_BASEFEE, "BASEFEE");

  evm_call_ctx_free(&evm);
  c4_state_free(&ctx.state);
}

/**
 * BASEFEE must reflect the minimal-length RLP `baseFeePerGas` of the header.
 */
void test_pap_lazy_block_header_basefee(void) {
  verify_ctx_t   ctx = {0};
  evm_call_ctx_t evm = {0};
  setup_pap_call(&ctx, &evm, CALL_ARGS_LATEST, BLOCK_OPCODES_CODE, sizeof(BLOCK_OPCODES_CODE));
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, run_until_done(&ctx, &evm, HEADER_PROOF_FILE),
                                ctx.state.error ? ctx.state.error : "unexpected status");
  TEST_ASSERT_EQUAL_UINT32(192, evm.call_result.len);
  TEST_ASSERT_TRUE(eth_el_header_get(evm.block_header, EL_BASE_FEE_PER_GAS).len < 32);
  assert_word_u64(evm.call_result.data + 0xa0, HEADER_BASEFEE, "BASEFEE");

  evm_call_ctx_free(&evm);
  c4_state_free(&ctx.state);
}

/**
 * The block tag of the call is forwarded to eth_getBlockHeader; a missing
 * tag falls back to "latest".
 */
void test_pap_lazy_block_header_request_params(void) {
  verify_ctx_t   ctx = {0};
  evm_call_ctx_t evm = {0};

  setup_pap_call(&ctx, &evm,
                 "[{\"from\":\"0x2222222222222222222222222222222222222222\","
                 "\"to\":\"0x1111111111111111111111111111111111111111\"},\"0x175d764\"]",
                 BLOCK_OPCODES_CODE, sizeof(BLOCK_OPCODES_CODE));
  TEST_ASSERT_EQUAL_INT(C4_PENDING, eth_run_call_evmone_with_events(&ctx, &evm, false));
  expect_header_request(&ctx.state, "{\"method\":\"eth_getBlockHeader\",\"params\":[\"0x175d764\"]}");
  evm_call_ctx_free(&evm);
  c4_state_free(&ctx.state);

  setup_pap_call(&ctx, &evm,
                 "[{\"from\":\"0x2222222222222222222222222222222222222222\","
                 "\"to\":\"0x1111111111111111111111111111111111111111\"}]",
                 BLOCK_OPCODES_CODE, sizeof(BLOCK_OPCODES_CODE));
  TEST_ASSERT_EQUAL_INT(C4_PENDING, eth_run_call_evmone_with_events(&ctx, &evm, false));
  expect_header_request(&ctx.state, HEADER_REQ_LATEST);
  evm_call_ctx_free(&evm);
  c4_state_free(&ctx.state);
}

/**
 * A concrete block tag matching the proven header is accepted.
 */
void test_pap_lazy_block_header_matching_number(void) {
  verify_ctx_t   ctx = {0};
  evm_call_ctx_t evm = {0};
  setup_pap_call(&ctx, &evm,
                 "[{\"from\":\"0x2222222222222222222222222222222222222222\","
                 "\"to\":\"0x1111111111111111111111111111111111111111\"},\"0x175d764\"]",
                 BLOCK_OPCODES_CODE, sizeof(BLOCK_OPCODES_CODE));
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, run_until_done(&ctx, &evm, HEADER_PROOF_FILE),
                                ctx.state.error ? ctx.state.error : "unexpected status");
  assert_word_u64(evm.call_result.data, HEADER_NUMBER, "NUMBER");
  evm_call_ctx_free(&evm);
  c4_state_free(&ctx.state);
}

/**
 * A proof for another block than the requested concrete tag is rejected.
 */
void test_pap_lazy_block_header_number_mismatch(void) {
  verify_ctx_t   ctx = {0};
  evm_call_ctx_t evm = {0};
  setup_pap_call(&ctx, &evm,
                 "[{\"from\":\"0x2222222222222222222222222222222222222222\","
                 "\"to\":\"0x1111111111111111111111111111111111111111\"},\"0x1\"]",
                 BLOCK_OPCODES_CODE, sizeof(BLOCK_OPCODES_CODE));

  TEST_ASSERT_EQUAL_INT(C4_PENDING, eth_run_call_evmone_with_events(&ctx, &evm, false));
  expect_header_request(&ctx.state, "{\"method\":\"eth_getBlockHeader\",\"params\":[\"0x1\"]}");
  TEST_ASSERT_EQUAL_INT(C4_ERROR, run_until_done(&ctx, &evm, HEADER_PROOF_FILE));
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx.state.error, "different block than requested"), ctx.state.error);
  TEST_ASSERT_NULL_MESSAGE(evm.block_header.data, "a rejected header must not be used");

  evm_call_ctx_free(&evm);
  c4_state_free(&ctx.state);
}

/**
 * For `latest` the freshness lower bound of the host is enforced.
 */
void test_pap_lazy_block_header_latest_too_old(void) {
  verify_ctx_t   ctx = {0};
  evm_call_ctx_t evm = {0};
  setup_pap_call(&ctx, &evm, CALL_ARGS_LATEST, BLOCK_OPCODES_CODE, sizeof(BLOCK_OPCODES_CODE));
  ctx.min_latest_block_ts = HEADER_TIMESTAMP + 1;

  TEST_ASSERT_EQUAL_INT(C4_ERROR, run_until_done(&ctx, &evm, HEADER_PROOF_FILE));
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx.state.error, "proof for latest too old"), ctx.state.error);
  TEST_ASSERT_NULL(evm.block_header.data);

  evm_call_ctx_free(&evm);
  c4_state_free(&ctx.state);
}

/**
 * A prover response carrying another proof type (here a call proof) is rejected.
 */
void test_pap_lazy_block_header_wrong_proof_type(void) {
  verify_ctx_t   ctx = {0};
  evm_call_ctx_t evm = {0};
  setup_pap_call(&ctx, &evm, CALL_ARGS_LATEST, BLOCK_OPCODES_CODE, sizeof(BLOCK_OPCODES_CODE));

  TEST_ASSERT_EQUAL_INT(C4_PENDING, eth_run_call_evmone_with_events(&ctx, &evm, false));
  expect_header_request(&ctx.state, HEADER_REQ_LATEST);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, run_until_done(&ctx, &evm, CALL_PROOF_FILE));
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx.state.error, "unexpected proof type"), ctx.state.error);
  TEST_ASSERT_NULL(evm.block_header.data);

  evm_call_ctx_free(&evm);
  c4_state_free(&ctx.state);
}

/**
 * A transport error on the prover request must surface as an error.
 */
void test_pap_lazy_block_header_request_error(void) {
  verify_ctx_t   ctx = {0};
  evm_call_ctx_t evm = {0};
  setup_pap_call(&ctx, &evm, CALL_ARGS_LATEST, BLOCK_OPCODES_CODE, sizeof(BLOCK_OPCODES_CODE));

  TEST_ASSERT_EQUAL_INT(C4_PENDING, eth_run_call_evmone_with_events(&ctx, &evm, false));
  data_request_t* req = expect_header_request(&ctx.state, HEADER_REQ_LATEST);
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
  TEST_ASSERT_NULL_MESSAGE(ctx.state.requests, "no request expected");
  TEST_ASSERT_FALSE(evm.block_ctx_used);
  TEST_ASSERT_NULL(evm.block_header.data);
  TEST_ASSERT_EQUAL_UINT32(32, evm.call_result.len);
  TEST_ASSERT_EQUAL_HEX8(0x2a, evm.call_result.data[31]);

  evm_call_ctx_free(&evm);
  c4_state_free(&ctx.state);
}

/**
 * A header already present in `evm->block_header` (e.g. taken from a previous
 * proofCall) takes precedence and suppresses the prover request.
 */
void test_pap_known_block_header_no_request(void) {
  verify_ctx_t   ctx = {0};
  evm_call_ctx_t evm = {0};
  setup_pap_call(&ctx, &evm, CALL_ARGS_LATEST, BLOCK_OPCODES_CODE, sizeof(BLOCK_OPCODES_CODE));

  bytes_t raw = read_testdata(ELECTRA_BLOCK_FILE);
  TEST_ASSERT_NOT_NULL(raw.data);
  json_t  block  = json_get(json_parse((char*) raw.data), "result");
  bytes_t header = {0};
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eth_el_header_build_from_json(&ctx.state, &header, C4_FORK_ELECTRA, block));
  evm.block_header = header; // borrowed by the evm, owned by the test

  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, eth_run_call_evmone_with_events(&ctx, &evm, false),
                                ctx.state.error ? ctx.state.error : "unexpected status");
  TEST_ASSERT_NULL_MESSAGE(ctx.state.requests, "no request expected for a known header");
  TEST_ASSERT_TRUE(evm.block_ctx_used);
  TEST_ASSERT_TRUE(evm.block_header.data == header.data);
  TEST_ASSERT_EQUAL_UINT32(192, evm.call_result.len);
  assert_word_u64(evm.call_result.data, json_get_uint64(block, "number"), "NUMBER");
  assert_word_u64(evm.call_result.data + 0x20, json_get_uint64(block, "timestamp"), "TIMESTAMP");
  assert_word_u64(evm.call_result.data + 0xa0, json_get_uint64(block, "baseFeePerGas"), "BASEFEE");

  evm_call_ctx_free(&evm);
  c4_state_free(&ctx.state);
  safe_free(header.data);
  safe_free(raw.data);
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
  TEST_ASSERT_NULL(ctx.state.requests);
  TEST_ASSERT_NULL(evm.block_header.data);
  TEST_ASSERT_EQUAL_UINT32(192, evm.call_result.len);
  assert_word_u64(evm.call_result.data, 0, "NUMBER");

  evm_call_ctx_free(&evm);
  c4_state_free(&ctx.state);
}

/**
 * Regression on the full RPC pipeline: the recorded PAP balanceOf call does
 * not read the block context and must not request a block header.
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
      TEST_ASSERT_FALSE_MESSAGE(bytes_contains(req->payload, "eth_getBlockHeader"), "unexpected eth_getBlockHeader request");
      TEST_ASSERT_FALSE_MESSAGE(bytes_contains(req->payload, "eth_getBlockByNumber"), "unexpected eth_getBlockByNumber request");
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

  char* result   = ssz_dump_to_str(rpc_ctx->verifier.data, false, true);
  char* norm_res = normalize_newlines(result);
  char* norm_exp = normalize_newlines(expected);
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

void setUp(void) {
  reset_local_filecache();
#ifdef EL_HEADER_CACHE
  c4_header_cache_clear();
#endif
}

void tearDown(void) {
  reset_local_filecache();
#ifdef EL_HEADER_CACHE
  c4_header_cache_clear();
#endif
}

int main(void) {
  UNITY_BEGIN();
#if defined(EVMONE) && defined(PAP)
  RUN_TEST(test_pap_lazy_block_header_success);
  RUN_TEST(test_pap_lazy_block_header_basefee);
  RUN_TEST(test_pap_lazy_block_header_request_params);
  RUN_TEST(test_pap_lazy_block_header_matching_number);
  RUN_TEST(test_pap_lazy_block_header_number_mismatch);
  RUN_TEST(test_pap_lazy_block_header_latest_too_old);
  RUN_TEST(test_pap_lazy_block_header_wrong_proof_type);
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
