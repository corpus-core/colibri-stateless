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

// Regression coverage for the `colibri_proofCall` access-list parser selection
// in `c4_get_eth_proofs` (src/chains/eth/prover/proof_call.c).
//
// For `colibri_proofCall` the prover's `trace` is *always* the access-list
// object `{"accessList":[{"address":...,"storageKeys":[...]}]}`, regardless of
// the `C4_PROVER_FLAG_USE_ACCESSLIST` prover flag (that flag only selects which
// trace builder is used for a plain `eth_call`).
//
// The bug: parser selection used to depend solely on the flag. With the flag
// UNSET the prestate-trace branch ran and iterated the object's PROPERTY NAMES,
// treating the literal key "accessList" as an account address. That produced an
// `eth_getProof("accessList", [], block)` request which upstream nodes reject.
//
// The fix branches on the actual trace shape (presence of an `"accessList"`
// array) so the access-list parser is used whenever a list is present.
//
// These tests drive `c4_get_eth_proofs` directly with an access-list-shaped
// trace and inspect the first pending `eth_getProof` request. They assert the
// queried account is the real address and never the literal "accessList".

#include "bytes.h"
#include "crypto.h"
#include "eth_tools.h"
#include "json.h"
#include "prover.h"
#include "ssz.h"
#include "state.h"
#include "unity.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#define TEST_ACCOUNT_ADDR "0xdac17f958d2ee523a2206206994597c13d831ec7"

// Drive c4_get_eth_proofs with the given prover flags and assert the resulting
// eth_getProof request targets the real account address (not "accessList").
static void run_accesslist_case(prover_flags_t flags) {
  // The access-list object is exactly what the prover receives as `trace` for
  // colibri_proofCall. params[0] is that same object (no "to" key), params[1]
  // is the block tag. Kept in a stack buffer so the zero-copy json_t stays valid.
  char params_str[] =
      "[{\"accessList\":[{\"address\":\"" TEST_ACCOUNT_ADDR "\","
      "\"storageKeys\":[\"0x0000000000000000000000000000000000000000000000000000000000000001\"]}]},"
      "\"latest\"]";

  prover_ctx_t ctx   = {0};
  ctx.method         = "colibri_proofCall";
  ctx.params         = json_parse(params_str);
  ctx.chain_id       = C4_CHAIN_MAINNET;
  ctx.flags          = flags;

  json_t        trace    = json_at(ctx.params, 0);
  ssz_builder_t builder  = {0};
  address_t     miner    = {0};

  c4_status_t status = c4_get_eth_proofs(&ctx, trace, 0x1234, &builder, miner, NULL);

  // The first eth_getProof request is created synchronously and returns PENDING.
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_PENDING, status,
                                ctx.state.error ? ctx.state.error : "expected C4_PENDING from c4_get_eth_proofs");

  data_request_t* req = c4_state_get_pending_request(&ctx.state);
  TEST_ASSERT_NOT_NULL_MESSAGE(req, "no pending request was created");
  TEST_ASSERT_NOT_NULL_MESSAGE(req->payload.data, "pending request has no payload");

  json_t payload = json_parse((char*) req->payload.data);
  char*  method  = json_as_string(json_get(payload, "method"), NULL);
  char*  account = json_as_string(json_at(json_get(payload, "params"), 0), NULL);

  TEST_ASSERT_EQUAL_STRING_MESSAGE("eth_getProof", method, "wrong RPC method for the proof request");

  // The core regression assertion: the queried account must be the real address
  // and must never be the literal property name "accessList".
  TEST_ASSERT_EQUAL_STRING_MESSAGE(TEST_ACCOUNT_ADDR, account,
                                   "eth_getProof must query the access-list address, not the property name");
  TEST_ASSERT_NULL_MESSAGE(strstr(account, "accessList"),
                           "eth_getProof account must not contain the literal \"accessList\"");

  safe_free(method);
  safe_free(account);
  ssz_builder_free(&builder);
  c4_state_free(&ctx.state);
}

// :: Regression: USE_ACCESSLIST flag UNSET (the previously broken path)
//
// Before the fix this fell into the prestate-trace branch and built
// eth_getProof("accessList", ...). The access-list parser must now be selected
// purely from the trace shape.
void test_proof_call_accesslist_without_flag(void) {
  run_accesslist_case(0);
}

// :: Control: USE_ACCESSLIST flag SET (was always correct)
//
// Pins that the flag-set path keeps producing the correct request, so both
// branches of the new condition converge on the same behaviour.
void test_proof_call_accesslist_with_flag(void) {
  run_accesslist_case(C4_PROVER_FLAG_USE_ACCESSLIST);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_proof_call_accesslist_without_flag);
  RUN_TEST(test_proof_call_accesslist_with_flag);
  return UNITY_END();
}
