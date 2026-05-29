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

// Coverage for the freshness check on `eth_call` (and friends) when the
// requested block tag is `"latest"`. The check is host-driven via
// `c4_rpc_ctx_set_min_latest_block_ts`; the C verifier never reads a wallclock.
//
// We replay the existing `eth_call1` / `simulate_simple` fixtures (which pin
// `params[1] = "latest"`) through the unified RPC pipeline with different
// lower-bound timestamps. The local prover emits a proof carrying the block
// context (timestamp), so we can exercise all three branches of the gate:
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
                     C4_CHAIN_MAINNET, C4_PROVER_FLAG_INCLUDE_CODE, 0,
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
                     C4_CHAIN_MAINNET, C4_PROVER_FLAG_INCLUDE_CODE, 0,
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
                     C4_CHAIN_MAINNET, C4_PROVER_FLAG_INCLUDE_CODE, 0,
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
                     C4_CHAIN_MAINNET, C4_PROVER_FLAG_INCLUDE_CODE, 0,
                     false /* local prover */,
                     UINT64_C(99999999999) /* year ~5138 → stale */,
                     EXPECT_ERROR_TOO_OLD);
}

// :: Test 5: PAP mode skips the freshness check (intentional, follow-up tracked)
//
// In PAP mode the proof is assembled lazily after `eth_getProof` and currently
// contains no block-context timestamp. To avoid spuriously rejecting valid
// PAP runs, the verifier explicitly skips the freshness check when
// `VERIFY_FLAG_PAP` is set. This regression test pins that behaviour: even
// with a far-future lower bound that would otherwise blow up Test 3, the PAP
// run completes successfully.

void test_freshness_pap_skips_check(void) {
  run_freshness_case("eth_call_pap_cached", "eth_call",
                     "[{\"to\":\"0xdac17f958d2ee523a2206206994597c13d831ec7\","
                     "\"data\":\"0x70a082310000000000000000000000008825ef664b8b43984bbf32b09e6a690c9b914931\"},\"latest\"]",
                     C4_CHAIN_MAINNET,
                     C4_PROVER_FLAG_USE_ACCESSLIST | C4_PROVER_FLAG_INCLUDE_CODE,
                     VERIFY_FLAG_PAP,
                     true /* remote prover for the cached PAP fixture */,
                     UINT64_C(99999999999) /* would normally trigger the check */,
                     EXPECT_SUCCESS);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_freshness_disabled_passes_on_latest);
  RUN_TEST(test_freshness_fresh_proof_passes);
  RUN_TEST(test_freshness_stale_proof_rejected);
  RUN_TEST(test_freshness_simulate_stale_rejected);
  RUN_TEST(test_freshness_pap_skips_check);
  return UNITY_END();
}
