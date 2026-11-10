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
// Three groups:
//
//   1. chain table, slot computation and method types (no proof involved)
//   2. `eez_get_l1_storage_value` against synthetic Merkle-Patricia tries, so every
//      rejection path can be reached with a consistent proof
//   3. the recorded RollupX fixture `test/data/eez_eth_getBlockByNumber`: prover and
//      verifier end to end, the L1 prover request, and tampered `eezL1Proof`s
//
// The fixture holds one answer of the Ethereum mainnet prover (`eth_getStorageAt` on the
// EEZ proxy) and the anchored L2 block 0x889da of RollupX. The `eth_getBlockByHash_0x1111..`,
// `0x2222..`, `0x3333..` and `0x4444..` files are hand-made: copies of the anchor block with a
// different `hash` / `number` (or a `null` result), used to reach the error messages for
// blocks that are not the anchor.

#include "c4_assert.h"
#include "unity.h"

#ifndef NO_CHAIN_EEZ

#include "eez_chains_conf.h"
#include "eez_prover.h"
#include "eez_verify.h"
#include "el_header.h"
#include "eth_verify.h"
#include "header_cache.h"
#include "patricia.h"
#include "rlp.h"
#include "version.h"

#define ROLLUPX     C4_CHAIN_EEZ_ROLLUPX
#define FIXTURE     "eez_eth_getBlockByNumber"
#define L1_RESPONSE FIXTURE "/eth_getStorageAt_0x4b2760f84b83d6b3829e30a7573b0172b6d6402c_0x679795a0195a1b76cdebb7c51d74e058aee929.ssz"

// values of the fixture
#define ANCHOR_NUMBER     0x889da
#define ANCHOR_NUMBER_HEX "0x889da"
#define ANCHOR_HASH_HEX   "dfa307b750921da7ba51362dac4c00bc81766d1344bca4f385ac2137f009aaf3"
#define ANCHOR_HASH       "0x" ANCHOR_HASH_HEX
#define ROLLUPX_PROXY     "4b2760f84b83d6b3829e30a7573b0172b6d6402c"
// keccak256(pad32(rollupId = 2) ++ pad32(mapping slot = 2)) + 1
#define ROLLUPX_ROOT_SLOT "679795a0195a1b76cdebb7c51d74e058aee92919b8c3389af86ef24535e8a28d"
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

