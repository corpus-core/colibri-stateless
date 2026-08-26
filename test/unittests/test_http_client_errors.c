/*
 * Tests for http_client_errors.c classification
 */

#include "unity.h"

#ifdef HTTP_SERVER

#include "../../src/chains/eth/server/eth_clients.h"
#include "../../src/server/server.h"
#include <string.h>

static bytes_t sbytes(const char* s) {
  return bytes((uint8_t*) s, (int) strlen(s));
}

static server_list_t two_beacon_servers(void) {
  static char*           urls[2];
  static server_health_t health[2];
  server_list_t          servers = {0};
  urls[0]                        = (char*) "http://a";
  urls[1]                        = (char*) "http://b";
  memset(health, 0, sizeof(health));
  servers.urls         = urls;
  servers.count        = 2;
  servers.health_stats = health;
  return servers;
}

void setUp(void) {}
void tearDown(void) {}

void test_rpc_200_success_no_error(void) {
  data_request_t req   = {0};
  req.type             = C4_DATA_TYPE_ETH_RPC;
  c4_response_type_t r = c4_classify_response(200, "/rpc", sbytes("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0x1\"}"), &req, NULL);
  TEST_ASSERT_EQUAL(C4_RESPONSE_SUCCESS, r);
}

void test_rpc_200_invalid_params_user(void) {
  data_request_t req      = {0};
  req.type                = C4_DATA_TYPE_ETH_RPC;
  const char*        body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":-32602,\"message\":\"invalid argument\"}}";
  c4_response_type_t r    = c4_classify_response(200, "/rpc", sbytes(body), &req, NULL);
  TEST_ASSERT_EQUAL(C4_RESPONSE_ERROR_USER, r);
}

void test_rpc_200_invalid_params_retry(void) {
  data_request_t req      = {0};
  req.type                = C4_DATA_TYPE_ETH_RPC;
  const char*        body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":-32602,\"message\":\"unsupported param form\"}}";
  c4_response_type_t r    = c4_classify_response(200, "/rpc", sbytes(body), &req, NULL);
  TEST_ASSERT_EQUAL(C4_RESPONSE_ERROR_RETRY, r);
  TEST_ASSERT_NOT_NULL(req.error);
  free(req.error);
  req.error = NULL;
}

void test_rpc_400_method_not_supported(void) {
  data_request_t req      = {0};
  req.type                = C4_DATA_TYPE_ETH_RPC;
  const char*        body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":-32004,\"message\":\"method not supported\"}}";
  c4_response_type_t r    = c4_classify_response(400, "/rpc", sbytes(body), &req, NULL);
  TEST_ASSERT_EQUAL(C4_RESPONSE_ERROR_METHOD_NOT_SUPPORTED, r);
  if (req.error) {
    free(req.error);
    req.error = NULL;
  }
}

void test_beacon_sync_lag_retry(void) {
  data_request_t req      = {0};
  req.type                = C4_DATA_TYPE_BEACON_API;
  const char*        url  = "/eth/v1/beacon/headers/0xabc";
  const char*        body = "Header not found";
  c4_response_type_t r    = c4_classify_response(404, url, sbytes(body), &req, NULL);
  TEST_ASSERT_EQUAL(C4_RESPONSE_ERROR_RETRY, r);
}

void test_http_401_retry(void) {
  data_request_t req   = {0};
  req.type             = C4_DATA_TYPE_ETH_RPC;
  c4_response_type_t r = c4_classify_response(401, "/rpc", sbytes(""), &req, NULL);
  TEST_ASSERT_EQUAL(C4_RESPONSE_ERROR_RETRY, r);
}

void test_http_404_user_rpc(void) {
  data_request_t req   = {0};
  req.type             = C4_DATA_TYPE_ETH_RPC;
  c4_response_type_t r = c4_classify_response(404, "/rpc", sbytes("not found"), &req, NULL);
  TEST_ASSERT_EQUAL(C4_RESPONSE_ERROR_USER, r);
}

void test_http_500_retry(void) {
  data_request_t     req = {0};
  c4_response_type_t r   = c4_classify_response(500, "/any", sbytes(""), &req, NULL);
  TEST_ASSERT_EQUAL(C4_RESPONSE_ERROR_RETRY, r);
}

void test_beacon_parent_root_empty_last_node_is_success(void) {
  data_request_t req     = {0};
  req.type               = C4_DATA_TYPE_BEACON_API;
  const char*        url = "/eth/v1/beacon/headers?parent_root=0xab";
  c4_response_type_t r   = c4_classify_response(200, url, sbytes("{\"data\":[]}"), &req, NULL);
  TEST_ASSERT_EQUAL(C4_RESPONSE_SUCCESS, r);
}

