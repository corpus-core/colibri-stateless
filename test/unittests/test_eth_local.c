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
  // c4_verify_free_data(&ctx);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_chainId);
  return UNITY_END();
}