// Answers all pending requests of `state` from the fixture. A request without a
// recorded answer fails with an error naming the missing file.
static void answer_from_fixture(c4_state_t* state) {
  data_request_t* req = NULL;
  while ((req = c4_state_get_pending_request(state))) {
    char  path[1024];
    char* name = c4_req_mockname(req);
    snprintf(path, sizeof(path), "%s/%s", FIXTURE, name);
    bytes_t content = read_testdata(path);
    if (content.data)
      req->response = content;
    else
      req->error = bprintf(NULL, "no fixture for %s", name);
    safe_free(name);
  }
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

// Runs a request through the unified RPC pipeline (local prover + verifier).
// Returns the status; `result` and `error` receive owned strings.
static c4_status_t run_rpc(const char* method, const char* args, uint64_t min_latest_block_ts, char** result, char** error) {
  c4_rpc_ctx_t* rpc    = c4_rpc_ctx_create(method, args, ROLLUPX, 0, 0, C4_PROVER_MODE_LOCAL);
  c4_status_t   status = C4_PENDING;
  if (min_latest_block_ts) c4_rpc_ctx_set_min_latest_block_ts(rpc, min_latest_block_ts);
  for (int i = 0; i < 30 && status == C4_PENDING; i++) {
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

// The L1 chain id lives twice: in the table of the module and in the switch of `util`
// (which must not depend on a chain module). Both have to agree for every chain.
void test_chain_table_matches_util(void) {
  size_t                    count = 0;
  const eez_chain_config_t* all   = eez_get_all_chain_configs(&count);
  TEST_ASSERT_NOT_NULL(all);
  TEST_ASSERT_TRUE(count >= 1);
  for (size_t i = 0; i < count; i++) {
    TEST_ASSERT_NOT_NULL(all[i].name);
    TEST_ASSERT_EQUAL_INT_MESSAGE(C4_CHAIN_TYPE_EEZ, c4_chain_type(all[i].chain_id), all[i].name);
    TEST_ASSERT_TRUE_MESSAGE(c4_get_l1_chain_id_for_l2(all[i].chain_id) == all[i].l1_chain_id, all[i].name);
    TEST_ASSERT_TRUE_MESSAGE(all[i].l1_chain_id != all[i].chain_id, all[i].name);
    TEST_ASSERT_EQUAL_INT_MESSAGE(C4_CHAIN_TYPE_ETHEREUM, c4_chain_type(all[i].l1_chain_id), all[i].name);
    TEST_ASSERT_FALSE_MESSAGE(bytes_all_zero(bytes((uint8_t*) all[i].proxy, 20)), all[i].name);
    TEST_ASSERT_EQUAL_PTR(&all[i], eez_get_chain_config(all[i].chain_id));
  }
}

void test_rollupx_config(void) {
  const eez_chain_config_t* conf = eez_get_chain_config(ROLLUPX);
  TEST_ASSERT_NOT_NULL(conf);
  TEST_ASSERT_TRUE(conf->chain_id == 0xaa29f);
  TEST_ASSERT_TRUE(conf->l1_chain_id == C4_CHAIN_MAINNET);
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
  TEST_ASSERT_EQUAL_INT(METHOD_UNPROOFABLE, method_type(ROLLUPX, "eth_gasPrice", "[]"));
  TEST_ASSERT_EQUAL_INT(METHOD_UNDEFINED, method_type(ROLLUPX, "eth_unknownMethod", "[]"));

  // the module only answers for EEZ chains
  TEST_ASSERT_EQUAL_INT(METHOD_UNDEFINED, method_type(C4_CHAIN_MAINNET, "eth_getBlockByNumber", "[\"latest\",false]"));
  TEST_ASSERT_EQUAL_INT(METHOD_UNDEFINED, method_type(C4_CHAIN_OP_BASE, "eth_getBlockByNumber", "[\"latest\",false]"));
  TEST_ASSERT_EQUAL_INT(METHOD_UNDEFINED, c4_eez_get_method_type(ROLLUPX, NULL, json_parse("[]"), 0));

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

typedef struct {
  const uint8_t* address;            // account in the trie and `address` field; NULL = proxy of RollupX
  const uint8_t* key;                // `key` field of the storage proof; NULL = root slot of RollupX
  int            storage_proofs;     // entries in `storageProof`; -1 = one
  int            account_fields;     // RLP fields of the account; 0 = four
  bool           slot_missing;       // the root slot is not part of the storage trie
  bool           truncate_storage;   // drop the leaf node of the storage proof
  bool           truncate_account;   // drop the leaf node of the account proof
  bool           wrong_storage_hash; // the account commits to another storage trie
  bool           empty_storage_proof;
  int            storage_hash_len; // length of the storage hash inside the account; 0 = 32
  bool           leaf_is_list;     // the storage leaf holds an RLP list instead of a byte string
} synth_opts_t;

static const synth_opts_t SYNTH_VALID = {.storage_proofs = -1};

typedef struct {
  ssz_ob_t  account;    // `EthAccountProof`, bytes owned
  bytes32_t state_root; // root of the account trie
} synth_t;

// Fills a trie with unrelated entries so the proven key sits below branch nodes.
static void add_filler(node_t** root, const char* domain) {
  for (uint32_t i = 0; i < 24; i++) {
    bytes32_t path  = {0};
    buffer_t  seed  = {0};
    buffer_t  value = {0};
    bprintf(&seed, "%s:%d", domain, i);
    keccak(seed.data, path);
    rlp_add_uint64(&value, (uint64_t) i + 1000);
    patricia_set_value(root, bytes(path, 32), value.data);
    buffer_free(&seed);
    buffer_free(&value);
  }
}

// Copy of a list of byte strings without its last element.
static ssz_ob_t without_last(ssz_ob_t list) {
  uint32_t      len     = ssz_len(list);
  ssz_builder_t builder = {.def = list.def};
  TEST_ASSERT_TRUE(len >= 2);
  for (uint32_t i = 0; i + 1 < len; i++)
    ssz_add_dynamic_list_bytes(&builder, (int) (len - 1), ssz_at(list, i).bytes);
  return ssz_builder_to_bytes(&builder);
}

// Builds an account proof for the root slot of RollupX holding `value`, with the
// deviations selected in `opts`.
static synth_t synth_account(bytes_t value, synth_opts_t opts) {
  const eez_chain_config_t* conf           = eez_get_chain_config(ROLLUPX);
  synth_t                   out            = {0};
  bytes32_t                 slot           = {0};
  bytes32_t                 path           = {0};
  bytes32_t                 other          = {0};
  buffer_t                  buf            = {0};
  node_t*                   storage        = NULL;
  node_t*                   accounts       = NULL;
  const uint8_t*            address        = opts.address ? opts.address : conf->proxy;
  int                       storage_proofs = opts.storage_proofs < 0 ? 1 : opts.storage_proofs;
  int                       fields         = opts.account_fields ? opts.account_fields : 4;

  // storage trie
  eez_root_slot(conf->rollup_id, slot);
  add_filler(&storage, "storage");
  if (!opts.slot_missing) {
    keccak(bytes(slot, 32), path);
    rlp_add_item(&buf, value);
    if (opts.leaf_is_list) rlp_to_list(&buf);
    patricia_set_value(&storage, bytes(path, 32), buf.data);
    buffer_reset(&buf);
  }
  keccak(bytes(slot, 32), path);
  ssz_ob_t storage_proof = patricia_create_merkle_proof(storage, bytes(path, 32));
  if (opts.truncate_storage || opts.empty_storage_proof) {
    ssz_ob_t full = storage_proof;
    if (opts.empty_storage_proof)
      storage_proof = (ssz_ob_t) {.def = full.def, .bytes = bytes(safe_calloc(1, 1), 0)};
    else
      storage_proof = without_last(full);
    safe_free(full.bytes.data);
  }

  // account: [nonce, balance, storageHash, codeHash]
  memset(other, 0x5a, 32);
  rlp_add_uint64(&buf, 1);
  rlp_add_uint64(&buf, 0);
  rlp_add_item(&buf, opts.wrong_storage_hash ? bytes(other, 32) : bytes(patricia_get_root(storage).data, opts.storage_hash_len ? (uint32_t) opts.storage_hash_len : 32));
  rlp_add_item(&buf, bytes(other, 32));
  for (int i = 4; i < fields; i++) rlp_add_uint64(&buf, 7);
  rlp_to_list(&buf);

  // account trie
  add_filler(&accounts, "accounts");
  keccak(bytes((uint8_t*) address, 20), path);
  patricia_set_value(&accounts, bytes(path, 32), buf.data);
  memcpy(out.state_root, patricia_get_root(accounts).data, 32);
  ssz_ob_t account_proof = patricia_create_merkle_proof(accounts, bytes(path, 32));
  if (opts.truncate_account) {
    ssz_ob_t full = account_proof;
    account_proof = without_last(full);
    safe_free(full.bytes.data);
  }

  // EthAccountProof
  ssz_builder_t    account      = ssz_builder_for_type(ETH_SSZ_VERIFY_ACCOUNT_PROOF);
  const ssz_def_t* storage_def  = ssz_get_def(account.def, "storageProof");
  ssz_builder_t    storage_list = {.def = storage_def};
  uint8_t          el_proof[33] = {0}; // selector 0 (`blockHash`) + 32 bytes, not read by the function under test
  for (int i = 0; i < storage_proofs; i++) {
    ssz_builder_t entry = {.def = storage_def->def.vector.type};
    ssz_add_bytes(&entry, "key", bytes((uint8_t*) (opts.key ? opts.key : slot), 32));
    ssz_add_bytes(&entry, "proof", storage_proof.bytes);
    ssz_add_dynamic_list_builders(&storage_list, storage_proofs, entry);
  }
  ssz_add_bytes(&account, "accountProof", account_proof.bytes);
  ssz_add_bytes(&account, "address", bytes((uint8_t*) address, 20));
  ssz_add_builders(&account, "storageProof", storage_list);
  ssz_add_bytes(&account, "elProof", bytes(el_proof, 33));
  out.account = ssz_builder_to_bytes(&account);

  safe_free(storage_proof.bytes.data);
  safe_free(account_proof.bytes.data);
  patricia_node_free(storage);
  patricia_node_free(accounts);
  buffer_free(&buf);
  return out;
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
  assert_storage_error(bytes(long_value, 33), SYNTH_VALID, "invalid storage value of the EEZ root slot!");

  uint8_t huge_value[200];
  memset(huge_value, 0x77, sizeof(huge_value));
  assert_storage_error(bytes(huge_value, 200), SYNTH_VALID, "invalid storage value of the EEZ root slot!");
}

void test_storage_value_wrong_contract(void) {
  bytes32_t    buf       = {0};
  uint8_t      other[20] = {0};
  synth_opts_t opts      = SYNTH_VALID;
  memset(other, 0x42, 20);
  opts.address = other;
  // a perfectly valid proof of the same slot in another contract
  assert_storage_error(hash_value(buf), opts, "EEZ proof is not about the configured proxy contract!");
}

void test_storage_value_wrong_slot(void) {
  bytes32_t    buf   = {0};
  bytes32_t    other = {0};
  synth_opts_t opts  = SYNTH_VALID;

  // the slot of another rollup
  eez_root_slot(3, other);
  opts.key = other;
  assert_storage_error(hash_value(buf), opts, "EEZ proof is not about the root slot of the rollup!");

  // the mapping slot itself, without the offset of the root field
  eez_root_slot(2, other);
  other[31]--;
  assert_storage_error(hash_value(buf), opts, "EEZ proof is not about the root slot of the rollup!");
}

void test_storage_value_needs_exactly_one_storage_proof(void) {
  bytes32_t    buf    = {0};
  synth_opts_t opts   = SYNTH_VALID;
  opts.storage_proofs = 2;
  assert_storage_error(hash_value(buf), opts, "EEZ proof must contain exactly one storage proof!");
  opts.storage_proofs = 0;
  assert_storage_error(hash_value(buf), opts, "EEZ proof must contain exactly one storage proof!");
}

void test_storage_value_wrong_state_root(void) {
  bytes32_t  buf   = {0};
  bytes32_t  value = {0};
  c4_state_t state = {0};
  synth_t    synth = synth_account(hash_value(buf), SYNTH_VALID);

  synth.state_root[7] ^= 0x01;
  TEST_ASSERT_EQUAL_INT(C4_ERROR, eez_get_l1_storage_value(&state, eez_get_chain_config(ROLLUPX), synth.account, bytes(synth.state_root, 32), value));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(state.error, "does not match the L1 state root!"), state.error);
  c4_state_free(&state);

  // missing state root (e.g. an L1 header without that field); a root of the wrong
  // length is covered by `test_storage_value_malformed_leaf`
  state = (c4_state_t) {0};
  TEST_ASSERT_EQUAL_INT(C4_ERROR, eez_get_l1_storage_value(&state, eez_get_chain_config(ROLLUPX), synth.account, NULL_BYTES, value));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(state.error, "missing L1 state root"), state.error);
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
  assert_storage_error(hash_value(buf), opts, "does not match the storage hash of the proxy!");
}

void test_storage_value_malformed_account(void) {
  bytes32_t    buf    = {0};
  synth_opts_t opts   = SYNTH_VALID;
  opts.account_fields = 5;
  assert_storage_error(hash_value(buf), opts, "invalid account of the EEZ proxy!");
}

// A proof that the slot does not exist would read as the value zero. It must be an error:
// without a stored hash there is no block.
void test_storage_value_exclusion_proof(void) {
  bytes32_t    buf  = {0};
  synth_opts_t opts = SYNTH_VALID;
  opts.slot_missing = true;
  assert_storage_error(hash_value(buf), opts, "invalid storage proof of the EEZ root slot!");
}

// Regression guard for a quirk of `patricia_verify`: a proof that stops at a branch node
// (the leaf is cut off) is reported as PATRICIA_FOUND with an empty value. Such a proof
// carries no value and must not be accepted.
void test_storage_value_truncated_proofs(void) {
  bytes32_t    buf  = {0};
  synth_opts_t opts = SYNTH_VALID;

  opts.truncate_storage = true;
  assert_storage_error(hash_value(buf), opts, "invalid storage proof of the EEZ root slot!");

  opts                     = SYNTH_VALID;
  opts.empty_storage_proof = true;
  assert_storage_error(hash_value(buf), opts, "invalid storage proof of the EEZ root slot!");

  opts                  = SYNTH_VALID;
  opts.truncate_account = true;
  assert_storage_error(hash_value(buf), opts, "invalid account proof of the EEZ proxy!");
}

// ---------------------------------------------------------------------------
// 3. recorded RollupX fixture
// ---------------------------------------------------------------------------

// local prover + verifier, sync committee already known
void test_fixture_block_by_number(void) {
  run_rpc_test(FIXTURE, 0, 0);
  // everything the verifier persisted for the sync committee belongs to the L1
  TEST_ASSERT_TRUE(stored("states_1"));
  TEST_ASSERT_FALSE(stored("states_696991"));
}

// The verifier starts with nothing but a trusted checkpoint. It can only succeed when the
// sync data of the L1 prover answer is carried over into the L2 proof and applied to the
// state of the L1 chain.
void test_fixture_sync_data_is_forwarded_and_stored_for_l1(void) {
  char* result = NULL;
  char* error  = NULL;
  // set through the L2 chain id: a checkpoint is a beacon root, so it lands on the L1
  c4_set_checkpoint(ROLLUPX, FIXTURE_CHECKPOINT);
  TEST_ASSERT_TRUE(stored("states_1"));
  TEST_ASSERT_FALSE(stored("states_696991"));
  TEST_ASSERT_FALSE(stored("sync_1_1907"));

  c4_status_t status = run_rpc("eth_getBlockByNumber", "[\"latest\",false]", 0, &result, &error);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, status, error ? error : "no error");
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(result, "\"number\":\"" ANCHOR_NUMBER_HEX "\""), result);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(result, "\"hash\":\"" ANCHOR_HASH "\""), result);

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
  set_state(ROLLUPX, FIXTURE);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, run_rpc("eth_blockNumber", "[]", 0, &result, &error), error ? error : "no error");
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(result, ANCHOR_NUMBER_HEX), result);
  safe_free(result);
  safe_free(error);

  static const char* accepted[][2] = {
      {"eth_getBlockByNumber", "[\"safe\",false]"},
      {"eth_getBlockByNumber", "[\"finalized\",false]"},
      {"eth_getBlockByNumber", "[\"" ANCHOR_NUMBER_HEX "\",false]"}, // the anchor by number
      {"eth_getBlockByHash", "[\"" ANCHOR_HASH "\",false]"},         // the anchor by hash
  };
  for (size_t i = 0; i < sizeof(accepted) / sizeof(accepted[0]); i++) {
    reset_local_filecache();
    c4_header_cache_clear();
    set_state(ROLLUPX, FIXTURE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, run_rpc(accepted[i][0], accepted[i][1], 0, &result, &error), error ? error : accepted[i][1]);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(result, "\"hash\":\"" ANCHOR_HASH "\""), result);
    safe_free(result);
    safe_free(error);
  }
}

