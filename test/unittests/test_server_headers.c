/*
 * Server tests for /eth/v1/beacon/headers/* (proxied via file mocks)
 */

#include "unity.h"

#ifdef HTTP_SERVER

#include "test_server_helper.h"

void setUp(void) {
  http_server_t config = (http_server_t) {0};
  config.port          = TEST_PORT;
  config.host          = TEST_HOST;
  config.chain_id      = 1;
  // Use localhost:5052 because mocks were recorded with that base URL
  config.beacon_nodes = (char*) "http://localhost:5052/";
  c4_test_server_setup(&config);
}

void tearDown(void) {
  c4_test_server_teardown();
}

static void seed_headers(void) {
  c4_test_server_seed_for_test("headers");
}

void test_headers_head(void) {
  seed_headers();

  int   status_code = 0;
  char* response    = send_http_request("GET", "/eth/v1/beacon/headers/head", NULL, &status_code);
  TEST_ASSERT_NOT_NULL(response);
  TEST_ASSERT_EQUAL(200, status_code);

  char* body = extract_json_body(response);
  TEST_ASSERT_NOT_NULL(body);
  TEST_ASSERT_TRUE(strlen(body) > 0);
  // Heuristically expect JSON payload
  TEST_ASSERT_NOT_NULL(strstr(body, "data"));

  free(body);
  free(response);
}

void test_headers_by_hash(void) {
  seed_headers();

  const char* path        = "/eth/v1/beacon/headers/0x75502f5e17b68b4d1870bebbe6468d50e8f87af1aacaf21a537678b2eca2b2d5";
  int         status_code = 0;
  char*       response    = send_http_request("GET", path, NULL, &status_code);
  TEST_ASSERT_NOT_NULL(response);
  TEST_ASSERT_EQUAL(200, status_code);

  char* body = extract_json_body(response);
  TEST_ASSERT_NOT_NULL(body);
  TEST_ASSERT_TRUE(strlen(body) > 0);
  TEST_ASSERT_NOT_NULL(strstr(body, "data"));

  free(body);
  free(response);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_headers_head);
  RUN_TEST(test_headers_by_hash);
  return UNITY_END();
}

#else
int main(void) {
  fprintf(stderr, "test_server_headers: Skipped (HTTP_SERVER not enabled)\n");
  return 0;
}
#endif
