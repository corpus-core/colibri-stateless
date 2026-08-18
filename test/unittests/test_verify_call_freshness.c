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

// Coverage for the freshness check across all block-tag-bearing RPC methods
// when the requested block tag is `"latest"` (or implicitly latest, e.g.
// `eth_blockNumber`). The check is host-driven via
// `c4_rpc_ctx_set_min_latest_block_ts`; the C verifier never reads a wallclock.
//
// We replay the existing fixtures through the unified RPC pipeline with
// different lower-bound timestamps. The local prover emits a proof carrying
// the block timestamp (either as `blockContext` for `eth_call`*, as the new
// `timestamp` union variant for account methods, or directly inside the
// payload for block/header methods), so we can exercise all three branches
// of the gate:
//
//   1. "check disabled" (`min_latest_block_ts = 0`) -> success
//   2. "fresh enough"   (`min_latest_block_ts <= block.timestamp`) -> success
//   3. "too old"        (`min_latest_block_ts >  block.timestamp`) -> error
//
// We deliberately use robust bounds rather than the exact block timestamp:
// `1` is below any real Ethereum block timestamp (always fresh) and a
// far-future value is above any real block timestamp (always stale). This
// keeps the tests stable across fixture regenerations while still asserting
// both comparison directions.

#include "bytes.h"
#include "c4_assert.h"
#include "ssz.h"
#include "unity.h"

void setUp(void) {
  reset_local_filecache();
}

void tearDown(void) {
  reset_local_filecache();
}

typedef enum {
  EXPECT_SUCCESS,
  EXPECT_ERROR_TOO_OLD,
  EXPECT_ERROR_NO_CONTEXT,
} freshness_expectation_t;

// Drive the unified RPC state machine through the recorded fixture and assert
// the expected outcome. `min_latest_block_ts == 0` disables the check.
static void run_freshness_case(
    const char*             dirname,
    const char*             method,
    const char*             args,
    chain_id_t              chain_id,
    prover_flags_t          prover_flags,
    verify_flags_t          verify_flags,
    bool                    remote_prover,
    uint64_t                min_latest_block_ts,
    freshness_expectation_t expectation) {

  set_state(chain_id, (char*) dirname);

  c4_rpc_ctx_t* rpc_ctx = c4_rpc_ctx_create((char*) method, (char*) args, chain_id,
                                            prover_flags, verify_flags,
                                            remote_prover ? 1 : 0);
  c4_rpc_ctx_set_min_latest_block_ts(rpc_ctx, min_latest_block_ts);

  bool        done   = false;
  c4_status_t status = C4_PENDING;
  while (!done) {
    status = c4_rpc_execute(rpc_ctx);
    if (status == C4_PENDING) {
      data_request_t* req;
      while ((req = c4_state_get_pending_request(c4_rpc_get_state(rpc_ctx)))) {
        char  tmp[1024];
        char* filename = c4_req_mockname(req);
        snprintf(tmp, sizeof(tmp), "%s/%s", dirname, filename);
        safe_free(filename);
        bytes_t content = read_testdata(tmp);
        TEST_ASSERT_NOT_NULL_MESSAGE(content.data, tmp);
        req->response = content;
      }
    }
    else {
      done = true;
    }
  }

  switch (expectation) {
    case EXPECT_SUCCESS: {
      if (status != C4_SUCCESS) {
        const char* err = rpc_ctx->error ? rpc_ctx->error
                                         : (rpc_ctx->verifier.state.error ? rpc_ctx->verifier.state.error : "unknown error");
        TEST_FAIL_MESSAGE(err);
      }
      break;
    }

    case EXPECT_ERROR_TOO_OLD: {
      TEST_ASSERT_EQUAL_INT_MESSAGE(C4_ERROR, status, "expected verifier error when proof for latest is too old");
      const char* err = rpc_ctx->verifier.state.error ? rpc_ctx->verifier.state.error : (rpc_ctx->error ? rpc_ctx->error : "");
      TEST_ASSERT_NOT_NULL_MESSAGE(strstr(err, "proof for latest too old"),
                                   bprintf(NULL, "expected freshness error, got: %s", err));
      break;
    }

    case EXPECT_ERROR_NO_CONTEXT: {
      TEST_ASSERT_EQUAL_INT_MESSAGE(C4_ERROR, status, "expected verifier error when block context is missing");
      const char* err = rpc_ctx->verifier.state.error ? rpc_ctx->verifier.state.error : (rpc_ctx->error ? rpc_ctx->error : "");
      TEST_ASSERT_NOT_NULL_MESSAGE(strstr(err, "cannot verify freshness of latest block without block context"),
                                   bprintf(NULL, "expected missing-block-context error, got: %s", err));
      break;
    }
  }

  c4_rpc_ctx_free(rpc_ctx);
}

