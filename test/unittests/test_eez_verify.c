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

// Tests of the EEZ (Ethereum Economic Zone) module, milestone 1.
//
// Four groups:
//
//   1. chain table, slot computation and method types (no proof involved)
//   2. `eez_get_l1_storage_value` against synthetic Merkle-Patricia tries, so every
//      rejection path can be reached with a consistent proof
//   3. the recorded RollupX fixture `test/data/eez_eth_getBlockByNumber`: prover and
//      verifier end to end, the L1 prover request, and tampered `eezL1Proof`s
//   4. blocks before the anchor (`eip2935` around the `eezL1Proof`): the decision of the
//      prover and the dispatch of the verifier. The checks of the `eip2935` proof itself
//      are not EEZ-specific and are tested in `test_eip2935.c`.
//
// Three fixtures:
//
// - `eez_eth_getBlockByNumber_local` is a recording of local mode: the raw L1 data (beacon
//   blocks, `eth_getProof` of the EEZ proxy, light client bootstrap) and the anchored L2
//   block 0x8de40 of RollupX. No prover is involved; the unified RPC pipeline builds the L1
//   proof itself. All tests that run the RPC pipeline in local mode use it. It also holds
//   the answer of an L1 prover for the same L1 data (`eth_getStorageAt_….ssz`, written by
//   `colibri-prover` from the files of this fixture): a stand-alone prover of the L2 (the
//   `createProof` / `getProof` API of the bindings, which does not run the RPC pipeline)
//   still asks the prover of its L1.
// - `eez_eth_getBlockByNumber` holds the answer of a prover instead: for the L2 request
//   (`eth_getBlockByNumber_latest_false.ssz`, remote mode, written by `colibri-prover` from
//   the files of this fixture) and for the L1 request of the L2 prover (`eth_getStorageAt` on
//   the EEZ proxy, recorded from the Ethereum mainnet prover), plus the anchored L2 block
//   0x889da. The tests that drive the prover of the L2 directly use it.
// - `eez_eip2935_blockByNumber` is a recording of local mode for a block before the anchor:
//   RollupX block 0x8fa67, five blocks before the anchor 0x8fa6c. Next to the raw L1 data it
//   holds the `eth_getProof` of the EIP-2935 history contract of RollupX at the anchor,
//   recorded right after the anchor changed on L1 (the node only serves proofs for its
//   newest blocks). It is the only fixture with a history proof that belongs to the state
//   of a real anchor. Like the local-mode fixture it also holds the answer of an L1 prover
//   (`eth_getStorageAt_….ssz`, written by `colibri-prover` from the files of this fixture)
//   for the stand-alone prover of the L2.
//
// In `eez_eth_getBlockByNumber`, the anchored L2 block is 0x889da of RollupX. The `eth_getBlockByHash_0x1111..`,
// `0x2222..`, `0x3333..` and `0x4444..` files are hand-made: copies of the anchor block with a
// different `hash` / `number` (or a `null` result), used to reach the error messages for
// blocks that are not the anchor. The files of block 0x889d9 (`0x3652..`, the parent of the
// anchor) are recorded from RollupX; the `eth_getProof` of the history contract for it is
// not (the node only serves proofs for its newest blocks), so the tests of group 4 answer
// that request with a synthetic proof.

#include "c4_assert.h"
#include "unity.h"

#ifndef NO_CHAIN_EEZ

#include "eez_chains_conf.h"
#include "eez_prover.h"
#include "eez_verify.h"
#include "eip2935.h"
#include "el_header.h"
#include "eth_verify.h"
#include "header_cache.h"
#include "patricia.h"
#include "rlp.h"
#include "synth_proofs.h"
#include "version.h"

#define ROLLUPX C4_CHAIN_EEZ_ROLLUPX
#define FIXTURE "eez_eth_getBlockByNumber"
// recording of local mode (raw L1 data instead of an L1 prover answer)
#define LOCAL_FIXTURE "eez_eth_getBlockByNumber_local"
#define L1_RESPONSE   FIXTURE "/eth_getStorageAt_0x4b2760f84b83d6b3829e30a7573b0172b6d6402c_0x679795a0195a1b76cdebb7c51d74e058aee929.ssz"

// values of the fixture
#define ANCHOR_NUMBER     0x889da
#define ANCHOR_NUMBER_HEX "0x889da"
#define ANCHOR_HASH_HEX   "dfa307b750921da7ba51362dac4c00bc81766d1344bca4f385ac2137f009aaf3"
#define ANCHOR_HASH       "0x" ANCHOR_HASH_HEX
#define ROLLUPX_PROXY     "4b2760f84b83d6b3829e30a7573b0172b6d6402c"
// keccak256(pad32(rollupId = 2) ++ pad32(mapping slot = 2)) + 1
#define ROLLUPX_ROOT_SLOT "679795a0195a1b76cdebb7c51d74e058aee92919b8c3389af86ef24535e8a28d"
// values of the local-mode fixture
#define LOCAL_ANCHOR_NUMBER     0x8de40
#define LOCAL_ANCHOR_NUMBER_HEX "0x8de40"
#define LOCAL_ANCHOR_HASH       "0xb4a0e700501016a08b4360751cea5dd4696f837cad22863a79caf7ba547caa08"
// finalized beacon block root the local-mode fixture was recorded with
#define LOCAL_CHECKPOINT "0x3c657ef6f07e8831971e9bc78fa8bd15b6e743141ee333d2c767ff781251b668"
// parent of the anchor, recorded from RollupX
#define PARENT_NUMBER     0x889d9
#define PARENT_NUMBER_HEX "0x889d9"
#define PARENT_HASH_HEX   "36523831d64a5c58c1cb5fdaaa47975c96930b036257a4d4c35e48ce5dadeac2"
#define PARENT_HASH       "0x" PARENT_HASH_HEX
// recording of local mode for a block before the anchor (history proof recorded from RollupX)
#define HISTORY_FIXTURE          "eez_eip2935_blockByNumber"
#define HISTORY_ANCHOR_NUMBER    0x8fa6c
#define HISTORY_ANCHOR_HASH_HEX  "b3bfc6c68c634ed9f7406fdacd7f6e3010a015cf683bc1035c78165c916bdbb7"
#define HISTORY_BLOCK_NUMBER     0x8fa67
#define HISTORY_BLOCK_NUMBER_HEX "0x8fa67"
#define HISTORY_BLOCK_HASH_HEX   "791e9549f9cc11673378d9fb16bf008509ba0949ad3c42eba5bf3af6e5784060"
// finalized beacon block root the fixture was recorded with (the L1 answer bootstraps from it)
#define FIXTURE_CHECKPOINT "0x4c9531434eb47654e6d047040b161dc4a66c7af301caf9968f3bad03534db85c"

void setUp(void) {
  reset_local_filecache();
  c4_header_cache_clear();
}

void tearDown(void) {
  reset_local_filecache();
  c4_header_cache_clear();
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

// Answers all pending requests of `state` from the fixture `dir`. A request without a
// recorded answer fails with an error naming the missing file.
static void answer_from(c4_state_t* state, const char* dir) {
  data_request_t* req = NULL;
  while ((req = c4_state_get_pending_request(state))) {
    char  path[1024];
    char* name = c4_req_mockname(req);
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    bytes_t content = read_testdata(path);
    if (content.data)
      req->response = content;
    else
      req->error = bprintf(NULL, "no fixture for %s", name);
    safe_free(name);
  }
}

// Answers all pending requests of `state` from the fixture with the prover answers.
static void answer_from_fixture(c4_state_t* state) {
  answer_from(state, FIXTURE);
}

// Runs the prover for RollupX against the fixture.
// Returns the proof (owned by the caller) or NULL_BYTES; `error` receives an owned copy of the error.
static bytes_t prove(const char* method, const char* args, char** error) {
  prover_ctx_t* ctx    = c4_prover_create((char*) method, (char*) args, ROLLUPX, 0);
  bytes_t       proof  = NULL_BYTES;
  c4_status_t   status = C4_PENDING;
  for (int i = 0; i < 20 && status == C4_PENDING; i++) {
    status = c4_prover_execute(ctx);
    if (status == C4_PENDING) answer_from_fixture(&ctx->state);
  }
  TEST_ASSERT_NOT_EQUAL_MESSAGE(C4_PENDING, status, "prover did not finish");
  if (status == C4_SUCCESS) proof = bytes_dup(ctx->proof);
  if (error) *error = ctx->state.error ? strdup(ctx->state.error) : NULL;
  c4_prover_free(ctx);
  return proof;
}

// Expects the prover to fail with an error containing `expected`.
static void assert_prover_error(const char* method, const char* args, const char* expected) {
  char*   error = NULL;
  bytes_t proof = prove(method, args, &error);
  TEST_ASSERT_NULL_MESSAGE(proof.data, "prover must fail");
  TEST_ASSERT_NOT_NULL_MESSAGE(error, "prover must report an error");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error, expected), error);
  safe_free(error);
}

// Runs the verifier once. Returns the status; `error` receives an owned copy of the error.
// The chain id of the context after the run is written to `chain_after`.
static c4_status_t verify_request(bytes_t request, const char* method, const char* args, chain_id_t chain, char** error, chain_id_t* chain_after) {
  verify_ctx_t ctx    = {0};
  c4_status_t  status = c4_verify_from_bytes(&ctx, request, (char*) method, json_parse((char*) args), chain);
  if (error) *error = ctx.state.error ? strdup(ctx.state.error) : NULL;
  if (chain_after) *chain_after = ctx.chain_id;
  if (status == C4_SUCCESS && !ctx.success) status = C4_ERROR;
  c4_verify_free_data(&ctx);
  return status;
}

// Expects the verifier to reject `request` for RollupX with an error containing `expected`.
static void assert_rejected(bytes_t request, const char* method, const char* args, const char* expected) {
  char*       error       = NULL;
  chain_id_t  chain_after = 0;
  c4_status_t status      = verify_request(request, method, args, ROLLUPX, &error, &chain_after);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_ERROR, status, "tampered proof must be rejected");
  TEST_ASSERT_NOT_NULL_MESSAGE(error, "rejection must carry an error");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error, expected), error);
  TEST_ASSERT_TRUE_MESSAGE(chain_after == ROLLUPX, "chain id of the context must be restored");
  safe_free(error);
}

// The `eezL1Proof` of a block proof request.
static ssz_ob_t eez_proof_of(bytes_t request) {
  ssz_ob_t req   = {.def = eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST), .bytes = request};
  ssz_ob_t proof = ssz_get(&req, "proof");
  ssz_ob_t el    = ssz_get(&proof, "elProof");
  TEST_ASSERT_NOT_NULL(el.def);
  TEST_ASSERT_EQUAL_STRING("eezL1Proof", el.def->name);
  return el;
}

// Returns a copy of `request` with one byte flipped at the position `target` points to.
// `target` must point into `request`.
static bytes_t flip_byte(bytes_t request, const uint8_t* target) {
  TEST_ASSERT_TRUE(target >= request.data && target < request.data + request.len);
  bytes_t copy = bytes_dup(request);
  copy.data[target - request.data] ^= 0x01;
  return copy;
}

// The error of an RPC context, wherever the failing phase left it.
static const char* rpc_error(c4_rpc_ctx_t* rpc) {
  if (rpc->error) return rpc->error;
  if (rpc->verifier.state.error) return rpc->verifier.state.error;
  return rpc->prover ? rpc->prover->state.error : NULL;
}

// Runs a request through the unified RPC pipeline in local mode (local prover for the L2
// and for its L1, then the verifier), answered from the local-mode fixture.
// Returns the status; `result` and `error` receive owned strings.
static c4_status_t run_rpc(const char* method, const char* args, uint64_t min_latest_block_ts, char** result, char** error) {
  c4_rpc_ctx_t* rpc    = c4_rpc_ctx_create(method, args, ROLLUPX, 0, 0, C4_PROVER_MODE_LOCAL);
  c4_status_t   status = C4_PENDING;
  if (min_latest_block_ts) c4_rpc_ctx_set_min_latest_block_ts(rpc, min_latest_block_ts);
  for (int i = 0; i < 40 && status == C4_PENDING; i++) {
    status = c4_rpc_execute(rpc);
    if (status == C4_PENDING) answer_from(c4_rpc_get_state(rpc), LOCAL_FIXTURE);
  }
  TEST_ASSERT_NOT_EQUAL_MESSAGE(C4_PENDING, status, "rpc did not finish");
  if (result) *result = status == C4_SUCCESS ? ssz_dump_to_str(rpc->verifier.data, false, true) : NULL;
  if (error) {
    const char* msg = rpc_error(rpc);
    *error          = msg ? strdup(msg) : NULL;
  }
  c4_rpc_ctx_free(rpc);
  return status;
}

// Whether the verifier storage (the local file cache of the tests) holds an entry `key`.
static bool stored(const char* key) {
  buffer_t buf   = {0};
  bool     found = file_get((char*) key, &buf);
  buffer_free(&buf);
  return found;
}

// ---------------------------------------------------------------------------
// 1. chain table, slot, method types
// ---------------------------------------------------------------------------

// The L1 of a configured rollup comes only from `c4_get_l1_chain_id_for_l2`.
// Its default returns the chain itself, so a chain added to the table without
// a case in that switch fails the checks below.
void test_chain_table_matches_util(void) {
  size_t                    count = 0;
  const eez_chain_config_t* all   = eez_get_all_chain_configs(&count);
  TEST_ASSERT_NOT_NULL(all);
  TEST_ASSERT_TRUE(count >= 1);
  for (size_t i = 0; i < count; i++) {
    chain_id_t l1 = c4_get_l1_chain_id_for_l2(all[i].chain_id);
    TEST_ASSERT_NOT_NULL(all[i].name);
    TEST_ASSERT_EQUAL_INT_MESSAGE(C4_CHAIN_TYPE_EEZ, c4_chain_type(all[i].chain_id), all[i].name);
    TEST_ASSERT_TRUE_MESSAGE(l1 != all[i].chain_id, all[i].name);
    TEST_ASSERT_EQUAL_INT_MESSAGE(C4_CHAIN_TYPE_ETHEREUM, c4_chain_type(l1), all[i].name);
    TEST_ASSERT_FALSE_MESSAGE(bytes_all_zero(bytes((uint8_t*) all[i].proxy, 20)), all[i].name);
    TEST_ASSERT_EQUAL_PTR(&all[i], eez_get_chain_config(all[i].chain_id));
  }
}

void test_rollupx_config(void) {
  const eez_chain_config_t* conf = eez_get_chain_config(ROLLUPX);
  TEST_ASSERT_NOT_NULL(conf);
  TEST_ASSERT_TRUE(conf->chain_id == 0xaa29f);
  TEST_ASSERT_TRUE(c4_get_l1_chain_id_for_l2(conf->chain_id) == C4_CHAIN_MAINNET);
  TEST_ASSERT_EQUAL_UINT64(2, conf->rollup_id);
  ASSERT_HEX_STRING_EQUAL(ROLLUPX_PROXY, conf->proxy, 20, "proxy");

  TEST_ASSERT_NULL(eez_get_chain_config(C4_CHAIN_MAINNET));
  TEST_ASSERT_NULL(eez_get_chain_config(C4_CHAIN_OP_BASE));
  TEST_ASSERT_NULL(eez_get_chain_config(0));
  TEST_ASSERT_NULL(eez_get_chain_config(0xaa29e));
}

void test_root_slot(void) {
  bytes32_t slot = {0};
  eez_root_slot(2, slot);
  ASSERT_HEX_STRING_EQUAL(ROLLUPX_ROOT_SLOT, slot, 32, "root slot of rollup 2");

  // independent computation: keccak256(pad32(id) ++ pad32(2)), then + 1
  uint8_t   preimage[64] = {0};
  bytes32_t expected     = {0};
  preimage[31]           = 2;
  preimage[63]           = 2;
  keccak(bytes(preimage, 64), expected);
  TEST_ASSERT_NOT_EQUAL(0xff, expected[31]); // no carry for this id, a plain increment is enough
  expected[31]++;
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, slot, 32);

  // the rollup id is part of the key
  bytes32_t other = {0};
  eez_root_slot(3, other);
  TEST_ASSERT_FALSE(memcmp(slot, other, 32) == 0);

  // a 64-bit id is written big-endian into the first word
  eez_root_slot(0x0102030405060708ULL, other);
  memset(preimage, 0, 64);
  for (int i = 0; i < 8; i++) preimage[24 + i] = (uint8_t) (i + 1);
  preimage[63] = 2;
  keccak(bytes(preimage, 64), expected);
  eez_bytes32_add(expected, 1);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, other, 32);
}

void test_bytes32_add_carries(void) {
  bytes32_t value = {0};

  value[31] = 0xff;
  eez_bytes32_add(value, 1);
  TEST_ASSERT_EQUAL_UINT8(0x00, value[31]);
  TEST_ASSERT_EQUAL_UINT8(0x01, value[30]);

  // carry across several bytes
  memset(value, 0, 32);
  value[31] = 0xfe;
  value[30] = 0xff;
  value[29] = 0xff;
  value[28] = 0x07;
  eez_bytes32_add(value, 3);
  TEST_ASSERT_EQUAL_UINT8(0x01, value[31]);
  TEST_ASSERT_EQUAL_UINT8(0x00, value[30]);
  TEST_ASSERT_EQUAL_UINT8(0x00, value[29]);
  TEST_ASSERT_EQUAL_UINT8(0x08, value[28]);

  // 2^256 - 1 + 1 wraps to zero, like the EVM addition
  memset(value, 0xff, 32);
  eez_bytes32_add(value, 1);
  TEST_ASSERT_TRUE(bytes_all_zero(bytes(value, 32)));

  // adding zero is a no-op
  memset(value, 0xab, 32);
  eez_bytes32_add(value, 0);
  for (int i = 0; i < 32; i++) TEST_ASSERT_EQUAL_UINT8(0xab, value[i]);
}

// Method type of the EEZ module for `method` with the JSON array `params`.
static method_type_t method_type(chain_id_t chain, const char* method, const char* params) {
  return c4_eez_get_method_type(chain, (char*) method, json_parse((char*) params), 0);
}

void test_method_types(void) {
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, method_type(ROLLUPX, "eth_getBlockByNumber", "[\"latest\",false]"));
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, method_type(ROLLUPX, "eth_getBlockByNumber", "[\"safe\",false]"));
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, method_type(ROLLUPX, "eth_getBlockByNumber", "[\"finalized\",false]"));
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, method_type(ROLLUPX, "eth_getBlockByNumber", "[\"0x10\",false]"));
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, method_type(ROLLUPX, "eth_blockNumber", "[]"));
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, method_type(ROLLUPX, "eth_getBalance", "[\"0x0000000000000000000000000000000000000000\",\"latest\"]"));
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, method_type(ROLLUPX, "eth_call", "[{},\"latest\"]"));

  // no block proof exists for these tags
  TEST_ASSERT_EQUAL_INT(METHOD_UNPROOFABLE, method_type(ROLLUPX, "eth_getBlockByNumber", "[\"pending\",false]"));
  TEST_ASSERT_EQUAL_INT(METHOD_UNPROOFABLE, method_type(ROLLUPX, "eth_getBlockByNumber", "[\"earliest\",false]"));
  TEST_ASSERT_EQUAL_INT(METHOD_UNPROOFABLE, method_type(ROLLUPX, "eth_getBalance", "[\"0x0000000000000000000000000000000000000000\",\"pending\"]"));
  TEST_ASSERT_EQUAL_INT(METHOD_UNPROOFABLE, method_type(ROLLUPX, "eth_getStorageAt", "[\"0x0000000000000000000000000000000000000000\",\"0x0\",\"pending\"]"));
  // the log methods carry their block tags inside the filter object
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, method_type(ROLLUPX, "eth_getLogs", "[{\"fromBlock\":\"latest\",\"toBlock\":\"latest\"}]"));
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, method_type(ROLLUPX, "eth_getLogs", "[{}]"));
  TEST_ASSERT_EQUAL_INT(METHOD_UNPROOFABLE, method_type(ROLLUPX, "eth_getLogs", "[{\"fromBlock\":\"earliest\",\"toBlock\":\"latest\"}]"));
  TEST_ASSERT_EQUAL_INT(METHOD_UNPROOFABLE, method_type(ROLLUPX, "eth_getLogs", "[{\"fromBlock\":\"latest\",\"toBlock\":\"pending\"}]"));
  TEST_ASSERT_EQUAL_INT(METHOD_UNPROOFABLE, method_type(ROLLUPX, "eth_verifyLogs", "[{\"toBlock\":\"pending\"}]"));

  TEST_ASSERT_EQUAL_INT(METHOD_LOCAL, method_type(ROLLUPX, "eth_chainId", "[]"));
  TEST_ASSERT_EQUAL_INT(METHOD_LOCAL, method_type(ROLLUPX, "net_version", "[]"));
  TEST_ASSERT_EQUAL_INT(METHOD_UNPROOFABLE, method_type(ROLLUPX, "eth_gasPrice", "[]"));
  TEST_ASSERT_EQUAL_INT(METHOD_UNPROOFABLE, method_type(ROLLUPX, "eth_estimateGas", "[{},\"latest\"]"));
  TEST_ASSERT_EQUAL_INT(METHOD_LOCAL, c4_eez_get_method_type(ROLLUPX, "eth_estimateGas", json_parse("[{},\"latest\"]"), VERIFY_FLAG_PAP));
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, method_type(ROLLUPX, "eth_getBlockReceipts", "[\"latest\"]"));
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, method_type(ROLLUPX, "eth_blobBaseFee", "[]"));
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, method_type(ROLLUPX, "eth_maxPriorityFeePerGas", "[]"));
  TEST_ASSERT_EQUAL_INT(METHOD_UNDEFINED, method_type(ROLLUPX, "eth_unknownMethod", "[]"));

  // the module only answers for EEZ chains
  TEST_ASSERT_EQUAL_INT(METHOD_UNDEFINED, method_type(C4_CHAIN_MAINNET, "eth_getBlockByNumber", "[\"latest\",false]"));
  TEST_ASSERT_EQUAL_INT(METHOD_UNDEFINED, method_type(C4_CHAIN_OP_BASE, "eth_getBlockByNumber", "[\"latest\",false]"));

  // dispatch through the generated verifier table
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, c4_get_method_type(ROLLUPX, "eth_getBlockByNumber", json_parse("[\"latest\",false]"), 0));
  TEST_ASSERT_EQUAL_INT(METHOD_LOCAL, c4_get_method_type(ROLLUPX, "eth_chainId", json_parse("[]"), 0));

  TEST_ASSERT_NOT_NULL(c4_eez_get_request_type(C4_CHAIN_TYPE_EEZ));
  TEST_ASSERT_NULL(c4_eez_get_request_type(C4_CHAIN_TYPE_ETHEREUM));
  TEST_ASSERT_NULL(c4_eez_get_request_type(C4_CHAIN_TYPE_OP));
  TEST_ASSERT_EQUAL_PTR(c4_eez_get_request_type(C4_CHAIN_TYPE_EEZ), c4_get_request_type(C4_CHAIN_TYPE_EEZ));
}

