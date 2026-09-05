/*
 * Copyright 2026 corpus.core
 * SPDX-License-Identifier: MIT
 *
 * Phase-1 request validation: JSON/SSZ is checked once per data_request_t
 * and `validated` is set so the same payload is not re-checked on re-entry.
 */

#include "unity.h"
#include "bytes.h"
#include "chains.h"
#include "eth_req.h"
#include "json.h"
#include "prover.h"
#include "state.h"
#include <stdlib.h>
#include <string.h>

#ifdef EVMONE
#include "call_ctx.h"
#endif

void setUp(void) {}
void tearDown(void) {}

void test_retry_after_clears_validated(void) {
  data_request_t req = {0};
  req.response       = bytes_dup(bytes((uint8_t*) "x", 1));
  req.validated      = true;

  TEST_ASSERT_TRUE(c4_state_retry_after(&req, 100, 3));
  TEST_ASSERT_FALSE(req.validated);
  TEST_ASSERT_NULL(req.response.data);
}

void test_get_data_request_by_response(void) {
  c4_state_t      state = {0};
  data_request_t* req   = safe_calloc(1, sizeof(data_request_t));
  req->id[0]            = 1; // non-zero so add_request does not hash a NULL url
  req->response         = bytes_dup(bytes((uint8_t*) "body", 4));
  c4_state_add_request(&state, req);

  TEST_ASSERT_EQUAL_PTR(req, c4_state_get_data_request_by_response(&state, req->response));
  TEST_ASSERT_NULL(c4_state_get_data_request_by_response(&state, bytes((uint8_t*) "body", 4)));

  c4_state_free(&state);
}

static const char* k_block_id = "\"0x1\"";

void test_eth_get_block_rejects_incomplete_json(void) {
  prover_ctx_t* ctx    = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, 0);
  json_t        block  = {.start = k_block_id, .len = 5, .type = JSON_TYPE_STRING};
  json_t        result = {0};

  TEST_ASSERT_EQUAL_INT(C4_PENDING, eth_get_block(ctx, block, false, &result));
  data_request_t* req = c4_state_get_pending_request(&ctx->state);
  TEST_ASSERT_NOT_NULL(req);

  const char* body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"number\":\"0x1\"}}";
  req->response    = bytes((uint8_t*) strdup(body), (uint32_t) strlen(body));

  TEST_ASSERT_EQUAL_INT(C4_ERROR, eth_get_block(ctx, block, false, &result));
  TEST_ASSERT_FALSE(req->validated);
  TEST_ASSERT_NOT_NULL(ctx->state.error);

  c4_prover_free(ctx);
}

void test_eth_get_block_validates_once(void) {
  prover_ctx_t* ctx    = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, 0);
  json_t        block  = {.start = k_block_id, .len = 5, .type = JSON_TYPE_STRING};
  json_t        result = {0};

  TEST_ASSERT_EQUAL_INT(C4_PENDING, eth_get_block(ctx, block, false, &result));
  data_request_t* req = c4_state_get_pending_request(&ctx->state);
  TEST_ASSERT_NOT_NULL(req);

  const char* body =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{"
      "\"number\":\"0x1\","
      "\"hash\":\"0x1111111111111111111111111111111111111111111111111111111111111111\","
      "\"stateRoot\":\"0x2222222222222222222222222222222222222222222222222222222222222222\","
      "\"receiptsRoot\":\"0x3333333333333333333333333333333333333333333333333333333333333333\","
      "\"transactionsRoot\":\"0x4444444444444444444444444444444444444444444444444444444444444444\""
      "}}";
  req->response = bytes((uint8_t*) strdup(body), (uint32_t) strlen(body));

  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eth_get_block(ctx, block, false, &result));
  TEST_ASSERT_TRUE(req->validated);
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eth_get_block(ctx, block, false, &result));
  TEST_ASSERT_TRUE(req->validated);

  c4_prover_free(ctx);
}

#ifdef EVMONE
void test_eth_get_storage_at_rejects_invalid_result(void) {
  verify_ctx_t      vctx = {0};
  evmone_context_t  ectx = {0};
  address_t         addr = {0};
  bytes32_t         key  = {0};
  bytes32_t         out  = {0};
  ectx.ctx               = &vctx;

  call_account_lazy_fetch_storage(&ectx, addr, key, out);
  data_request_t* req = c4_state_get_pending_request(&vctx.state);
  TEST_ASSERT_NOT_NULL(req);

  const char* body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}";
  req->response    = bytes((uint8_t*) strdup(body), (uint32_t) strlen(body));
  memset(out, 0xff, 32);

  call_account_lazy_fetch_storage(&ectx, addr, key, out);
  TEST_ASSERT_NOT_NULL(vctx.state.error);
  TEST_ASSERT_FALSE(req->validated);
  for (int i = 0; i < 32; i++)
    TEST_ASSERT_EQUAL_UINT8(0xff, out[i]);

  c4_state_free(&vctx.state);
}
#endif

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_retry_after_clears_validated);
  RUN_TEST(test_get_data_request_by_response);
  RUN_TEST(test_eth_get_block_rejects_incomplete_json);
  RUN_TEST(test_eth_get_block_validates_once);
#ifdef EVMONE
  RUN_TEST(test_eth_get_storage_at_rejects_invalid_result);
#endif
  return UNITY_END();
}