void test_beacon_parent_root_empty_retries_when_peer_left(void) {
  data_request_t req           = {0};
  req.type                     = C4_DATA_TYPE_BEACON_API;
  req.response_node_index      = 0;
  req.node_exclude_mask        = 0;
  server_list_t      servers   = two_beacon_servers();
  const char*        url       = "/eth/v1/beacon/headers?parent_root=0xab";
  c4_response_type_t r         = c4_classify_response(200, url, sbytes("{\"execution_optimistic\":false,\"data\":[]}"), &req, &servers);
  TEST_ASSERT_EQUAL(C4_RESPONSE_ERROR_RETRY, r);
}

void test_beacon_parent_root_empty_pretty_printed_retries(void) {
  data_request_t req         = {0};
  req.type                   = C4_DATA_TYPE_BEACON_API;
  req.response_node_index    = 0;
  req.node_exclude_mask      = 0;
  server_list_t      servers = two_beacon_servers();
  const char*        url     = "/eth/v1/beacon/headers?parent_root=0xab";
  const char*        body    = "{\n  \"data\": []\n}";
  c4_response_type_t r       = c4_classify_response(200, url, sbytes(body), &req, &servers);
  TEST_ASSERT_EQUAL(C4_RESPONSE_ERROR_RETRY, r);
}

void test_beacon_parent_root_empty_exhausted_peers_is_success(void) {
  data_request_t req           = {0};
  req.type                     = C4_DATA_TYPE_BEACON_API;
  req.response_node_index      = 0;
  req.node_exclude_mask        = 1u << 1;
  server_list_t      servers   = two_beacon_servers();
  const char*        url       = "/eth/v1/beacon/headers?parent_root=0xab";
  c4_response_type_t r         = c4_classify_response(200, url, sbytes("{\"data\":[]}"), &req, &servers);
  TEST_ASSERT_EQUAL(C4_RESPONSE_SUCCESS, r);
}

void test_beacon_parent_root_data_array_not_fooled_by_substring(void) {
  data_request_t req      = {0};
  req.type                = C4_DATA_TYPE_BEACON_API;
  server_list_t  servers  = two_beacon_servers();
  const char*    url      = "/eth/v1/beacon/headers?parent_root=0xab";
  const char*    body     = "{\"note\":\"\\\"data\\\":[]\",\"data\":[{\"root\":\"0xcd\",\"canonical\":true}]}";
  c4_response_type_t r    = c4_classify_response(200, url, sbytes(body), &req, &servers);
  TEST_ASSERT_EQUAL(C4_RESPONSE_SUCCESS, r);
}

void test_beacon_parent_root_nimbus_500_method_not_supported(void) {
  data_request_t req      = {0};
  req.type                = C4_DATA_TYPE_BEACON_API;
  const char*        url  = "/eth/v1/beacon/headers?parent_root=0xab";
  const char*        body = "{\"code\":500,\"message\":\"NoImplementationError: query by parent_root\"}";
  c4_response_type_t r    = c4_classify_response(500, url, sbytes(body), &req, NULL);
  TEST_ASSERT_EQUAL(C4_RESPONSE_ERROR_METHOD_NOT_SUPPORTED, r);
}

void test_beacon_parent_root_not_implemented_method_not_supported(void) {
  data_request_t req      = {0};
  req.type                = C4_DATA_TYPE_BEACON_API;
  const char*        url  = "/eth/v1/beacon/headers?parent_root=0xab";
  const char*        body = "{\"code\":500,\"message\":\"query by parent_root is not implemented\"}";
  c4_response_type_t r    = c4_classify_response(500, url, sbytes(body), &req, NULL);
  TEST_ASSERT_EQUAL(C4_RESPONSE_ERROR_METHOD_NOT_SUPPORTED, r);
}

void test_beacon_parent_root_500_without_nimbus_marker_retries(void) {
  data_request_t req      = {0};
  req.type                = C4_DATA_TYPE_BEACON_API;
  const char*        url  = "/eth/v1/beacon/headers?parent_root=0xab";
  const char*        body = "{\"code\":500,\"message\":\"internal server error\"}";
  c4_response_type_t r    = c4_classify_response(500, url, sbytes(body), &req, NULL);
  TEST_ASSERT_EQUAL(C4_RESPONSE_ERROR_RETRY, r);
}