void test_module_ignores_other_chains(void) {
  verify_ctx_t vctx = {.chain_id = C4_CHAIN_MAINNET, .method = "eth_chainId", .args = json_parse("[]")};
  TEST_ASSERT_FALSE(c4_eez_verify(&vctx));
  vctx.chain_id = C4_CHAIN_OP_BASE;
  TEST_ASSERT_FALSE(c4_eez_verify(&vctx));

  prover_ctx_t* pctx = c4_prover_create("eth_blockNumber", "[]", C4_CHAIN_MAINNET, 0);
  TEST_ASSERT_FALSE(eez_prover_execute(pctx));
  TEST_ASSERT_NULL(pctx->state.requests);
  TEST_ASSERT_NULL(pctx->state.error);
  c4_prover_free(pctx);
}

// A local method must not skip sync. Invalid sync data fails the request.
void test_local_method_applies_sync_first(void) {
  verify_ctx_t ctx  = {.chain_id = ROLLUPX, .method = "eth_chainId"};
  ssz_def_t    none = {.name = "broken", .type = SSZ_TYPE_CONTAINER};
  ctx.sync_data.def = &none;
  TEST_ASSERT_TRUE(c4_eez_verify(&ctx));
  TEST_ASSERT_FALSE(ctx.success);
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  TEST_ASSERT_EQUAL_STRING("invalid sync_data!", ctx.state.error);
  c4_state_free(&ctx.state);
}

// The freshness gate for `latest` is suspended for EEZ only (milestone 1).
void test_freshness_gate_is_suspended_for_eez_only(void) {
  verify_ctx_t ctx = {.chain_id = ROLLUPX, .min_latest_block_ts = UINT64_MAX};
  TEST_ASSERT_TRUE(eth_check_latest_freshness(&ctx, true, true, 1));
  TEST_ASSERT_TRUE(eth_check_latest_freshness(&ctx, true, false, 0));
  TEST_ASSERT_NULL(ctx.state.error);

  ctx.chain_id = C4_CHAIN_MAINNET;
  TEST_ASSERT_FALSE(eth_check_latest_freshness(&ctx, true, true, 1));
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  c4_state_free(&ctx.state);

  ctx = (verify_ctx_t) {.chain_id = C4_CHAIN_OP_BASE, .min_latest_block_ts = UINT64_MAX};
  TEST_ASSERT_FALSE(eth_check_latest_freshness(&ctx, true, true, 1));
  c4_state_free(&ctx.state);
}

// ---------------------------------------------------------------------------
// 2. storage value of the root slot (synthetic tries)
// ---------------------------------------------------------------------------

// Builds an account proof for the root slot of RollupX holding `value`, with the
// deviations selected in `opts`.
static synth_t synth_account(bytes_t value, synth_opts_t opts) {
  const eez_chain_config_t* conf = eez_get_chain_config(ROLLUPX);
  bytes32_t                 slot = {0};
  eez_root_slot(conf->rollup_id, slot);
  return synth_account_proof(conf->proxy, slot, value, opts);
}

// Expects `eez_get_l1_storage_value` to fail with an error containing `expected`
// and to leave `value` zeroed.
static void assert_storage_error(bytes_t slot_value, synth_opts_t opts, const char* expected) {
  synth_t    synth = synth_account(slot_value, opts);
  c4_state_t state = {0};
  bytes32_t  value;
  memset(value, 0xee, 32);
  c4_status_t status = eez_get_l1_storage_value(&state, eez_get_chain_config(ROLLUPX), synth.account, bytes(synth.state_root, 32), value);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_ERROR, status, expected);
  TEST_ASSERT_NOT_NULL(state.error);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(state.error, expected), state.error);
  TEST_ASSERT_TRUE_MESSAGE(bytes_all_zero(bytes(value, 32)), "value must stay zero on error");
  c4_state_free(&state);
  safe_free(synth.account.bytes.data);
}

// Fills `buf` with a recognizable 32-byte value (0xa0, 0xa1, ...) to store in the root slot.
static bytes_t hash_value(bytes32_t buf) {
  for (int i = 0; i < 32; i++) buf[i] = (uint8_t) (0xa0 + i);
  return bytes(buf, 32);
}

void test_storage_value_valid(void) {
  bytes32_t  expected = {0};
  bytes32_t  value    = {0};
  c4_state_t state    = {0};
  synth_t    synth    = synth_account(hash_value(expected), SYNTH_VALID);

  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(synth.account, true, &state), state.error ? state.error : "synthetic account proof must be valid SSZ");
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eez_get_l1_storage_value(&state, eez_get_chain_config(ROLLUPX), synth.account, bytes(synth.state_root, 32), value));
  TEST_ASSERT_NULL(state.error);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, value, 32);
  safe_free(synth.account.bytes.data);
}

// Storage values are stored without leading zeros and are left-padded to 32 bytes.
void test_storage_value_short_is_left_padded(void) {
  uint8_t    short_value[] = {0x01, 0x02, 0x03};
  bytes32_t  value;
  c4_state_t state = {0};
  synth_t    synth = synth_account(bytes(short_value, 3), SYNTH_VALID);
  memset(value, 0xee, 32);

  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eez_get_l1_storage_value(&state, eez_get_chain_config(ROLLUPX), synth.account, bytes(synth.state_root, 32), value));
  for (int i = 0; i < 29; i++) TEST_ASSERT_EQUAL_UINT8(0, value[i]);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(short_value, value + 29, 3);
  safe_free(synth.account.bytes.data);
}

// A leaf longer than a storage word must not be copied into the 32-byte result.
void test_storage_value_longer_than_32_bytes(void) {
  uint8_t long_value[33];
  memset(long_value, 0x77, sizeof(long_value));
  assert_storage_error(bytes(long_value, 33), SYNTH_VALID, "invalid storage value of the slot!");

  uint8_t huge_value[200];
  memset(huge_value, 0x77, sizeof(huge_value));
  assert_storage_error(bytes(huge_value, 200), SYNTH_VALID, "invalid storage value of the slot!");
}

void test_storage_value_wrong_contract(void) {
  bytes32_t    buf       = {0};
  uint8_t      other[20] = {0};
  synth_opts_t opts      = SYNTH_VALID;
  memset(other, 0x42, 20);
  opts.address = other;
  // a perfectly valid proof of the same slot in another contract
  assert_storage_error(hash_value(buf), opts, "account proof is not about the expected contract!");
}

void test_storage_value_wrong_slot(void) {
  bytes32_t    buf   = {0};
  bytes32_t    other = {0};
  synth_opts_t opts  = SYNTH_VALID;

  // the slot of another rollup
  eez_root_slot(3, other);
  opts.key = other;
  assert_storage_error(hash_value(buf), opts, "storage proof is not about the expected slot!");

  // the mapping slot itself, without the offset of the root field
  eez_root_slot(2, other);
  other[31]--;
  assert_storage_error(hash_value(buf), opts, "storage proof is not about the expected slot!");
}

void test_storage_value_needs_exactly_one_storage_proof(void) {
  bytes32_t    buf    = {0};
  synth_opts_t opts   = SYNTH_VALID;
  opts.storage_proofs = 2;
  assert_storage_error(hash_value(buf), opts, "account proof must contain exactly one storage proof!");
  opts.storage_proofs = 0;
  assert_storage_error(hash_value(buf), opts, "account proof must contain exactly one storage proof!");
}

void test_storage_value_wrong_state_root(void) {
  bytes32_t  buf   = {0};
  bytes32_t  value = {0};
  c4_state_t state = {0};
  synth_t    synth = synth_account(hash_value(buf), SYNTH_VALID);

  synth.state_root[7] ^= 0x01;
  TEST_ASSERT_EQUAL_INT(C4_ERROR, eez_get_l1_storage_value(&state, eez_get_chain_config(ROLLUPX), synth.account, bytes(synth.state_root, 32), value));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(state.error, "does not match the state root!"), state.error);
  c4_state_free(&state);

  // missing state root (e.g. an L1 header without that field); a root of the wrong
  // length is covered by `test_storage_value_malformed_leaf`
  state = (c4_state_t) {0};
  TEST_ASSERT_EQUAL_INT(C4_ERROR, eez_get_l1_storage_value(&state, eez_get_chain_config(ROLLUPX), synth.account, NULL_BYTES, value));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(state.error, "missing state root for the storage proof!"), state.error);
  c4_state_free(&state);

  // unknown chain
  state = (c4_state_t) {0};
  TEST_ASSERT_EQUAL_INT(C4_ERROR, eez_get_l1_storage_value(&state, NULL, synth.account, bytes(synth.state_root, 32), value));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(state.error, "unsupported EEZ chain!"), state.error);
  c4_state_free(&state);
  safe_free(synth.account.bytes.data);
}

void test_storage_value_wrong_storage_hash(void) {
  bytes32_t    buf        = {0};
  synth_opts_t opts       = SYNTH_VALID;
  opts.wrong_storage_hash = true;
  assert_storage_error(hash_value(buf), opts, "does not match the storage hash of the contract!");
}

void test_storage_value_malformed_account(void) {
  bytes32_t    buf    = {0};
  synth_opts_t opts   = SYNTH_VALID;
  opts.account_fields = 5;
  assert_storage_error(hash_value(buf), opts, "invalid account of the contract!");
}

// A proof that the slot does not exist would read as the value zero. It must be an error:
// without a stored hash there is no block.
void test_storage_value_exclusion_proof(void) {
  bytes32_t    buf  = {0};
  synth_opts_t opts = SYNTH_VALID;
  opts.slot_missing = true;
  assert_storage_error(hash_value(buf), opts, "invalid storage proof of the slot!");
}

// Regression guard for a quirk of `patricia_verify`: a proof that stops at a branch node
// (the leaf is cut off) is reported as PATRICIA_FOUND with an empty value. Such a proof
// carries no value and must not be accepted.
void test_storage_value_truncated_proofs(void) {
  bytes32_t    buf  = {0};
  synth_opts_t opts = SYNTH_VALID;

  opts.truncate_storage = true;
  assert_storage_error(hash_value(buf), opts, "invalid storage proof of the slot!");

  opts                     = SYNTH_VALID;
  opts.empty_storage_proof = true;
  assert_storage_error(hash_value(buf), opts, "invalid storage proof of the slot!");

  opts                  = SYNTH_VALID;
  opts.truncate_account = true;
  assert_storage_error(hash_value(buf), opts, "invalid account proof of the contract!");
}

// ---------------------------------------------------------------------------
// 3. recorded RollupX fixture
// ---------------------------------------------------------------------------

// local prover + verifier, sync committee already known
void test_fixture_block_by_number(void) {
  // local mode: the proof of the L2 block and the L1 proof inside it are built locally
  run_rpc_test(LOCAL_FIXTURE, 0, 0);
  // everything the verifier persisted for the sync committee belongs to the L1
  TEST_ASSERT_TRUE(stored("states_1"));
  TEST_ASSERT_FALSE(stored("states_696991"));
}

// The stand-alone prover of the L2 (no RPC pipeline around it, as in `createProof` of the
// bindings) asks the prover of the L1. The local-mode fixture holds that answer as well, and
// the proof is the one the RPC pipeline builds on its own.
void test_local_fixture_serves_the_standalone_prover(void) {
  char*         error  = NULL;
  c4_status_t   status = C4_PENDING;
  int           l1     = 0;
  prover_ctx_t* ctx    = c4_prover_create("eth_getBlockByNumber", "[\"latest\",false]", ROLLUPX, 0);
  for (int i = 0; i < 20 && status == C4_PENDING; i++) {
    status = c4_prover_execute(ctx);
    for (data_request_t* req = ctx->state.requests; req; req = req->next)
      if (req->type == C4_DATA_TYPE_PROVER && c4_state_is_pending(req)) l1++;
    if (status == C4_PENDING) answer_from(&ctx->state, LOCAL_FIXTURE);
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, status, ctx->state.error ? ctx->state.error : "prover failed");
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, l1, "the prover of the L1 is asked once");
  bytes_t request = bytes_dup(ctx->proof);
  c4_prover_free(ctx);

  set_state(ROLLUPX, LOCAL_FIXTURE);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, verify_request(request, "eth_getBlockByNumber", "[\"latest\",false]", ROLLUPX, &error, NULL), error ? error : "no error");
  ssz_ob_t el = eez_proof_of(request);
  TEST_ASSERT_EQUAL_UINT64(LOCAL_ANCHOR_NUMBER, eth_el_header_get_uint64(ssz_get(&el, "elHeader").bytes, EL_BLOCK_NUMBER));
  safe_free(request.data);
  safe_free(error);
}

// Remote mode: the prover of the L2 builds the complete proof, the client only verifies.
void test_fixture_block_by_number_remote(void) {
  run_rpc_test(FIXTURE, 0, 0);
  TEST_ASSERT_TRUE(stored("states_1"));
  TEST_ASSERT_FALSE(stored("states_696991"));
}

// The verifier starts with nothing but a trusted checkpoint. It can only succeed when the
// sync data of the L1 prover answer is carried over into the L2 proof and applied to the
// state of the L1 chain.
void test_fixture_sync_data_is_forwarded_and_stored_for_l1(void) {
  char* error = NULL;
  // set through the L2 chain id: a checkpoint is a beacon root, so it lands on the L1
  c4_set_checkpoint(ROLLUPX, FIXTURE_CHECKPOINT);
  TEST_ASSERT_TRUE(stored("states_1"));
  TEST_ASSERT_FALSE(stored("states_696991"));
  TEST_ASSERT_FALSE(stored("sync_1_1907"));

  // the proof of the L2 prover, which got the sync data from the prover of the L1
  bytes_t request = prove("eth_getBlockByNumber", "[\"latest\",false]", &error);
  TEST_ASSERT_NOT_NULL_MESSAGE(request.data, error ? error : "prover failed");
  safe_free(error);
  error = NULL;

  c4_status_t status = verify_request(request, "eth_getBlockByNumber", "[\"latest\",false]", ROLLUPX, &error, NULL);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, status, error ? error : "no error");

  TEST_ASSERT_TRUE_MESSAGE(stored("sync_1_1907"), "sync committee of the L1 period must be stored");
  TEST_ASSERT_FALSE_MESSAGE(stored("states_696991"), "no sync state may be stored for the L2 chain id");
  TEST_ASSERT_FALSE(stored("sync_696991_1907"));
  safe_free(request.data);
  safe_free(error);
}

// Local mode from a trusted checkpoint only: the proof carries no sync data, the verifier
// asks the Beacon API of the L1 for it and stores the result for the L1 chain.
void test_local_mode_from_a_checkpoint(void) {
  char* result = NULL;
  char* error  = NULL;
  c4_set_checkpoint(ROLLUPX, LOCAL_CHECKPOINT);
  TEST_ASSERT_TRUE(stored("states_1"));
  TEST_ASSERT_FALSE(stored("states_696991"));
  TEST_ASSERT_FALSE(stored("sync_1_1907"));

  c4_status_t status = run_rpc("eth_getBlockByNumber", "[\"latest\",false]", 0, &result, &error);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, status, error ? error : "no error");
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(result, "\"number\":\"" LOCAL_ANCHOR_NUMBER_HEX "\""), result);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(result, "\"hash\":\"" LOCAL_ANCHOR_HASH "\""), result);

  TEST_ASSERT_TRUE_MESSAGE(stored("sync_1_1907"), "sync committee of the L1 period must be stored");
  TEST_ASSERT_FALSE_MESSAGE(stored("states_696991"), "no sync state may be stored for the L2 chain id");
  TEST_ASSERT_FALSE(stored("sync_696991_1907"));
  safe_free(result);
  safe_free(error);
}

// The L2 proof carries the sync data and the account proof of the L1 prover answer
// unchanged, and its L2 header hashes to the anchor.
void test_fixture_proof_contains_sync_data(void) {
  set_state(ROLLUPX, FIXTURE);
  char*   error   = NULL;
  bytes_t request = prove("eth_getBlockByNumber", "[\"latest\",false]", &error);
  TEST_ASSERT_NOT_NULL_MESSAGE(request.data, error ? error : "prover failed");

  ssz_ob_t req = {.def = eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST), .bytes = request};
  ssz_ob_t l1  = {.def = req.def, .bytes = read_testdata(L1_RESPONSE)};
  TEST_ASSERT_NOT_NULL(l1.bytes.data);
  ssz_ob_t sync     = ssz_get(&req, "sync_data");
  ssz_ob_t l1_sync  = ssz_get(&l1, "sync_data");
  ssz_ob_t el       = eez_proof_of(request);
  ssz_ob_t account  = ssz_get(&el, "account");
  ssz_ob_t l1_proof = ssz_get(&l1, "proof");

  // the sync data and the account proof are the ones of the L1 answer, byte for byte
  TEST_ASSERT_TRUE(l1_sync.bytes.len > 0);
  TEST_ASSERT_EQUAL_UINT32(l1_sync.bytes.len, sync.bytes.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(l1_sync.bytes.data, sync.bytes.data, sync.bytes.len);
  TEST_ASSERT_EQUAL_STRING(l1_sync.def->name, sync.def->name);
  TEST_ASSERT_EQUAL_UINT32(l1_proof.bytes.len, account.bytes.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(l1_proof.bytes.data, account.bytes.data, account.bytes.len);

  // the first version byte of a request names the chain type
  TEST_ASSERT_EQUAL_UINT8(C4_CHAIN_TYPE_EEZ, request.data[0]);

  // the L2 header in the proof hashes to the anchor
  bytes32_t hash = {0};
  keccak(ssz_get(&el, "elHeader").bytes, hash);
  ASSERT_HEX_STRING_EQUAL(ANCHOR_HASH_HEX, hash, 32, "L2 header hash");

  safe_free(l1.bytes.data);
  safe_free(request.data);
  safe_free(error);
}

void test_fixture_methods_and_block_identifiers(void) {
  char* result = NULL;
  char* error  = NULL;

  // header only: the header is rebuilt from eth_getBlockByHash
  set_state(ROLLUPX, LOCAL_FIXTURE);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, run_rpc("eth_blockNumber", "[]", 0, &result, &error), error ? error : "no error");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(result, LOCAL_ANCHOR_NUMBER_HEX), result);
  safe_free(result);
  safe_free(error);

  static const char* accepted[][2] = {
      {"eth_getBlockByNumber", "[\"" LOCAL_ANCHOR_NUMBER_HEX "\",false]"}, // the anchor by number
      {"eth_getBlockByHash", "[\"" LOCAL_ANCHOR_HASH "\",false]"},         // the anchor by hash
  };
  for (size_t i = 0; i < sizeof(accepted) / sizeof(accepted[0]); i++) {
    reset_local_filecache();
    c4_header_cache_clear();
    set_state(ROLLUPX, LOCAL_FIXTURE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, run_rpc(accepted[i][0], accepted[i][1], 0, &result, &error), error ? error : accepted[i][1]);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(result, "\"hash\":\"" LOCAL_ANCHOR_HASH "\""), result);
    safe_free(result);
    safe_free(error);
  }

  // `safe` and `finalized` are passed on to L1 (see `test_l1_prover_request_block_tag`):
  // here the L2 prover gets the same L1 answer for them, and the proof verifies
  static const char* tags[] = {"[\"safe\",false]", "[\"finalized\",false]"};
  for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
    reset_local_filecache();
    c4_header_cache_clear();
    set_state(ROLLUPX, FIXTURE);
    bytes_t request = prove("eth_getBlockByNumber", tags[i], &error);
    TEST_ASSERT_NOT_NULL_MESSAGE(request.data, error ? error : tags[i]);
    safe_free(error);
    error = NULL;
    TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, verify_request(request, "eth_getBlockByNumber", tags[i], ROLLUPX, &error, NULL), error ? error : tags[i]);
    safe_free(request.data);
    safe_free(error);
  }
}

