/*
 * Tests for http_client_errors.c classification
 */

#include "unity.h"

#ifdef HTTP_SERVER

#include "../../src/server/server.h"

static bytes_t sbytes(const char* s) {
  return bytes((uint8_t*) s, (int) strlen(s));
}

void setUp(void) {}
void tearDown(void) {}

void test_rpc_200_success_no_error(void) {
  data_request_t req   = {0};
  req.type             = C4_DATA_TYPE_ETH_RPC;
  c4_response_type_t r = c4_classify_response(200, "/rpc", sbytes("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0x1\"}"), &req);
  TEST_ASSERT_EQUAL(C4_RESPONSE_SUCCESS, r);
}

void test_rpc_200_invalid_params_user(void) {
  data_request_t req      = {0};
  req.type                = C4_DATA_TYPE_ETH_RPC;
  const char*        body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":-32602,\"message\":\"invalid argument\"}}";
  c4_response_type_t r    = c4_classify_response(200, "/rpc", sbytes(body), &req);
  TEST_ASSERT_EQUAL(C4_RESPONSE_ERROR_USER, r);
}

void test_rpc_200_invalid_params_retry(void) {
  data_request_t req      = {0};
  req.type                = C4_DATA_TYPE_ETH_RPC;
  const char*        body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":-32602,\"message\":\"unsupported param form\"}}";
  c4_response_type_t r    = c4_classify_response(200, "/rpc", sbytes(body), &req);
  TEST_ASSERT_EQUAL(C4_RESPONSE_ERROR_RETRY, r);
  TEST_ASSERT_NOT_NULL(req.error);
  free(req.error);
  req.error = NULL;
}

void test_rpc_400_method_not_supported(void) {
  data_request_t req      = {0};
  req.type                = C4_DATA_TYPE_ETH_RPC;
  const char*        body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":-32004,\"message\":\"method not supported\"}}";
  c4_response_type_t r    = c4_classify_response(400, "/rpc", sbytes(body), &req);
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
  c4_response_type_t r    = c4_classify_response(404, url, sbytes(body), &req);
  TEST_ASSERT_EQUAL(C4_RESPONSE_ERROR_RETRY, r);
}

void test_http_401_retry(void) {
  data_request_t req   = {0};
  req.type             = C4_DATA_TYPE_ETH_RPC;
  c4_response_type_t r = c4_classify_response(401, "/rpc", sbytes(""), &req);
  TEST_ASSERT_EQUAL(C4_RESPONSE_ERROR_RETRY, r);
}

void test_http_404_user_rpc(void) {
  data_request_t req   = {0};
  req.type             = C4_DATA_TYPE_ETH_RPC;
  c4_response_type_t r = c4_classify_response(404, "/rpc", sbytes("not found"), &req);
  TEST_ASSERT_EQUAL(C4_RESPONSE_ERROR_USER, r);
}

void test_http_500_retry(void) {
  data_request_t     req = {0};
  c4_response_type_t r   = c4_classify_response(500, "/any", sbytes(""), &req);
  TEST_ASSERT_EQUAL(C4_RESPONSE_ERROR_RETRY, r);
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
  return UNITY_END();
}

#else
int main(void) {
  fprintf(stderr, "test_http_client_errors: Skipped (HTTP_SERVER not enabled)\n");
  return 0;
}
#endif
