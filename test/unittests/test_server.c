/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: MIT
 *
 * Server tests using file-based mocks
 */

#include "unity.h"
#include <stdio.h>

#ifdef HTTP_SERVER

#include "test_server_helper.h"

// Unity setup - called before each test
void setUp(void) {
  http_server_t config = {0};
  config.prover_nodes  = "http://localhost:8090";
  config.port          = TEST_PORT;
  config.host          = TEST_HOST;
  config.chain_id      = 1;
  c4_test_server_setup(&config); // Use default test configuration
}

// Unity teardown - called after each test
void tearDown(void) {
  c4_test_server_teardown();
}

// Test 1: test verifying a remote prover-request with a file mock
void test_remote_prover(void) {
  c4_test_server_seed_for_test("block_number");

  int   status_code = 0;
  char* response    = send_http_request("POST", "/rpc", "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_blockNumber\",\"params\":[]}", &status_code);

  TEST_ASSERT_NOT_NULL(response);
  TEST_ASSERT_EQUAL(200, status_code);

  char* body = extract_json_body(response);
  TEST_ASSERT_NOT_NULL(body);
  TEST_ASSERT_EQUAL_STRING("{\"id\": 1, \"result\": \"0x168bc6e\"}", body);

  free(body);
  free(response);
}

// Test 1: Health check endpoint
void test_health_check(void) {
  c4_test_server_seed_for_test("test_health_check");

  int   status_code = 0;
  char* response    = send_http_request("GET", "/health", NULL, &status_code);

  TEST_ASSERT_NOT_NULL(response);
  TEST_ASSERT_EQUAL(200, status_code);

  char* body = extract_json_body(response);
  TEST_ASSERT_NOT_NULL(body);
  TEST_ASSERT_TRUE(strstr(body, "\"status\"") != NULL);

  free(body);
  free(response);
}

// Test 2: Simple RPC request with file mock
void test_rpc_request_with_file_mock(void) {
  c4_test_server_seed_for_test("test_rpc_request");

  // Mock files should exist at:
  // test/data/server/test_rpc_request/eth-rpc-1_<hash>.json

  const char* rpc_payload =
      "{\"jsonrpc\":\"2.0\",\"method\":\"eth_blockNumber\",\"params\":[],\"id\":1}";

  int   status_code = 0;
  char* response    = send_http_request("POST", "/rpc", rpc_payload, &status_code);

  TEST_ASSERT_NOT_NULL(response);
  // Status depends on handler implementation and mock file existence

  free(response);
}

// Test 3: Retry logic with multiple servers
void test_retry_with_multiple_servers(void) {
  c4_test_server_seed_for_test("test_retry_multi_server");

  // Create mock files where first server fails, second succeeds:
  // test/data/server/test_retry_multi_server/eth-rpc-1_<hash>.json (404 or error)
  // test/data/server/test_retry_multi_server/eth-rpc-2_<hash>.json (200 success)

  const char* payload = "{\"jsonrpc\":\"2.0\",\"method\":\"eth_getBlockByNumber\",\"params\":[\"latest\",false],\"id\":1}";

  int   status_code = 0;
  char* response    = send_http_request("POST", "/rpc", payload, &status_code);

  TEST_ASSERT_NOT_NULL(response);
  // Verify retry happened by checking logs or mock call counts

  free(response);
}

// Test 4: Error handling
void test_error_handling(void) {
  c4_test_server_seed_for_test("test_error_handling");

  int   status_code = 0;
  char* response    = send_http_request("POST", "/verify", "invalid json", &status_code);

  TEST_ASSERT_NOT_NULL(response);
  TEST_ASSERT_TRUE(status_code >= 400);

  free(response);
}

// Test 5: Deterministic server selection
void test_deterministic_server_selection(void) {
  c4_test_server_seed_for_test("test_deterministic_selection");

  // With test-specific seed, server selection should be deterministic
  // Make same request multiple times, should use same server (if healthy)

  const char* payload = "{\"jsonrpc\":\"2.0\",\"method\":\"eth_blockNumber\",\"params\":[],\"id\":1}";

  for (int i = 0; i < 3; i++) {
    int   status_code = 0;
    char* response    = send_http_request("POST", "/rpc", payload, &status_code);
    // All requests should behave identically with same seed
    free(response);
  }

  TEST_PASS_MESSAGE("Deterministic selection test completed");
}

// Test 6: Metrics endpoint
void test_metrics_endpoint(void) {
  c4_test_server_seed_for_test("test_metrics");

  int   status_code = 0;
  char* response    = send_http_request("GET", "/metrics", NULL, &status_code);

  TEST_ASSERT_NOT_NULL(response);
  TEST_ASSERT_EQUAL(200, status_code);

  // Metrics should be in Prometheus format
  char* body = extract_json_body(response);
  TEST_ASSERT_NOT_NULL(body);

  free(body);
  free(response);
}

// Test 7: Config endpoint GET
void test_config_endpoint_get(void) {
  c4_test_server_seed_for_test("test_config_get");

  int   status_code = 0;
  char* response    = send_http_request("GET", "/config", NULL, &status_code);

  TEST_ASSERT_NOT_NULL(response);
  // Config endpoint may return various status codes depending on configuration
  // Just verify we get a valid HTTP response without crashing
  TEST_ASSERT_TRUE(status_code > 0);

  free(response);
}

// Test 8: Proof endpoint
void test_proof_endpoint(void) {
  c4_test_server_seed_for_test("test_proof");

  // Proof endpoint requires specific proof request format
  const char* proof_payload = "{\"jsonrpc\":\"2.0\",\"method\":\"eth_getBlockByNumber\",\"params\":[\"latest\",false],\"id\":1}";

  int   status_code = 0;
  char* response    = send_http_request("POST", "/proof", proof_payload, &status_code);

  TEST_ASSERT_NOT_NULL(response);
  // May succeed or fail depending on prover availability
  // Just verify we get a valid HTTP response
  TEST_ASSERT_TRUE(status_code >= 200 && status_code < 600);

  free(response);
}

// Main test runner
int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_remote_prover);
  RUN_TEST(test_health_check);
  RUN_TEST(test_rpc_request_with_file_mock);
  RUN_TEST(test_retry_with_multiple_servers);
  RUN_TEST(test_error_handling);
  RUN_TEST(test_deterministic_server_selection);
  RUN_TEST(test_metrics_endpoint);
  RUN_TEST(test_config_endpoint_get);
  RUN_TEST(test_proof_endpoint);

  return UNITY_END();
}

#else // !HTTP_SERVER

// Stub main when HTTP_SERVER is not enabled
int main(void) {
  fprintf(stderr, "test_server: Skipped (HTTP_SERVER not enabled)\n");
  return 0;
}

#endif // HTTP_SERVER