// The freshness bound of the host does not reject the (old) anchor block on EEZ.
void test_fixture_latest_passes_freshness_bound(void) {
  char* result = NULL;
  char* error  = NULL;
  set_state(ROLLUPX, LOCAL_FIXTURE);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, run_rpc("eth_getBlockByNumber", "[\"latest\",false]", UINT64_MAX, &result, &error), error ? error : "no error");
  safe_free(result);
  safe_free(error);
}

// Blocks the anchor does not reach are errors that say why. Blocks of the 8191 blocks
// before the anchor are proven with an `eip2935` proof, see group 4.
void test_fixture_blocks_other_than_the_anchor(void) {
  assert_prover_error("eth_getBlockByNumber", "[\"0x889db\",false]", "block 559579 is newer than the last block of RollupX proven on L1 (559578)");
  // 559578 - 8191 = 551387 is the oldest block in the EIP-2935 window, 551386 the first outside
  assert_prover_error("eth_getBlockByNumber", "[\"0x869da\",false]", "block 551386 is more than 8191 blocks older than the last block of RollupX proven on L1 (559578); it needs a historic proof");
  assert_prover_error("eth_getBlockByNumber", "[\"0x0\",false]", "it needs a historic proof");

  // by hash: the number of the block decides the message
  assert_prover_error("eth_getBlockByHash", "[\"0x1111111111111111111111111111111111111111111111111111111111111111\",false]", "block 16 is more than 8191 blocks older");
  assert_prover_error("eth_getBlockByHash", "[\"0x4444444444444444444444444444444444444444444444444444444444444444\",false]", "block 559579 is newer than the last block of RollupX proven on L1");
  assert_prover_error("eth_getBlockByHash", "[\"0x2222222222222222222222222222222222222222222222222222222222222222\",false]", "is not part of the chain anchored on L1");
  assert_prover_error("eth_getBlockByHash", "[\"0x3333333333333333333333333333333333333333333333333333333333333333\",false]", "not found on RollupX");
}

// A block the verifier is asked about by number must be the one the proof is about.
void test_fixture_proof_for_other_block_number_is_rejected(void) {
  set_state(ROLLUPX, FIXTURE);
  char*   error   = NULL;
  bytes_t request = prove("eth_getBlockByNumber", "[\"latest\",false]", &error);
  TEST_ASSERT_NOT_NULL_MESSAGE(request.data, error ? error : "prover failed");

  chain_id_t chain_after = 0;
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, verify_request(request, "eth_getBlockByNumber", "[\"latest\",false]", ROLLUPX, NULL, &chain_after));
  TEST_ASSERT_TRUE(chain_after == ROLLUPX);
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, verify_request(request, "eth_getBlockByNumber", "[\"" ANCHOR_NUMBER_HEX "\",false]", ROLLUPX, NULL, NULL));
  TEST_ASSERT_EQUAL_INT(C4_ERROR, verify_request(request, "eth_getBlockByNumber", "[\"0x889db\",false]", ROLLUPX, NULL, NULL));
  TEST_ASSERT_EQUAL_INT(C4_ERROR, verify_request(request, "eth_getBlockByHash", "[\"0x1111111111111111111111111111111111111111111111111111111111111111\",false]", ROLLUPX, NULL, NULL));

  safe_free(request.data);
  safe_free(error);
}

// The request to the L1 prover: routed by the L1 chain id, carries the client state,
// never a private key and never `last_block_hash`.
void test_l1_prover_request(void) {
  static const uint8_t client_state[] = {0x01, 0x73, 0x07, 0x00, 0x00};
  uint8_t              secret[32];
  memset(secret, 0xab, sizeof(secret));

  prover_ctx_t* ctx = c4_prover_create("eth_blockNumber", "[]", ROLLUPX, 0);
  ctx->client_state = bytes_dup(bytes((uint8_t*) client_state, sizeof(client_state)));
  ctx->witness_key  = bytes_dup(bytes(secret, 32)); // signing key of a prover server
  memset(ctx->last_block_hash, 0xcd, 32);

  TEST_ASSERT_EQUAL_INT(C4_PENDING, c4_prover_execute(ctx));
  data_request_t* req = ctx->state.requests;
  TEST_ASSERT_NOT_NULL(req);
  TEST_ASSERT_NULL_MESSAGE(req->next, "the L2 node is only asked once the anchor is known");
  TEST_ASSERT_EQUAL_INT(C4_DATA_TYPE_PROVER, req->type);
  TEST_ASSERT_TRUE_MESSAGE(req->chain_id == C4_CHAIN_MAINNET, "the request must be routed to the L1 prover");
  TEST_ASSERT_EQUAL_INT(C4_DATA_METHOD_POST, req->method);
  TEST_ASSERT_EQUAL_INT(C4_DATA_ENCODING_SSZ, req->encoding);
  TEST_ASSERT_NULL(req->url);

  char* payload = bprintf(NULL, "%r", req->payload);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(payload, "\"method\":\"eth_getStorageAt\""), payload);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(payload, "\"params\":[\"0x" ROLLUPX_PROXY "\",\"0x" ROLLUPX_ROOT_SLOT "\",\"latest\"]"), payload);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(payload, "\"c4\":\"0x0173070000\""), payload);
  char* version = bprintf(NULL, "\"version\":%d,", c4_current_version_number());
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(payload, version), "the version of this build, not the (older) default of the prover context");
  safe_free(version);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(12, req->ttl, "one L1 slot");
  TEST_ASSERT_NULL_MESSAGE(strstr(payload, "signers"), "a 32-byte witness key is a private key and must not be sent");
  TEST_ASSERT_NULL_MESSAGE(strstr(payload, "abababab"), "private key leaked into the L1 request");
  TEST_ASSERT_NULL_MESSAGE(strstr(payload, "last_block_hash"), "the L1 block must always be proven by a clProof");
  TEST_ASSERT_NULL_MESSAGE(strstr(payload, "cdcdcdcd"), "the L2 block hash must not be sent to the L1 prover");
  TEST_ASSERT_NULL(strstr(payload, "zk_proof"));
  TEST_ASSERT_TRUE_MESSAGE(json_parse(payload).type == JSON_TYPE_OBJECT, payload);
  safe_free(payload);

  // re-entering does not create a second request
  TEST_ASSERT_EQUAL_INT(C4_PENDING, c4_prover_execute(ctx));
  TEST_ASSERT_EQUAL_PTR(req, ctx->state.requests);
  TEST_ASSERT_NULL(req->next);
  c4_prover_free(ctx);

  // signer addresses of the client (n * 20 bytes) are forwarded, and so is the zk flag
  uint8_t signers[40];
  memset(signers, 0x12, sizeof(signers));
  ctx              = c4_prover_create("eth_blockNumber", "[]", ROLLUPX, C4_PROVER_FLAG_ZK_PROOF);
  ctx->witness_key = bytes_dup(bytes(signers, 40));
  TEST_ASSERT_EQUAL_INT(C4_PENDING, c4_prover_execute(ctx));
  payload = bprintf(NULL, "%r", ctx->state.requests->payload);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(payload, "\"signers\":\"0x1212121212"), payload);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(payload, "\"zk_proof\":true"), payload);
  TEST_ASSERT_NULL_MESSAGE(strstr(payload, "\"c4\""), "no client state, no c4 field");
  TEST_ASSERT_TRUE_MESSAGE(json_parse(payload).type == JSON_TYPE_OBJECT, payload);
  safe_free(payload);
  c4_prover_free(ctx);
}

// `safe` and `finalized` are finality statements of the L1 and are passed on.
// A block number or hash reads the newest anchor.
void test_l1_prover_request_block_tag(void) {
  static const char* cases[][3] = {
      {"eth_getBlockByNumber", "[\"latest\",false]", "\"latest\"]"},
      {"eth_getBlockByNumber", "[\"safe\",false]", "\"safe\"]"},
      {"eth_getBlockByNumber", "[\"finalized\",false]", "\"finalized\"]"},
      {"eth_getBlockByNumber", "[\"0x10\",false]", "\"latest\"]"},
      {"eth_getBlockByHash", "[\"" ANCHOR_HASH "\",false]", "\"latest\"]"},
      {"eth_getBalance", "[\"0x0000000000000000000000000000000000000000\",\"finalized\"]", "\"finalized\"]"},
      {"eth_blockNumber", "[]", "\"latest\"]"},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    prover_ctx_t* ctx = c4_prover_create((char*) cases[i][0], (char*) cases[i][1], ROLLUPX, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(C4_PENDING, c4_prover_execute(ctx), cases[i][1]);
    TEST_ASSERT_NOT_NULL(ctx->state.requests);
    TEST_ASSERT_EQUAL_INT(C4_DATA_TYPE_PROVER, ctx->state.requests->type);
    char* payload = bprintf(NULL, "%r", ctx->state.requests->payload);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(payload, cases[i][2]), payload);
    safe_free(payload);
    c4_prover_free(ctx);
  }
}

// Answers of the L1 prover that must not end up in a proof.
void test_l1_prover_answer_is_checked(void) {
  // an error object instead of an SSZ request
  prover_ctx_t* ctx = c4_prover_create("eth_blockNumber", "[]", ROLLUPX, 0);
  TEST_ASSERT_EQUAL_INT(C4_PENDING, c4_prover_execute(ctx));
  ctx->state.requests->response = bytes_dup(bytes((uint8_t*) "{\"error\":\"boom\"}", 16));
  TEST_ASSERT_EQUAL_INT(C4_ERROR, c4_prover_execute(ctx));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx->state.error, "L1 prover rejected the EEZ anchor request"), ctx->state.error);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx->state.error, "boom"), ctx->state.error);
  c4_prover_free(ctx);

  // transport error
  ctx = c4_prover_create("eth_blockNumber", "[]", ROLLUPX, 0);
  TEST_ASSERT_EQUAL_INT(C4_PENDING, c4_prover_execute(ctx));
  ctx->state.requests->error = strdup("connection refused");
  TEST_ASSERT_EQUAL_INT(C4_ERROR, c4_prover_execute(ctx));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx->state.error, "L1 prover request for the EEZ anchor failed: connection refused"), ctx->state.error);
  c4_prover_free(ctx);

  // garbage
  ctx = c4_prover_create("eth_blockNumber", "[]", ROLLUPX, 0);
  TEST_ASSERT_EQUAL_INT(C4_PENDING, c4_prover_execute(ctx));
  uint8_t garbage[64];
  memset(garbage, 0xff, sizeof(garbage));
  ctx->state.requests->response = bytes_dup(bytes(garbage, sizeof(garbage)));
  TEST_ASSERT_EQUAL_INT(C4_ERROR, c4_prover_execute(ctx));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx->state.error, "invalid response of the L1 prover"), ctx->state.error);
  c4_prover_free(ctx);

  // a valid answer about another contract: the address is flipped inside the L1 answer
  bytes_t  l1      = read_testdata(L1_RESPONSE);
  ssz_ob_t l1_req  = {.def = eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST), .bytes = l1};
  ssz_ob_t account = ssz_get(&l1_req, "proof");
  TEST_ASSERT_NOT_NULL(l1.data);
  bytes_t tampered = flip_byte(l1, ssz_get(&account, "address").bytes.data);
  ctx              = c4_prover_create("eth_blockNumber", "[]", ROLLUPX, 0);
  TEST_ASSERT_EQUAL_INT(C4_PENDING, c4_prover_execute(ctx));
  ctx->state.requests->response = tampered;
  TEST_ASSERT_EQUAL_INT(C4_ERROR, c4_prover_execute(ctx));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx->state.error, "account proof is not about the expected contract!"), ctx->state.error);
  c4_prover_free(ctx);
  safe_free(l1.data);
}

// Each tampered proof is a copy of the valid one with a single byte flipped.
void test_tampered_proofs_are_rejected(void) {
  set_state(ROLLUPX, FIXTURE);
  const char* method  = "eth_getBlockByNumber";
  const char* args    = "[\"latest\",false]";
  char*       error   = NULL;
  bytes_t     request = prove(method, args, &error);
  TEST_ASSERT_NOT_NULL_MESSAGE(request.data, error ? error : "prover failed");
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, verify_request(request, method, args, ROLLUPX, NULL, NULL), "the untouched proof must verify");

  ssz_ob_t el            = eez_proof_of(request);
  ssz_ob_t account       = ssz_get(&el, "account");
  ssz_ob_t storage       = ssz_at(ssz_get(&account, "storageProof"), 0);
  ssz_ob_t account_proof = ssz_get(&account, "accountProof");
  ssz_ob_t storage_proof = ssz_get(&storage, "proof");
  bytes_t  l2_header     = ssz_get(&el, "elHeader").bytes;
  bytes_t  account_leaf  = ssz_at(account_proof, ssz_len(account_proof) - 1).bytes;
  bytes_t  storage_leaf  = ssz_at(storage_proof, ssz_len(storage_proof) - 1).bytes;
  bytes_t  tampered      = NULL_BYTES;
  TEST_ASSERT_EQUAL_UINT32(1, ssz_len(ssz_get(&account, "storageProof")));

  // another L2 header: its hash is no longer the one stored on L1
  tampered = flip_byte(request, l2_header.data + l2_header.len - 1);
  assert_rejected(tampered, method, args, "L2 header does not match the block hash stored on L1!");
  safe_free(tampered.data);

  // the state root inside the L2 header (what account proofs of the L2 are checked against)
  tampered = flip_byte(request, eth_el_header_get(l2_header, EL_STATE_ROOT).data);
  assert_rejected(tampered, method, args, "L2 header does not match the block hash stored on L1!");
  safe_free(tampered.data);

  // another contract
  tampered = flip_byte(request, ssz_get(&account, "address").bytes.data + 19);
  assert_rejected(tampered, method, args, "account proof is not about the expected contract!");
  safe_free(tampered.data);

  // another slot
  tampered = flip_byte(request, ssz_get(&storage, "key").bytes.data + 31);
  assert_rejected(tampered, method, args, "storage proof is not about the expected slot!");
  safe_free(tampered.data);

  // another stored value: the last byte of the storage leaf is the last byte of the hash.
  // The leaf no longer hashes to what its parent node commits to.
  tampered = flip_byte(request, storage_leaf.data + storage_leaf.len - 1);
  assert_rejected(tampered, method, args, "invalid storage proof of the slot!");
  safe_free(tampered.data);

  // another account (the last byte of the account leaf is part of the code hash)
  tampered = flip_byte(request, account_leaf.data + account_leaf.len - 1);
  assert_rejected(tampered, method, args, "invalid account proof of the contract!");
  safe_free(tampered.data);

  // Another state trie: a sibling hash in the root node of the account proof is changed.
  // The proof stays consistent in itself but leads to another root than the one of the
  // verified L1 header. The root of the mainnet state trie is a full branch node:
  // `f9 02 11`, then 16 children of `a0` + 32 bytes each.
  bytes32_t account_path = {0};
  bytes_t   account_root = ssz_at(account_proof, 0).bytes;
  keccak(ssz_get(&account, "address").bytes, account_path);
  TEST_ASSERT_EQUAL_UINT32(532, account_root.len);
  TEST_ASSERT_EQUAL_UINT8(0xf9, account_root.data[0]);
  uint32_t sibling = ((uint32_t) (account_path[0] >> 4) + 1) % 16; // any child that is not on the path
  TEST_ASSERT_EQUAL_UINT8(0xa0, account_root.data[3 + sibling * 33]);
  tampered = flip_byte(request, account_root.data + 3 + sibling * 33 + 1);
  assert_rejected(tampered, method, args, "account proof of the contract does not match the state root!");
  safe_free(tampered.data);

  // another L1 block: a byte of the L1 header inside the consensus proof
  ssz_ob_t l1_proof  = ssz_get(&account, "elProof");
  bytes_t  l1_header = ssz_get(&l1_proof, "elHeader").bytes;
  TEST_ASSERT_EQUAL_STRING("clProof", l1_proof.def->name);
  tampered = flip_byte(request, eth_el_header_get(l1_header, EL_STATE_ROOT).data);
  assert_rejected(tampered, method, args, "invalid body root for cl proof!");
  safe_free(tampered.data);

  safe_free(request.data);
  safe_free(error);
}

// The valid proof through the registered block hook: header and hash are returned, the
// chain id is restored and the header is cached for the L2 only.
void test_block_hook_accepts_the_anchor(void) {
  set_state(ROLLUPX, FIXTURE);
  char*   error   = NULL;
  bytes_t request = prove("eth_getBlockByNumber", "[\"latest\",false]", &error);
  TEST_ASSERT_NOT_NULL_MESSAGE(request.data, error ? error : "prover failed");

  ssz_ob_t     el     = eez_proof_of(request);
  bytes_t      header = NULL_BYTES;
  bytes32_t    hash   = {0};
  verify_ctx_t ctx    = {.chain_id = ROLLUPX};

  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, eez_verify_block_proof(&ctx, el, &header, hash), ctx.state.error ? ctx.state.error : "no error");
  TEST_ASSERT_TRUE(ctx.chain_id == ROLLUPX);
  ASSERT_HEX_STRING_EQUAL(ANCHOR_HASH_HEX, hash, 32, "block hash");
  TEST_ASSERT_EQUAL_UINT64(ANCHOR_NUMBER, eth_el_header_get_uint64(header, EL_BLOCK_NUMBER));
  TEST_ASSERT_TRUE(eth_el_header_is_valid(header));
#ifdef EL_HEADER_CACHE
  // the verified L2 header is cached under the L2 chain id, never under the L1 chain id
  TEST_ASSERT_NOT_NULL(c4_header_cache_get_by_hash(ROLLUPX, hash));
  TEST_ASSERT_NULL(c4_header_cache_get_by_hash(C4_CHAIN_MAINNET, hash));
  TEST_ASSERT_NOT_NULL(c4_header_cache_get_by_number(ROLLUPX, ANCHOR_NUMBER));
#endif
  c4_verify_free_data(&ctx);

  // a chain without a table entry
  ctx = (verify_ctx_t) {.chain_id = C4_CHAIN_MAINNET};
  TEST_ASSERT_EQUAL_INT(C4_ERROR, eez_verify_block_proof(&ctx, el, &header, hash));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx.state.error, "unsupported EEZ chain!"), ctx.state.error);
  c4_verify_free_data(&ctx);

  safe_free(request.data);
  safe_free(error);
}

// Every variant of the union but `eezL1Proof` and `eip2935` is rejected by name by the
// hook, and every variant but `eezL1Proof` by the function that verifies the `eezL1Proof`.
// An `eip2935` proof is passed on, and what it wraps is checked as a block of the rollup:
// the `clProof` of an L1 block is not one.
void test_block_hook_rejects_other_variants(void) {
  set_state(ROLLUPX, FIXTURE);
  char*   error   = NULL;
  bytes_t request = prove("eth_getBlockByNumber", "[\"latest\",false]", &error);
  TEST_ASSERT_NOT_NULL_MESSAGE(request.data, error ? error : "prover failed");

  ssz_ob_t         el       = eez_proof_of(request);
  ssz_ob_t         account  = ssz_get(&el, "account");
  bytes_t          header   = NULL_BYTES;
  bytes32_t        hash     = {0};
  const ssz_def_t* el_union = ssz_get_def(account.def, "elProof");
  TEST_ASSERT_EQUAL_INT(SSZ_TYPE_UNION, el_union->type);
  TEST_ASSERT_EQUAL_UINT32(6, el_union->def.container.len);
  TEST_ASSERT_EQUAL_STRING("eezL1Proof", el_union->def.container.elements[4].name);
  TEST_ASSERT_EQUAL_STRING("eip2935", el_union->def.container.elements[5].name);

  for (uint32_t i = 0; i < el_union->def.container.len; i++) {
    const ssz_def_t* variant = el_union->def.container.elements + i;
    if (strcmp(variant->name, "eezL1Proof") == 0) continue;
    bool         history = strcmp(variant->name, "eip2935") == 0;
    ssz_ob_t     foreign = {.def = variant, .bytes = el.bytes};
    verify_ctx_t ctx     = {.chain_id = ROLLUPX};
    TEST_ASSERT_EQUAL_INT_MESSAGE(C4_ERROR, eez_verify_block_proof(&ctx, foreign, &header, hash), variant->name);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx.state.error, history ? "clProof is not valid for a chain without its own consensus layer!" : "expected eezL1Proof or eip2935"), ctx.state.error);
    TEST_ASSERT_TRUE(ctx.chain_id == ROLLUPX);
    c4_verify_free_data(&ctx);

    ctx = (verify_ctx_t) {.chain_id = ROLLUPX};
    TEST_ASSERT_EQUAL_INT_MESSAGE(C4_ERROR, eez_verify_l1_proof(&ctx, foreign, &header, hash), variant->name);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx.state.error, "expected eezL1Proof"), ctx.state.error);
    c4_verify_free_data(&ctx);
  }

  // a variant without a definition
  verify_ctx_t ctx = {.chain_id = ROLLUPX};
  TEST_ASSERT_EQUAL_INT(C4_ERROR, eez_verify_block_proof(&ctx, (ssz_ob_t) {.bytes = el.bytes}, &header, hash));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx.state.error, "invalid block type!"), ctx.state.error);
  c4_verify_free_data(&ctx);

  safe_free(request.data);
  safe_free(error);
}