// The freshness bound of the host does not reject the (old) anchor block on EEZ.
void test_fixture_latest_passes_freshness_bound(void) {
  char* result = NULL;
  char* error  = NULL;
  set_state(ROLLUPX, FIXTURE);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, run_rpc("eth_getBlockByNumber", "[\"latest\",false]", UINT64_MAX, &result, &error), error ? error : "no error");
  safe_free(result);
  safe_free(error);
}

// Milestone 1 proves the anchor only. Every other block is an error that says why.
void test_fixture_blocks_other_than_the_anchor(void) {
  assert_prover_error("eth_getBlockByNumber", "[\"0x889db\",false]", "block 559579 is newer than the last block of RollupX proven on L1 (559578)");
  assert_prover_error("eth_getBlockByNumber", "[\"0x889d9\",false]", "block 559577 is older than the last block of RollupX proven on L1 (559578); it needs an EIP-2935 proof");
  // 559578 - 8191 = 551387 is the oldest block in the EIP-2935 window, 551386 the first outside
  assert_prover_error("eth_getBlockByNumber", "[\"0x869db\",false]", "block 551387 is older than the last block of RollupX proven on L1 (559578); it needs an EIP-2935 proof");
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
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx->state.error, "EEZ proof is not about the configured proxy contract!"), ctx->state.error);
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
  assert_rejected(tampered, method, args, "EEZ proof is not about the configured proxy contract!");
  safe_free(tampered.data);

  // another slot
  tampered = flip_byte(request, ssz_get(&storage, "key").bytes.data + 31);
  assert_rejected(tampered, method, args, "EEZ proof is not about the root slot of the rollup!");
  safe_free(tampered.data);

  // another stored value: the last byte of the storage leaf is the last byte of the hash.
  // The leaf no longer hashes to what its parent node commits to.
  tampered = flip_byte(request, storage_leaf.data + storage_leaf.len - 1);
  assert_rejected(tampered, method, args, "invalid storage proof of the EEZ root slot!");
  safe_free(tampered.data);

  // another account (the last byte of the account leaf is part of the code hash)
  tampered = flip_byte(request, account_leaf.data + account_leaf.len - 1);
  assert_rejected(tampered, method, args, "invalid account proof of the EEZ proxy!");
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
  assert_rejected(tampered, method, args, "account proof of the EEZ proxy does not match the L1 state root!");
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
  TEST_ASSERT_TRUE(eez_is_el_header(header));
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