#define ETH_CALL1_DIR    "eth_call1"
#define ETH_CALL1_METHOD "eth_call"
#define ETH_CALL1_ARGS_LATEST                                                                         \
  "[{\"to\":\"0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48\","                                          \
  "\"data\":\"0x70a0823100000000000000000000000037305b1cd40574e4c5ce33f8e8306be057fd7341\"},\"latest\"]"

// :: Test 1: freshness check disabled (min_ts = 0) → success even on `latest`
//
// Establishes that the existing eth_call flow is unaffected when the host
// opts out of the freshness check (the default for embedded targets and
// for any binding that explicitly sets `max_latest_age_seconds = 0`).

void test_freshness_disabled_passes_on_latest(void) {
  run_freshness_case(ETH_CALL1_DIR, ETH_CALL1_METHOD, ETH_CALL1_ARGS_LATEST,
                     C4_CHAIN_MAINNET, C4_PROVER_FLAG_INCLUDE_CODE | C4_PROVER_FLAG_USE_DEBUG_TRACE, 0,
                     false /* local prover */,
                     0 /* check disabled */,
                     EXPECT_SUCCESS);
}

// :: Test 2: enabled check + fresh proof → success
//
// A lower bound of `1` is below any real Ethereum block timestamp, so the
// proof's block context is always considered fresh and the call verifies.

void test_freshness_fresh_proof_passes(void) {
  run_freshness_case(ETH_CALL1_DIR, ETH_CALL1_METHOD, ETH_CALL1_ARGS_LATEST,
                     C4_CHAIN_MAINNET, C4_PROVER_FLAG_INCLUDE_CODE | C4_PROVER_FLAG_USE_DEBUG_TRACE, 0,
                     false /* local prover */,
                     1 /* below any real block timestamp → fresh */,
                     EXPECT_SUCCESS);
}

// :: Test 3: enabled check + stale proof → "proof for latest too old"
//
// A far-future lower bound is above any real block timestamp, so the proof
// is classified as stale and rejected. This pins the actual replay-protection
// behaviour the feature exists for.

void test_freshness_stale_proof_rejected(void) {
  run_freshness_case(ETH_CALL1_DIR, ETH_CALL1_METHOD, ETH_CALL1_ARGS_LATEST,
                     C4_CHAIN_MAINNET, C4_PROVER_FLAG_INCLUDE_CODE | C4_PROVER_FLAG_USE_DEBUG_TRACE, 0,
                     false /* local prover */,
                     UINT64_C(99999999999) /* year ~5138 → stale */,
                     EXPECT_ERROR_TOO_OLD);
}

// NOTE: A dedicated "non-`latest` block tag bypasses the check" case is not
// listed here as its own test. The freshness gate triggers only when
// `args[1] == "latest"` *and* `min_latest_block_ts > 0`; every other
// `eth_call*` test in the suite already runs with `min_latest_block_ts == 0`
// and concrete block tags, exercising the negative-path implicitly. Adding a
// fixture with a synthetic non-latest tag would require new RPC mock data and
// did not seem worth the maintenance cost for a one-line guard.

// :: Test 4: colibri_simulateTransaction routes through the same gate
//
// `eth_call`, `eth_estimateGas`, and `colibri_simulateTransaction` share the
// same `verify_call_proof` dispatcher (see eth_verify.c). Pin the gate for
// `colibri_simulateTransaction` so a future refactor that splits the methods
// cannot silently disable the freshness check on the simulate path.