// The L1 block referenced by hash only (`blockHash`) instead of a clProof. Header caches
// are keyed by hash, so this must never be resolved - not even when a header with that
// hash is cached. The L1 header is cached under both chain ids to prove it.
void test_l1_block_by_hash_is_rejected(void) {
  set_state(ROLLUPX, FIXTURE);
  char*   error   = NULL;
  bytes_t request = prove("eth_getBlockByNumber", "[\"latest\",false]", &error);
  TEST_ASSERT_NOT_NULL_MESSAGE(request.data, error ? error : "prover failed");

  ssz_ob_t  el        = eez_proof_of(request);
  ssz_ob_t  account   = ssz_get(&el, "account");
  ssz_ob_t  l1_proof  = ssz_get(&account, "elProof");
  bytes_t   header    = NULL_BYTES;
  bytes32_t hash      = {0};
  bytes_t   l1_header = ssz_get(&l1_proof, "elHeader").bytes;
  bytes32_t l1_hash   = {0};
  uint8_t   by_hash[33];
  TEST_ASSERT_EQUAL_STRING("clProof", l1_proof.def->name);
  keccak(l1_header, l1_hash);
  by_hash[0] = 0; // selector of `blockHash`
  memcpy(by_hash + 1, l1_hash, 32);
  c4_header_cache_put(C4_CHAIN_MAINNET, eth_el_header_get_uint64(l1_header, EL_BLOCK_NUMBER), l1_hash, l1_header, NULL);
  c4_header_cache_put(ROLLUPX, eth_el_header_get_uint64(l1_header, EL_BLOCK_NUMBER), l1_hash, l1_header, NULL);

  ssz_builder_t new_account = ssz_builder_for_type(ETH_SSZ_VERIFY_ACCOUNT_PROOF);
  ssz_add_bytes(&new_account, "accountProof", ssz_get(&account, "accountProof").bytes);
  ssz_add_bytes(&new_account, "address", ssz_get(&account, "address").bytes);
  ssz_add_bytes(&new_account, "storageProof", ssz_get(&account, "storageProof").bytes);
  ssz_add_bytes(&new_account, "elProof", bytes(by_hash, 33));
  ssz_ob_t      account_by_hash = ssz_builder_to_bytes(&new_account);
  ssz_builder_t new_el          = {.def = el.def};
  ssz_add_bytes(&new_el, "elHeader", ssz_get(&el, "elHeader").bytes);
  ssz_add_bytes(&new_el, "account", account_by_hash.bytes);
  ssz_ob_t   el_by_hash = ssz_builder_to_bytes(&new_el);
  c4_state_t ssz_state  = {0};
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(el_by_hash, true, &ssz_state), ssz_state.error ? ssz_state.error : "rebuilt proof must be valid SSZ");
  ssz_ob_t rebuilt_account = ssz_get(&el_by_hash, "account");
  TEST_ASSERT_EQUAL_STRING("blockHash", ssz_get(&rebuilt_account, "elProof").def->name);

  verify_ctx_t ctx = {.chain_id = ROLLUPX};
  TEST_ASSERT_EQUAL_INT(C4_ERROR, eez_verify_block_proof(&ctx, el_by_hash, &header, hash));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx.state.error, "the L1 block of an EEZ proof must be proven by a clProof!"), ctx.state.error);
  TEST_ASSERT_TRUE(ctx.chain_id == ROLLUPX);
  c4_verify_free_data(&ctx);

  safe_free(account_by_hash.bytes.data);
  safe_free(el_by_hash.bytes.data);
  safe_free(request.data);
  safe_free(error);
}

// What the proxy stores is a hash. Only a preimage that parses as an execution header is
// handed to the proof verifiers, which read its fields by position.
void test_el_header_shape(void) {
  bytes_t file = read_testdata(FIXTURE "/eth_getBlockByHash_" ANCHOR_HASH "_false.json");
  TEST_ASSERT_NOT_NULL(file.data);
  c4_state_t state  = {0};
  bytes_t    header = NULL_BYTES;
  json_t     block  = json_get(json_parse((char*) file.data), "result");
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, eth_el_header_build_from_json(&state, &header, C4_FORK_ELECTRA, block), state.error ? state.error : "no error");
  TEST_ASSERT_TRUE(eth_el_header_is_valid(header));

  // not a list, an empty list, nothing at all
  uint8_t string_item[] = {0x83, 0x01, 0x02, 0x03};
  uint8_t empty_list[]  = {0xc0};
  TEST_ASSERT_FALSE(eth_el_header_is_valid(bytes(string_item, sizeof(string_item))));
  TEST_ASSERT_FALSE(eth_el_header_is_valid(bytes(empty_list, sizeof(empty_list))));
  TEST_ASSERT_FALSE(eth_el_header_is_valid(NULL_BYTES));
  // a truncated header
  TEST_ASSERT_FALSE(eth_el_header_is_valid(bytes(header.data, header.len - 1)));

  // lists of 14 and 15 fields that only differ in their number of fields
  for (int fields = 14; fields <= 15; fields++) {
    buffer_t  buf        = {0};
    bytes32_t word       = {0};
    uint8_t   bloom[256] = {0};
    for (int i = 0; i < fields; i++) {
      if (i == 6)
        rlp_add_item(&buf, bytes(bloom, 256));
      else if (i == 0 || i == 3 || i == 4 || i == 5)
        rlp_add_item(&buf, bytes(word, 32));
      else
        rlp_add_uint64(&buf, (uint64_t) i + 1);
    }
    rlp_to_list(&buf);
    TEST_ASSERT_EQUAL_INT_MESSAGE(fields == 15, eth_el_header_is_valid(buf.data), fields == 15 ? "15 fields" : "14 fields");
    buffer_free(&buf);
  }

  // 15 fields, but one of the fixed-length fields has another length
  static const int fixed[] = {0, 3, 4, 5, 6}; // parentHash, stateRoot, transactionsRoot, receiptsRoot, logsBloom
  for (size_t f = 0; f < sizeof(fixed) / sizeof(fixed[0]); f++) {
    buffer_t  buf        = {0};
    bytes32_t word       = {0};
    uint8_t   bloom[256] = {0};
    for (int i = 0; i < 15; i++) {
      uint32_t cut = i == fixed[f] ? 1 : 0;
      if (i == 6)
        rlp_add_item(&buf, bytes(bloom, 256 - cut));
      else if (i == 0 || i == 3 || i == 4 || i == 5)
        rlp_add_item(&buf, bytes(word, 32 - cut));
      else
        rlp_add_uint64(&buf, (uint64_t) i + 1);
    }
    rlp_to_list(&buf);
    TEST_ASSERT_FALSE_MESSAGE(eth_el_header_is_valid(buf.data), "a fixed-length field of the wrong length");
    buffer_free(&buf);
  }

  safe_free(header.data);
  safe_free(file.data);
}

// The answer of the L1 prover is a complete, valid proof for Ethereum mainnet. Handed to
// the verifier as a proof for RollupX it must be rejected: a consensus proof authenticates
// an L1 block, and L1 state must never be accepted as L2 state.
void test_l1_proof_is_not_a_proof_for_the_l2(void) {
  const char* method      = "eth_getStorageAt";
  const char* args        = "[\"0x" ROLLUPX_PROXY "\",\"0x" ROLLUPX_ROOT_SLOT "\",\"latest\"]";
  bytes_t     l1          = read_testdata(L1_RESPONSE);
  char*       error       = NULL;
  chain_id_t  chain_after = 0;
  TEST_ASSERT_NOT_NULL(l1.data);

  // control: on mainnet the very same bytes verify
  set_state(C4_CHAIN_MAINNET, FIXTURE);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, verify_request(l1, method, args, C4_CHAIN_MAINNET, &error, NULL), error ? error : "L1 answer must verify on mainnet");
  safe_free(error);

  assert_rejected(l1, method, args, "clProof is not valid for a chain without its own consensus layer!");

  // also with the chain type of the request patched to EEZ
  bytes_t patched = bytes_dup(l1);
  patched.data[0] = C4_CHAIN_TYPE_EEZ;
  assert_rejected(patched, method, args, "clProof is not valid for a chain without its own consensus layer!");
  safe_free(patched.data);

  // and an L2 proof is not a proof for mainnet
  bytes_t request = prove("eth_getBlockByNumber", "[\"latest\",false]", &error);
  TEST_ASSERT_NOT_NULL_MESSAGE(request.data, error ? error : "prover failed");
  safe_free(error);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, verify_request(request, "eth_getBlockByNumber", "[\"latest\",false]", C4_CHAIN_MAINNET, &error, &chain_after));
  TEST_ASSERT_TRUE(chain_after == C4_CHAIN_MAINNET);
  safe_free(error);
  safe_free(request.data);
  safe_free(l1.data);
}

// ---------------------------------------------------------------------------
// 4. additional coverage (proposed by the QA review)
// ---------------------------------------------------------------------------

// Copy of a C4Request with another proof and / or without sync data.
// `proof` is the selected variant of the `proof` union (NULL = keep the one of `request`).
static bytes_t rebuild_request(bytes_t request, ssz_ob_t* proof, bool keep_sync_data) {
  ssz_ob_t      req     = {.def = eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST), .bytes = request};
  ssz_builder_t builder = ssz_builder_for_type(ETH_SSZ_VERIFY_REQUEST);
  ssz_ob_t      data    = ssz_get(&req, "data");
  ssz_ob_t      sync    = ssz_get(&req, "sync_data");
  ssz_add_bytes(&builder, "version", ssz_get(&req, "version").bytes);
  if (data.def->type == SSZ_TYPE_NONE)
    ssz_add_bytes(&builder, "data", bytes(NULL, 1)); // selector 0 (`none`)
  else
    ssz_add_ob(&builder, "data", data);
  ssz_add_ob(&builder, "proof", proof ? *proof : ssz_get(&req, "proof"));
  if (keep_sync_data && sync.def->type != SSZ_TYPE_NONE)
    ssz_add_ob(&builder, "sync_data", sync);
  else
    ssz_add_bytes(&builder, "sync_data", bytes(NULL, 1));
  bytes_t    out   = ssz_builder_to_bytes(&builder).bytes;
  c4_state_t state = {0};
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid((ssz_ob_t) {.def = req.def, .bytes = out}, true, &state), state.error ? state.error : "rebuilt request must be valid SSZ");
  return out;
}

// A canned answer for the requests whose mock name starts with `prefix`.
// `content.data == NULL` answers with a transport error.
typedef struct {
  const char* prefix;
  bytes_t     content;
} override_t;

// Answers all pending requests of `state`: from `overrides` (terminated by a NULL prefix,
// may be NULL) when the mock name matches, from the fixture otherwise.
static void answer_with_overrides(c4_state_t* state, const override_t* overrides) {
  data_request_t* req = NULL;
  while ((req = c4_state_get_pending_request(state))) {
    char              path[1024];
    char*             name  = c4_req_mockname(req);
    const override_t* match = NULL;
    for (const override_t* o = overrides; o && o->prefix && !match; o++)
      if (strncmp(name, o->prefix, strlen(o->prefix)) == 0) match = o;
    if (match && match->content.data)
      req->response = bytes_dup(match->content);
    else if (match)
      req->error = strdup("overridden");
    else {
      snprintf(path, sizeof(path), "%s/%s", FIXTURE, name);
      bytes_t file = read_testdata(path);
      if (file.data)
        req->response = file;
      else
        req->error = bprintf(NULL, "no fixture for %s", name);
    }
    safe_free(name);
  }
}

// Runs the prover for RollupX with `overrides` (see `answer_with_overrides`).
// `last_block_hash` (may be NULL) is the block the client claims to have verified.
static bytes_t prove_with(const char* method, const char* args, prover_flags_t flags, const uint8_t* last_block_hash, const override_t* overrides, char** error) {
  prover_ctx_t* ctx    = c4_prover_create((char*) method, (char*) args, ROLLUPX, flags);
  bytes_t       proof  = NULL_BYTES;
  c4_status_t   status = C4_PENDING;
  if (last_block_hash) memcpy(ctx->last_block_hash, last_block_hash, 32);
  for (int i = 0; i < 20 && status == C4_PENDING; i++) {
    status = c4_prover_execute(ctx);
    if (status == C4_PENDING) answer_with_overrides(&ctx->state, overrides);
  }
  TEST_ASSERT_NOT_EQUAL_MESSAGE(C4_PENDING, status, "prover did not finish");
  if (status == C4_SUCCESS) proof = bytes_dup(ctx->proof);
  if (error) *error = ctx->state.error ? strdup(ctx->state.error) : NULL;
  c4_prover_free(ctx);
  return proof;
}

// Expects the prover to fail with `expected` when the request `prefix` is answered with `content`.
static void assert_prover_error_with(const char* method, const char* args, const char* prefix, bytes_t content, const char* expected) {
  override_t overrides[] = {{prefix, content}, {NULL, NULL_BYTES}};
  char*      error       = NULL;
  bytes_t    proof       = prove_with(method, args, 0, NULL, overrides, &error);
  TEST_ASSERT_NULL_MESSAGE(proof.data, "prover must fail");
  TEST_ASSERT_NOT_NULL_MESSAGE(error, "prover must report an error");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error, expected), error);
  safe_free(error);
}

static bytes_t str_bytes(const char* str) {
  return bytes((uint8_t*) str, (uint32_t) strlen(str));
}

static int count_requests(c4_state_t* state, data_request_type_t type) {
  int count = 0;
  for (data_request_t* req = state->requests; req; req = req->next)
    if (req->type == type) count++;
  return count;
}

// The request of `type` that was added last (new requests are put in front of the list).
static data_request_t* newest_request(c4_state_t* state, data_request_type_t type) {
  for (data_request_t* req = state->requests; req; req = req->next)
    if (req->type == type) return req;
  return NULL;
}

// --- gap 1 -----------------------------------------------------------------
// The L1 block is verified with the L1 chain id inside the block hook. When that
// verification has to wait for data (PENDING) or fails, the context must be back on the
// L2 chain id, and the request it left behind must be addressed to the L1.
void test_l1_block_pending_and_error_keep_the_l2_chain_id(void) {
  const char* method = "eth_getBlockByNumber";
  const char* args   = "[\"latest\",false]";
  char*       error  = NULL;
  set_state(ROLLUPX, FIXTURE);
  bytes_t request = prove(method, args, &error);
  TEST_ASSERT_NOT_NULL_MESSAGE(request.data, error ? error : "prover failed");
  bytes_t no_sync = rebuild_request(request, NULL, false);

  // control: the sync committee is in the storage, the proof needs no sync data
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, verify_request(no_sync, method, args, ROLLUPX, NULL, NULL));

  // Only a checkpoint is known and the proof brings no sync data: the sync committee is
  // requested while the consensus proof of the L1 block is verified.
  reset_local_filecache();
  c4_header_cache_clear();
  c4_set_checkpoint(ROLLUPX, FIXTURE_CHECKPOINT);
  verify_ctx_t ctx = {0};
  TEST_ASSERT_EQUAL_INT(C4_PENDING, c4_verify_from_bytes(&ctx, no_sync, (char*) method, json_parse((char*) args), ROLLUPX));
  TEST_ASSERT_TRUE_MESSAGE(ctx.chain_id == ROLLUPX, "chain id must be restored when the L1 block is pending");
  TEST_ASSERT_FALSE(ctx.success);
  data_request_t* req = c4_state_get_pending_request(&ctx.state);
  TEST_ASSERT_NOT_NULL(req);
  TEST_ASSERT_NULL_MESSAGE(req->next, "exactly one request");
  TEST_ASSERT_EQUAL_INT(C4_DATA_TYPE_BEACON_API, req->type);
  TEST_ASSERT_TRUE_MESSAGE(req->chain_id == C4_CHAIN_MAINNET, "the beacon request must be routed to the L1");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(req->url, "light_client/bootstrap/" FIXTURE_CHECKPOINT), req->url);

  // re-entry while the request is open: still pending, no second request, same chain
  TEST_ASSERT_EQUAL_INT(C4_PENDING, c4_verify(&ctx));
  TEST_ASSERT_TRUE(ctx.chain_id == ROLLUPX);
  TEST_ASSERT_EQUAL_PTR(req, ctx.state.requests);
  TEST_ASSERT_NULL(req->next);

  // the L1 request fails: error, and again the L2 chain id
  req->error = strdup("beacon node is down");
  TEST_ASSERT_EQUAL_INT(C4_ERROR, c4_verify(&ctx));
  TEST_ASSERT_TRUE_MESSAGE(ctx.chain_id == ROLLUPX, "chain id must be restored when the L1 block fails");
  TEST_ASSERT_FALSE(ctx.success);
  TEST_ASSERT_FALSE_MESSAGE(stored("states_696991"), "nothing may be stored for the L2 chain id");
  c4_verify_free_data(&ctx);

  // an invalid consensus proof of the L1 block (its EL header was changed)
  set_state(ROLLUPX, FIXTURE);
  ssz_ob_t el        = eez_proof_of(request);
  ssz_ob_t account   = ssz_get(&el, "account");
  ssz_ob_t l1_proof  = ssz_get(&account, "elProof");
  bytes_t  l1_header = ssz_get(&l1_proof, "elHeader").bytes;
  bytes_t  tampered  = flip_byte(request, eth_el_header_get(l1_header, EL_STATE_ROOT).data);
  assert_rejected(tampered, method, args, "invalid body root for cl proof!");
  safe_free(tampered.data);

  safe_free(no_sync.data);
  safe_free(request.data);
  safe_free(error);
}

// --- gap 2 -----------------------------------------------------------------
// Runs a request in remote-prover mode. The request to the prover is answered with
// `answer`; `payload` receives an owned copy of its body.
static c4_status_t run_remote(const char* method, const char* args, bytes_t answer, char** payload, char** result, char** error) {
  c4_rpc_ctx_t* rpc    = c4_rpc_ctx_create(method, args, ROLLUPX, 0, 0, C4_PROVER_MODE_REMOTE);
  c4_status_t   status = c4_rpc_execute(rpc);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_PENDING, status, "a remote proof must be requested first");
  data_request_t* req = c4_state_get_pending_request(c4_rpc_get_state(rpc));
  TEST_ASSERT_NOT_NULL(req);
  TEST_ASSERT_NULL_MESSAGE(req->next, "only the prover is asked");
  TEST_ASSERT_EQUAL_INT(C4_DATA_TYPE_PROVER, req->type);
  TEST_ASSERT_TRUE_MESSAGE(req->chain_id == ROLLUPX, "the proof is requested from the prover of the L2");
  TEST_ASSERT_EQUAL_INT(C4_DATA_ENCODING_SSZ, req->encoding);
  *payload      = bprintf(NULL, "%r", req->payload);
  req->response = bytes_dup(answer);
  for (int i = 0; i < 10 && status == C4_PENDING; i++) {
    status = c4_rpc_execute(rpc);
    if (status == C4_PENDING) answer_from_fixture(c4_rpc_get_state(rpc));
  }
  TEST_ASSERT_NOT_EQUAL_MESSAGE(C4_PENDING, status, "rpc did not finish");
  if (result) *result = status == C4_SUCCESS ? ssz_dump_to_str(rpc->verifier.data, false, true) : NULL;
  if (error) {
    const char* msg = rpc->error ? rpc->error : rpc->verifier.state.error;
    *error          = msg ? strdup(msg) : NULL;
  }
  c4_rpc_ctx_free(rpc);
  return status;
}

// Remote-prover mode: the client sends the client state of its L1 and, once it holds the
// verified anchor header, its hash. A prover may then reference the block by hash only.
void test_remote_prover_mode_and_block_hash_variant(void) {
  const char* method  = "eth_getBlockByNumber";
  const char* args    = "[\"latest\",false]";
  char*       error   = NULL;
  char*       payload = NULL;
  char*       result  = NULL;
  bytes32_t   anchor  = {0};
  hex_to_bytes(ANCHOR_HASH_HEX, -1, bytes(anchor, 32));

  set_state(ROLLUPX, FIXTURE);
  bytes_t full = prove(method, args, &error);
  TEST_ASSERT_NOT_NULL_MESSAGE(full.data, error ? error : "prover failed");
  safe_free(error);

  // first request: nothing is cached yet
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, run_remote(method, args, full, &payload, &result, &error), error ? error : "no error");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(payload, "\"method\": \"eth_getBlockByNumber\""), payload);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(payload, "\"c4\":\"0x0173070000\""), "the client state of the L1 (states_1) must be sent");
  TEST_ASSERT_NULL_MESSAGE(strstr(payload, "last_block_hash"), payload);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(result, "\"hash\":\"" ANCHOR_HASH "\""), result);
  safe_free(payload);
  safe_free(result);
  safe_free(error);

