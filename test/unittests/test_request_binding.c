/*
 * Copyright (c) 2025,2026 corpus.core GmbH
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

// A proof is only an answer to the request it was built for. These tests build a proof with
// the prover from a recorded fixture and then hand it to the verifier together with another
// request: another block, another storage slot or another method. Every such combination
// has to be rejected, while the request the proof was built for keeps verifying.

#include "bytes.h"
#include "c4_assert.h"
#include "el_header.h"
#include "ssz.h"
#include "unity.h"

#define MAINNET C4_CHAIN_MAINNET

#define STORAGE_DIR     "eth_getStorageAt1"
#define STORAGE_ADDRESS "0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48"
#define STORAGE_SLOT    "0x7050c9e0f4ca769c69bd3a8ef740bc37934f8e2c036e5a723fd8ee048ed3f8c3"
#define STORAGE_OTHER   "0x7050c9e0f4ca769c69bd3a8ef740bc37934f8e2c036e5a723fd8ee048ed3f8c4"
#define STORAGE_VALUE   "\"0x00000000000000000000000043506849d7c04f9138d1a2050bbf3a0c054402dd\""
#define STORAGE_ARGS    "[\"" STORAGE_ADDRESS "\",\"" STORAGE_SLOT "\",\"latest\"]"

#define BALANCE_DIR     "eth_getBalance1"
#define BALANCE_ADDRESS "0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5"
#define BALANCE_ARGS    "[\"" BALANCE_ADDRESS "\",\"latest\"]"

#define CALL_DIR  "eth_call1"
#define CALL_TX   "{\"to\":\"0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48\",\"data\":\"0x70a0823100000000000000000000000037305b1cd40574e4c5ce33f8e8306be057fd7341\"}"
#define CALL_ARGS "[" CALL_TX ",\"latest\"]"

#define RECEIPTS_DIR  "eth_getBlockReceipts1"
#define RECEIPTS_ARGS "[\"latest\"]"

#define OTHER_HASH "0x1111111111111111111111111111111111111111111111111111111111111111"

void setUp(void) {
  reset_local_filecache();
}

void tearDown(void) {
  reset_local_filecache();
}

// Answers all pending requests of a state from the files of a fixture.
static void answer_from(c4_state_t* state, const char* dirname) {
  data_request_t* req;
  while ((req = c4_state_get_pending_request(state))) {
    char  tmp[1024];
    char* filename = c4_req_mockname(req);
    snprintf(tmp, sizeof(tmp), "%s/%s", dirname, filename);
    safe_free(filename);
    bytes_t content = read_testdata(tmp);
    TEST_ASSERT_NOT_NULL_MESSAGE(content.data, tmp);
    req->response = content;
  }
}

// Runs the prover with the given flags against a fixture and returns the serialized request
// (owned by the caller).
static bytes_t prove(const char* dirname, const char* method, const char* args, prover_flags_t flags) {
#ifdef PROVER_CACHE
  // a block cached as `latest` by an earlier run must not leak into this fixture
  c4_prover_cache_cleanup(0xffffffffffffffffULL, 0);
#endif
  c4_status_t   status = C4_PENDING;
  prover_ctx_t* ctx    = c4_prover_create((char*) method, (char*) args, MAINNET, flags);
  for (int i = 0; i < 50 && status == C4_PENDING; i++) {
    status = c4_prover_execute(ctx);
    if (status == C4_PENDING) answer_from(&ctx->state, dirname);
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, status, ctx->state.error ? ctx->state.error : "prover did not finish");
  bytes_t request = bytes_dup(ctx->proof);
  c4_prover_free(ctx);
  return request;
}

// Verifies a serialized request for the given method and arguments against the sync state of
// a fixture, with the flags a host passes. `result` receives the verified data as JSON,
// `error` the error message; both are owned by the caller.
static c4_status_t verify_request_with(const char* dirname, bytes_t request, const char* method, const char* args, verify_flags_t flags, char** result, char** error) {
  verify_ctx_t ctx = {0};
  reset_local_filecache();
  set_state(MAINNET, (char*) dirname);
  c4_status_t status = c4_verify_init(&ctx, request, (char*) method, json_parse((char*) args), MAINNET, flags);
  if (status == C4_SUCCESS) status = c4_verify(&ctx);
  for (int i = 0; i < 50 && status == C4_PENDING; i++) {
    answer_from(&ctx.state, dirname);
    status = c4_verify(&ctx);
  }
  if (result) *result = status == C4_SUCCESS ? ssz_dump_to_str(ctx.data, false, true) : NULL;
  if (error) *error = ctx.state.error ? strdup(ctx.state.error) : NULL;
  c4_verify_free_data(&ctx);
  return status;
}

// Verifies a serialized request without any host flags.
static c4_status_t verify_request(const char* dirname, bytes_t request, const char* method, const char* args, char** result, char** error) {
  return verify_request_with(dirname, request, method, args, 0, result, error);
}

// Asserts that the request is rejected for the given method and arguments, and that it is
// rejected for the expected reason: `reason` has to be part of the error message.
static void assert_rejected(const char* dirname, bytes_t request, const char* method, const char* args, const char* what, const char* reason) {
  char*       result = NULL;
  char*       error  = NULL;
  c4_status_t status = verify_request(dirname, request, method, args, &result, &error);
  char*       msg    = bprintf(NULL, "%s: status %d with result %s", what, (int) status, result ? result : "-");
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_ERROR, status, msg);
  TEST_ASSERT_NOT_NULL_MESSAGE(error, what);
  safe_free(msg);
  msg = bprintf(NULL, "%s: rejected with '%s' instead of '%s'", what, error, reason);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error, reason), msg);
  safe_free(msg);
  safe_free(result);
  safe_free(error);
}

// Asserts that the request verifies and returns the verified data as JSON (owned by the caller).
static char* assert_accepted(const char* dirname, bytes_t request, const char* method, const char* args, const char* what) {
  char*       result = NULL;
  char*       error  = NULL;
  c4_status_t status = verify_request(dirname, request, method, args, &result, &error);
  char*       msg    = bprintf(NULL, "%s: %s", what, error ? error : "no error");
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, status, msg);
  safe_free(msg);
  safe_free(error);
  return result;
}

// The RLP execution header a proof with a `clProof` commits to.
static bytes_t proven_header(bytes_t request) {
  ssz_ob_t req      = {.bytes = request, .def = c4_get_request_type(C4_CHAIN_TYPE_ETHEREUM)};
  ssz_ob_t proof    = ssz_get(&req, "proof");
  ssz_ob_t el_proof = ssz_get(&proof, "elProof");
  TEST_ASSERT_NOT_NULL(el_proof.def);
  TEST_ASSERT_EQUAL_STRING("clProof", el_proof.def->name);
  return ssz_get(&el_proof, "elHeader").bytes;
}

// The raw bytes (selector included) of one of the three unions of a request.
static bytes_t request_union(bytes_t request, int index) {
  uint32_t start = uint32_from_le(request.data + 4 + index * 4);
  uint32_t end   = index == 2 ? request.len : uint32_from_le(request.data + 8 + index * 4);
  TEST_ASSERT_TRUE(start <= end && end <= request.len);
  return bytes(request.data + start, end - start);
}

// Copy of a request with the given raw unions (selector included) for `data` and `proof`.
static bytes_t rebuild_request(bytes_t request, bytes_t data, bytes_t proof) {
  ssz_builder_t builder = ssz_builder_for_def(c4_get_request_type(C4_CHAIN_TYPE_ETHEREUM));
  ssz_add_bytes(&builder, "version", bytes(request.data, 4));
  ssz_add_bytes(&builder, "data", data);
  ssz_add_bytes(&builder, "proof", proof);
  ssz_add_bytes(&builder, "sync_data", request_union(request, 2));
  return ssz_builder_to_bytes(&builder).bytes;
}

// Copy of a request with its `data` replaced by a 32 byte value chosen by the prover.
static bytes_t with_forged_data(bytes_t request, uint8_t selector) {
  uint8_t forged[33];
  memset(forged, 0x42, sizeof(forged));
  forged[0] = selector;
  return rebuild_request(request, bytes(forged, 33), request_union(request, 1));
}

// Copy of a request without any data.
static bytes_t without_data(bytes_t request) {
  uint8_t none[1] = {0};
  return rebuild_request(request, bytes(none, 1), request_union(request, 1));
}

// Copy of the first hex string following `key` in a JSON text (owned by the caller).
static char* first_hex_after(const char* json, const char* key) {
  const char* pos = strstr(json, key);
  TEST_ASSERT_NOT_NULL_MESSAGE(pos, key);
  pos = strstr(pos, "\"0x");
  TEST_ASSERT_NOT_NULL_MESSAGE(pos, key);
  const char* end = strchr(pos + 1, '"');
  TEST_ASSERT_NOT_NULL_MESSAGE(end, key);
  // no strndup: it is not part of the C runtime of MSVC
  size_t len  = (size_t) (end - pos - 1);
  char*  copy = safe_malloc(len + 1);
  memcpy(copy, pos + 1, len);
  copy[len] = '\0';
  return copy;
}

// Copy of a call request whose `elProof` is replaced by the one of another request, so the
// accounts are proven against the state of one block while the proof commits to another.
static bytes_t with_el_proof_of(bytes_t request, bytes_t donor) {
  const ssz_def_t* req_def     = c4_get_request_type(C4_CHAIN_TYPE_ETHEREUM);
  ssz_ob_t         req         = {.bytes = request, .def = req_def};
  ssz_ob_t         donor_req   = {.bytes = donor, .def = req_def};
  ssz_ob_t         proof       = ssz_get(&req, "proof");
  ssz_ob_t         donor_proof = ssz_get(&donor_req, "proof");
  ssz_ob_t         el_proof    = ssz_get(&donor_proof, "elProof");
  ssz_builder_t    call        = ssz_builder_for_def(proof.def);
  ssz_builder_t    builder     = ssz_builder_for_def(req_def);
  buffer_t         call_union  = {0};
  TEST_ASSERT_EQUAL_STRING("CallProof", proof.def->name);
  ssz_add_bytes(&call, "accounts", ssz_get(&proof, "accounts").bytes);
  ssz_add_bytes(&call, "elProof", bytes(el_proof.bytes.data - 1, el_proof.bytes.len + 1));
  ssz_ob_t call_ob = ssz_builder_to_bytes(&call);
  buffer_append(&call_union, bytes(request_union(request, 1).data, 1)); // selector of the call proof
  buffer_append(&call_union, call_ob.bytes);
  ssz_add_bytes(&builder, "version", bytes(request.data, 4));
  ssz_add_bytes(&builder, "data", request_union(request, 0));
  ssz_add_bytes(&builder, "proof", call_union.data);
  ssz_add_bytes(&builder, "sync_data", request_union(donor, 2));
  safe_free(call_ob.bytes.data);
  buffer_free(&call_union);
  return ssz_builder_to_bytes(&builder).bytes;
}

// The proof of a storage slot answers the block it was built for, by tag, number or hash.
void test_storage_proof_for_the_requested_block(void) {
  bytes_t   request    = prove(STORAGE_DIR, "eth_getStorageAt", STORAGE_ARGS, 0);
  bytes_t   header     = proven_header(request);
  uint64_t  number     = eth_el_header_get_uint64(header, EL_BLOCK_NUMBER);
  bytes32_t block_hash = {0};
  keccak(header, block_hash);

  char* by_tag    = assert_accepted(STORAGE_DIR, request, "eth_getStorageAt", STORAGE_ARGS, "latest");
  char* args      = bprintf(NULL, "[\"" STORAGE_ADDRESS "\",\"" STORAGE_SLOT "\",\"0x%lx\"]", number);
  char* by_number = assert_accepted(STORAGE_DIR, request, "eth_getStorageAt", args, "block number of the proof");
  safe_free(args);
  args          = bprintf(NULL, "[\"" STORAGE_ADDRESS "\",\"" STORAGE_SLOT "\",\"0x%x\"]", bytes(block_hash, 32));
  char* by_hash = assert_accepted(STORAGE_DIR, request, "eth_getStorageAt", args, "block hash of the proof");
  safe_free(args);

  TEST_ASSERT_EQUAL_STRING(STORAGE_VALUE, by_tag);
  TEST_ASSERT_EQUAL_STRING(STORAGE_VALUE, by_number);
  TEST_ASSERT_EQUAL_STRING(STORAGE_VALUE, by_hash);
  safe_free(by_tag);
  safe_free(by_number);
  safe_free(by_hash);
  safe_free(request.data);
}

// A proof of another block must not answer a request for a specific block.
void test_storage_proof_of_another_block_is_rejected(void) {
  bytes_t  request = prove(STORAGE_DIR, "eth_getStorageAt", STORAGE_ARGS, 0);
  uint64_t number  = eth_el_header_get_uint64(proven_header(request), EL_BLOCK_NUMBER);

  char* args = bprintf(NULL, "[\"" STORAGE_ADDRESS "\",\"" STORAGE_SLOT "\",\"0x%lx\"]", number - 1);
  assert_rejected(STORAGE_DIR, request, "eth_getStorageAt", args, "previous block number", "blocknumber mismatch");
  safe_free(args);
  args = bprintf(NULL, "[\"" STORAGE_ADDRESS "\",\"" STORAGE_SLOT "\",\"0x%lx\"]", number + 1);
  assert_rejected(STORAGE_DIR, request, "eth_getStorageAt", args, "next block number", "blocknumber mismatch");
  safe_free(args);
  assert_rejected(STORAGE_DIR, request, "eth_getStorageAt", "[\"" STORAGE_ADDRESS "\",\"" STORAGE_SLOT "\",\"0x1\"]", "block 1", "blocknumber mismatch");
  assert_rejected(STORAGE_DIR, request, "eth_getStorageAt", "[\"" STORAGE_ADDRESS "\",\"" STORAGE_SLOT "\",\"" OTHER_HASH "\"]", "other block hash", "blockhash mismatch");
  assert_rejected(STORAGE_DIR, request, "eth_getStorageAt", "[\"" STORAGE_ADDRESS "\",\"" STORAGE_SLOT "\",\"pending\"]", "pending", "block tag not provable");
  assert_rejected(STORAGE_DIR, request, "eth_getStorageAt", "[\"" STORAGE_ADDRESS "\",\"" STORAGE_SLOT "\",\"earliest\"]", "earliest", "block tag not provable");
  assert_rejected(STORAGE_DIR, request, "eth_getStorageAt", "[\"" STORAGE_ADDRESS "\",\"" STORAGE_SLOT "\"]", "missing block", "invalid arguments for account proof");
  safe_free(request.data);
}

// A block identifier the verifier can not read must not be treated as a match.
void test_unreadable_block_identifiers_are_rejected(void) {
  bytes_t request = prove(BALANCE_DIR, "eth_getBalance", BALANCE_ARGS, 0);
  struct {
    const char* block;
    const char* reason;
  } blocks[] = {
      {"\"0X1\"", "block identifier: "},
      {"\"0xzz\"", "block identifier: "},
      {"\"0x01\"", "block identifier: "},
      {"\"latestx\"", "block identifier: "},
      {"\"\"", "missing or unsupported block identifier"},
      {"\"0x10000000000000000\"", "invalid block number"},
      {"5", "missing or unsupported block identifier"},
      {"null", "missing or unsupported block identifier"},
      {"{\"blockNumber\":\"0x1\"}", "missing or unsupported block identifier"},
  };
  for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++) {
    char* args = bprintf(NULL, "[\"" BALANCE_ADDRESS "\",%s]", blocks[i].block);
    assert_rejected(BALANCE_DIR, request, "eth_getBalance", args, blocks[i].block, blocks[i].reason);
    safe_free(args);
  }

  // `safe` and `finalized` name a block the verifier can not derive from the proof, so they
  // match every verified block, like `latest`.
  safe_free(assert_accepted(BALANCE_DIR, request, "eth_getBalance", "[\"" BALANCE_ADDRESS "\",\"safe\"]", "safe"));
  safe_free(assert_accepted(BALANCE_DIR, request, "eth_getBalance", "[\"" BALANCE_ADDRESS "\",\"finalized\"]", "finalized"));
  safe_free(request.data);
}

// An address the verifier can not read must not disable the comparison with the proof.
void test_unreadable_addresses_are_rejected(void) {
  bytes_t     request     = prove(BALANCE_DIR, "eth_getBalance", BALANCE_ARGS, 0);
  const char* addresses[] = {"\"0X1111111111111111111111111111111111111111\"", "\"0y1111111111111111111111111111111111111111\"", "\"0x12\"", "\"\"", "5", "null"};
  const char* methods[]   = {"eth_getBalance", "eth_getTransactionCount"};
  for (size_t m = 0; m < sizeof(methods) / sizeof(methods[0]); m++) {
    for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); i++) {
      char* args = bprintf(NULL, "[%s,\"latest\"]", addresses[i]);
      assert_rejected(BALANCE_DIR, request, methods[m], args, addresses[i], "address: ");
      safe_free(args);
    }
  }
  safe_free(request.data);
}

// A proof without the storage proof of the requested slot is no answer to eth_getStorageAt,
// and the keys of eth_getProof have to be a list. Without any key eth_getProof is answered
// by the account alone.
void test_account_proof_without_storage_proofs(void) {
  bytes_t request = prove(BALANCE_DIR, "eth_getBalance", BALANCE_ARGS, 0);
  assert_rejected(BALANCE_DIR, request, "eth_getStorageAt", "[\"" BALANCE_ADDRESS "\",\"" STORAGE_SLOT "\",\"latest\"]", "no storage proof", "proof does not match the storage key in request");
  assert_rejected(BALANCE_DIR, request, "eth_getProof", "[\"" BALANCE_ADDRESS "\",\"" STORAGE_SLOT "\",\"latest\"]", "keys are no list", "proof does not match the storage keys in request");

  bytes_t no_data = without_data(request);
  char*   proof   = assert_accepted(BALANCE_DIR, no_data, "eth_getProof", "[\"" BALANCE_ADDRESS "\",[],\"latest\"]", "no keys");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(proof, "\"balance\":\"0xb38a37badf49f791\""), proof);
  safe_free(proof);
  safe_free(no_data.data);
  safe_free(request.data);
}

// The proof of one storage slot must not answer a request for another slot or account.
void test_storage_proof_of_another_slot_is_rejected(void) {
  bytes_t request = prove(STORAGE_DIR, "eth_getStorageAt", STORAGE_ARGS, 0);
  assert_rejected(STORAGE_DIR, request, "eth_getStorageAt", "[\"" STORAGE_ADDRESS "\",\"" STORAGE_OTHER "\",\"latest\"]", "other slot", "proof does not match the storage key in request");
  assert_rejected(STORAGE_DIR, request, "eth_getStorageAt", "[\"" STORAGE_ADDRESS "\",\"0x0\",\"latest\"]", "slot 0", "storage key: ");
  assert_rejected(STORAGE_DIR, request, "eth_getStorageAt", "[\"" BALANCE_ADDRESS "\",\"" STORAGE_SLOT "\",\"latest\"]", "other account", "proof does not match the address in request");
  safe_free(request.data);
}

// The storage keys of an eth_getProof request have to be the ones of the proof, in order.
void test_get_proof_binds_the_storage_keys(void) {
  const char* dir     = "eth_getProof1";
  const char* address = "0xB685760EBD368a891F27ae547391F4E2A289895b";
  const char* key1    = "0x0000000000000000000000000000000000000000000000000000000000000001";
  const char* key2    = "0x0000000000000000000000000000000000000000000000000000000000000002";
  const char* key3    = "0x0000000000000000000000000000000000000000000000000000000000000003";
  char*       honest  = bprintf(NULL, "[\"%s\",[\"%s\",\"%s\"],\"latest\"]", address, key1, key2);
  char*       swapped = bprintf(NULL, "[\"%s\",[\"%s\",\"%s\"],\"latest\"]", address, key2, key1);
  char*       other   = bprintf(NULL, "[\"%s\",[\"%s\",\"%s\"],\"latest\"]", address, key1, key3);
  char*       fewer   = bprintf(NULL, "[\"%s\",[\"%s\"],\"latest\"]", address, key1);
  char*       more    = bprintf(NULL, "[\"%s\",[\"%s\",\"%s\",\"%s\"],\"latest\"]", address, key1, key2, key3);
  char*       block   = bprintf(NULL, "[\"%s\",[\"%s\",\"%s\"],\"0x1\"]", address, key1, key2);
  bytes_t     request = prove(dir, "eth_getProof", honest, 0);

  safe_free(assert_accepted(dir, request, "eth_getProof", honest, "keys of the proof"));
  assert_rejected(dir, request, "eth_getProof", swapped, "swapped keys", "proof does not match the storage key in request");
  assert_rejected(dir, request, "eth_getProof", other, "other key", "proof does not match the storage key in request");
  assert_rejected(dir, request, "eth_getProof", fewer, "fewer keys", "proof does not match the storage keys in request");
  assert_rejected(dir, request, "eth_getProof", more, "more keys", "proof does not match the storage keys in request");
  assert_rejected(dir, request, "eth_getProof", block, "other block", "blocknumber mismatch");
  safe_free(honest);
  safe_free(swapped);
  safe_free(other);
  safe_free(fewer);
  safe_free(more);
  safe_free(block);
  safe_free(request.data);
}

// The other account methods are bound to the requested block as well.
void test_balance_proof_of_another_block_is_rejected(void) {
  bytes_t  request = prove(BALANCE_DIR, "eth_getBalance", BALANCE_ARGS, 0);
  uint64_t number  = eth_el_header_get_uint64(proven_header(request), EL_BLOCK_NUMBER);
  char*    args    = bprintf(NULL, "[\"" BALANCE_ADDRESS "\",\"0x%lx\"]", number);
  safe_free(assert_accepted(BALANCE_DIR, request, "eth_getBalance", BALANCE_ARGS, "latest"));
  safe_free(assert_accepted(BALANCE_DIR, request, "eth_getBalance", args, "block number of the proof"));
  safe_free(args);

  const char* methods[] = {"eth_getBalance", "eth_getTransactionCount"};
  for (int i = 0; i < 2; i++) {
    assert_rejected(BALANCE_DIR, request, methods[i], "[\"" BALANCE_ADDRESS "\",\"0x1\"]", methods[i], "blocknumber mismatch");
    assert_rejected(BALANCE_DIR, request, methods[i], "[\"" BALANCE_ADDRESS "\",\"" OTHER_HASH "\"]", methods[i], "blockhash mismatch");
    assert_rejected(BALANCE_DIR, request, methods[i], "[\"" BALANCE_ADDRESS "\"]", methods[i], "invalid arguments for account proof");
  }
  safe_free(request.data);
}

// An account proof only answers account methods. For any other method the verifier would
// have to return data it never checked.
void test_account_proof_is_no_proof_for_other_methods(void) {
  bytes_t request = prove(BALANCE_DIR, "eth_getBalance", BALANCE_ARGS, 0);
  struct {
    const char* method;
    const char* args;
  } others[] = {
      {"eth_blockNumber", "[]"},
      {"eth_call", CALL_ARGS},
      {"eth_getTransactionByHash", "[\"" OTHER_HASH "\"]"},
      {"eth_getTransactionReceipt", "[\"" OTHER_HASH "\"]"},
      {"eth_getLogs", "[{\"fromBlock\":\"latest\",\"toBlock\":\"latest\"}]"},
      {"eth_getBlockByNumber", "[\"latest\",false]"},
      {"eth_gasPrice", "[]"},
  };
  size_t count = sizeof(others) / sizeof(others[0]);

  // data chosen by the prover: selector 1 = 32 bytes, 2 = bytes, 3 = uint256
  for (uint8_t selector = 1; selector <= 3; selector++) {
    bytes_t forged = with_forged_data(request, selector);
    for (size_t i = 0; i < count; i++)
      assert_rejected(BALANCE_DIR, forged, others[i].method, others[i].args, others[i].method, "method mismatch for account proof");
    safe_free(forged.data);
  }
  for (size_t i = 0; i < count; i++)
    assert_rejected(BALANCE_DIR, request, others[i].method, others[i].args, others[i].method, "method mismatch for account proof");
  safe_free(request.data);
}

// The state a call ran on has to be the state of the requested block.
void test_call_proof_of_another_block_is_rejected(void) {
  bytes_t   request    = prove(CALL_DIR, "eth_call", CALL_ARGS, C4_PROVER_FLAG_INCLUDE_CODE | C4_PROVER_FLAG_USE_DEBUG_TRACE);
  bytes_t   header     = proven_header(request);
  uint64_t  number     = eth_el_header_get_uint64(header, EL_BLOCK_NUMBER);
  bytes32_t block_hash = {0};
  keccak(header, block_hash);

  safe_free(assert_accepted(CALL_DIR, request, "eth_call", CALL_ARGS, "latest"));
  char* args = bprintf(NULL, "[" CALL_TX ",\"0x%lx\"]", number);
  safe_free(assert_accepted(CALL_DIR, request, "eth_call", args, "block number of the proof"));
  safe_free(args);
  args = bprintf(NULL, "[" CALL_TX ",\"0x%x\"]", bytes(block_hash, 32));
  safe_free(assert_accepted(CALL_DIR, request, "eth_call", args, "block hash of the proof"));
  safe_free(args);

  args = bprintf(NULL, "[" CALL_TX ",\"0x%lx\"]", number - 1);
  assert_rejected(CALL_DIR, request, "eth_call", args, "previous block number", "blocknumber mismatch");
  safe_free(args);
  assert_rejected(CALL_DIR, request, "eth_call", "[" CALL_TX ",\"0x1\"]", "block 1", "blocknumber mismatch");
  assert_rejected(CALL_DIR, request, "eth_call", "[" CALL_TX ",\"" OTHER_HASH "\"]", "other block hash", "blockhash mismatch");
  assert_rejected(CALL_DIR, request, "eth_call", "[" CALL_TX ",\"pending\"]", "pending", "block tag not provable");
  // the block defaults to latest
  safe_free(assert_accepted(CALL_DIR, request, "eth_call", "[" CALL_TX "]", "default block"));
  safe_free(request.data);
}

// Without a proof there is no state to run the call on. It must not run on an empty state.
void test_call_without_a_proof_is_rejected(void) {
  uint8_t none[1]  = {0};
  bytes_t request  = prove(CALL_DIR, "eth_call", CALL_ARGS, C4_PROVER_FLAG_INCLUDE_CODE | C4_PROVER_FLAG_USE_DEBUG_TRACE);
  bytes_t no_proof = rebuild_request(request, bytes(none, 1), bytes(none, 1));
  assert_rejected(CALL_DIR, no_proof, "eth_call", CALL_ARGS, "call without a proof", "missing call proof");
  assert_rejected(CALL_DIR, no_proof, "eth_call", "[{\"to\":\"0x1111111111111111111111111111111111111111\",\"data\":\"0x\"},\"0x1\"]", "call to an unproven account", "missing call proof");
  safe_free(no_proof.data);
  safe_free(request.data);
}

// The accounts a call runs on have to be part of the state of the proven block. Here they
// belong to the state of one block while the proof commits to the header of another one.
void test_call_accounts_of_another_state_are_rejected(void) {
  bytes_t request = prove(CALL_DIR, "eth_call", CALL_ARGS, C4_PROVER_FLAG_INCLUDE_CODE | C4_PROVER_FLAG_USE_DEBUG_TRACE);
  bytes_t donor   = prove(BALANCE_DIR, "eth_getBalance", BALANCE_ARGS, 0);
  bytes_t forged  = with_el_proof_of(request, donor);
  // control: a request rebuilt with its own block proof still verifies
  bytes_t rebuilt = with_el_proof_of(request, request);
  safe_free(assert_accepted(CALL_DIR, rebuilt, "eth_call", CALL_ARGS, "rebuilt request"));
  safe_free(rebuilt.data);
  TEST_ASSERT_FALSE(bytes_eq(proven_header(request), proven_header(forged)));
  assert_rejected(BALANCE_DIR, forged, "eth_call", CALL_ARGS, "accounts of another state", "State root mismatch between accounts and execution header");
  safe_free(forged.data);
  safe_free(donor.data);
  safe_free(request.data);
}

// The receipts of a block only answer a request for that block.
void test_block_receipts_of_another_block_are_rejected(void) {
  bytes_t   request    = prove(RECEIPTS_DIR, "eth_getBlockReceipts", RECEIPTS_ARGS, 0);
  bytes_t   header     = proven_header(request);
  uint64_t  number     = eth_el_header_get_uint64(header, EL_BLOCK_NUMBER);
  bytes32_t block_hash = {0};
  keccak(header, block_hash);

  safe_free(assert_accepted(RECEIPTS_DIR, request, "eth_getBlockReceipts", RECEIPTS_ARGS, "latest"));
  char* args = bprintf(NULL, "[\"0x%lx\"]", number);
  safe_free(assert_accepted(RECEIPTS_DIR, request, "eth_getBlockReceipts", args, "block number of the proof"));
  safe_free(args);
  args = bprintf(NULL, "[\"0x%x\"]", bytes(block_hash, 32));
  safe_free(assert_accepted(RECEIPTS_DIR, request, "eth_getBlockReceipts", args, "block hash of the proof"));
  safe_free(args);

  args = bprintf(NULL, "[\"0x%lx\"]", number + 1);
  assert_rejected(RECEIPTS_DIR, request, "eth_getBlockReceipts", args, "next block number", "blocknumber mismatch");
  safe_free(args);
  assert_rejected(RECEIPTS_DIR, request, "eth_getBlockReceipts", "[\"0x1\"]", "block 1", "blocknumber mismatch");
  assert_rejected(RECEIPTS_DIR, request, "eth_getBlockReceipts", "[\"" OTHER_HASH "\"]", "other block hash", "blockhash mismatch");
  assert_rejected(RECEIPTS_DIR, request, "eth_blockNumber", "[]", "other method", "method mismatch for block receipts proof");
  assert_rejected(RECEIPTS_DIR, request, "eth_getBlockReceipts", "[]", "missing block", "missing or unsupported block identifier");
  assert_rejected(RECEIPTS_DIR, request, "eth_getBlockReceipts", "[\"pending\"]", "pending", "block tag not provable");
  safe_free(request.data);
}

#define LOGS_DIR     "eth_getLogs1"
#define LOGS_ADDRESS "0xdac17f958d2ee523a2206206994597c13d831ec7"
#define LOGS_BLOCK   "0x14d7970"
#define LOGS_ARGS    "[{\"address\":[\"" LOGS_ADDRESS "\"],\"fromBlock\":\"" LOGS_BLOCK "\",\"toBlock\":\"" LOGS_BLOCK "\"}]"

// The logs of an eth_getLogs result have to match the filter of the request.
void test_logs_have_to_match_the_filter(void) {
  bytes_t request = prove(LOGS_DIR, "eth_getLogs", LOGS_ARGS, 0);
  safe_free(assert_accepted(LOGS_DIR, request, "eth_getLogs", LOGS_ARGS, "filter of the proof"));
  safe_free(assert_accepted(LOGS_DIR, request, "eth_getLogs", "[{\"address\":\"" LOGS_ADDRESS "\",\"fromBlock\":\"0x1\",\"toBlock\":\"latest\"}]", "wider range"));
  safe_free(assert_accepted(LOGS_DIR, request, "eth_getLogs", "[{\"fromBlock\":\"" LOGS_BLOCK "\",\"toBlock\":\"" LOGS_BLOCK "\"}]", "no address"));

  assert_rejected(LOGS_DIR, request, "eth_getLogs", "[{\"address\":[\"0x1111111111111111111111111111111111111111\"],\"fromBlock\":\"" LOGS_BLOCK "\",\"toBlock\":\"" LOGS_BLOCK "\"}]", "other address", "log does not match the filter of the request");
  assert_rejected(LOGS_DIR, request, "eth_getLogs", "[{\"address\":[\"" LOGS_ADDRESS "\"],\"topics\":[\"" OTHER_HASH "\"],\"fromBlock\":\"" LOGS_BLOCK "\",\"toBlock\":\"" LOGS_BLOCK "\"}]", "other topic", "log does not match the filter of the request");
  assert_rejected(LOGS_DIR, request, "eth_getLogs", "[{\"address\":[\"" LOGS_ADDRESS "\"],\"fromBlock\":\"0x1\",\"toBlock\":\"0x2\"}]", "earlier range", "log is newer than the requested block range");
  assert_rejected(LOGS_DIR, request, "eth_getLogs", "[{\"address\":[\"" LOGS_ADDRESS "\"],\"fromBlock\":\"0x14d7971\",\"toBlock\":\"latest\"}]", "later range", "log is older than the requested block range");
  assert_rejected(LOGS_DIR, request, "eth_getLogs", "[{\"address\":[\"" LOGS_ADDRESS "\"],\"blockHash\":\"" OTHER_HASH "\"}]", "other block hash", "log is not part of the requested block");
  assert_rejected(LOGS_DIR, request, "eth_getLogs", "[]", "no filter", "eth_getLogs requires a filter object in args");
  assert_rejected(LOGS_DIR, request, "eth_blockNumber", "[]", "other method", "method mismatch for logs proof");
  safe_free(request.data);
}

// Filters selecting the proven logs in other ways are accepted, and `earliest` is block 0.
void test_logs_filter_variants(void) {
  bytes_t request = prove(LOGS_DIR, "eth_getLogs", LOGS_ARGS, 0);
  char*   logs    = assert_accepted(LOGS_DIR, request, "eth_getLogs", LOGS_ARGS, "filter of the proof");
  char*   hash    = first_hex_after(logs, "\"blockHash\"");
  char*   args    = bprintf(NULL, "[{\"address\":\"" LOGS_ADDRESS "\",\"blockHash\":\"%s\"}]", hash);
  safe_free(assert_accepted(LOGS_DIR, request, "eth_getLogs", args, "block hash of the logs"));
  safe_free(args);
  safe_free(hash);
  safe_free(logs);

  safe_free(assert_accepted(LOGS_DIR, request, "eth_getLogs", "[{\"topics\":[],\"fromBlock\":\"" LOGS_BLOCK "\",\"toBlock\":\"" LOGS_BLOCK "\"}]", "no topics"));
  safe_free(assert_accepted(LOGS_DIR, request, "eth_getLogs", "[{\"topics\":[null],\"fromBlock\":\"" LOGS_BLOCK "\",\"toBlock\":\"" LOGS_BLOCK "\"}]", "any topic"));
  safe_free(assert_accepted(LOGS_DIR, request, "eth_getLogs", "[{\"fromBlock\":\"earliest\",\"toBlock\":\"latest\"}]", "earliest to latest"));
  assert_rejected(LOGS_DIR, request, "eth_getLogs", "[{\"fromBlock\":\"earliest\",\"toBlock\":\"earliest\"}]", "block 0 only", "log is newer than the requested block range");
  safe_free(request.data);
}

// A filter entry the verifier can not read would match every log, so the filter is validated.
void test_unreadable_logs_filter_is_rejected(void) {
  bytes_t request = prove(LOGS_DIR, "eth_getLogs", LOGS_ARGS, 0);
  struct {
    const char* filter;
    const char* reason;
  } filters[] = {
      {"{\"address\":[\"0X1111111111111111111111111111111111111111\"]}", "Invalid eth_getLogs filter: "},
      {"{\"address\":\"0x1111\"}", "Invalid eth_getLogs filter: "},
      {"{\"address\":5}", "Invalid eth_getLogs filter: "},
      {"{\"topics\":[\"0x1234\"]}", "Invalid eth_getLogs filter: "},
      {"{\"topics\":[\"0X1111111111111111111111111111111111111111111111111111111111111111\"]}", "Invalid eth_getLogs filter: "},
      {"{\"topics\":[[\"0x12\",\"0x34\"]]}", "Invalid eth_getLogs filter: "},
      {"{\"topics\":[null,null,null,null,null]}", "a log has at most 4 topics"},
      {"{\"fromBlock\":99999999}", "Invalid eth_getLogs filter: "},
      {"{\"fromBlock\":\"0X14d7971\"}", "Invalid eth_getLogs filter: "},
      {"{\"blockHash\":\"0x12\"}", "Invalid eth_getLogs filter: "},
      {"{\"fromBlock\":\"0x10000000000000000\"}", "unsupported block identifier in the filter"},
      {"{\"toBlock\":\"latestx\"}", "Invalid eth_getLogs filter: "},
  };
  for (size_t i = 0; i < sizeof(filters) / sizeof(filters[0]); i++) {
    char* args = bprintf(NULL, "[%s]", filters[i].filter);
    assert_rejected(LOGS_DIR, request, "eth_getLogs", args, filters[i].filter, filters[i].reason);
    safe_free(args);
  }
  safe_free(request.data);
}

// The checks walk a list of logs. Data of any other type would pass all of them unchecked.
void test_logs_proof_needs_a_list_of_logs(void) {
  bytes_t request = prove(LOGS_DIR, "eth_getLogs", LOGS_ARGS, 0);
  bytes_t no_data = without_data(request);
  assert_rejected(LOGS_DIR, no_data, "eth_getLogs", LOGS_ARGS, "no data", "logs proof requires a list of logs as data");
  for (uint8_t selector = 1; selector <= 3; selector++) {
    bytes_t forged = with_forged_data(request, selector);
    assert_rejected(LOGS_DIR, forged, "eth_getLogs", LOGS_ARGS, "data of another type", "logs proof requires a list of logs as data");
    safe_free(forged.data);
  }
  safe_free(no_data.data);
  safe_free(request.data);
}

// eth_verifyLogs verifies the logs passed as arguments. Logs without a proof, logs differing
// from the proven ones and logs chosen by the prover must not pass.
void test_verify_logs_checks_the_logs_of_the_request(void) {
  uint8_t no_data[1]     = {0};
  uint8_t empty_proof[1] = {4}; // selector of the logs proof, followed by an empty list
  bytes_t request        = prove(LOGS_DIR, "eth_getLogs", LOGS_ARGS, 0);
  char*   logs           = assert_accepted(LOGS_DIR, request, "eth_getLogs", LOGS_ARGS, "filter of the proof");
  TEST_ASSERT_EQUAL_UINT8(4, request_union(request, 1).data[0]);
  TEST_ASSERT_NOT_NULL(strstr(logs, LOGS_ADDRESS));

  // the proven logs as arguments, without any data of the prover
  bytes_t no_data_request = rebuild_request(request, bytes(no_data, 1), request_union(request, 1));
  safe_free(assert_accepted(LOGS_DIR, no_data_request, "eth_verifyLogs", logs, "proven logs"));

  // one byte of the address of the first log changed
  char* tampered = strdup(logs);
  char* address  = strstr(tampered, LOGS_ADDRESS);
  address[2]     = address[2] == 'd' ? 'e' : 'd';
  assert_rejected(LOGS_DIR, no_data_request, "eth_verifyLogs", tampered, "tampered log", "missing the log within the tx");

  // the proof covers none of the logs
  bytes_t no_proof = rebuild_request(request, bytes(no_data, 1), bytes(empty_proof, 1));
  assert_rejected(LOGS_DIR, no_proof, "eth_verifyLogs", logs, "logs without a proof", "missing log proof!");
  assert_rejected(LOGS_DIR, no_proof, "eth_verifyLogs", tampered, "tampered logs without a proof", "missing log proof!");

  // the prover sends the proven logs as data, the request asks for other ones
  assert_rejected(LOGS_DIR, request, "eth_verifyLogs", tampered, "logs chosen by the prover", "data must be empty");

  // the flag marking data as built by the verifier is not accepted from the host
  char* error = NULL;
  TEST_ASSERT_EQUAL_INT(C4_ERROR, verify_request_with(LOGS_DIR, request, "eth_verifyLogs", tampered, VERIFY_FLAG_FREE_DATA, NULL, &error));
  TEST_ASSERT_NOT_NULL(error);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error, "data must be empty"), error);
  safe_free(error);

  safe_free(tampered);
  safe_free(no_proof.data);
  safe_free(no_data_request.data);
  safe_free(logs);
  safe_free(request.data);
}

// A receipt proof is bound to the transaction hash and the method of the request.
void test_receipt_proof_of_another_request_is_rejected(void) {
  const char* dir     = "eth_getTransactionReceipt1";
  const char* honest  = "[\"0x1c6a9f182ee398a34d6e4a28bbb55f8f5b10101d60589279b26cec199f021d99\"]";
  bytes_t     request = prove(dir, "eth_getTransactionReceipt", honest, 0);
  safe_free(assert_accepted(dir, request, "eth_getTransactionReceipt", honest, "transaction of the proof"));
  assert_rejected(dir, request, "eth_getTransactionReceipt", "[\"" OTHER_HASH "\"]", "other transaction", "invalid transaction hash!");
  assert_rejected(dir, request, "eth_blockNumber", "[]", "other method", "method mismatch for receipt proof");
  assert_rejected(dir, request, "eth_getTransactionByHash", honest, "transaction instead of receipt", "method mismatch for receipt proof");

  // the receipt of the proof is compared with the data, so the data has to be a receipt
  bytes_t no_data = without_data(request);
  assert_rejected(dir, no_data, "eth_getTransactionReceipt", honest, "no data", "receipt proof requires a receipt as data");
  for (uint8_t selector = 1; selector <= 3; selector++) {
    bytes_t forged = with_forged_data(request, selector);
    assert_rejected(dir, forged, "eth_getTransactionReceipt", honest, "data of another type", "receipt proof requires a receipt as data");
    safe_free(forged.data);
  }
  safe_free(no_data.data);
  safe_free(request.data);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_storage_proof_for_the_requested_block);
  RUN_TEST(test_storage_proof_of_another_block_is_rejected);
  RUN_TEST(test_unreadable_block_identifiers_are_rejected);
  RUN_TEST(test_unreadable_addresses_are_rejected);
  RUN_TEST(test_account_proof_without_storage_proofs);
  RUN_TEST(test_storage_proof_of_another_slot_is_rejected);
  RUN_TEST(test_get_proof_binds_the_storage_keys);
  RUN_TEST(test_balance_proof_of_another_block_is_rejected);
  RUN_TEST(test_account_proof_is_no_proof_for_other_methods);
  RUN_TEST(test_call_proof_of_another_block_is_rejected);
  RUN_TEST(test_call_without_a_proof_is_rejected);
  RUN_TEST(test_call_accounts_of_another_state_are_rejected);
  RUN_TEST(test_block_receipts_of_another_block_are_rejected);
  RUN_TEST(test_logs_have_to_match_the_filter);
  RUN_TEST(test_logs_filter_variants);
  RUN_TEST(test_unreadable_logs_filter_is_rejected);
  RUN_TEST(test_logs_proof_needs_a_list_of_logs);
  RUN_TEST(test_verify_logs_checks_the_logs_of_the_request);
  RUN_TEST(test_receipt_proof_of_another_request_is_rejected);
  return UNITY_END();
}