// Every variant of the union but `eezL1Proof` is rejected by name, by the hook and by the
// function that verifies the `eezL1Proof` itself.
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
    bool         reserved = strcmp(variant->name, "eip2935") == 0;
    ssz_ob_t     foreign  = {.def = variant, .bytes = el.bytes};
    verify_ctx_t ctx      = {.chain_id = ROLLUPX};
    TEST_ASSERT_EQUAL_INT_MESSAGE(C4_ERROR, eez_verify_block_proof(&ctx, foreign, &header, hash), variant->name);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx.state.error, reserved ? "eip2935 block proofs are not supported yet!" : "expected eezL1Proof"), ctx.state.error);
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
  TEST_ASSERT_TRUE(eez_is_el_header(header));

  // not a list, an empty list, nothing at all
  uint8_t string_item[] = {0x83, 0x01, 0x02, 0x03};
  uint8_t empty_list[]  = {0xc0};
  TEST_ASSERT_FALSE(eez_is_el_header(bytes(string_item, sizeof(string_item))));
  TEST_ASSERT_FALSE(eez_is_el_header(bytes(empty_list, sizeof(empty_list))));
  TEST_ASSERT_FALSE(eez_is_el_header(NULL_BYTES));
  // a truncated header
  TEST_ASSERT_FALSE(eez_is_el_header(bytes(header.data, header.len - 1)));

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
    TEST_ASSERT_EQUAL_INT_MESSAGE(fields == 15, eez_is_el_header(buf.data), fields == 15 ? "15 fields" : "14 fields");
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
    TEST_ASSERT_FALSE_MESSAGE(eez_is_el_header(buf.data), "a fixed-length field of the wrong length");
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
  assert_prover_error_with("eth_blockNumber", "[]", by_hash, other, "the L2 header does not hash to the block " ANCHOR_HASH " anchored on L1");
  assert_prover_error_with("eth_blockNumber", "[]", by_hash, str_bytes("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":null}"), "the L2 node does not know the block " ANCHOR_HASH " anchored on L1");
  assert_prover_error_with("eth_blockNumber", "[]", by_hash, NULL_BYTES, "overridden");

  // with body (debug_getRawBlock): one nibble of the header changed
  char* text   = bprintf(NULL, "%r", raw); // NUL-terminated copy
  char* marker = strstr(text, "\"result\":\"0x");
  TEST_ASSERT_NOT_NULL(marker);
  TEST_ASSERT_TRUE(strlen(marker) > 200);
  char* nibble = marker + 12 + 100; // inside the RLP header
  *nibble      = *nibble == '0' ? '1' : '0';
  assert_prover_error_with("eth_getBlockByNumber", "[\"latest\",false]", raw_block, str_bytes(text), "the L2 header does not hash to the block " ANCHOR_HASH " anchored on L1");
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

// --- small additions that close surviving mutants ---------------------------

// Local RPC pipeline: the L1 prover request carries the client state of the L1 chain.
void test_rpc_sends_l1_client_state_to_l1_prover(void) {
  set_state(ROLLUPX, FIXTURE); // states_1 = 0x0173070000, nothing stored for the L2 chain id
  c4_rpc_ctx_t* rpc = c4_rpc_ctx_create("eth_blockNumber", "[]", ROLLUPX, 0, 0, C4_PROVER_MODE_LOCAL);
  TEST_ASSERT_EQUAL_INT(C4_PENDING, c4_rpc_execute(rpc));
  data_request_t* req = c4_state_get_pending_request(c4_rpc_get_state(rpc));
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
  c4_rpc_ctx_free(rpc);

  // the version is the one of this build, not the one of the client
  prover_ctx_t* ctx = c4_prover_create("eth_blockNumber", "[]", ROLLUPX, 0);
  ctx->version      = c4_version_number(1, 0, 0);
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
  assert_storage_error(NULL_BYTES, SYNTH_VALID, "invalid storage value of the EEZ root slot!");

  // a state root of the right bytes but the wrong length is "missing", not "mismatching"
  bytes32_t  buf   = {0};
  bytes32_t  value = {0};
  c4_state_t state = {0};
  synth_t    synth = synth_account(hash_value(buf), SYNTH_VALID);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, eez_get_l1_storage_value(&state, eez_get_chain_config(ROLLUPX), synth.account, bytes(synth.state_root, 31), value));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(state.error, "missing L1 state root"), state.error);
  c4_state_free(&state);
  state = (c4_state_t) {0};
  TEST_ASSERT_EQUAL_INT(C4_ERROR, eez_get_l1_storage_value(&state, eez_get_chain_config(ROLLUPX), synth.account, bytes(NULL, 32), value));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(state.error, "missing L1 state root"), state.error);
  c4_state_free(&state);
  safe_free(synth.account.bytes.data);
}

