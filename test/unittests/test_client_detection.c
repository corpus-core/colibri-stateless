/*
 * Tests for client detection parsing (beacon and rpc)
 */

#include "../../src/chains/eth/server/eth_clients.h"
#include "../../src/server/server.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_detect_beacon_nimbus(void) {
  const char*          json = "{\"data\":{\"version\":\"Nimbus/v25.9.2-9839f1-stateofus\"}}";
  beacon_client_type_t t    = c4_parse_client_version_response(json, C4_DATA_TYPE_BEACON_API);
  TEST_ASSERT_EQUAL(BEACON_CLIENT_NIMBUS, t);
}

void test_detect_beacon_lodestar(void) {
  const char*          json = "{\"data\":{\"version\":\"Lodestar/v1.35.0/a8e3089\"}}";
  beacon_client_type_t t    = c4_parse_client_version_response(json, C4_DATA_TYPE_BEACON_API);
  TEST_ASSERT_EQUAL(BEACON_CLIENT_LODESTAR, t);
}

void test_detect_beacon_lighthouse(void) {
  const char*          json = "{\"data\":{\"version\":\"Lighthouse/v7.1.0-cfb1f73/x86_64-linux\"}}";
  beacon_client_type_t t    = c4_parse_client_version_response(json, C4_DATA_TYPE_BEACON_API);
  TEST_ASSERT_EQUAL(BEACON_CLIENT_LIGHTHOUSE, t);
}

void test_detect_beacon_unknown(void) {
  const char*          json = "{\"data\":{\"version\":\"\"}}";
  beacon_client_type_t t    = c4_parse_client_version_response(json, C4_DATA_TYPE_BEACON_API);
  TEST_ASSERT_EQUAL(BEACON_CLIENT_UNKNOWN, t);
}

void test_detect_rpc_geth(void) {
  const char*          json = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"Geth/v1.10.26-stable-...\"}";
  beacon_client_type_t t    = c4_parse_client_version_response(json, C4_DATA_TYPE_ETH_RPC);
  TEST_ASSERT_EQUAL(RPC_CLIENT_GETH, t);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_detect_beacon_nimbus);
  RUN_TEST(test_detect_beacon_lodestar);
  RUN_TEST(test_detect_beacon_lighthouse);
  RUN_TEST(test_detect_beacon_unknown);
  RUN_TEST(test_detect_rpc_geth);
  return UNITY_END();
}