#ifdef EL_HEADER_CACHE
  // a prover that is told the client holds the anchor header answers with `blockHash`
  bytes_t by_hash = prove_with(method, args, 0, anchor, NULL, &error);
  TEST_ASSERT_NOT_NULL_MESSAGE(by_hash.data, error ? error : "prover failed");
  ssz_ob_t req   = {.def = eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST), .bytes = by_hash};
  ssz_ob_t proof = ssz_get(&req, "proof");
  ssz_ob_t el    = ssz_get(&proof, "elProof");
  TEST_ASSERT_EQUAL_STRING("blockHash", el.def->name);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(anchor, el.bytes.data, 32);
  TEST_ASSERT_TRUE_MESSAGE(by_hash.len < full.len, "the proof by hash must be smaller");
  safe_free(error);

  // second request: the client advertises the verified anchor and accepts the short proof
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, run_remote(method, args, by_hash, &payload, &result, &error), error ? error : "no error");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(payload, "\"last_block_hash\":\"" ANCHOR_HASH "\""), payload);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(result, "\"hash\":\"" ANCHOR_HASH "\""), result);
  safe_free(payload);
  safe_free(result);
  safe_free(error);

  // The cache is scoped by chain: the same header cached for the L1 does not resolve an L2 hash.
  ssz_ob_t full_el   = eez_proof_of(full);
  bytes_t  l2_header = ssz_get(&full_el, "elHeader").bytes;
  c4_header_cache_clear();
  c4_header_cache_put(C4_CHAIN_MAINNET, ANCHOR_NUMBER, anchor, l2_header, NULL);
  assert_rejected(by_hash, method, args, "referenced block header not found in the verifier cache!");
  // and a client that never verified the block rejects the short proof
  c4_header_cache_clear();
  assert_rejected(by_hash, method, args, "referenced block header not found in the verifier cache!");
  safe_free(by_hash.data);
#endif
  safe_free(full.data);
}

// --- gap 3 -----------------------------------------------------------------
// Resolves a block through the block hook of the EEZ prover, answering from the fixture.
static c4_status_t resolve_block(prover_ctx_t* ctx, const char* block, bool with_body, eth_block_t* out) {
  c4_status_t status = C4_PENDING;
  for (int i = 0; i < 10 && status == C4_PENDING; i++) {
    status = eez_get_el_block(ctx, json_parse((char*) block), out, with_body);
    if (status == C4_PENDING) answer_from_fixture(&ctx->state);
  }
  return status;
}

// A resolved anchor is kept as a snapshot in the prover state. Proofs that need further
// data after the block (accounts, calls, logs) re-enter the hook and must get the very
// same block without asking L1 or L2 again.
void test_anchor_snapshot_is_reused(void) {
  prover_ctx_t* ctx         = c4_prover_create("eth_getBlockByNumber", "[\"latest\",false]", ROLLUPX, 0);
  eth_block_t   first       = {0};
  eth_block_t   second      = {0};
  eth_block_t   header_only = {0};
  eth_block_t   finalized   = {0};

  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, resolve_block(ctx, "\"latest\"", true, &first), ctx->state.error ? ctx->state.error : "no error");
  TEST_ASSERT_EQUAL_INT(C4_BLOCK_PROOF_TYPE_EEZ, first.proof_type);
  TEST_ASSERT_EQUAL_UINT64_MESSAGE(ANCHOR_NUMBER, first.slot, "`slot` carries the block number (callers test it for != 0)");
  ASSERT_HEX_STRING_EQUAL(ANCHOR_HASH_HEX, first.el_block_hash, 32, "el_block_hash");
  TEST_ASSERT_NOT_NULL_MESSAGE(first.el_body.def, "body requested");
  TEST_ASSERT_EQUAL_UINT32(0, ssz_len(ssz_get(&first.el_body, "transactions")));
  TEST_ASSERT_EQUAL_STRING("clProof", ssz_get(&first.eez.account, "elProof").def->name);
  TEST_ASSERT_TRUE(first.eez.sync_data.bytes.len > 0);
  TEST_ASSERT_EQUAL_INT(1, count_requests(&ctx->state, C4_DATA_TYPE_PROVER));
  TEST_ASSERT_EQUAL_INT(1, count_requests(&ctx->state, C4_DATA_TYPE_ETH_RPC));
  TEST_ASSERT_EQUAL_INT(1, count_requests(&ctx->state, C4_DATA_TYPE_CACHE));

  // Break the answer of the L2 node: a second resolution from the requests would now fail.
  data_request_t* l2 = newest_request(&ctx->state, C4_DATA_TYPE_ETH_RPC);
  safe_free(l2->response.data);
  l2->response = bytes_dup(str_bytes("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":null}"));

  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, eez_get_el_block(ctx, json_parse("\"latest\""), &second, true), "second call must be served from the snapshot");
  TEST_ASSERT_EQUAL_PTR(first.el_header.data, second.el_header.data);
  TEST_ASSERT_EQUAL_PTR(first.el_body.bytes.data, second.el_body.bytes.data);
  TEST_ASSERT_EQUAL_PTR(first.eez.account.bytes.data, second.eez.account.bytes.data);
  TEST_ASSERT_EQUAL_PTR(first.eez.sync_data.bytes.data, second.eez.sync_data.bytes.data);
  TEST_ASSERT_EQUAL_UINT64(ANCHOR_NUMBER, second.slot);
  TEST_ASSERT_EQUAL_INT(1, count_requests(&ctx->state, C4_DATA_TYPE_CACHE));

  // `with_body` is part of the key: the header-only block is resolved on its own
  // (eth_getBlockByHash), but the L1 answer is shared.
  TEST_ASSERT_EQUAL_INT(C4_PENDING, eez_get_el_block(ctx, json_parse("\"latest\""), &header_only, false));
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_requests(&ctx->state, C4_DATA_TYPE_PROVER), "the L1 answer is reused");
  TEST_ASSERT_EQUAL_INT(2, count_requests(&ctx->state, C4_DATA_TYPE_ETH_RPC));
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, resolve_block(ctx, "\"latest\"", false, &header_only), ctx->state.error ? ctx->state.error : "no error");
  TEST_ASSERT_NULL_MESSAGE(header_only.el_body.def, "no body requested");
  // the header rebuilt from eth_getBlockByHash is byte-identical to the one of the raw block
  TEST_ASSERT_EQUAL_UINT32(first.el_header.len, header_only.el_header.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(first.el_header.data, header_only.el_header.data, first.el_header.len);
  TEST_ASSERT_EQUAL_INT(2, count_requests(&ctx->state, C4_DATA_TYPE_CACHE));

  // the block identifier is part of the key: `finalized` reads another L1 block
  TEST_ASSERT_EQUAL_INT(C4_PENDING, eez_get_el_block(ctx, json_parse("\"finalized\""), &finalized, true));
  TEST_ASSERT_EQUAL_INT(2, count_requests(&ctx->state, C4_DATA_TYPE_PROVER));
  char* payload = bprintf(NULL, "%r", newest_request(&ctx->state, C4_DATA_TYPE_PROVER)->payload);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(payload, "\"finalized\"]"), payload);
  safe_free(payload);

  c4_prover_free(ctx);
}

// --- gap 4 -----------------------------------------------------------------
// The L2 node is not trusted either: whatever it returns for the anchor hash must hash
// to the anchor, with and without a body.
void test_l2_node_answer_is_checked(void) {
  const char* by_hash   = "eth_getBlockByHash_" ANCHOR_HASH;
  const char* raw_block = "debug_getRawBlock_" ANCHOR_HASH;
  bytes_t     other     = read_testdata(FIXTURE "/eth_getBlockByHash_0x4444444444444444444444444444444444444444444444444444444444444444_false.json");
  bytes_t     raw       = read_testdata(FIXTURE "/debug_getRawBlock_" ANCHOR_HASH ".json");
  TEST_ASSERT_NOT_NULL(other.data);
  TEST_ASSERT_NOT_NULL(raw.data);

  // header only (eth_getBlockByHash): another block, an unknown block, a transport error
  assert_prover_error_with("eth_blockNumber", "[]", by_hash, other, "the L2 header does not hash to the block " ANCHOR_HASH);
  assert_prover_error_with("eth_blockNumber", "[]", by_hash, str_bytes("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":null}"), "the L2 node does not know the block " ANCHOR_HASH);
  assert_prover_error_with("eth_blockNumber", "[]", by_hash, NULL_BYTES, "overridden");

  // with body (debug_getRawBlock): one nibble of the header changed
  char* text   = bprintf(NULL, "%r", raw); // NUL-terminated copy
  char* marker = strstr(text, "\"result\":\"0x");
  TEST_ASSERT_NOT_NULL(marker);
  TEST_ASSERT_TRUE(strlen(marker) > 200);
  char* nibble = marker + 12 + 100; // inside the RLP header
  *nibble      = *nibble == '0' ? '1' : '0';
  assert_prover_error_with("eth_getBlockByNumber", "[\"latest\",false]", raw_block, str_bytes(text), "the L2 header does not hash to the block " ANCHOR_HASH);
  safe_free(text);

  // with body: not a block at all (`0xc0` is an empty RLP list)
  assert_prover_error_with("eth_getBlockByNumber", "[\"latest\",false]", raw_block, str_bytes("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0xc0\"}"), "Invalid RLP");
  assert_prover_error_with("eth_getBlockByNumber", "[\"latest\",false]", raw_block, NULL_BYTES, "overridden");

  // a block tag without a proof (a prover server gets these directly)
  assert_prover_error("eth_getBlockByNumber", "[\"earliest\",false]", "block \"earliest\" cannot be proven on RollupX");
  assert_prover_error("eth_getBlockByNumber", "[\"pending\",false]", "block \"pending\" cannot be proven on RollupX");

  safe_free(other.data);
  safe_free(raw.data);
}

// --- gap 5 -----------------------------------------------------------------
// Runs the prover with `l1_answer` as the response of the L1 prover.
static c4_status_t prove_with_l1_answer(const char* method, const char* args, prover_flags_t flags, bytes_t l1_answer, bytes_t* proof, char** error) {
  override_t overrides[] = {{"eth_getStorageAt_0x" ROLLUPX_PROXY, l1_answer}, {NULL, NULL_BYTES}};
  char*      err         = NULL;
  bytes_t    out         = prove_with(method, args, flags, NULL, overrides, &err);
  if (proof)
    *proof = out;
  else
    safe_free(out.data);
  if (error)
    *error = err;
  else
    safe_free(err);
  return out.data ? C4_SUCCESS : C4_ERROR;
}

// Shapes of the L1 answer the prover must not turn into a proof.
void test_l1_prover_answer_must_be_an_account_proof_with_a_cl_proof(void) {
  char*   error = NULL;
  bytes_t l1    = read_testdata(L1_RESPONSE);
  TEST_ASSERT_NOT_NULL(l1.data);

  // a valid C4Request, but a block proof instead of an account proof
  set_state(ROLLUPX, FIXTURE);
  bytes_t block_proof = prove("eth_getBlockByNumber", "[\"latest\",false]", &error);
  TEST_ASSERT_NOT_NULL_MESSAGE(block_proof.data, error ? error : "prover failed");
  safe_free(error);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, prove_with_l1_answer("eth_blockNumber", "[]", 0, block_proof, NULL, &error));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error, "L1 prover did not return an account proof for the EEZ anchor"), error);
  safe_free(error);
  safe_free(block_proof.data);

  // the account proof of the fixture, its L1 block referenced by hash only
  ssz_ob_t l1_req    = {.def = eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST), .bytes = l1};
  ssz_ob_t account   = ssz_get(&l1_req, "proof");
  ssz_ob_t l1_block  = ssz_get(&account, "elProof");
  bytes_t  l1_header = ssz_get(&l1_block, "elHeader").bytes;
  uint8_t  by_hash[33];
  by_hash[0] = 0; // selector of `blockHash`
  keccak(l1_header, by_hash + 1);
  ssz_builder_t builder = ssz_builder_for_type(ETH_SSZ_VERIFY_ACCOUNT_PROOF);
  ssz_add_bytes(&builder, "accountProof", ssz_get(&account, "accountProof").bytes);
  ssz_add_bytes(&builder, "address", ssz_get(&account, "address").bytes);
  ssz_add_bytes(&builder, "storageProof", ssz_get(&account, "storageProof").bytes);
  ssz_add_bytes(&builder, "elProof", bytes(by_hash, 33));
  ssz_ob_t account_by_hash = ssz_builder_to_bytes(&builder);
  account_by_hash.def      = account.def;
  bytes_t answer           = rebuild_request(l1, &account_by_hash, true);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, prove_with_l1_answer("eth_blockNumber", "[]", 0, answer, NULL, &error));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error, "L1 prover did not prove the L1 block of the EEZ anchor with a clProof"), error);
  safe_free(error);
  safe_free(answer.data);
  safe_free(account_by_hash.bytes.data);
  safe_free(l1.data);
}

// A client whose sync committee is current gets no sync data from the L1 prover. The L2
// proof then carries none either, whatever flags the L2 prover runs with: it has no
// consensus layer and must never build sync data on its own.
void test_l1_answer_without_sync_data(void) {
  bytes_t                     l1      = read_testdata(L1_RESPONSE);
  bytes_t                     no_sync = rebuild_request(l1, NULL, false);
  static const prover_flags_t flags[] = {
      0,
      C4_PROVER_FLAG_INCLUDE_SYNC,                          // set by the server for every request with a client state
      C4_PROVER_FLAG_ZK_PROOF,                              // `zk_proof: true` of the client
      C4_PROVER_FLAG_CHAIN_STORE,                           // server with a period store
      C4_PROVER_FLAG_ZK_PROOF | C4_PROVER_FLAG_CHAIN_STORE, // both
      C4_PROVER_FLAG_INCLUDE_SYNC | C4_PROVER_FLAG_ZK_PROOF | C4_PROVER_FLAG_CHAIN_STORE,
  };
  set_state(ROLLUPX, FIXTURE); // the verifier already holds the sync committee
  for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) {
    char*   error = NULL;
    bytes_t proof = NULL_BYTES;
    char    label[32];
    snprintf(label, sizeof(label), "flags 0x%x", (unsigned) flags[i]);
    c4_status_t status = prove_with_l1_answer("eth_getBlockByNumber", "[\"latest\",false]", flags[i], no_sync, &proof, &error);
    TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, status, error ? error : label);
    ssz_ob_t req = {.def = eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST), .bytes = proof};
    TEST_ASSERT_EQUAL_INT_MESSAGE(SSZ_TYPE_NONE, ssz_get(&req, "sync_data").def->type, label);
    TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, verify_request(proof, "eth_getBlockByNumber", "[\"latest\",false]", ROLLUPX, NULL, NULL), label);
    safe_free(proof.data);
    safe_free(error);
  }
  safe_free(no_sync.data);
  safe_free(l1.data);
}

// --- gap 6 -----------------------------------------------------------------
// The other prover modes run on the same pieces: hybrid and light-client delegate the
// block to the prover of the L2 (GET url with the client state of the L1).
// Both modes need the header cache: the delegated block is verified once and then
// referenced by hash.
#ifdef EL_HEADER_CACHE
void test_hybrid_and_light_client_mode(void) {
  static const c4_prover_mode_t modes[] = {C4_PROVER_MODE_HYBRID, C4_PROVER_MODE_LIGHT_CLIENT};
  char*                         error   = NULL;
  set_state(ROLLUPX, FIXTURE);
  bytes_t full = prove("eth_getBlockByNumber", "[\"latest\",false]", &error);
  TEST_ASSERT_NOT_NULL_MESSAGE(full.data, error ? error : "prover failed");
  safe_free(error);

  for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
    reset_local_filecache();
    c4_header_cache_clear();
    set_state(ROLLUPX, FIXTURE);
    c4_rpc_ctx_t* rpc       = c4_rpc_ctx_create("eth_getBlockByNumber", "[\"latest\",false]", ROLLUPX, 0, 0, modes[m]);
    c4_status_t   status    = C4_PENDING;
    int           delegated = 0;
    for (int i = 0; i < 10 && status == C4_PENDING; i++) {
      status = c4_rpc_execute(rpc);
      if (status != C4_PENDING) break;
      data_request_t* req = NULL;
      while ((req = c4_state_get_pending_request(c4_rpc_get_state(rpc)))) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(C4_DATA_TYPE_PROVER, req->type, "only the prover of the L2 is asked");
        TEST_ASSERT_TRUE(req->chain_id == ROLLUPX);
        TEST_ASSERT_EQUAL_INT(C4_DATA_METHOD_GET, req->method);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(req->url, "proof/eth_getBlockByNumber/latest/"), req->url);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(req->url, "/0x0173070000"), "the client state of the L1 is part of the url");
        req->response = bytes_dup(full);
        delegated++;
      }
    }
    const char* msg = rpc->error ? rpc->error : rpc->verifier.state.error;
    TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, status, msg ? msg : "no error");
    TEST_ASSERT_EQUAL_INT(1, delegated);
    char* result = ssz_dump_to_str(rpc->verifier.data, false, true);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(result, "\"hash\":\"" ANCHOR_HASH "\""), result);
    safe_free(result);
    c4_rpc_ctx_free(rpc);
  }
  safe_free(full.data);
}
#endif

// --- small additions that close surviving mutants ---------------------------

// Runs the RPC pipeline for `eth_blockNumber` in `mode` against the local-mode fixture and
// counts what the host is asked for. A request of the prover of the L2 (remote mode) fails,
// a request whose mock name starts with `fail_prefix` (may be NULL) fails as well.
typedef struct {
  int         l1_prover; // PROVER requests for the L1 chain
  int         l2_prover; // PROVER requests for the rollup
  int         l1_rpc;    // execution RPC requests for the L1 chain
  int         l2_rpc;    // execution RPC requests for the rollup
  int         l1_beacon; // Beacon API requests, always for the L1 chain
  int         other;     // anything else, including a Beacon API request for the rollup
  c4_status_t status;
  char*       error; // owned
} host_requests_t;

static host_requests_t run_and_count(c4_prover_mode_t mode, const char* fail_prefix) {
  host_requests_t out = {.status = C4_PENDING};
  c4_rpc_ctx_t*   rpc = c4_rpc_ctx_create("eth_blockNumber", "[]", ROLLUPX, 0, 0, mode);
  for (int i = 0; i < 40 && out.status == C4_PENDING; i++) {
    out.status = c4_rpc_execute(rpc);
    if (out.status != C4_PENDING) break;
    data_request_t* req = NULL;
    while ((req = c4_state_get_pending_request(c4_rpc_get_state(rpc)))) {
      char* name = c4_req_mockname(req);
      bool  l1   = req->chain_id == C4_CHAIN_MAINNET;
      bool  l2   = req->chain_id == ROLLUPX;
      if (req->type == C4_DATA_TYPE_PROVER && (l1 || l2))
        (l1 ? out.l1_prover++ : out.l2_prover++);
      else if (req->type == C4_DATA_TYPE_ETH_RPC && (l1 || l2))
        (l1 ? out.l1_rpc++ : out.l2_rpc++);
      else if (req->type == C4_DATA_TYPE_BEACON_API && l1)
        out.l1_beacon++;
      else
        out.other++;

      char path[1024];
      snprintf(path, sizeof(path), "%s/%s", LOCAL_FIXTURE, name);
      bytes_t content = NULL_BYTES;
      if (req->type != C4_DATA_TYPE_PROVER && !(fail_prefix && strncmp(name, fail_prefix, strlen(fail_prefix)) == 0)) content = read_testdata(path);
      if (content.data)
        req->response = content;
      else
        req->error = bprintf(NULL, "no answer for %s", name);
      safe_free(name);
    }
  }
  const char* msg = rpc_error(rpc);
  out.error       = msg ? strdup(msg) : NULL;
  c4_rpc_ctx_free(rpc);
  return out;
}

// Local mode needs no prover: the proof of the L1 block is built by a local prover of the
// L1 chain. The host only sees execution RPC requests of both chains and Beacon API
// requests of the L1.
void test_local_mode_builds_the_l1_proof_locally(void) {
  set_state(ROLLUPX, LOCAL_FIXTURE);
  host_requests_t seen = run_and_count(C4_PROVER_MODE_LOCAL, NULL);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, seen.status, seen.error ? seen.error : "no error");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, seen.l1_prover, "no prover of the L1 is asked");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, seen.l2_prover, "no prover of the L2 is asked");
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, seen.l1_rpc, "eth_getProof of the EEZ proxy on L1");
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, seen.l2_rpc, "the anchor block from the L2 node");
  TEST_ASSERT_TRUE_MESSAGE(seen.l1_beacon >= 1, "the L1 block from the Beacon API");
  TEST_ASSERT_EQUAL_INT(0, seen.other);
  safe_free(seen.error);
}

// The local fallback of remote mode is a local proof as a whole: when the prover of the L2
// does not answer, the L1 proof is built locally as well.
void test_remote_mode_falls_back_to_a_local_proof(void) {
  set_state(ROLLUPX, LOCAL_FIXTURE);
  host_requests_t seen = run_and_count(C4_PROVER_MODE_REMOTE, NULL);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, seen.status, seen.error ? seen.error : "no error");
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, seen.l2_prover, "the prover of the L2 is asked first");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, seen.l1_prover, "no prover of the L1 is asked");
  TEST_ASSERT_EQUAL_INT(1, seen.l1_rpc);
  TEST_ASSERT_EQUAL_INT(1, seen.l2_rpc);
  TEST_ASSERT_EQUAL_INT(0, seen.other);
  safe_free(seen.error);
}