void test_freshness_simulate_stale_rejected(void) {
  run_freshness_case("simulate_simple", "colibri_simulateTransaction",
                     "[{\"to\":\"0x0742d35Cc6634C0532925a3b844Bc9e7595f0bEb\","
                     "\"data\":\"0x06fdde03\"},\"latest\"]",
                     C4_CHAIN_MAINNET, C4_PROVER_FLAG_INCLUDE_CODE | C4_PROVER_FLAG_USE_DEBUG_TRACE, 0,
                     false /* local prover */,
                     UINT64_C(99999999999) /* year ~5138 → stale */,
                     EXPECT_ERROR_TOO_OLD);
}

// :: PAP-mode freshness gate
//
// In PAP mode there is no usable proof when `verify_call_proof` first runs;
// the call proof arrives later via `colibri_proofCall` (same SSZ structure as
// an `eth_call` proof) and is verified inside `pap_verify_proof_response`,
// which now also enforces the freshness gate. The two tests below pin both
// halves of that behaviour using the `eth_call_pap_cached` fixture.
//
// NOTE on the fixture: its recorded `colibri_proofCall` response predates the
// block-context feature (prover < 1.1.15), so the verified sub-proof carries
// no timestamp. We can therefore exercise the "missing context" branch but
// not the timestamp comparison itself (that path is already covered for the
// locally generated proofs in Tests 2/3).

// :: Test 5: PAP + enabled check + proof without block context -> fail-closed
//
// When the host opts into the freshness check but the (older) PAP proof does
// not carry a block context, the verifier must refuse to vouch for a `latest`
// result rather than silently accepting a potentially stale proof. The lower
// bound value is irrelevant here -- the gate fails before any timestamp
// comparison -- so we use `1` to make clear this is about the missing context,
// not about being "too old".

void test_freshness_pap_missing_context_rejected(void) {
  run_freshness_case("eth_call_pap_cached", "eth_call",
                     "[{\"to\":\"0xdac17f958d2ee523a2206206994597c13d831ec7\","
                     "\"data\":\"0x70a082310000000000000000000000008825ef664b8b43984bbf32b09e6a690c9b914931\"},\"latest\"]",
                     C4_CHAIN_MAINNET,
                     C4_PROVER_FLAG_INCLUDE_CODE,
                     VERIFY_FLAG_PAP,
                     true /* remote prover for the cached PAP fixture */,
                     1 /* check enabled; fails on missing context regardless of value */,
                     EXPECT_ERROR_NO_CONTEXT);
}

// :: Test 6: PAP + disabled check (min_ts = 0) -> success
//
// Confirms the PAP path is unaffected when the host opts out of the freshness
// check (the binding default when `max_latest_age_seconds = 0`). This also
// guards against the gate accidentally firing on the disabled code path.

void test_freshness_pap_disabled_passes(void) {
  run_freshness_case("eth_call_pap_cached", "eth_call",
                     "[{\"to\":\"0xdac17f958d2ee523a2206206994597c13d831ec7\","
                     "\"data\":\"0x70a082310000000000000000000000008825ef664b8b43984bbf32b09e6a690c9b914931\"},\"latest\"]",
                     C4_CHAIN_MAINNET,
                     C4_PROVER_FLAG_INCLUDE_CODE,
                     VERIFY_FLAG_PAP,
                     true /* remote prover for the cached PAP fixture */,
                     0 /* check disabled */,
                     EXPECT_SUCCESS);
}

// :: Account methods (timestamp union variant)
//
// `eth_getBalance`, `eth_getCode` (via getBalance flow), `eth_getStorageAt`,
// `eth_getTransactionCount` and `eth_getProof` all share the
// `verify_account_proof` path. For non-pinned tags the prover (>= 1.1.27)
// emits the `timestamp` variant of `ETH_STATE_BLOCK_UNION` so the verifier
// can run the freshness gate without a full block-context multi-proof.
// We only assert the stale-path for each method (fresh-path is implicitly
// covered by the existing `test_eth_verify_*` integration tests, which run
// with `min_latest_block_ts == 0`).

