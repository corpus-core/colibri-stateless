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

#include "colibri.h"
#include "unity.h"

void setUp(void) {}

void tearDown(void) {}

void test_verify_create_ctx_args_ownership(void) {
  // json_parse skips leading whitespace. c4_verify_free_ctx frees ctx->args.start,
  // so that pointer must be the malloc base, including when args have a prefix.
  bytes_t proof = {0};
  void*   ctx   = c4_verify_create_ctx(proof, "eth_chainId", "  []", 1, NULL, 0);
  TEST_ASSERT_NOT_NULL(ctx);
  c4_verify_free_ctx(ctx);

  ctx = c4_verify_create_ctx(proof, "eth_chainId", "[]", 1, NULL, 0);
  TEST_ASSERT_NOT_NULL(ctx);
  c4_verify_free_ctx(ctx);
}

void test_verify_create_ctx_rejects_bad_input(void) {
  bytes_t proof = {0};
  // Valid array, but eth_getBalance needs a proof. args_owned is allocated and
  // must be released on the init-failure path.
  TEST_ASSERT_NULL(c4_verify_create_ctx(proof, "eth_getBalance", "  []", 1, NULL, 0));
  TEST_ASSERT_NULL(c4_verify_create_ctx(proof, "eth_chainId", "not-json", 1, NULL, 0));
  TEST_ASSERT_NULL(c4_verify_create_ctx(proof, "eth_chainId", NULL, 1, NULL, 0));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_verify_create_ctx_args_ownership);
  RUN_TEST(test_verify_create_ctx_rejects_bad_input);
  return UNITY_END();
}