// A failure of the local L1 prover ends the request with the error of the L1 request.
void test_local_mode_reports_the_error_of_the_l1_proof(void) {
  set_state(ROLLUPX, LOCAL_FIXTURE);
  host_requests_t seen = run_and_count(C4_PROVER_MODE_LOCAL, "eth_getProof_");
  TEST_ASSERT_EQUAL_INT(C4_ERROR, seen.status);
  TEST_ASSERT_NOT_NULL(seen.error);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(seen.error, "L1 prover request for the EEZ anchor failed"), seen.error);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(seen.error, "no answer for eth_getProof_"), seen.error);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, seen.l2_rpc, "the L2 node is not asked without an anchor");
  safe_free(seen.error);
}

// The prover of the L2 on its own (a prover server, or the CLI prover) asks the prover of
// the L1 and passes the client state of the L1 chain on.
void test_prover_sends_l1_client_state_to_l1_prover(void) {
  static const uint8_t client_state[] = {0x01, 0x73, 0x07, 0x00, 0x00};
  prover_ctx_t*        ctx            = c4_prover_create("eth_blockNumber", "[]", ROLLUPX, 0);
  ctx->client_state                   = bytes_dup(bytes((uint8_t*) client_state, sizeof(client_state)));
  TEST_ASSERT_EQUAL_INT(C4_PENDING, c4_prover_execute(ctx));
  data_request_t* req = c4_state_get_pending_request(&ctx->state);
  TEST_ASSERT_NOT_NULL(req);
  TEST_ASSERT_EQUAL_INT(C4_DATA_TYPE_PROVER, req->type);
  TEST_ASSERT_TRUE(req->chain_id == C4_CHAIN_MAINNET);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(12, req->ttl, "one L1 slot");
  char* payload  = bprintf(NULL, "%r", req->payload);
  char* expected = bprintf(NULL, "\"version\":%d,", c4_current_version_number());
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(payload, "\"c4\":\"0x0173070000\""), payload);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(payload, expected), payload);
  safe_free(expected);
  safe_free(payload);
  c4_prover_free(ctx);

  // the version is the one of this build, not the one of the client
  ctx          = c4_prover_create("eth_blockNumber", "[]", ROLLUPX, 0);
  ctx->version = c4_version_number(1, 0, 0);
  TEST_ASSERT_EQUAL_INT(C4_PENDING, c4_prover_execute(ctx));
  payload  = bprintf(NULL, "%r", ctx->state.requests->payload);
  expected = bprintf(NULL, "\"version\":%d}", c4_current_version_number());
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(payload, expected), payload);
  safe_free(expected);
  safe_free(payload);
  c4_prover_free(ctx);
}

void test_local_methods_and_unknown_eez_chain(void) {
  char* result = NULL;
  char* error  = NULL;
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, run_rpc("eth_chainId", "[]", 0, &result, &error), error ? error : "no error");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(result, "0xaa29f"), result);
  safe_free(result);
  safe_free(error);

  // a chain id of type EEZ without a table entry: no module takes it
  chain_id_t unknown = CHAIN_ID(C4_CHAIN_TYPE_EEZ, 5);
  TEST_ASSERT_EQUAL_INT(C4_CHAIN_TYPE_EEZ, c4_chain_type(unknown));
  TEST_ASSERT_NULL(eez_get_chain_config(unknown));
  verify_ctx_t vctx = {.chain_id = unknown, .method = "eth_chainId", .args = json_parse("[]")};
  TEST_ASSERT_FALSE(c4_eez_verify(&vctx));
  TEST_ASSERT_NULL(vctx.state.error);
  prover_ctx_t* pctx = c4_prover_create("eth_blockNumber", "[]", unknown, 0);
  TEST_ASSERT_FALSE(eez_prover_execute(pctx));
  TEST_ASSERT_NULL(pctx->state.requests);
  c4_prover_free(pctx);

  set_state(ROLLUPX, FIXTURE);
  bytes_t request = prove("eth_getBlockByNumber", "[\"latest\",false]", &error);
  TEST_ASSERT_NOT_NULL_MESSAGE(request.data, error ? error : "prover failed");
  safe_free(error);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, verify_request(request, "eth_getBlockByNumber", "[\"latest\",false]", unknown, &error, NULL));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error, "is not supported"), error);
  safe_free(error);

  // an eezL1Proof without an L2 header
  ssz_ob_t      el      = eez_proof_of(request);
  ssz_builder_t builder = {.def = el.def};
  ssz_add_bytes(&builder, "elHeader", NULL_BYTES);
  ssz_add_bytes(&builder, "account", ssz_get(&el, "account").bytes);
  ssz_ob_t     empty  = ssz_builder_to_bytes(&builder);
  bytes_t      header = NULL_BYTES;
  bytes32_t    hash   = {0};
  verify_ctx_t ctx    = {.chain_id = ROLLUPX};
  TEST_ASSERT_EQUAL_INT(C4_ERROR, eez_verify_l1_proof(&ctx, empty, &header, hash));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx.state.error, "missing L2 header in the EEZ proof!"), ctx.state.error);
  TEST_ASSERT_TRUE(ctx.chain_id == ROLLUPX);
  c4_verify_free_data(&ctx);
  safe_free(empty.bytes.data);
  safe_free(request.data);
}

// Leaves of the storage trie that are no storage word.
void test_storage_value_malformed_leaf(void) {
  // an empty string (0x80): a deleted slot is absent from the trie, never stored as empty
  assert_storage_error(NULL_BYTES, SYNTH_VALID, "invalid storage value of the slot!");

  // a state root of the right bytes but the wrong length is "missing", not "mismatching"
  bytes32_t  buf   = {0};
  bytes32_t  value = {0};
  c4_state_t state = {0};
  synth_t    synth = synth_account(hash_value(buf), SYNTH_VALID);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, eez_get_l1_storage_value(&state, eez_get_chain_config(ROLLUPX), synth.account, bytes(synth.state_root, 31), value));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(state.error, "missing state root for the storage proof!"), state.error);
  c4_state_free(&state);
  state = (c4_state_t) {0};
  TEST_ASSERT_EQUAL_INT(C4_ERROR, eez_get_l1_storage_value(&state, eez_get_chain_config(ROLLUPX), synth.account, bytes(NULL, 32), value));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(state.error, "missing state root for the storage proof!"), state.error);
  c4_state_free(&state);
  safe_free(synth.account.bytes.data);
}

// An account or a leaf that does not have the RLP shape of an account / a storage word.
void test_storage_value_malformed_storage_hash_and_leaf(void) {
  bytes32_t    buf  = {0};
  synth_opts_t opts = SYNTH_VALID;

  // a storage hash shorter than a hash must not be compared as 32 bytes
  opts.storage_hash_len = 31;
  assert_storage_error(hash_value(buf), opts, "invalid storage hash of the contract!");
  opts.storage_hash_len = 1;
  assert_storage_error(hash_value(buf), opts, "invalid storage hash of the contract!");

  // a list where the storage word is expected
  uint8_t short_value[] = {0x01, 0x02, 0x03};
  opts                  = SYNTH_VALID;
  opts.leaf_is_list     = true;
  assert_storage_error(bytes(short_value, 3), opts, "invalid storage value of the slot!");
  assert_storage_error(hash_value(buf), opts, "invalid storage value of the slot!");
}

// --- synthetic L1 answers ---------------------------------------------------

// An answer of the L1 prover whose root slot holds `value`. Account and storage proof are
// synthetic and the state root in the L1 header of the fixture is replaced by their root.
// The prover accepts it (it does not verify the consensus proof of its L1 prover, the
// verifier does); the consensus proof itself is no longer valid.
static bytes_t synth_l1_answer(bytes_t value) {
  bytes_t       l1      = read_testdata(L1_RESPONSE);
  ssz_ob_t      l1_req  = {.def = eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST), .bytes = l1};
  ssz_ob_t      account = ssz_get(&l1_req, "proof");
  synth_t       synth   = synth_account(value, SYNTH_VALID);
  ssz_builder_t builder = ssz_builder_for_type(ETH_SSZ_VERIFY_ACCOUNT_PROOF);
  TEST_ASSERT_NOT_NULL(l1.data);
  ssz_add_bytes(&builder, "accountProof", ssz_get(&synth.account, "accountProof").bytes);
  ssz_add_bytes(&builder, "address", ssz_get(&synth.account, "address").bytes);
  ssz_add_bytes(&builder, "storageProof", ssz_get(&synth.account, "storageProof").bytes);
  ssz_add_ob(&builder, "elProof", ssz_get(&account, "elProof")); // the clProof of the fixture
  ssz_ob_t synth_proof = ssz_builder_to_bytes(&builder);
  synth_proof.def      = account.def;
  bytes_t  answer      = rebuild_request(l1, &synth_proof, true);
  ssz_ob_t req         = {.def = l1_req.def, .bytes = answer};
  ssz_ob_t proof       = ssz_get(&req, "proof");
  ssz_ob_t l1_block    = ssz_get(&proof, "elProof");
  bytes_t  state_root  = eth_el_header_get(ssz_get(&l1_block, "elHeader").bytes, EL_STATE_ROOT);
  TEST_ASSERT_EQUAL_STRING("clProof", l1_block.def->name);
  TEST_ASSERT_EQUAL_UINT32(32, state_root.len);
  memcpy(state_root.data, synth.state_root, 32);
  safe_free(synth_proof.bytes.data);
  safe_free(synth.account.bytes.data);
  safe_free(l1.data);
  return answer;
}

// A rollup without a proven block: the root slot holds zero.
void test_no_block_anchored_on_l1(void) {
  uint8_t    zero        = 0;
  bytes_t    l1_answer   = synth_l1_answer(bytes(&zero, 1));
  override_t overrides[] = {{"eth_getStorageAt_", l1_answer}, {NULL, NULL_BYTES}};
  char*      error       = NULL;
  bytes_t    proof       = prove_with("eth_blockNumber", "[]", 0, NULL, overrides, &error);
  TEST_ASSERT_NULL(proof.data);
  TEST_ASSERT_NOT_NULL(error);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error, "no L2 block hash is stored on L1 for RollupX"), error);
  safe_free(error);
  safe_free(l1_answer.data);
}

// The `eth_getBlockByHash` answer of the anchor, optionally without `requestsHash` and
// with `extra` members (each followed by a comma) added. Returns an owned string.
static char* l2_block_json(bool requests_hash, const char* extra) {
  bytes_t file = read_testdata(FIXTURE "/eth_getBlockByHash_" ANCHOR_HASH "_false.json");
  TEST_ASSERT_NOT_NULL(file.data);
  char* text     = bprintf(NULL, "%r", file);
  char* requests = strstr(text, "\"requestsHash\":");
  char* size     = strstr(text, "\"size\":");
  TEST_ASSERT_NOT_NULL(requests);
  TEST_ASSERT_NOT_NULL(size);
  TEST_ASSERT_TRUE_MESSAGE(requests < size && strchr(requests, ',') + 1 == size, "`size` must follow `requestsHash` in the fixture");
  char* out = bprintf(NULL, "%r%s%s", bytes((uint8_t*) text, (uint32_t) ((requests_hash ? size : requests) - text)), extra ? extra : "", size);
  safe_free(text);
  safe_free(file.data);
  return out;
}

// Without a body the header is rebuilt from JSON. Its layout depends on the fork of the
// L2 block, which is derived from the members of the object. A wrong fork yields a header
// that does not hash to the anchor, i.e. no header-only proof at all after an L2 fork.
void test_header_fork_of_the_l2_block(void) {
  static const struct {
    const char* name;
    fork_id_t   fork;
    bool        requests_hash;
    const char* extra;
  } cases[] = {
      {"deneb", C4_FORK_DENEB, false, NULL},
      {"electra", C4_FORK_ELECTRA, true, NULL},
      {"gloas", C4_FORK_GLOAS, true, "\"blockAccessListHash\":\"0x1111111111111111111111111111111111111111111111111111111111111111\",\"slotNumber\":\"0x7\","},
      {"gloas (blockAccessListHash only)", C4_FORK_GLOAS, true, "\"blockAccessListHash\":\"0x1111111111111111111111111111111111111111111111111111111111111111\","},
      {"gloas (slotNumber only)", C4_FORK_GLOAS, true, "\"slotNumber\":\"0x7\","},
  };
  bytes32_t hashes[sizeof(cases) / sizeof(cases[0])] = {0};
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    char*      json     = l2_block_json(cases[i].requests_hash, cases[i].extra);
    c4_state_t state    = {0};
    bytes_t    expected = NULL_BYTES;
    TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, eth_el_header_build_from_json(&state, &expected, cases[i].fork, json_get(json_parse(json), "result")), state.error ? state.error : cases[i].name);
    keccak(expected, hashes[i]);

    bytes_t       l1_answer   = synth_l1_answer(bytes(hashes[i], 32));
    override_t    overrides[] = {{"eth_getStorageAt_", l1_answer}, {"eth_getBlockByHash_", str_bytes(json)}, {NULL, NULL_BYTES}};
    prover_ctx_t* ctx         = c4_prover_create("eth_blockNumber", "[]", ROLLUPX, 0);
    eth_block_t   block       = {0};
    c4_status_t   status      = C4_PENDING;
    for (int n = 0; n < 10 && status == C4_PENDING; n++) {
      status = eez_get_el_block(ctx, json_parse("\"latest\""), &block, false);
      if (status == C4_PENDING) answer_with_overrides(&ctx->state, overrides);
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, status, ctx->state.error ? ctx->state.error : cases[i].name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected.len, block.el_header.len, cases[i].name);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected.data, block.el_header.data, expected.len, cases[i].name);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(ANCHOR_NUMBER, block.slot, cases[i].name);
    c4_prover_free(ctx);
    safe_free(l1_answer.data);
    safe_free(expected.data);
    safe_free(json);
  }
  // the cases are different headers: a wrong fork cannot pass by accident
  TEST_ASSERT_FALSE(memcmp(hashes[0], hashes[1], 32) == 0);
  TEST_ASSERT_FALSE(memcmp(hashes[1], hashes[2], 32) == 0);
  ASSERT_HEX_STRING_EQUAL(ANCHOR_HASH_HEX, hashes[1], 32, "the unchanged fixture is the anchor");
}

// --- block identifiers and logs ---------------------------------------------

// Identifiers without a proof fail before anything is requested from L1 or L2.
void test_unprovable_block_identifiers_fail_without_a_request(void) {
  static const char* blocks[] = {
      "\"pending\"",
      "\"earliest\"",
      "\"0x\"",
      "\"0xzz\"",
      "\"889da\"",
      "\"0x11111111111111111\"",                                                  // 17 nibbles: no uint64
      "\"0xdfa307b750921da7ba51362dac4c00bc81766d1344bca4f385ac2137f009aa\"",     // 31 bytes: neither number nor hash
      "\"0xdfa307b750921da7ba51362dac4c00bc81766d1344bca4f385ac2137f009aaf3ff\"", // 33 bytes
      "{\"blockNumber\":\"0x889da\"}",
      "5",
  };
  for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++) {
    prover_ctx_t* ctx   = c4_prover_create("eth_getBlockByNumber", "[\"latest\",false]", ROLLUPX, 0);
    eth_block_t   block = {0};
    TEST_ASSERT_EQUAL_INT_MESSAGE(C4_ERROR, eez_get_el_block(ctx, json_parse((char*) blocks[i]), &block, false), blocks[i]);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx->state.error, "cannot be proven on RollupX"), ctx->state.error);
    TEST_ASSERT_NULL_MESSAGE(ctx->state.requests, "no request may be sent for an identifier without a proof");
    c4_prover_free(ctx);
  }

  // numbers and hashes of every case are accepted as identifiers (and then ask the L1)
  static const char* accepted[] = {"\"0x0\"", "\"0x889DA\"", "\"0xffffffffffffffff\"", "\"" ANCHOR_HASH "\"", "\"latest\"", "\"safe\"", "\"finalized\"", "null"};
  for (size_t i = 0; i < sizeof(accepted) / sizeof(accepted[0]); i++) {
    prover_ctx_t* ctx   = c4_prover_create("eth_getBlockByNumber", "[\"latest\",false]", ROLLUPX, 0);
    eth_block_t   block = {0};
    TEST_ASSERT_EQUAL_INT_MESSAGE(C4_PENDING, eez_get_el_block(ctx, json_parse((char*) accepted[i]), &block, false), accepted[i]);
    TEST_ASSERT_EQUAL_INT(1, count_requests(&ctx->state, C4_DATA_TYPE_PROVER));
    c4_prover_free(ctx);
  }
}

#define ZEROS_64  "0000000000000000000000000000000000000000000000000000000000000000"
#define ZEROS_512 ZEROS_64 ZEROS_64 ZEROS_64 ZEROS_64 ZEROS_64 ZEROS_64 ZEROS_64 ZEROS_64
#define TEST_LOG                                                                                                                    \
  "{\"address\":\"0x" ROLLUPX_PROXY "\",\"topics\":[\"0x" ZEROS_64 "\"],\"data\":\"0x\",\"blockNumber\":\"" ANCHOR_NUMBER_HEX "\"," \
  "\"transactionHash\":\"0x" ZEROS_64 "\",\"transactionIndex\":\"0x0\",\"blockHash\":\"" ANCHOR_HASH "\",\"logIndex\":\"0x0\",\"removed\":false}"

