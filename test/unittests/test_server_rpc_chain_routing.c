/*
 * Copyright 2025,2026 corpus.core
 * SPDX-License-Identifier: MIT
 *
 * Verifies that `c4_get_server_list` picks the per-chain RPC list registered via
 * `c4_register_eth_rpc_list` and falls back to the server's built-in list for
 * `chain_id == 0`, `chain_id == http_server.chain_id`, and non-RPC request types.
 */

#include "unity.h"
#include <stdio.h>

#ifdef HTTP_SERVER

#include "../../src/server/server.h"
#include <string.h>

void setUp(void) {
  // A clean slate: the test looks only at the eth RPC list, and only the entries
  // registered below must be present. Set the server chain so lookups for that chain
  // reliably return the built-in list, not a registered one.
  memset(&http_server, 0, sizeof(http_server));
  http_server.chain_id = 1; // Mainnet
}
void tearDown(void) {}

void test_get_server_list_null_chain_returns_builtin(void) {
  // No registrations yet. The handler must still return the built-in (initially empty)
  // list rather than NULL -- the global list is static.
  server_list_t* l = c4_get_server_list(C4_DATA_TYPE_ETH_RPC, 0);
  TEST_ASSERT_NOT_NULL(l);
  TEST_ASSERT_EQUAL_UINT(0, l->count);
}

void test_register_eth_rpc_list_is_returned_for_registered_chain(void) {
  c4_register_eth_rpc_list(696991, "http://rollupx-1:8545,http://rollupx-2:8545");
  // Registered chain -> extra list
  server_list_t* rollupx = c4_get_server_list(C4_DATA_TYPE_ETH_RPC, 696991);
  TEST_ASSERT_NOT_NULL(rollupx);
  TEST_ASSERT_EQUAL_UINT(2, rollupx->count);

  // Unrelated chain -> built-in (empty here)
  server_list_t* other = c4_get_server_list(C4_DATA_TYPE_ETH_RPC, 42);
  TEST_ASSERT_NOT_NULL(other);
  TEST_ASSERT_NOT_EQUAL(rollupx, other);

  // Server's own chain -> built-in, even if the registry was called with it (the
  // registration itself is a no-op for the server chain, but the lookup must also
  // never route to an extra entry in that case).
  server_list_t* own = c4_get_server_list(C4_DATA_TYPE_ETH_RPC, http_server.chain_id);
  TEST_ASSERT_NOT_NULL(own);
  TEST_ASSERT_NOT_EQUAL(rollupx, own);
}

void test_register_eth_rpc_list_no_op_for_own_chain(void) {
  // Registering the server's own chain must not shadow the built-in list.
  c4_register_eth_rpc_list(http_server.chain_id, "http://would-shadow:8545");
  server_list_t* own = c4_get_server_list(C4_DATA_TYPE_ETH_RPC, http_server.chain_id);
  TEST_ASSERT_NOT_NULL(own);
  TEST_ASSERT_EQUAL_UINT(0, own->count);
}

void test_get_server_list_other_types_ignore_chain(void) {
  // Beacon / prover / checkpointz remain single-chain: the chain parameter is not
  // consulted for them, so any chain returns the same static list.
  server_list_t* a = c4_get_server_list(C4_DATA_TYPE_BEACON_API, 0);
  server_list_t* b = c4_get_server_list(C4_DATA_TYPE_BEACON_API, 696991);
  TEST_ASSERT_EQUAL_PTR(a, b);

  a = c4_get_server_list(C4_DATA_TYPE_PROVER, 0);
  b = c4_get_server_list(C4_DATA_TYPE_PROVER, 696991);
  TEST_ASSERT_EQUAL_PTR(a, b);

  a = c4_get_server_list(C4_DATA_TYPE_CHECKPOINTZ, 0);
  b = c4_get_server_list(C4_DATA_TYPE_CHECKPOINTZ, 696991);
  TEST_ASSERT_EQUAL_PTR(a, b);

  // Unknown type -> NULL
  TEST_ASSERT_NULL(c4_get_server_list((data_request_type_t) 999, 0));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_get_server_list_null_chain_returns_builtin);
  RUN_TEST(test_register_eth_rpc_list_is_returned_for_registered_chain);
  RUN_TEST(test_register_eth_rpc_list_no_op_for_own_chain);
  RUN_TEST(test_get_server_list_other_types_ignore_chain);
  return UNITY_END();
}

#else
int main(void) {
  fprintf(stderr, "test_server_rpc_chain_routing: Skipped (HTTP_SERVER not enabled)\n");
  return 0;
}
#endif
