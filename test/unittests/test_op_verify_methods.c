/*
 * Copyright (c) 2025 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in the
 * Software without restriction, including without limitation the rights to
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

#include "chains.h"
#include "chains/op/verifier/op_verify.h"
#include "json.h"
#include "unity.h"
#include "verify.h"

void setUp(void) {}

void tearDown(void) {}

static json_t empty_params(void) {
  json_t j = {.start = "{}", .len = 2, .type = JSON_TYPE_OBJECT};
  return j;
}

/** EL hybrid-style RPCs must be classified like Ethereum once OP shares the ETH prover/verifier path. */
void test_op_el_extensions_are_proofable(void) {
  chain_id_t op    = C4_CHAIN_OP_MAINNET;
  json_t     params = empty_params();

  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, c4_op_get_method_type(op, "eth_getBlockReceipts", params, 0));
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, c4_op_get_method_type(op, "eth_getBlockHeader", params, 0));
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, c4_op_get_method_type(op, "eth_blobBaseFee", params, 0));
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, c4_op_get_method_type(op, "eth_maxPriorityFeePerGas", params, 0));
}

void test_op_classifier_is_only_for_op_stack_ids(void) {
  json_t params = empty_params();
  TEST_ASSERT_EQUAL_INT(METHOD_UNDEFINED, c4_op_get_method_type(C4_CHAIN_MAINNET, "eth_getBlockReceipts", params, 0));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_op_el_extensions_are_proofable);
  RUN_TEST(test_op_classifier_is_only_for_op_stack_ids);
  return UNITY_END();
}