// Regression: `eth_getLogs` with at least one log crashed for a chain without a chain
// spec (NULL dereference when the sync period of the newest block was computed). An EEZ
// chain has no consensus layer and therefore no chain spec.
//
// The anchor of the fixture holds no transaction, so the log and its receipt are made up:
// the prover has to get through, and the verifier has to reject what it built.
void test_logs_proof_for_a_chain_without_chain_spec(void) {
  const char* method      = "eth_getLogs";
  const char* args        = "[{\"fromBlock\":\"" ANCHOR_NUMBER_HEX "\",\"toBlock\":\"" ANCHOR_NUMBER_HEX "\"}]";
  const char* logs        = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":[" TEST_LOG "]}";
  const char* receipts    = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":[{\"blockHash\":\"" ANCHOR_HASH "\",\"blockNumber\":\"" ANCHOR_NUMBER_HEX "\",\"contractAddress\":null,"
                            "\"cumulativeGasUsed\":\"0x5208\",\"effectiveGasPrice\":\"0x1\",\"from\":\"0x" ROLLUPX_PROXY "\",\"gasUsed\":\"0x5208\",\"logs\":[" TEST_LOG "],"
                            "\"logsBloom\":\"0x" ZEROS_512 "\",\"status\":\"0x1\",\"to\":\"0x" ROLLUPX_PROXY "\",\"transactionHash\":\"0x" ZEROS_64 "\",\"transactionIndex\":\"0x0\",\"type\":\"0x2\"}]}";
  override_t  overrides[] = {{"eth_getLogs_", str_bytes(logs)}, {"eth_getBlockReceipts_", str_bytes(receipts)}, {NULL, NULL_BYTES}};
  char*       error       = NULL;
  TEST_ASSERT_NULL_MESSAGE(c4_eth_get_chain_spec(ROLLUPX), "the test is about a chain without a chain spec");

  set_state(ROLLUPX, FIXTURE);
  bytes_t proof = prove_with(method, args, 0, NULL, overrides, &error);
  TEST_ASSERT_NOT_NULL_MESSAGE(proof.data, error ? error : "prover failed");
  safe_free(error);

  // one block, proven through L1, with the sync data of the L1 answer
  ssz_ob_t req    = {.def = eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST), .bytes = proof};
  ssz_ob_t blocks = ssz_get(&req, "proof");
  TEST_ASSERT_EQUAL_UINT32(1, ssz_len(blocks));
  ssz_ob_t block = ssz_at(blocks, 0);
  TEST_ASSERT_EQUAL_UINT64(ANCHOR_NUMBER, ssz_get_uint64(&block, "blockNumber"));
  TEST_ASSERT_EQUAL_STRING("eezL1Proof", ssz_get(&block, "elProof").def->name);
  TEST_ASSERT_TRUE_MESSAGE(ssz_get(&req, "sync_data").def->type != SSZ_TYPE_NONE, "sync data of the L1 answer");

  // the made-up receipt is not part of the (empty) anchor block
  TEST_ASSERT_EQUAL_INT(C4_ERROR, verify_request(proof, method, args, ROLLUPX, &error, NULL));
  TEST_ASSERT_NOT_NULL(error);
  safe_free(error);
  safe_free(proof.data);

  // no log at all: an empty proof
  override_t no_logs[] = {{"eth_getLogs_", str_bytes("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":[]}")}, {NULL, NULL_BYTES}};
  proof                = prove_with(method, args, 0, NULL, no_logs, &error);
  TEST_ASSERT_NOT_NULL_MESSAGE(proof.data, error ? error : "prover failed");
  safe_free(error);
  safe_free(proof.data);
}

// ---------------------------------------------------------------------------
// 4. blocks before the anchor (eip2935)
// ---------------------------------------------------------------------------

#define HISTORY_PROOF_REQUEST "eth_getProof_0x0000f90827f1c53a10cb7a02335b175320002935"

// The JSON-RPC answer of `eth_getProof` for the EIP-2935 history contract with `value`
// in the slot of block `number`. The tries are synthetic, so the proof is consistent in
// itself but does not belong to the state of the recorded anchor. `key_json` and
// `value_json` replace the `key` and the `value` field of the answer when set (a node
// may report both as quantities without leading zeros).
static bytes_t history_proof_answer_with(uint64_t number, bytes_t value, const char* key_json, const char* value_json) {
  bytes32_t slot = {0};
  buffer_t  json = {0};
  eth_eip2935_slot(number, slot);
  synth_t  synth         = synth_account_proof(ETH_EIP2935_HISTORY_ADDRESS, slot, value, SYNTH_VALID);
  ssz_ob_t account_proof = ssz_get(&synth.account, "accountProof");
  ssz_ob_t storage_list  = ssz_get(&synth.account, "storageProof");
  ssz_ob_t storage       = ssz_at(storage_list, 0);
  ssz_ob_t storage_proof = ssz_get(&storage, "proof");

  bprintf(&json, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"address\":\"0x%x\",\"accountProof\":[", bytes((uint8_t*) ETH_EIP2935_HISTORY_ADDRESS, 20));
  for (uint32_t i = 0; i < ssz_len(account_proof); i++) bprintf(&json, "%s\"0x%x\"", i ? "," : "", ssz_at(account_proof, i).bytes);
  // code hash and storage hash only have to be 32-byte words: the prover does not read them
  bprintf(&json, "],\"balance\":\"0x0\",\"codeHash\":\"0x%x\",\"nonce\":\"0x1\",\"storageHash\":\"0x%x\",\"storageProof\":[{\"key\":", bytes(slot, 32), bytes(slot, 32));
  if (key_json)
    bprintf(&json, "\"%s\"", key_json);
  else
    bprintf(&json, "\"0x%x\"", bytes(slot, 32));
  bprintf(&json, ",\"value\":");
  if (value_json)
    bprintf(&json, "\"%s\"", value_json);
  else
    bprintf(&json, "\"0x%x\"", value);
  bprintf(&json, ",\"proof\":[");
  for (uint32_t i = 0; i < ssz_len(storage_proof); i++) bprintf(&json, "%s\"0x%x\"", i ? "," : "", ssz_at(storage_proof, i).bytes);
  bprintf(&json, "]}]}}");
  safe_free(synth.account.bytes.data);
  return json.data;
}

// `history_proof_answer_with` with the full 32-byte key, the way it was requested.
static bytes_t history_proof_answer(uint64_t number, bytes_t value, const char* value_json) {
  return history_proof_answer_with(number, value, NULL, value_json);
}

// 32 bytes from a hex string without prefix.
static bytes_t hash_of(const char* hex, bytes32_t out) {
  TEST_ASSERT_EQUAL_INT(32, hex_to_bytes(hex, -1, bytes(out, 32)));
  return bytes(out, 32);
}

// Runs the prover for a block before the anchor; the history contract answers with `stored`
// for the slot of the parent of the anchor.
static bytes_t prove_before_anchor(const char* method, const char* args, bytes_t stored, char** error) {
  bytes_t    answer      = history_proof_answer(PARENT_NUMBER, stored, NULL);
  override_t overrides[] = {{HISTORY_PROOF_REQUEST, answer}, {NULL, NULL_BYTES}};
  bytes_t    proof       = prove_with(method, args, 0, NULL, overrides, error);
  safe_free(answer.data);
  return proof;
}

// The prover decides: the parent of the anchor is proven by an `eip2935` proof that wraps
// the `eezL1Proof` of the anchor.
void test_block_before_the_anchor_gets_an_eip2935_proof(void) {
  bytes32_t   parent        = {0};
  bytes32_t   hash          = {0};
  bytes32_t   slot          = {0};
  char*       error         = NULL;
  const char* requests[][2] = {
      {"eth_getBlockHeader", "[\"" PARENT_NUMBER_HEX "\"]"},         // header rebuilt from eth_getBlockByHash
      {"eth_getBlockByNumber", "[\"" PARENT_NUMBER_HEX "\",false]"}, // with body: raw block
      {"eth_getBlockHeader", "[\"" PARENT_HASH "\"]"},               // by hash: the number is looked up first
      {"eth_getBlockByHash", "[\"" PARENT_HASH "\",true]"},
  };
  set_state(ROLLUPX, FIXTURE);

  // what the anchor itself is proven with
  bytes_t anchor_request = prove("eth_getBlockByNumber", "[\"latest\",false]", &error);
  TEST_ASSERT_NOT_NULL_MESSAGE(anchor_request.data, error ? error : "prover failed");
  ssz_ob_t anchor_proof = eez_proof_of(anchor_request);
  safe_free(error);
  error = NULL;

  for (size_t i = 0; i < sizeof(requests) / sizeof(requests[0]); i++) {
    const char* what    = requests[i][1];
    bytes_t     request = prove_before_anchor(requests[i][0], requests[i][1], hash_of(PARENT_HASH_HEX, parent), &error);
    TEST_ASSERT_NOT_NULL_MESSAGE(request.data, error ? error : what);

    ssz_ob_t   req       = {.def = eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST), .bytes = request};
    c4_state_t ssz_state = {0};
    TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(req, true, &ssz_state), ssz_state.error ? ssz_state.error : what);
    ssz_ob_t proof = ssz_get(&req, "proof");
    ssz_ob_t el    = ssz_get(&proof, "elProof");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("eip2935", el.def->name, what);

    // outer header: the requested block
    bytes_t header = ssz_get(&el, "elHeader").bytes;
    keccak(header, hash);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(parent, hash, 32, what);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(PARENT_NUMBER, eth_el_header_get_uint64(header, EL_BLOCK_NUMBER), what);

    // history contract, slot of the requested block
    ssz_ob_t account = ssz_get(&el, "account");
    ssz_ob_t storage = ssz_get(&account, "storageProof");
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(ETH_EIP2935_HISTORY_ADDRESS, ssz_get(&account, "address").bytes.data, 20, what);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, ssz_len(storage), what);
    ssz_ob_t entry = ssz_at(storage, 0);
    eth_eip2935_slot(PARENT_NUMBER, slot);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(slot, ssz_get(&entry, "key").bytes.data, 32, what);
    TEST_ASSERT_EQUAL_UINT8(0x0a, slot[30]); // 559577 % 8191 = 2589 = 0x0a1d
    TEST_ASSERT_EQUAL_UINT8(0x1d, slot[31]);

    // inner proof: the anchor, exactly as it is proven on its own
    ssz_ob_t inner = ssz_get(&account, "elProof");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("eezL1Proof", inner.def->name, what);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(anchor_proof.bytes.len, inner.bytes.len, what);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(anchor_proof.bytes.data, inner.bytes.data, inner.bytes.len, what);

    // sync data of the L1 prover is still forwarded
    ssz_ob_t anchor_req = {.def = req.def, .bytes = anchor_request};
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(ssz_get(&anchor_req, "sync_data").bytes.len, ssz_get(&req, "sync_data").bytes.len, what);

    safe_free(request.data);
    safe_free(error);
    error = NULL;
  }
  safe_free(anchor_request.data);
}

// The prover does not check the proof of the L2 node. The verifier does: a history proof
// that does not belong to the state of the anchor is rejected, and so is everything the
// request would have returned.
void test_history_proof_of_another_state_is_rejected(void) {
  bytes32_t parent = {0};
  char*     error  = NULL;
  set_state(ROLLUPX, FIXTURE);
  bytes_t request = prove_before_anchor("eth_getBlockByNumber", "[\"" PARENT_NUMBER_HEX "\",false]", hash_of(PARENT_HASH_HEX, parent), &error);
  TEST_ASSERT_NOT_NULL_MESSAGE(request.data, error ? error : "prover failed");
  safe_free(error);

  assert_rejected(request, "eth_getBlockByNumber", "[\"" PARENT_NUMBER_HEX "\",false]", "account proof of the contract does not match the state root!");
#ifdef EL_HEADER_CACHE
  // the anchor inside the proof is verified and cached, the block before it is not
  TEST_ASSERT_NOT_NULL(c4_header_cache_get_by_number(ROLLUPX, ANCHOR_NUMBER));
  TEST_ASSERT_NULL(c4_header_cache_get_by_number(ROLLUPX, PARENT_NUMBER));
  TEST_ASSERT_NULL(c4_header_cache_get_by_hash(ROLLUPX, parent));
#endif
  safe_free(request.data);
}

// What the prover asks the L2 node for: the slot of the block in the state of the anchor.
void test_history_proof_request(void) {
  // 551387 = 559578 - 8191 is the oldest block of the window; its slot is the one of the anchor (0x0a1e)
  const char* blocks[][2] = {{PARENT_NUMBER_HEX, "0000000000000000000000000000000000000000000000000000000000000a1d"}, {"0x869db", "0000000000000000000000000000000000000000000000000000000000000a1e"}};
  for (size_t b = 0; b < 2; b++) {
    char        args[64];
    c4_status_t status = C4_PENDING;
    const char* found  = NULL;
    snprintf(args, sizeof(args), "[\"%s\",false]", blocks[b][0]);
    prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", args, ROLLUPX, 0);
    for (int i = 0; i < 20 && status == C4_PENDING && !found; i++) {
      status = c4_prover_execute(ctx);
      for (data_request_t* req = ctx->state.requests; req && !found; req = req->next)
        if (req->payload.data && strstr((char*) req->payload.data, "eth_getProof")) found = (char*) req->payload.data;
      if (status == C4_PENDING && !found) answer_from_fixture(&ctx->state);
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(found, blocks[b][0]);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(found, "0x0000f90827f1c53a10cb7a02335b175320002935"), found);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(found, blocks[b][1]), found);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(found, "\"" ANCHOR_NUMBER_HEX "\""), found);
    // the proof is asked from the L2 node; the only prover request is the one for the anchor on L1
    TEST_ASSERT_EQUAL_INT(1, count_requests(&ctx->state, C4_DATA_TYPE_PROVER));
    c4_prover_free(ctx);
  }
}

// The answer of the L2 node for the history slot is checked as far as the prover can.
void test_history_proof_answer_is_checked(void) {
  bytes32_t buf   = {0};
  char*     error = NULL;
  set_state(ROLLUPX, FIXTURE);

  // a slot that holds nothing
  bytes_t    empty       = history_proof_answer(PARENT_NUMBER, hash_of(PARENT_HASH_HEX, buf), "0x0");
  override_t overrides[] = {{HISTORY_PROOF_REQUEST, empty}, {NULL, NULL_BYTES}};
  TEST_ASSERT_NULL(prove_with("eth_getBlockByNumber", "[\"" PARENT_NUMBER_HEX "\",false]", 0, NULL, overrides, &error).data);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error, "the EIP-2935 history contract of RollupX holds no hash for block 559577 in the state of block 559578"), error);
  safe_free(error);
  safe_free(empty.data);

  // a block the L2 node does not know
  memset(buf, 0x33, 32);
  TEST_ASSERT_NULL(prove_before_anchor("eth_getBlockHeader", "[\"" PARENT_NUMBER_HEX "\"]", bytes(buf, 32), &error).data);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error, "the L2 node does not know the block 0x3333"), error);
  safe_free(error);

  // a block of another height in the slot (header only, and with the body)
  TEST_ASSERT_NULL(prove_before_anchor("eth_getBlockHeader", "[\"" PARENT_NUMBER_HEX "\"]", hash_of(ANCHOR_HASH_HEX, buf), &error).data);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error, "the EIP-2935 history contract of RollupX does not hold block 559577 in its slot"), error);
  safe_free(error);
  TEST_ASSERT_NULL(prove_before_anchor("eth_getBlockByNumber", "[\"" PARENT_NUMBER_HEX "\",true]", hash_of(ANCHOR_HASH_HEX, buf), &error).data);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error, "does not hold block 559577 in its slot"), error);
  safe_free(error);

  // requested by hash, but the chain anchored on L1 has another block at that height
  TEST_ASSERT_NULL(prove_before_anchor("eth_getBlockByHash", "[\"" PARENT_HASH "\",false]", hash_of(ANCHOR_HASH_HEX, buf), &error).data);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error, "is not part of the chain anchored on L1 (block 559577 has hash " ANCHOR_HASH ")"), error);
  safe_free(error);

  // the proof of another slot
  bytes_t    other_slot = history_proof_answer(PARENT_NUMBER + 1, hash_of(PARENT_HASH_HEX, buf), NULL);
  override_t other[]    = {{HISTORY_PROOF_REQUEST, other_slot}, {NULL, NULL_BYTES}};
  TEST_ASSERT_NULL(prove_with("eth_getBlockByNumber", "[\"" PARENT_NUMBER_HEX "\",false]", 0, NULL, other, &error).data);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error, "returned the proof of another slot"), error);
  safe_free(error);
  safe_free(other_slot.data);

  // the node fails (RollupX: "distance to target block exceeds maximum proof window")
  assert_prover_error_with("eth_getBlockByNumber", "[\"" PARENT_NUMBER_HEX "\",false]", "eth_getProof_", NULL_BYTES, "overridden");
  assert_prover_error_with("eth_getBlockByNumber", "[\"" PARENT_NUMBER_HEX "\",false]", "eth_getProof_", str_bytes("{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":-32000,\"message\":\"distance to target block exceeds maximum proof window\"}}"), "distance to target block exceeds maximum proof window");
}

// A node may echo the storage key as a quantity. The proof always carries the 32-byte word.
void test_history_proof_with_a_short_key(void) {
  static const char* keys[] = {"0xa1d", "0x0a1d", "0x0000000000000000000000000000000000000000000000000000000000000a1d"};
  bytes32_t          parent = {0};
  bytes32_t          slot   = {0};
  char*              error  = NULL;
  set_state(ROLLUPX, FIXTURE);
  eth_eip2935_slot(PARENT_NUMBER, slot);

  for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
    bytes_t    answer      = history_proof_answer_with(PARENT_NUMBER, hash_of(PARENT_HASH_HEX, parent), keys[i], NULL);
    override_t overrides[] = {{HISTORY_PROOF_REQUEST, answer}, {NULL, NULL_BYTES}};
    bytes_t    request     = prove_with("eth_getBlockHeader", "[\"" PARENT_NUMBER_HEX "\"]", 0, NULL, overrides, &error);
    TEST_ASSERT_NOT_NULL_MESSAGE(request.data, error ? error : keys[i]);

    ssz_ob_t   req       = {.def = eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST), .bytes = request};
    c4_state_t ssz_state = {0};
    TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(req, true, &ssz_state), ssz_state.error ? ssz_state.error : keys[i]);
    ssz_ob_t proof   = ssz_get(&req, "proof");
    ssz_ob_t el      = ssz_get(&proof, "elProof");
    ssz_ob_t account = ssz_get(&el, "account");
    ssz_ob_t entry   = ssz_at(ssz_get(&account, "storageProof"), 0);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("eip2935", el.def->name, keys[i]);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(32, ssz_get(&entry, "key").bytes.len, keys[i]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(slot, ssz_get(&entry, "key").bytes.data, 32, keys[i]);

    safe_free(request.data);
    safe_free(answer.data);
    safe_free(error);
    error = NULL;
  }

  // a short key of another slot, and a key that is no 32-byte word
  static const char* rejected[][2] = {
      {"0xa1e", "returned the proof of another slot"},
      {"0x010000000000000000000000000000000000000000000000000000000000000a1d", "eth_getProof"},
  };
  for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++) {
    bytes_t    answer      = history_proof_answer_with(PARENT_NUMBER, hash_of(PARENT_HASH_HEX, parent), rejected[i][0], NULL);
    override_t overrides[] = {{HISTORY_PROOF_REQUEST, answer}, {NULL, NULL_BYTES}};
    TEST_ASSERT_NULL_MESSAGE(prove_with("eth_getBlockHeader", "[\"" PARENT_NUMBER_HEX "\"]", 0, NULL, overrides, &error).data, rejected[i][0]);
    TEST_ASSERT_NOT_NULL_MESSAGE(error, rejected[i][0]);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error, rejected[i][1]), error);
    safe_free(answer.data);
    safe_free(error);
    error = NULL;
  }
}

// Runs `c4_eth_get_eip2935_proof` for the parent of the anchor against a node that answers
// with `answer`. Returns the status; `hash` receives the stored hash, `error` an owned copy
// of the error.
static c4_status_t read_history_slot(bytes_t answer, bytes32_t hash, char** error) {
  override_t    overrides[] = {{HISTORY_PROOF_REQUEST, answer}, {NULL, NULL_BYTES}};
  prover_ctx_t* ctx         = c4_prover_create("eth_getBlockHeader", "[\"" PARENT_NUMBER_HEX "\"]", ROLLUPX, 0);
  json_t        proof       = {0};
  c4_status_t   status      = C4_PENDING;
  for (int i = 0; i < 5 && status == C4_PENDING; i++) {
    status = c4_eth_get_eip2935_proof(ctx, PARENT_NUMBER, ANCHOR_NUMBER, &proof, hash);
    if (status == C4_PENDING) answer_with_overrides(&ctx->state, overrides);
  }
  if (error) *error = ctx->state.error ? strdup(ctx->state.error) : NULL;
  c4_prover_free(ctx);
  return status;
}

// The storage value is a quantity: a hash with leading zero nibbles comes back shorter.
void test_history_proof_value_shapes(void) {
  bytes32_t parent   = {0};
  bytes32_t hash     = {0};
  bytes32_t expected = {0};
  char*     error    = NULL;
  hash_of(PARENT_HASH_HEX, parent);

  // 63 nibbles: the first nibble of the hash is zero
  memcpy(expected, parent, 32);
  expected[0] &= 0x0f;
  bytes_t answer = history_proof_answer(PARENT_NUMBER, bytes(parent, 32), "0x"
                                                                          "6523831d64a5c58c1cb5fdaaa47975c96930b036257a4d4c35e48ce5dadeac2");
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, read_history_slot(answer, hash, &error), error ? error : "63 nibbles");
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, hash, 32);
  safe_free(answer.data);
  safe_free(error);

  // a single byte
  memset(expected, 0, 32);
  expected[31] = 0xab;
  answer       = history_proof_answer(PARENT_NUMBER, bytes(parent, 32), "0xab");
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, read_history_slot(answer, hash, &error), error ? error : "one byte");
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, hash, 32);
  safe_free(answer.data);
  safe_free(error);

  // an empty slot
  memset(expected, 0, 32);
  memset(hash, 0xff, 32);
  answer = history_proof_answer(PARENT_NUMBER, bytes(parent, 32), "0x0");
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, read_history_slot(answer, hash, &error), error ? error : "zero");
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, hash, 32);
  safe_free(answer.data);
  safe_free(error);

  // not a quantity of at most 32 bytes: an error, and never a hash
  static const char* invalid[] = {
      "0x0036523831d64a5c58c1cb5fdaaa47975c96930b036257a4d4c35e48ce5dadeac2",  // leading zeros
      "0x136523831d64a5c58c1cb5fdaaa47975c96930b036257a4d4c35e48ce5dadeac2",   // 65 nibbles
      "0x01136523831d64a5c58c1cb5fdaaa47975c96930b036257a4d4c35e48ce5dadeac2", // 33 bytes
  };
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    memset(hash, 0xff, 32);
    answer = history_proof_answer(PARENT_NUMBER, bytes(parent, 32), invalid[i]);
    TEST_ASSERT_EQUAL_INT_MESSAGE(C4_ERROR, read_history_slot(answer, hash, &error), invalid[i]);
    TEST_ASSERT_NOT_NULL_MESSAGE(error, invalid[i]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected, hash, 32, invalid[i]);
    safe_free(answer.data);
    safe_free(error);
    error = NULL;
  }
}

// The node must answer with exactly the one storage proof that was asked for.
void test_history_proof_needs_exactly_one_storage_proof(void) {
  bytes32_t parent = {0};
  bytes32_t hash   = {0};
  char*     error  = NULL;
  bytes_t   answer = history_proof_answer(PARENT_NUMBER, hash_of(PARENT_HASH_HEX, parent), NULL);
  char*     list   = strstr((char*) answer.data, "\"storageProof\":[");
  TEST_ASSERT_NOT_NULL(list);
  char* entry = list + strlen("\"storageProof\":[");
  char* end   = strstr(entry, "}]}}");
  TEST_ASSERT_NOT_NULL(end);

  // no entry
  buffer_t none = {0};
  buffer_append(&none, bytes(answer.data, (uint32_t) (entry - (char*) answer.data)));
  buffer_add_chars(&none, "]}}");
  TEST_ASSERT_EQUAL_INT(C4_ERROR, read_history_slot(none.data, hash, &error));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error, "must return exactly one storage proof"), error);
  safe_free(error);
  buffer_free(&none);

  // the same entry twice
  buffer_t twice = {0};
  buffer_append(&twice, bytes(answer.data, (uint32_t) (end + 1 - (char*) answer.data)));
  buffer_add_chars(&twice, ",");
  buffer_append(&twice, bytes((uint8_t*) entry, (uint32_t) (end + 1 - entry)));
  buffer_add_chars(&twice, "]}}");
  TEST_ASSERT_EQUAL_INT(C4_ERROR, read_history_slot(twice.data, hash, &error));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(error, "must return exactly one storage proof"), error);
  safe_free(error);
  buffer_free(&twice);
  safe_free(answer.data);
}

