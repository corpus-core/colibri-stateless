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

#ifdef HTTP_SERVER
#include "server/server.h"
#endif

#ifdef HTTP_SERVER
static uint64_t saved_server_chain = 0;
static char*    saved_server_l1    = NULL;
static int      server_state_saved = 0;

static char* test_block_network(const char* url, const char* payload) {
  (void) url;
  (void) payload;
  return NULL;
}
#endif

void setUp(void) {}

void tearDown(void) {
#ifdef HTTP_SERVER
  if (server_state_saved) {
    http_server.chain_id     = saved_server_chain;
    http_server.rpc_l1_nodes = saved_server_l1;
    c4_test_url_rewriter     = NULL;
    server_state_saved       = 0;
  }
#endif
}

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

#ifdef HTTP_SERVER
void test_server_eth_rpc_list_follows_chain(void) {
  saved_server_chain       = http_server.chain_id;
  saved_server_l1          = http_server.rpc_l1_nodes;
  server_state_saved       = 1;
  http_server.rpc_l1_nodes = (char*) "https://l1.example";
  c4_test_url_rewriter     = test_block_network;

  data_request_t req = {0};
  req.type           = C4_DATA_TYPE_ETH_RPC;
  server_list_t* primary = c4_get_server_list(C4_DATA_TYPE_ETH_RPC);

  http_server.chain_id = 1;
  req.chain_id         = 1;
  TEST_ASSERT_EQUAL_PTR(primary, c4_get_effective_server_list(C4_DATA_TYPE_ETH_RPC, NULL, &req));

  req.chain_id = 0;
  TEST_ASSERT_NULL(c4_get_effective_server_list(C4_DATA_TYPE_ETH_RPC, NULL, &req));
  req.chain_id = 10;
  TEST_ASSERT_NULL(c4_get_effective_server_list(C4_DATA_TYPE_ETH_RPC, NULL, &req));

  http_server.chain_id = 8453;
  req.chain_id         = 8453;
  TEST_ASSERT_EQUAL_PTR(primary, c4_get_effective_server_list(C4_DATA_TYPE_ETH_RPC, NULL, &req));

  server_list_t proxy = {0};
  proxy.count         = 1;
  request_t parent    = {0};
  parent.proxy_rpc_servers = &proxy;
  TEST_ASSERT_EQUAL_PTR(&proxy, c4_get_effective_server_list(C4_DATA_TYPE_ETH_RPC, &parent, &req));

  req.chain_id = 1;
  server_list_t* l1 = c4_get_effective_server_list(C4_DATA_TYPE_ETH_RPC, NULL, &req);
  TEST_ASSERT_NOT_NULL(l1);
  TEST_ASSERT_TRUE(l1 != primary);
  TEST_ASSERT_TRUE(l1->count == 1);
  TEST_ASSERT_EQUAL_STRING("https://l1.example", l1->urls[0]);
  TEST_ASSERT_EQUAL_PTR(l1, c4_get_effective_server_list(C4_DATA_TYPE_ETH_RPC, &parent, &req));

  req.chain_id = 10;
  TEST_ASSERT_NULL(c4_get_effective_server_list(C4_DATA_TYPE_ETH_RPC, &parent, &req));
  req.chain_id = 0;
  TEST_ASSERT_NULL(c4_get_effective_server_list(C4_DATA_TYPE_ETH_RPC, &parent, &req));
}
#endif

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_l1_chain_id_is_mainnet);
  RUN_TEST(test_eth_rpc_request_carries_chain_and_distinct_ids);
  RUN_TEST(test_mockname_ignores_chain_id);
#ifdef HTTP_SERVER
  RUN_TEST(test_server_eth_rpc_list_follows_chain);
#endif
  return UNITY_END();
}