void test_freshness_account_balance_stale_rejected(void) {
  run_freshness_case("eth_getBalance1", "eth_getBalance",
                     "[\"0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5\",\"latest\"]",
                     C4_CHAIN_MAINNET, 0, 0,
                     false,
                     UINT64_C(99999999999),
                     EXPECT_ERROR_TOO_OLD);
}

void test_freshness_account_balance_fresh_passes(void) {
  run_freshness_case("eth_getBalance1", "eth_getBalance",
                     "[\"0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5\",\"latest\"]",
                     C4_CHAIN_MAINNET, 0, 0,
                     false,
                     1,
                     EXPECT_SUCCESS);
}

void test_freshness_account_storage_at_stale_rejected(void) {
  // `eth_getStorageAt` has the block tag at args[2], not args[1]; this guards
  // against accidentally hard-coding the index in the verifier.
  run_freshness_case("eth_getStorageAt1", "eth_getStorageAt",
                     "[\"0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48\","
                     "\"0x7050c9e0f4ca769c69bd3a8ef740bc37934f8e2c036e5a723fd8ee048ed3f8c3\","
                     "\"latest\"]",
                     C4_CHAIN_MAINNET, 0, 0,
                     false,
                     UINT64_C(99999999999),
                     EXPECT_ERROR_TOO_OLD);
}

void test_freshness_account_transaction_count_stale_rejected(void) {
  run_freshness_case("eth_getTransactionCount1", "eth_getTransactionCount",
                     "[\"0xd2674dA94285660c9b2353131bef2d8211369A4B\",\"latest\"]",
                     C4_CHAIN_MAINNET, 0, 0,
                     false,
                     UINT64_C(99999999999),
                     EXPECT_ERROR_TOO_OLD);
}

void test_freshness_account_get_proof_stale_rejected(void) {
  // `eth_getProof` also has the block tag at args[2].
  run_freshness_case("eth_getProof1", "eth_getProof",
                     "[\"0xB685760EBD368a891F27ae547391F4E2A289895b\","
                     "[\"0x0000000000000000000000000000000000000000000000000000000000000001\","
                     "\"0x0000000000000000000000000000000000000000000000000000000000000002\"],"
                     "\"latest\"]",
                     C4_CHAIN_MAINNET, 0, 0,
                     false,
                     UINT64_C(99999999999),
                     EXPECT_ERROR_TOO_OLD);
}

// :: Block methods
//
// `eth_getBlockByNumber` with `params[0] == "latest"`. The execution payload
// is part of the proof, so the timestamp is unconditionally available.

void test_freshness_block_by_number_latest_stale_rejected(void) {
  run_freshness_case("eth_getBlockByNumber_electra", "eth_getBlockByNumber",
                     "[\"latest\",false]",
                     C4_CHAIN_MAINNET, 0, 0,
                     false,
                     UINT64_C(99999999999),
                     EXPECT_ERROR_TOO_OLD);
}

void test_freshness_block_by_number_latest_fresh_passes(void) {
  run_freshness_case("eth_getBlockByNumber_electra", "eth_getBlockByNumber",
                     "[\"latest\",false]",
                     C4_CHAIN_MAINNET, 0, 0,
                     false,
                     1,
                     EXPECT_SUCCESS);
}

// :: BlockNumber (implicit-latest)
//
// `eth_blockNumber` has no block-tag argument; the request always implicitly
// targets `latest`, so the gate must fire even with empty args.

void test_freshness_block_number_stale_rejected(void) {
  run_freshness_case("eth_blockNumber_electra", "eth_blockNumber",
                     "[]",
                     C4_CHAIN_MAINNET, 0, 0,
                     false,
                     UINT64_C(99999999999),
                     EXPECT_ERROR_TOO_OLD);
}