// The anchor requested by its number or by its hash is the anchor: the same `eezL1Proof`
// as for `latest`, without a history proof, and it verifies.
void test_anchor_by_number_and_hash_without_body(void) {
  static const char* args[] = {"[\"" ANCHOR_NUMBER_HEX "\"]", "[\"" ANCHOR_HASH "\"]"};
  char*              error  = NULL;
  set_state(ROLLUPX, FIXTURE);
  bytes_t latest = prove("eth_getBlockHeader", "[\"latest\"]", &error);
  TEST_ASSERT_NOT_NULL_MESSAGE(latest.data, error ? error : "prover failed");
  safe_free(error);
  error = NULL;

  for (size_t i = 0; i < sizeof(args) / sizeof(args[0]); i++) {
    bytes_t request = prove("eth_getBlockHeader", args[i], &error);
    TEST_ASSERT_NOT_NULL_MESSAGE(request.data, error ? error : args[i]);
    safe_free(error);
    error = NULL;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(latest.len, request.len, args[i]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(latest.data, request.data, latest.len, args[i]);

    reset_local_filecache();
    c4_header_cache_clear();
    set_state(ROLLUPX, FIXTURE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, verify_request(request, "eth_getBlockHeader", args[i], ROLLUPX, &error, NULL), error ? error : args[i]);
    safe_free(error);
    error = NULL;
    safe_free(request.data);
  }
  safe_free(latest.data);
}

// A client that already verified the block before the anchor gets the `blockHash` variant.
void test_block_before_the_anchor_known_to_the_client(void) {
  bytes32_t  parent      = {0};
  char*      error       = NULL;
  bytes_t    answer      = history_proof_answer(PARENT_NUMBER, hash_of(PARENT_HASH_HEX, parent), NULL);
  override_t overrides[] = {{HISTORY_PROOF_REQUEST, answer}, {NULL, NULL_BYTES}};
  set_state(ROLLUPX, FIXTURE);
  bytes_t request = prove_with("eth_getBlockByNumber", "[\"" PARENT_NUMBER_HEX "\",false]", 0, parent, overrides, &error);
  TEST_ASSERT_NOT_NULL_MESSAGE(request.data, error ? error : "prover failed");
  ssz_ob_t req   = {.def = eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST), .bytes = request};
  ssz_ob_t proof = ssz_get(&req, "proof");
  ssz_ob_t el    = ssz_get(&proof, "elProof");
  TEST_ASSERT_EQUAL_STRING("blockHash", el.def->name);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(parent, el.bytes.data, 32);
  safe_free(request.data);
  safe_free(answer.data);
  safe_free(error);
}

// A block before the anchor with recorded data only: local mode builds the `eip2935` proof
// around the `eezL1Proof` of the anchor from the answers of RollupX and Ethereum mainnet,
// and the verifier accepts it. Nothing in this proof is synthetic.
void test_fixture_block_before_the_anchor(void) {
  bytes32_t expected = {0};
  bytes32_t hash     = {0};
  bytes32_t slot     = {0};

  // prover and verifier end to end; the result is the block as the node returned it
  run_rpc_test(HISTORY_FIXTURE, 0, 0);
  TEST_ASSERT_TRUE(stored("states_1"));
  TEST_ASSERT_FALSE(stored("states_696991"));

  // the same request once more, to look at the proof the verifier accepted
  reset_local_filecache();
  c4_header_cache_clear();
  set_state(ROLLUPX, HISTORY_FIXTURE);
  c4_rpc_ctx_t* rpc    = c4_rpc_ctx_create("eth_getBlockByNumber", "[\"" HISTORY_BLOCK_NUMBER_HEX "\",false]", ROLLUPX, 0, 0, C4_PROVER_MODE_LOCAL);
  c4_status_t   status = C4_PENDING;
  for (int i = 0; i < 40 && status == C4_PENDING; i++) {
    status = c4_rpc_execute(rpc);
    if (status == C4_PENDING) answer_from(c4_rpc_get_state(rpc), HISTORY_FIXTURE);
  }
  const char* msg = rpc_error(rpc);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, status, msg ? msg : "no error");

  // outer proof: the requested block, proven through the history contract
  ssz_ob_t el = ssz_get(&rpc->verifier.proof, "elProof");
  TEST_ASSERT_NOT_NULL(el.def);
  TEST_ASSERT_EQUAL_STRING("eip2935", el.def->name);
  bytes_t header = ssz_get(&el, "elHeader").bytes;
  keccak(header, hash);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(hash_of(HISTORY_BLOCK_HASH_HEX, expected).data, hash, 32);
  TEST_ASSERT_EQUAL_UINT64(HISTORY_BLOCK_NUMBER, eth_el_header_get_uint64(header, EL_BLOCK_NUMBER));

  // history contract of the rollup, slot of the requested block
  ssz_ob_t account = ssz_get(&el, "account");
  ssz_ob_t storage = ssz_get(&account, "storageProof");
  TEST_ASSERT_EQUAL_UINT8_ARRAY(ETH_EIP2935_HISTORY_ADDRESS, ssz_get(&account, "address").bytes.data, 20);
  TEST_ASSERT_EQUAL_UINT32(1, ssz_len(storage));
  ssz_ob_t entry = ssz_at(storage, 0);
  eth_eip2935_slot(HISTORY_BLOCK_NUMBER, slot);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(slot, ssz_get(&entry, "key").bytes.data, 32);

  // inner proof: the anchor, proven on L1
  ssz_ob_t inner = ssz_get(&account, "elProof");
  TEST_ASSERT_NOT_NULL(inner.def);
  TEST_ASSERT_EQUAL_STRING("eezL1Proof", inner.def->name);
  bytes_t anchor = ssz_get(&inner, "elHeader").bytes;
  keccak(anchor, hash);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(hash_of(HISTORY_ANCHOR_HASH_HEX, expected).data, hash, 32);
  TEST_ASSERT_EQUAL_UINT64(HISTORY_ANCHOR_NUMBER, eth_el_header_get_uint64(anchor, EL_BLOCK_NUMBER));

  c4_rpc_ctx_free(rpc);
}

// The stand-alone prover of the L2 (`createProof` of the bindings) proves the same block
// with the answer of an L1 prover, which the fixture holds as well. The bindings run every
// fixture this way.
void test_history_fixture_serves_the_standalone_prover(void) {
  const char*   args   = "[\"" HISTORY_BLOCK_NUMBER_HEX "\",false]";
  char*         error  = NULL;
  c4_status_t   status = C4_PENDING;
  int           l1     = 0;
  prover_ctx_t* ctx    = c4_prover_create("eth_getBlockByNumber", (char*) args, ROLLUPX, 0);
  for (int i = 0; i < 20 && status == C4_PENDING; i++) {
    status = c4_prover_execute(ctx);
    for (data_request_t* req = ctx->state.requests; req; req = req->next)
      if (req->type == C4_DATA_TYPE_PROVER && c4_state_is_pending(req)) l1++;
    if (status == C4_PENDING) answer_from(&ctx->state, HISTORY_FIXTURE);
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, status, ctx->state.error ? ctx->state.error : "prover failed");
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, l1, "the prover of the L1 is asked once");
  bytes_t request = bytes_dup(ctx->proof);
  c4_prover_free(ctx);

  ssz_ob_t req   = {.def = eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST), .bytes = request};
  ssz_ob_t proof = ssz_get(&req, "proof");
  ssz_ob_t el    = ssz_get(&proof, "elProof");
  TEST_ASSERT_NOT_NULL(el.def);
  TEST_ASSERT_EQUAL_STRING("eip2935", el.def->name);
  TEST_ASSERT_EQUAL_UINT64(HISTORY_BLOCK_NUMBER, eth_el_header_get_uint64(ssz_get(&el, "elHeader").bytes, EL_BLOCK_NUMBER));

  set_state(ROLLUPX, HISTORY_FIXTURE);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, verify_request(request, "eth_getBlockByNumber", (char*) args, ROLLUPX, &error, NULL), error ? error : "no error");
  safe_free(request.data);
  safe_free(error);
}

#ifdef EL_HEADER_CACHE
// A complete request with an `eip2935` proof verifies for RollupX. The proof is synthetic:
// the newer block is referenced by hash, so it must be a header the verifier holds for the
// rollup. A header cached for the L1 does not count.
void test_eip2935_request_verifies_for_rollupx(void) {
  synth_eip2935_opts_t opts  = {.older = SYNTH_EIP2935_NEWER - 100, .account = SYNTH_VALID, .unknown_newer = true};
  synth_eip2935_case_t c     = synth_eip2935_case(opts, ROLLUPX);
  char*                error = NULL;
  char                 args[40];
  uint8_t              none = 0;
  snprintf(args, sizeof(args), "[\"0x%llx\"]", (unsigned long long) opts.older);

  // a header-only block proof around the synthetic `eip2935` member
  ssz_builder_t block = ssz_builder_for_type(ETH_SSZ_VERIFY_BLOCK_PROOF);
  ssz_add_bytes(&block, "body", bytes(&none, 1));
  ssz_add_ob(&block, "elProof", c.proof);
  ssz_ob_t proof = ssz_builder_to_bytes(&block);

  set_state(ROLLUPX, FIXTURE);
  bytes_t template = prove("eth_getBlockHeader", "[\"latest\"]", &error);
  TEST_ASSERT_NOT_NULL_MESSAGE(template.data, error ? error : "prover failed");
  safe_free(error);
  error           = NULL;
  bytes_t request = rebuild_request(template, &proof, false);

  // the newer block is unknown
  c4_header_cache_clear();
  assert_rejected(request, "eth_getBlockHeader", args, "referenced block header not found in the verifier cache!");
  // known as a block of the L1 only
  c4_header_cache_put(C4_CHAIN_MAINNET, SYNTH_EIP2935_NEWER, c.newer_hash, c.newer_header, NULL);
  assert_rejected(request, "eth_getBlockHeader", args, "referenced block header not found in the verifier cache!");
  TEST_ASSERT_NULL(c4_header_cache_get_by_hash(ROLLUPX, c.older_hash));

  // known as a block of the rollup: accepted, and the older block is cached for the rollup
  c4_header_cache_put(ROLLUPX, SYNTH_EIP2935_NEWER, c.newer_hash, c.newer_header, NULL);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, verify_request(request, "eth_getBlockHeader", args, ROLLUPX, &error, NULL), error ? error : "no error");
  safe_free(error);
  TEST_ASSERT_NOT_NULL(c4_header_cache_get_by_hash(ROLLUPX, c.older_hash));
  TEST_ASSERT_NULL(c4_header_cache_get_by_hash(C4_CHAIN_MAINNET, c.older_hash));

  // the proof does not answer a request for another block
  error = NULL;
  TEST_ASSERT_EQUAL_INT(C4_ERROR, verify_request(request, "eth_getBlockHeader", "[\"0x1\"]", ROLLUPX, &error, NULL));
  TEST_ASSERT_NOT_NULL(error);
  safe_free(error);

  c4_header_cache_clear();
  safe_free(request.data);
  safe_free(template.data);
  safe_free(proof.bytes.data);
  synth_eip2935_free(&c);
}
#endif

// The resolved block is kept per request: the second lookup of the same block sends nothing.
void test_block_before_the_anchor_is_resolved_once(void) {
  bytes32_t     parent      = {0};
  bytes_t       answer      = history_proof_answer(PARENT_NUMBER, hash_of(PARENT_HASH_HEX, parent), NULL);
  override_t    overrides[] = {{HISTORY_PROOF_REQUEST, answer}, {NULL, NULL_BYTES}};
  prover_ctx_t* ctx         = c4_prover_create("eth_getBlockByNumber", "[\"" PARENT_NUMBER_HEX "\",false]", ROLLUPX, 0);
  eth_block_t   block       = {0};
  eth_block_t   again       = {0};
  c4_status_t   status      = C4_PENDING;
  json_t        id          = json_parse("\"" PARENT_NUMBER_HEX "\"");
  for (int i = 0; i < 20 && status == C4_PENDING; i++) {
    status = eez_get_el_block(ctx, id, &block, false);
    if (status == C4_PENDING) answer_with_overrides(&ctx->state, overrides);
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, status, ctx->state.error ? ctx->state.error : "block not resolved");
  TEST_ASSERT_EQUAL_INT(C4_BLOCK_PROOF_TYPE_EEZ, block.proof_type);
  TEST_ASSERT_EQUAL_UINT64(PARENT_NUMBER, block.slot);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(parent, block.el_block_hash, 32);
  TEST_ASSERT_NOT_NULL(block.eez.history.newer_header.data);
  TEST_ASSERT_EQUAL_UINT64(ANCHOR_NUMBER, eth_el_header_get_uint64(block.eez.history.newer_header, EL_BLOCK_NUMBER));
  TEST_ASSERT_NOT_NULL(block.eez.history.history_proof.start);

  int requests = 0;
  for (data_request_t* req = ctx->state.requests; req; req = req->next) requests++;
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eez_get_el_block(ctx, id, &again, false));
  int after = 0;
  for (data_request_t* req = ctx->state.requests; req; req = req->next) after++;
  TEST_ASSERT_EQUAL_INT(requests, after);
  TEST_ASSERT_EQUAL_PTR(block.el_header.data, again.el_header.data);
  TEST_ASSERT_EQUAL_PTR(block.eez.history.newer_header.data, again.eez.history.newer_header.data);

  // the anchor of the same context has no history proof
  eth_block_t anchor = {0};
  status             = C4_PENDING;
  for (int i = 0; i < 20 && status == C4_PENDING; i++) {
    status = eez_get_el_block(ctx, json_parse("\"latest\""), &anchor, false);
    if (status == C4_PENDING) answer_with_overrides(&ctx->state, overrides);
  }
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, status);
  TEST_ASSERT_EQUAL_UINT64(ANCHOR_NUMBER, anchor.slot);
  TEST_ASSERT_NULL(anchor.eez.history.newer_header.data);
  TEST_ASSERT_NULL(anchor.eez.history.history_proof.start);

  c4_prover_free(ctx);
  safe_free(answer.data);
}

// The verifier hook of an EEZ chain accepts `eip2935` proofs. The proof is synthetic: the
// newer block is a header the context already verified for RollupX.
void test_block_hook_accepts_eip2935(void) {
  synth_eip2935_opts_t opts   = {.older = SYNTH_EIP2935_NEWER - 100, .account = SYNTH_VALID};
  synth_eip2935_case_t c      = synth_eip2935_case(opts, ROLLUPX);
  bytes_t              header = NULL_BYTES;
  bytes32_t            hash   = {0};

  // through the dispatcher of the Ethereum module, as every proof verifier calls it
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, c4_verify_block(&c.ctx, c.proof, &header, hash), c.ctx.state.error ? c.ctx.state.error : "eip2935 must be accepted for RollupX");
  TEST_ASSERT_EQUAL_UINT8_ARRAY(c.older_hash, hash, 32);
  TEST_ASSERT_EQUAL_UINT64(opts.older, eth_el_header_get_uint64(header, EL_BLOCK_NUMBER));
  TEST_ASSERT_TRUE(c.ctx.chain_id == ROLLUPX);
#ifdef EL_HEADER_CACHE
  // cached as a block of the rollup, not of its L1
  TEST_ASSERT_NOT_NULL(c4_header_cache_get_by_hash(ROLLUPX, c.older_hash));
  TEST_ASSERT_NULL(c4_header_cache_get_by_hash(C4_CHAIN_MAINNET, c.older_hash));
#endif
  synth_eip2935_free(&c);

  // the window is enforced for the rollup as well
  opts.older = SYNTH_EIP2935_NEWER - 8192;
  c          = synth_eip2935_case(opts, ROLLUPX);
  header     = NULL_BYTES;
  TEST_ASSERT_EQUAL_INT(C4_ERROR, eez_verify_block_proof(&c.ctx, c.proof, &header, hash));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(c.ctx.state.error, "block is outside the EIP-2935 history window of the newer block!"), c.ctx.state.error);
  TEST_ASSERT_NULL(header.data);
  synth_eip2935_free(&c);
}

#endif // NO_CHAIN_EEZ

#ifdef NO_CHAIN_EEZ
void setUp(void) {}
void tearDown(void) {}
#endif

int main(void) {
  UNITY_BEGIN();
#ifndef NO_CHAIN_EEZ
  RUN_TEST(test_chain_table_matches_util);
  RUN_TEST(test_rollupx_config);
  RUN_TEST(test_root_slot);
  RUN_TEST(test_bytes32_add_carries);
  RUN_TEST(test_method_types);
  RUN_TEST(test_module_ignores_other_chains);
  RUN_TEST(test_local_method_applies_sync_first);
  RUN_TEST(test_freshness_gate_is_suspended_for_eez_only);

  RUN_TEST(test_storage_value_valid);
  RUN_TEST(test_storage_value_short_is_left_padded);
  RUN_TEST(test_storage_value_longer_than_32_bytes);
  RUN_TEST(test_storage_value_wrong_contract);
  RUN_TEST(test_storage_value_wrong_slot);
  RUN_TEST(test_storage_value_needs_exactly_one_storage_proof);
  RUN_TEST(test_storage_value_wrong_state_root);
  RUN_TEST(test_storage_value_wrong_storage_hash);
  RUN_TEST(test_storage_value_malformed_account);
  RUN_TEST(test_storage_value_exclusion_proof);
  RUN_TEST(test_storage_value_truncated_proofs);

  RUN_TEST(test_fixture_block_by_number);
  RUN_TEST(test_local_fixture_serves_the_standalone_prover);
  RUN_TEST(test_fixture_block_by_number_remote);
  RUN_TEST(test_local_mode_from_a_checkpoint);
  RUN_TEST(test_fixture_sync_data_is_forwarded_and_stored_for_l1);
  RUN_TEST(test_fixture_proof_contains_sync_data);
  RUN_TEST(test_fixture_methods_and_block_identifiers);
  RUN_TEST(test_fixture_latest_passes_freshness_bound);
  RUN_TEST(test_fixture_blocks_other_than_the_anchor);
  RUN_TEST(test_fixture_proof_for_other_block_number_is_rejected);
  RUN_TEST(test_l1_prover_request);
  RUN_TEST(test_l1_prover_request_block_tag);
  RUN_TEST(test_l1_prover_answer_is_checked);
  RUN_TEST(test_tampered_proofs_are_rejected);
  RUN_TEST(test_block_hook_accepts_the_anchor);
  RUN_TEST(test_block_hook_rejects_other_variants);
  RUN_TEST(test_l1_block_by_hash_is_rejected);
  RUN_TEST(test_el_header_shape);
  RUN_TEST(test_l1_proof_is_not_a_proof_for_the_l2);

  RUN_TEST(test_l1_block_pending_and_error_keep_the_l2_chain_id);
  RUN_TEST(test_remote_prover_mode_and_block_hash_variant);
  RUN_TEST(test_anchor_snapshot_is_reused);
  RUN_TEST(test_l2_node_answer_is_checked);
  RUN_TEST(test_l1_prover_answer_must_be_an_account_proof_with_a_cl_proof);
  RUN_TEST(test_l1_answer_without_sync_data);
#ifdef EL_HEADER_CACHE
  RUN_TEST(test_hybrid_and_light_client_mode);
#endif
  RUN_TEST(test_prover_sends_l1_client_state_to_l1_prover);
  RUN_TEST(test_local_mode_builds_the_l1_proof_locally);
  RUN_TEST(test_remote_mode_falls_back_to_a_local_proof);
  RUN_TEST(test_local_mode_reports_the_error_of_the_l1_proof);
  RUN_TEST(test_local_methods_and_unknown_eez_chain);
  RUN_TEST(test_storage_value_malformed_leaf);
  RUN_TEST(test_storage_value_malformed_storage_hash_and_leaf);
  RUN_TEST(test_no_block_anchored_on_l1);
  RUN_TEST(test_header_fork_of_the_l2_block);
  RUN_TEST(test_unprovable_block_identifiers_fail_without_a_request);
  RUN_TEST(test_logs_proof_for_a_chain_without_chain_spec);

  RUN_TEST(test_block_before_the_anchor_gets_an_eip2935_proof);
  RUN_TEST(test_history_proof_of_another_state_is_rejected);
  RUN_TEST(test_history_proof_request);
  RUN_TEST(test_history_proof_answer_is_checked);
  RUN_TEST(test_history_proof_with_a_short_key);
  RUN_TEST(test_history_proof_value_shapes);
  RUN_TEST(test_history_proof_needs_exactly_one_storage_proof);
  RUN_TEST(test_anchor_by_number_and_hash_without_body);
  RUN_TEST(test_block_before_the_anchor_known_to_the_client);
  RUN_TEST(test_fixture_block_before_the_anchor);
  RUN_TEST(test_history_fixture_serves_the_standalone_prover);
#ifdef EL_HEADER_CACHE
  RUN_TEST(test_eip2935_request_verifies_for_rollupx);
#endif
  RUN_TEST(test_block_before_the_anchor_is_resolved_once);
  RUN_TEST(test_block_hook_accepts_eip2935);
#endif
  return UNITY_END();
}
