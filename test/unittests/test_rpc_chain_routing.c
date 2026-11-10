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

#include "bytes.h"
#include "chains.h"
#include "eth_req.h"
#include "prover.h"
#include "state.h"
#include "unity.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_l1_chain_id_is_mainnet(void) {
  TEST_ASSERT_TRUE(c4_get_l1_chain_id_for_l2(1) == 1);
  TEST_ASSERT_TRUE(c4_get_l1_chain_id_for_l2(10) == 1);
  TEST_ASSERT_TRUE(c4_get_l1_chain_id_for_l2(8453) == 1);
}

void test_eth_rpc_request_carries_chain_and_distinct_ids(void) {
  prover_ctx_t*   mainnet = c4_prover_create("eth_blockNumber", "[]", 1, 0);
  prover_ctx_t*   base    = c4_prover_create("eth_blockNumber", "[]", 8453, 0);
  json_t          result  = {0};
  data_request_t* on_l1   = NULL;
  data_request_t* on_l2   = NULL;

  TEST_ASSERT_EQUAL_INT(C4_PENDING, c4_send_eth_rpc(mainnet, "eth_blockNumber", "[]", 0, &result, &on_l1));
  TEST_ASSERT_EQUAL_INT(C4_PENDING, c4_send_eth_rpc(base, "eth_blockNumber", "[]", 0, &result, &on_l2));
  TEST_ASSERT_NOT_NULL(on_l1);
  TEST_ASSERT_NOT_NULL(on_l2);
  TEST_ASSERT_TRUE(on_l1->chain_id == 1);
  TEST_ASSERT_TRUE(on_l2->chain_id == 8453);
  TEST_ASSERT_FALSE(bytes_eq(bytes(on_l1->id, 32), bytes(on_l2->id, 32)));

  data_request_t* again = NULL;
  TEST_ASSERT_EQUAL_INT(C4_PENDING, c4_send_eth_rpc(mainnet, "eth_blockNumber", "[]", 0, &result, &again));
  TEST_ASSERT_EQUAL_PTR(on_l1, again);

  c4_prover_free(mainnet);
  c4_prover_free(base);
}

void test_mockname_ignores_chain_id(void) {
  const char*    body = "{\"jsonrpc\":\"2.0\",\"method\":\"eth_blockNumber\",\"params\":[],\"id\":1}";
  data_request_t l1   = {0};
  data_request_t l2   = {0};

  l1.type     = C4_DATA_TYPE_ETH_RPC;
  l2.type     = C4_DATA_TYPE_ETH_RPC;
  l1.chain_id = 1;
  l2.chain_id = 8453;
  l1.payload  = bytes((uint8_t*) body, (uint32_t) strlen(body));
  l2.payload  = l1.payload;
  c4_chain_request_id(l1.id, l1.chain_id, l1.payload);
  c4_chain_request_id(l2.id, l2.chain_id, l2.payload);
  TEST_ASSERT_FALSE(bytes_eq(bytes(l1.id, 32), bytes(l2.id, 32)));

  char* name_l1 = c4_req_mockname(&l1);
  char* name_l2 = c4_req_mockname(&l2);
  TEST_ASSERT_NOT_NULL(name_l1);
  TEST_ASSERT_EQUAL_STRING(name_l1, name_l2);
  TEST_ASSERT_NULL(strstr(name_l1, "8453"));
  safe_free(name_l1);
  safe_free(name_l2);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_l1_chain_id_is_mainnet);
  RUN_TEST(test_eth_rpc_request_carries_chain_and_distinct_ids);
  RUN_TEST(test_mockname_ignores_chain_id);
  return UNITY_END();
}
