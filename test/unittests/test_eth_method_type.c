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

#include "chains.h"
#include "json.h"
#include "unity.h"
#include "verify.h"

void setUp(void) {}
void tearDown(void) {}

static method_type_t method_type(const char* method, const char* params) {
  return c4_get_method_type(C4_CHAIN_MAINNET, (char*) method, json_parse(params), 0);
}

// Requests with a normal (pinned or "latest") block tag stay proofable. The block-tag argument
// sits at different parameter positions depending on the method.
void test_method_type_proofable() {
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, method_type("eth_getBalance", "[\"0xabc\",\"latest\"]"));
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, method_type("eth_getTransactionCount", "[\"0xabc\",\"0x1\"]"));
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, method_type("eth_getStorageAt", "[\"0xabc\",\"0x0\",\"latest\"]"));
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, method_type("eth_getProof", "[\"0xabc\",[],\"finalized\"]"));
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, method_type("eth_getBlockByNumber", "[\"0x1\",false]"));
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, method_type("eth_call", "[{\"to\":\"0xabc\"},\"latest\"]"));
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, method_type("eth_getLogs", "[{\"fromBlock\":\"0x1\",\"toBlock\":\"0x2\"}]"));
}

// "pending" is not proofable at any of the supported block-tag positions.
void test_method_type_pending_unproofable() {
  TEST_ASSERT_EQUAL_INT(METHOD_UNPROOFABLE, method_type("eth_getBalance", "[\"0xabc\",\"pending\"]"));
  TEST_ASSERT_EQUAL_INT(METHOD_UNPROOFABLE, method_type("eth_getTransactionCount", "[\"0xabc\",\"pending\"]"));
  TEST_ASSERT_EQUAL_INT(METHOD_UNPROOFABLE, method_type("eth_getStorageAt", "[\"0xabc\",\"0x0\",\"pending\"]"));
  TEST_ASSERT_EQUAL_INT(METHOD_UNPROOFABLE, method_type("eth_getBlockByNumber", "[\"pending\",false]"));
  TEST_ASSERT_EQUAL_INT(METHOD_UNPROOFABLE, method_type("eth_call", "[{\"to\":\"0xabc\"},\"pending\"]"));
  TEST_ASSERT_EQUAL_INT(METHOD_UNPROOFABLE, method_type("eth_getLogs", "[{\"fromBlock\":\"0x1\",\"toBlock\":\"pending\"}]"));
}

// "earliest" (genesis) is treated the same way as "pending".
void test_method_type_earliest_unproofable() {
  TEST_ASSERT_EQUAL_INT(METHOD_UNPROOFABLE, method_type("eth_getBalance", "[\"0xabc\",\"earliest\"]"));
  TEST_ASSERT_EQUAL_INT(METHOD_UNPROOFABLE, method_type("eth_getProof", "[\"0xabc\",[],\"earliest\"]"));
  TEST_ASSERT_EQUAL_INT(METHOD_UNPROOFABLE, method_type("eth_getBlockByNumber", "[\"earliest\",false]"));
  TEST_ASSERT_EQUAL_INT(METHOD_UNPROOFABLE, method_type("eth_getLogs", "[{\"fromBlock\":\"earliest\",\"toBlock\":\"latest\"}]"));
}

// Methods without a block-tag argument are unaffected by pending/earliest handling.
void test_method_type_no_block_tag() {
  TEST_ASSERT_EQUAL_INT(METHOD_LOCAL, method_type("eth_chainId", "[]"));
  // block-hash based lookups have no block tag, so a nonsense params array does not flip them
  TEST_ASSERT_EQUAL_INT(METHOD_PROOFABLE, method_type("eth_getTransactionByHash", "[\"0xabc\"]"));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_method_type_proofable);
  RUN_TEST(test_method_type_pending_unproofable);
  RUN_TEST(test_method_type_earliest_unproofable);
  RUN_TEST(test_method_type_no_block_tag);
  return UNITY_END();
}
