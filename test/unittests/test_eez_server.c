/*
 * Copyright 2025,2026 corpus.core
 * SPDX-License-Identifier: MIT
 *
 * Tests for the prover server of an EEZ chain: it serves the cache-friendly block proof
 * of hybrid mode (`GET /proof/<method>/<block>/<version>/<zk|std>/<c4>`), which a server
 * registers per chain type.
 */

#include "unity.h"
#include <stdio.h>

#if defined(HTTP_SERVER) && !defined(NO_CHAIN_EEZ)

#include "test_server_helper.h"

#define ROLLUPX_CHAIN_ID 696991

void setUp(void) {
  http_server_t config = {0};
  config.port          = TEST_PORT;
  config.host          = TEST_HOST;
  config.chain_id      = ROLLUPX_CHAIN_ID;
  c4_test_server_setup(&config);
}

void tearDown(void) {
  c4_test_server_teardown();
}

/**
 * Sends a GET request to the test server and checks the HTTP status and the error text.
 *
 * @param path request path
 * @param expected_status expected HTTP status code
 * @param expected_text text the response body must contain
 */
static void assert_get(const char* path, int expected_status, const char* expected_text) {
  int   status = 0;
  char* resp   = send_http_request("GET", path, NULL, &status);
  TEST_ASSERT_NOT_NULL(resp);
  TEST_ASSERT_EQUAL_MESSAGE(expected_status, status, resp);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(resp, expected_text), resp);
  free(resp);
}

// Without the route the server answers every `GET /proof/...` with 405 "Method not
// allowed", which is what a hybrid client of an EEZ chain would get. The answers below
// come from the handler itself, so they prove that it is registered for an EEZ chain.
void test_block_proof_route_is_registered(void) {
  assert_get("/proof/eth_getFoo/latest/3/std/0x", 400, "Unsupported method for proof GET");
  assert_get("/proof/eth_getBlockHeader", 400, "Invalid proof GET path");
}

// The path is validated before a prover is started, exactly as for an Ethereum chain.
void test_block_proof_route_validates_the_path(void) {
  assert_get("/proof/eth_getBlockHeader/lat!est/3/std/0x", 400, "Invalid block identifier");
  assert_get("/proof/eth_getBlockByNumber/latest/3/maybe/0x", 400, "Invalid zk segment");
  assert_get("/proof/eth_getBlockHeader/latest/3/std/0x123", 400, "Invalid c4 client_state");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_block_proof_route_is_registered);
  RUN_TEST(test_block_proof_route_validates_the_path);
  return UNITY_END();
}

#else
int main(void) {
  fprintf(stderr, "test_eez_server: Skipped (HTTP_SERVER or CHAIN_EEZ not enabled)\n");
  return 0;
}
#endif
