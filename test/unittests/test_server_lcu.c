/*
 * Server tests for /eth/v1/beacon/light_client/updates
 */

#include "unity.h"

#ifdef HTTP_SERVER

#include "test_server_helper.h"

void setUp(void) {
  http_server_t config = (http_server_t) {0};
  config.port          = TEST_PORT;
  config.host          = TEST_HOST;
  config.chain_id      = 1;
  // Use provided period_store with prepared data
  char pstore[512];
  snprintf(pstore, sizeof(pstore), "%s/server/period_store", TESTDATA_DIR);
  config.period_store = strdup(pstore);
  // Use localhost:5052 to match recorded URLs
  config.beacon_nodes = (char*) "http://localhost:5052/";
  c4_test_server_setup(&config);
}

void tearDown(void) {
  c4_test_server_teardown();
}

void test_lcu_updates_valid_range(void) {
  int   status_code = 0;
  char* response    = send_http_request("GET", "/eth/v1/beacon/light_client/updates?start_period=1571&count=2", NULL, &status_code);
  TEST_ASSERT_NOT_NULL(response);
  TEST_ASSERT_EQUAL(200, status_code);

  // Parse Content-Length from headers and assert it's roughly 25KB * count
  const char* cl = strstr(response, "Content-Length: ");
  TEST_ASSERT_NOT_NULL(cl);
  size_t content_length = (size_t) atoi(cl + 16);
  TEST_ASSERT_TRUE(content_length > 40000); // ~2 * 25kB lower-bounded (safety margin)

  free(response);
}

void test_lcu_updates_invalid_args(void) {
  int   status_code = 0;
  char* response    = send_http_request("GET", "/eth/v1/beacon/light_client/updates?start_period=0&count=0", NULL, &status_code);
  TEST_ASSERT_NOT_NULL(response);
  TEST_ASSERT_TRUE(status_code >= 400);
  free(response);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_lcu_updates_valid_range);
  RUN_TEST(test_lcu_updates_invalid_args);
  return UNITY_END();
}

#else
int main(void) {
  fprintf(stderr, "test_server_lcu: Skipped (HTTP_SERVER not enabled)\n");
  return 0;
}
#endif