// An account or a leaf that does not have the RLP shape of an account / a storage word.
void test_storage_value_malformed_storage_hash_and_leaf(void) {
  bytes32_t    buf  = {0};
  synth_opts_t opts = SYNTH_VALID;

  // a storage hash shorter than a hash must not be compared as 32 bytes
  opts.storage_hash_len = 31;
  assert_storage_error(hash_value(buf), opts, "invalid storage hash of the EEZ proxy!");
  opts.storage_hash_len = 1;
  assert_storage_error(hash_value(buf), opts, "invalid storage hash of the EEZ proxy!");

  // a list where the storage word is expected
  uint8_t short_value[] = {0x01, 0x02, 0x03};
  opts                  = SYNTH_VALID;
  opts.leaf_is_list     = true;
  assert_storage_error(bytes(short_value, 3), opts, "invalid storage value of the EEZ root slot!");
  assert_storage_error(hash_value(buf), opts, "invalid storage value of the EEZ root slot!");
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
  RUN_TEST(test_hybrid_and_light_client_mode);
  RUN_TEST(test_rpc_sends_l1_client_state_to_l1_prover);
  RUN_TEST(test_local_methods_and_unknown_eez_chain);
  RUN_TEST(test_storage_value_malformed_leaf);
  RUN_TEST(test_storage_value_malformed_storage_hash_and_leaf);
  RUN_TEST(test_no_block_anchored_on_l1);
  RUN_TEST(test_header_fork_of_the_l2_block);
  RUN_TEST(test_unprovable_block_identifiers_fail_without_a_request);
  RUN_TEST(test_logs_proof_for_a_chain_without_chain_spec);
#endif
  return UNITY_END();
}
