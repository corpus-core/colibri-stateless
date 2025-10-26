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
#include "beacon_types.h"
#include "bytes.h"
#include "c4_assert.h"
#include "ssz.h"
#include "unity.h"
void setUp(void) {
}

void tearDown(void) {
}

void test_chainId() {
  verify_ctx_t ctx    = {0};
  c4_status_t  status = c4_verify_from_bytes(&ctx, NULL_BYTES, "eth_chainId", (json_t) {.start = "[]", .len = 2, .type = JSON_TYPE_ARRAY}, C4_CHAIN_MAINNET);
  TEST_ASSERT_MESSAGE(status == C4_SUCCESS, "c4_verify_from_bytes failed");
  TEST_ASSERT_MESSAGE(ctx.data.bytes.len == 8 && ctx.data.bytes.data[0] == 0x01, "c4_verify_from_bytes failed");
  c4_verify_free_data(&ctx);
}

void test_chainId_sepolia() {
  verify_ctx_t ctx    = {0};
  c4_status_t  status = c4_verify_from_bytes(&ctx, NULL_BYTES, "eth_chainId", (json_t) {.start = "[]", .len = 2, .type = JSON_TYPE_ARRAY}, C4_CHAIN_SEPOLIA);
  TEST_ASSERT_MESSAGE(status == C4_SUCCESS, "c4_verify_from_bytes failed for Sepolia");
  // Sepolia chain ID is 11155111 (0xaa36a7)
  TEST_ASSERT_MESSAGE(ctx.data.bytes.len == 8, "Invalid chain ID length");
  c4_verify_free_data(&ctx);
}

void test_protocolVersion() {
  verify_ctx_t ctx    = {0};
  c4_status_t  status = c4_verify_from_bytes(&ctx, NULL_BYTES, "eth_protocolVersion", (json_t) {.start = "[]", .len = 2, .type = JSON_TYPE_ARRAY}, C4_CHAIN_MAINNET);
  TEST_ASSERT_MESSAGE(status == C4_SUCCESS, "c4_verify_from_bytes failed for eth_protocolVersion");
  TEST_ASSERT_MESSAGE(ctx.data.bytes.len > 0, "Protocol version should not be empty");
  c4_verify_free_data(&ctx);
}

void test_clientVersion() {
  verify_ctx_t ctx    = {0};
  c4_status_t  status = c4_verify_from_bytes(&ctx, NULL_BYTES, "web3_clientVersion", (json_t) {.start = "[]", .len = 2, .type = JSON_TYPE_ARRAY}, C4_CHAIN_MAINNET);
  TEST_ASSERT_MESSAGE(status == C4_SUCCESS, "c4_verify_from_bytes failed for web3_clientVersion");
  TEST_ASSERT_MESSAGE(ctx.data.bytes.len > 0, "Client version should not be empty");
  c4_verify_free_data(&ctx);
}

void test_gasPrice() {
  verify_ctx_t ctx    = {0};
  c4_status_t  status = c4_verify_from_bytes(&ctx, NULL_BYTES, "eth_gasPrice", (json_t) {.start = "[]", .len = 2, .type = JSON_TYPE_ARRAY}, C4_CHAIN_MAINNET);
  // Gas price is not implemented locally, should return error or pending
  TEST_ASSERT_MESSAGE(status == C4_ERROR || status == C4_PENDING, "eth_gasPrice should not succeed locally");
  c4_verify_free_data(&ctx);
}

void test_invalid_method() {
  verify_ctx_t ctx    = {0};
  c4_status_t  status = c4_verify_from_bytes(&ctx, NULL_BYTES, "invalid_method", (json_t) {.start = "[]", .len = 2, .type = JSON_TYPE_ARRAY}, C4_CHAIN_MAINNET);
  TEST_ASSERT_MESSAGE(status == C4_ERROR, "Invalid method should return error");
  c4_verify_free_data(&ctx);
}

void test_chainId_with_invalid_params() {
  verify_ctx_t ctx = {0};
  // Pass invalid params (should be empty array)
  c4_status_t status = c4_verify_from_bytes(&ctx, NULL_BYTES, "eth_chainId", (json_t) {.start = "[123]", .len = 5, .type = JSON_TYPE_ARRAY}, C4_CHAIN_MAINNET);
  // Should still succeed as params are ignored for chainId
  TEST_ASSERT_MESSAGE(status == C4_SUCCESS, "eth_chainId should ignore extra params");
  c4_verify_free_data(&ctx);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_chainId);
  RUN_TEST(test_chainId_sepolia);
  RUN_TEST(test_protocolVersion);
  RUN_TEST(test_clientVersion);
  RUN_TEST(test_gasPrice);
  RUN_TEST(test_invalid_method);
  RUN_TEST(test_chainId_with_invalid_params);
  return UNITY_END();
}