void test_freshness_block_number_fresh_passes(void) {
  run_freshness_case("eth_blockNumber_electra", "eth_blockNumber",
                     "[]",
                     C4_CHAIN_MAINNET, 0, 0,
                     false,
                     1,
                     EXPECT_SUCCESS);
}

// :: Header methods
//
// `eth_getBlockHeader` carries the timestamp directly in the proven header.

void test_freshness_block_header_stale_rejected(void) {
  run_freshness_case("eth_getBlockHeader1", "eth_getBlockHeader",
                     "[\"latest\"]",
                     C4_CHAIN_MAINNET, 0, 0,
                     false,
                     UINT64_C(99999999999),
                     EXPECT_ERROR_TOO_OLD);
}

void test_freshness_block_header_fresh_passes(void) {
  run_freshness_case("eth_getBlockHeader1", "eth_getBlockHeader",
                     "[\"latest\"]",
                     C4_CHAIN_MAINNET, 0, 0,
                     false,
                     1,
                     EXPECT_SUCCESS);
}

// :: eth_blobBaseFee / eth_maxPriorityFeePerGas (empty-args latest)
//
// Both methods take no arguments and implicitly target `latest`; the
// `verify_block_proof` short-circuits `is_latest` on `json_len == 0`.
// The dispatch shares `c4_proof_block` (see eth_prover.c) and the
// `EthBlockProof` SSZ shape (body union NONE), so the existing
// `eth_getBlockHeader1` fixture is reusable -- the prover defaults
// `block_arg` to `"latest"` when `params == []`, producing identical
// beacon requests.

void test_freshness_blob_base_fee_implicit_latest_stale_rejected(void) {
  run_freshness_case("eth_getBlockHeader1", "eth_blobBaseFee",
                     "[]",
                     C4_CHAIN_MAINNET, 0, 0,
                     false,
                     UINT64_C(99999999999),
                     EXPECT_ERROR_TOO_OLD);
}

void test_freshness_blob_base_fee_implicit_latest_fresh_passes(void) {
  run_freshness_case("eth_getBlockHeader1", "eth_blobBaseFee",
                     "[]",
                     C4_CHAIN_MAINNET, 0, 0,
                     false,
                     1,
                     EXPECT_SUCCESS);
}

void test_freshness_max_priority_fee_per_gas_implicit_latest_stale_rejected(void) {
  run_freshness_case("eth_getBlockHeader1", "eth_maxPriorityFeePerGas",
                     "[]",
                     C4_CHAIN_MAINNET, 0, 0,
                     false,
                     UINT64_C(99999999999),
                     EXPECT_ERROR_TOO_OLD);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_freshness_disabled_passes_on_latest);
  RUN_TEST(test_freshness_fresh_proof_passes);
  RUN_TEST(test_freshness_stale_proof_rejected);
  RUN_TEST(test_freshness_simulate_stale_rejected);
  RUN_TEST(test_freshness_pap_missing_context_rejected);
  RUN_TEST(test_freshness_pap_disabled_passes);
  RUN_TEST(test_freshness_account_balance_stale_rejected);
  RUN_TEST(test_freshness_account_balance_fresh_passes);
  RUN_TEST(test_freshness_account_storage_at_stale_rejected);
  RUN_TEST(test_freshness_account_transaction_count_stale_rejected);
  RUN_TEST(test_freshness_account_get_proof_stale_rejected);
  RUN_TEST(test_freshness_block_by_number_latest_stale_rejected);
  RUN_TEST(test_freshness_block_by_number_latest_fresh_passes);
  RUN_TEST(test_freshness_block_number_stale_rejected);
  RUN_TEST(test_freshness_block_number_fresh_passes);
  RUN_TEST(test_freshness_block_header_stale_rejected);
  RUN_TEST(test_freshness_block_header_fresh_passes);
  RUN_TEST(test_freshness_blob_base_fee_implicit_latest_stale_rejected);
  RUN_TEST(test_freshness_blob_base_fee_implicit_latest_fresh_passes);
  RUN_TEST(test_freshness_max_priority_fee_per_gas_implicit_latest_stale_rejected);
  return UNITY_END();
}
