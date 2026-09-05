/*
 * Copyright 2026 corpus.core
 * SPDX-License-Identifier: MIT
 *
 * Phase-1 request validation: JSON/SSZ is checked once per data_request_t
 * and `validated` is set so the same payload is not re-checked on re-entry.
 */

#include "beacon.h"
#include "bytes.h"
#include "chains.h"
#include "eth_req.h"
#include "json.h"
#include "prover.h"
#include "state.h"
#include "unity.h"
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

void test_send_beacon_ssz_null_def_does_not_validate(void) {
  prover_ctx_t*   ctx    = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, 0);
  ssz_ob_t        result = {0};
  data_request_t* req    = NULL;

  TEST_ASSERT_EQUAL_INT(C4_PENDING, c4_send_beacon_ssz(ctx, "eth/v2/beacon/blocks/head", NULL, NULL, 0, &result, &req));
  TEST_ASSERT_NOT_NULL(req);

  uint8_t* body = safe_calloc(32, 1);
  req->response = bytes(body, 32);

  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, c4_send_beacon_ssz(ctx, "eth/v2/beacon/blocks/head", NULL, NULL, 0, &result, &req));
  TEST_ASSERT_FALSE(req->validated);
  TEST_ASSERT_EQUAL_PTR(body, result.bytes.data);

  c4_prover_free(ctx);
}

void test_eth_debug_get_raw_block_decodes_to_bytes(void) {
  prover_ctx_t* ctx      = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, 0);
  uint8_t       hash[32] = {0};
  bytes_t       result   = {0};

  TEST_ASSERT_EQUAL_INT(C4_PENDING, eth_debug_get_raw_block(ctx, hash, &result));
  data_request_t* req = c4_state_get_pending_request(&ctx->state);
  TEST_ASSERT_NOT_NULL(req);

  const char* body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0xdeadbeef\"}";
  req->response    = bytes((uint8_t*) strdup(body), (uint32_t) strlen(body));

  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eth_debug_get_raw_block(ctx, hash, &result));
  TEST_ASSERT_TRUE(req->validated);
  TEST_ASSERT_EQUAL_UINT32(4, result.len);
  TEST_ASSERT_EQUAL_UINT8(0xde, result.data[0]);
  TEST_ASSERT_EQUAL_UINT8(0xad, result.data[1]);
  TEST_ASSERT_EQUAL_UINT8(0xbe, result.data[2]);
  TEST_ASSERT_EQUAL_UINT8(0xef, result.data[3]);
  TEST_ASSERT_EQUAL_PTR(req->response.data, result.data);

  bytes_t again = {0};
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eth_debug_get_raw_block(ctx, hash, &again));
  TEST_ASSERT_EQUAL_PTR(result.data, again.data);
  TEST_ASSERT_EQUAL_UINT32(4, again.len);
  TEST_ASSERT_TRUE(req->validated);

  c4_prover_free(ctx);
}

void test_eth_debug_get_raw_block_rejects_invalid_json(void) {
  prover_ctx_t* ctx      = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, 0);
  uint8_t       hash[32] = {0};
  bytes_t       result   = {0};

  TEST_ASSERT_EQUAL_INT(C4_PENDING, eth_debug_get_raw_block(ctx, hash, &result));
  data_request_t* req = c4_state_get_pending_request(&ctx->state);
  TEST_ASSERT_NOT_NULL(req);

  const char* body      = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}";
  uint8_t*    json_body = (uint8_t*) strdup(body);
  uint32_t    json_len  = (uint32_t) strlen(body);
  req->response         = bytes(json_body, json_len);

  TEST_ASSERT_EQUAL_INT(C4_ERROR, eth_debug_get_raw_block(ctx, hash, &result));
  TEST_ASSERT_FALSE(req->validated);
  TEST_ASSERT_NOT_NULL(ctx->state.error);
  TEST_ASSERT_EQUAL_PTR(json_body, req->response.data);
  TEST_ASSERT_EQUAL_UINT32(json_len, req->response.len);
  TEST_ASSERT_EQUAL_MEMORY(body, req->response.data, json_len);

  c4_prover_free(ctx);
}

void test_eth_debug_get_raw_block_rejects_empty_hex(void) {
  prover_ctx_t* ctx      = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, 0);
  uint8_t       hash[32] = {0};
  bytes_t       result   = {0};

  TEST_ASSERT_EQUAL_INT(C4_PENDING, eth_debug_get_raw_block(ctx, hash, &result));
  data_request_t* req = c4_state_get_pending_request(&ctx->state);
  TEST_ASSERT_NOT_NULL(req);

  const char* body      = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0x\"}";
  uint8_t*    json_body = (uint8_t*) strdup(body);
  uint32_t    json_len  = (uint32_t) strlen(body);
  req->response         = bytes(json_body, json_len);

  TEST_ASSERT_EQUAL_INT(C4_ERROR, eth_debug_get_raw_block(ctx, hash, &result));
  TEST_ASSERT_FALSE(req->validated);
  TEST_ASSERT_NOT_NULL(ctx->state.error);
  TEST_ASSERT_NOT_NULL(strstr(ctx->state.error, "empty"));
  TEST_ASSERT_EQUAL_PTR(json_body, req->response.data);
  TEST_ASSERT_EQUAL_UINT32(json_len, req->response.len);
  TEST_ASSERT_EQUAL_MEMORY(body, req->response.data, json_len);

  c4_prover_free(ctx);
}

void test_eth_debug_get_raw_block_decodes_long_hex(void) {
  prover_ctx_t* ctx      = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, 0);
  uint8_t       hash[32] = {0};
  bytes_t       result   = {0};

  TEST_ASSERT_EQUAL_INT(C4_PENDING, eth_debug_get_raw_block(ctx, hash, &result));
  data_request_t* req = c4_state_get_pending_request(&ctx->state);
  TEST_ASSERT_NOT_NULL(req);

  const char* body =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0x"
      "000102030405060708090a0b0c0d0e0f"
      "101112131415161718191a1b1c1d1e1f"
      "202122232425262728292a2b2c2d2e2f"
      "303132333435363738393a3b3c3d3e3f\"}";
  req->response = bytes((uint8_t*) strdup(body), (uint32_t) strlen(body));

  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, eth_debug_get_raw_block(ctx, hash, &result));
  TEST_ASSERT_TRUE(req->validated);
  TEST_ASSERT_EQUAL_UINT32(64, result.len);
  TEST_ASSERT_EQUAL_PTR(req->response.data, result.data);
  for (uint32_t i = 0; i < 64; i++)
    TEST_ASSERT_EQUAL_UINT8((uint8_t) i, result.data[i]);

  c4_prover_free(ctx);
}

#ifdef EVMONE
void test_eth_get_storage_at_rejects_invalid_result(void) {
  verify_ctx_t     vctx = {0};
  evmone_context_t ectx = {0};
  address_t        addr = {0};
  bytes32_t        key  = {0};
  bytes32_t        out  = {0};
  ectx.ctx              = &vctx;

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
  RUN_TEST(test_send_beacon_ssz_null_def_does_not_validate);
  RUN_TEST(test_eth_debug_get_raw_block_decodes_to_bytes);
  RUN_TEST(test_eth_debug_get_raw_block_rejects_invalid_json);
  RUN_TEST(test_eth_debug_get_raw_block_rejects_empty_hex);
  RUN_TEST(test_eth_debug_get_raw_block_decodes_long_hex);
#ifdef EVMONE
  RUN_TEST(test_eth_get_storage_at_rejects_invalid_result);
#endif
  return UNITY_END();
}
