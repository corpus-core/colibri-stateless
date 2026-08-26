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

// datei: test_addiere.c
#include "bytes.h"
#include "c4_assert.h"
#include "eth_prover.h"
#include "prover.h"
#include "ssz.h"
#include "state.h"
#include "sync_committee.h"
#include "unity.h"
#include <string.h>

void setUp(void) {
  reset_local_filecache();
}

void tearDown(void) {
  reset_local_filecache();
}

void test_sync() {
  set_state(C4_CHAIN_MAINNET, "eth_sync");
  bytes_t      update = read_testdata("eth_sync/light_client_update.ssz");
  verify_ctx_t ctx    = {0};
  ctx.chain_id        = C4_CHAIN_MAINNET;
  TEST_ASSERT_TRUE_MESSAGE(c4_handle_client_updates(&ctx, update), "Failed to update");
  safe_free(update.data);
  c4_state_free(&ctx.state);
}

static uint32_t request_count(const c4_state_t* state) {
  uint32_t n = 0;
  for (data_request_t* r = state->requests; r; r = r->next) n++;
  return n;
}

static void assert_requests_have_usable_urls(const c4_state_t* state, const char* where) {
  TEST_ASSERT_NOT_NULL_MESSAGE(state->requests, where);
  for (data_request_t* r = state->requests; r; r = r->next) {
    TEST_ASSERT_NOT_NULL_MESSAGE(r->url, where);
    TEST_ASSERT_TRUE_MESSAGE(strlen(r->url) > 0, where);
  }
}

static void seed_dummy_sync_period(uint32_t period) {
  enum { KEYS_LEN = 512 * 48 };
  uint8_t*  keys = (uint8_t*) safe_calloc(1, KEYS_LEN);
  bytes32_t prev = {0};
  TEST_ASSERT_TRUE_MESSAGE(c4_set_sync_period(period, bytes(keys, KEYS_LEN), C4_CHAIN_MAINNET, prev),
                           "failed to seed dummy sync period");
  safe_free(keys);
}

// Regression: if a Beacon light-client request is still pending, req_client_update
// must return pending instead of allocating a second request whose URL points at
// the already-freed format buffer (strlen(NULL) in c4_state_add_request).
void test_get_validators_pending_client_update_does_not_duplicate_null_url(void) {
  seed_dummy_sync_period(100);

  verify_ctx_t         ctx = {0};
  c4_sync_validators_t vs  = {0};
  ctx.chain_id             = C4_CHAIN_MAINNET;

  c4_status_t first = c4_get_validators(&ctx, 102, &vs, NULL);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_PENDING, first, ctx.state.error ? ctx.state.error : "first get_validators");
  TEST_ASSERT_NULL(ctx.state.error);
  TEST_ASSERT_EQUAL_UINT32(1, request_count(&ctx.state));
  assert_requests_have_usable_urls(&ctx.state, "first light-client update request");
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.requests->url, "eth/v1/beacon/light_client/updates?start_period="));

  char* url_first = strdup(ctx.state.requests->url);

  c4_status_t second = c4_get_validators(&ctx, 102, &vs, NULL);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_PENDING, second, ctx.state.error ? ctx.state.error : "second get_validators");
  TEST_ASSERT_NULL(ctx.state.error);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, request_count(&ctx.state),
                                   "pending re-entry must not append a second request");
  assert_requests_have_usable_urls(&ctx.state, "second light-client update request");
  TEST_ASSERT_EQUAL_STRING(url_first, ctx.state.requests->url);

  safe_free(url_first);
  c4_state_free(&ctx.state);
}

// Same fallthrough existed in req_bootstrap: a still-pending bootstrap request
// used to create a duplicate with a freed URL.
void test_get_validators_pending_bootstrap_does_not_duplicate_null_url(void) {
  bytes32_t checkpoint = {0};
  memset(checkpoint, 0xab, 32);
  c4_eth_set_trusted_checkpoint(C4_CHAIN_MAINNET, checkpoint);

  verify_ctx_t         ctx = {0};
  c4_sync_validators_t vs  = {0};
  ctx.chain_id             = C4_CHAIN_MAINNET;

  c4_status_t first = c4_get_validators(&ctx, 100, &vs, NULL);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_PENDING, first, ctx.state.error ? ctx.state.error : "first bootstrap");
  TEST_ASSERT_NULL(ctx.state.error);
  TEST_ASSERT_EQUAL_UINT32(1, request_count(&ctx.state));
  assert_requests_have_usable_urls(&ctx.state, "first bootstrap request");
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.requests->url, "eth/v1/beacon/light_client/bootstrap/0x"));

  char* url_first = strdup(ctx.state.requests->url);

  c4_status_t second = c4_get_validators(&ctx, 100, &vs, NULL);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_PENDING, second, ctx.state.error ? ctx.state.error : "second bootstrap");
  TEST_ASSERT_NULL(ctx.state.error);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, request_count(&ctx.state),
                                   "pending re-entry must not append a second bootstrap request");
  assert_requests_have_usable_urls(&ctx.state, "second bootstrap request");
  TEST_ASSERT_EQUAL_STRING(url_first, ctx.state.requests->url);

  safe_free(url_first);
  c4_state_free(&ctx.state);
}

// Prover twin of the same bug. c4_prover_execute returns early while anything is
// pending, so we re-enter c4_proof_sync directly (same helper, two periods).
void test_proof_sync_pending_client_update_does_not_duplicate_null_url(void) {
  prover_ctx_t* ctx = c4_prover_create("eth_proof_sync", "[100]", C4_CHAIN_MAINNET, 0);
  TEST_ASSERT_NOT_NULL(ctx);

  c4_status_t first = c4_proof_sync(ctx);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_PENDING, first, ctx->state.error ? ctx->state.error : "first proof_sync");
  TEST_ASSERT_NULL(ctx->state.error);
  TEST_ASSERT_EQUAL_UINT32(2, request_count(&ctx->state));
  assert_requests_have_usable_urls(&ctx->state, "first proof_sync requests");

  char* url_a = strdup(ctx->state.requests->url);
  char* url_b = strdup(ctx->state.requests->next->url);

  c4_status_t second = c4_proof_sync(ctx);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_PENDING, second, ctx->state.error ? ctx->state.error : "second proof_sync");
  TEST_ASSERT_NULL(ctx->state.error);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, request_count(&ctx->state),
                                   "pending re-entry must not append extra light-client update requests");
  assert_requests_have_usable_urls(&ctx->state, "second proof_sync requests");
  TEST_ASSERT_EQUAL_STRING(url_a, ctx->state.requests->url);
  TEST_ASSERT_EQUAL_STRING(url_b, ctx->state.requests->next->url);

  safe_free(url_a);
  safe_free(url_b);
  c4_prover_free(ctx);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_sync);
  RUN_TEST(test_get_validators_pending_client_update_does_not_duplicate_null_url);
  RUN_TEST(test_get_validators_pending_bootstrap_does_not_duplicate_null_url);
  RUN_TEST(test_proof_sync_pending_client_update_does_not_duplicate_null_url);
  return UNITY_END();
}