void test_beacon_parent_root_500_not_implemented_in_hint_retries(void) {
  data_request_t req      = {0};
  req.type                = C4_DATA_TYPE_BEACON_API;
  const char*        url  = "/eth/v1/beacon/headers?parent_root=0xab";
  const char*        body = "{\"code\":500,\"message\":\"internal server error\",\"hint\":\"not implemented\"}";
  c4_response_type_t r    = c4_classify_response(500, url, sbytes(body), &req, NULL);
  TEST_ASSERT_EQUAL(C4_RESPONSE_ERROR_RETRY, r);
}

void test_request_fix_url_rewrites_lodestar_historical_summaries_for_nimbus(void) {
  char* in  = (char*) "eth/v1/lodestar/states/head/historical_summaries";
  char* out = c4_request_fix_url(in, NULL, BEACON_CLIENT_NIMBUS);
  TEST_ASSERT_NOT_NULL(strstr(out, "nimbus/v1/debug/beacon/states/head/historical_summaries"));
}

void test_request_fix_url_rewrites_lodestar_state_root_for_nimbus(void) {
  char* in  = (char*) "eth/v1/lodestar/states/0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/historical_summaries";
  char* out = c4_request_fix_url(in, NULL, BEACON_CLIENT_NIMBUS);
  TEST_ASSERT_NOT_NULL(strstr(out, "nimbus/v1/debug/beacon/states/0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/historical_summaries"));
}

void test_request_fix_url_keeps_lodestar_url_for_lodestar(void) {
  char* in  = (char*) "eth/v1/lodestar/states/head/historical_summaries";
  char* out = c4_request_fix_url(in, NULL, BEACON_CLIENT_LODESTAR);
  TEST_ASSERT_EQUAL_STRING(in, out);
}

void test_beacon_parent_root_match_is_success(void) {
  data_request_t req      = {0};
  req.type                = C4_DATA_TYPE_BEACON_API;
  const char*        url  = "/eth/v1/beacon/headers?parent_root=0xab";
  const char*        body = "{\"data\":[{\"root\":\"0xcd\",\"canonical\":true}]}";
  c4_response_type_t r    = c4_classify_response(200, url, sbytes(body), &req, NULL);
  TEST_ASSERT_EQUAL(C4_RESPONSE_SUCCESS, r);
}

void test_beacon_slot_empty_data_is_success(void) {
  data_request_t req     = {0};
  req.type               = C4_DATA_TYPE_BEACON_API;
  const char*        url = "/eth/v1/beacon/headers?slot=101";
  c4_response_type_t r   = c4_classify_response(200, url, sbytes("{\"data\":[]}"), &req, NULL);
  TEST_ASSERT_EQUAL(C4_RESPONSE_SUCCESS, r);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_rpc_200_success_no_error);
  RUN_TEST(test_rpc_200_invalid_params_user);
  RUN_TEST(test_rpc_200_invalid_params_retry);
  RUN_TEST(test_rpc_400_method_not_supported);
  RUN_TEST(test_beacon_sync_lag_retry);
  RUN_TEST(test_http_401_retry);
  RUN_TEST(test_http_404_user_rpc);
  RUN_TEST(test_http_500_retry);
  RUN_TEST(test_beacon_parent_root_empty_last_node_is_success);
  RUN_TEST(test_beacon_parent_root_empty_retries_when_peer_left);
  RUN_TEST(test_beacon_parent_root_empty_pretty_printed_retries);
  RUN_TEST(test_beacon_parent_root_empty_exhausted_peers_is_success);
  RUN_TEST(test_beacon_parent_root_data_array_not_fooled_by_substring);
  RUN_TEST(test_beacon_parent_root_nimbus_500_method_not_supported);
  RUN_TEST(test_beacon_parent_root_not_implemented_method_not_supported);
  RUN_TEST(test_beacon_parent_root_500_without_nimbus_marker_retries);
  RUN_TEST(test_beacon_parent_root_500_not_implemented_in_hint_retries);
  RUN_TEST(test_beacon_parent_root_match_is_success);
  RUN_TEST(test_beacon_slot_empty_data_is_success);
  RUN_TEST(test_request_fix_url_rewrites_lodestar_historical_summaries_for_nimbus);
  RUN_TEST(test_request_fix_url_rewrites_lodestar_state_root_for_nimbus);
  RUN_TEST(test_request_fix_url_keeps_lodestar_url_for_lodestar);
  return UNITY_END();
}

#else
int main(void) {
  fprintf(stderr, "test_http_client_errors: Skipped (HTTP_SERVER not enabled)\n");
  return 0;
}
#endif
