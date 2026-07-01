/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: MIT
 *
 * Tests for the cache-friendly hybrid block proof GET endpoint:
 *  - client-side URL builder (eth_build_delegated_block_get_url)
 *  - server-side GET path parsing / validation
 */

#include "unity.h"
#include <stdio.h>

#ifdef HTTP_SERVER

#include "../../bindings/colibri_common.h"
#include "../../src/prover/prover.h"
#include "../../src/util/json.h"
#include "test_server_helper.h"

// Declared in src/chains/eth/prover/beacon.h (avoid the header's relative includes here).
extern char* eth_build_delegated_block_get_url(const char* method, json_t block, uint32_t version,
                                               prover_flags_t flags, bytes_t client_state, bytes_t witness_key);

// Declared in src/chains/eth/server/handler.h.
extern void c4_eth_block_cache_control(char* out, size_t cap, const char* block, chain_id_t chain_id);

void setUp(void) {
  http_server_t config = {0};
  config.port          = TEST_PORT;
  config.host          = TEST_HOST;
  config.chain_id      = 1;
  c4_test_server_setup(&config);
}

void tearDown(void) {
  c4_test_server_teardown();
}

// -- URL builder tests --

void test_url_builder_header_tag(void) {
  json_t   block = json_parse("\"latest\"");
  uint8_t  cs[]  = {0x01, 0x02};
  char*    url   = eth_build_delegated_block_get_url("eth_getBlockHeader", block, 3, 0, bytes(cs, 2), NULL_BYTES);
  TEST_ASSERT_NOT_NULL(url);
  TEST_ASSERT_EQUAL_STRING("proof/eth_getBlockHeader/latest/3/std/0x0102", url);
  safe_free(url);
}

void test_url_builder_block_by_number_omits_include_tx(void) {
  // The includeTx boolean must NOT appear in the URL; only method/block are relevant.
  json_t block = json_parse("\"0x64\"");
  char*  url   = eth_build_delegated_block_get_url("eth_getBlockByNumber", block, 5, C4_PROVER_FLAG_ZK_PROOF, NULL_BYTES, NULL_BYTES);
  TEST_ASSERT_NOT_NULL(url);
  TEST_ASSERT_EQUAL_STRING("proof/eth_getBlockByNumber/0x64/5/zk/0x", url);
  safe_free(url);
}

void test_url_builder_with_signers_query(void) {
  json_t   block   = json_parse("\"finalized\"");
  uint8_t  cs[]    = {0x0a};
  uint8_t  keys[]  = {0xaa, 0xbb, 0xcc};
  char*    url     = eth_build_delegated_block_get_url("eth_getBlockHeader", block, 7, 0, bytes(cs, 1), bytes(keys, 3));
  TEST_ASSERT_NOT_NULL(url);
  TEST_ASSERT_EQUAL_STRING("proof/eth_getBlockHeader/finalized/7/std/0x0a?signers=0xaabbcc", url);
  safe_free(url);
}

void test_url_builder_version_zero(void) {
  // version == 0 (client did not pin a version) must still yield a well-formed segment.
  json_t block = json_parse("\"latest\"");
  char*  url   = eth_build_delegated_block_get_url("eth_getBlockHeader", block, 0, 0, NULL_BYTES, NULL_BYTES);
  TEST_ASSERT_NOT_NULL(url);
  TEST_ASSERT_EQUAL_STRING("proof/eth_getBlockHeader/latest/0/std/0x", url);
  safe_free(url);
}

void test_url_builder_zk_with_client_state_and_signers(void) {
  // All optional parts combined: concrete block number, zk flag, non-empty client_state and signers.
  json_t   block = json_parse("\"0x1b4\"");
  uint8_t  cs[]  = {0xde, 0xad};
  uint8_t  keys[] = {0x01, 0x02, 0x03, 0x04};
  char*    url   = eth_build_delegated_block_get_url("eth_getBlockByNumber", block, 12, C4_PROVER_FLAG_ZK_PROOF,
                                                     bytes(cs, 2), bytes(keys, 4));
  TEST_ASSERT_NOT_NULL(url);
  TEST_ASSERT_EQUAL_STRING("proof/eth_getBlockByNumber/0x1b4/12/zk/0xdead?signers=0x01020304", url);
  safe_free(url);
}

void test_url_builder_empty_client_state_pointer(void) {
  // A non-NULL pointer with len 0 must be treated exactly like NULL_BYTES ("0x").
  json_t  block = json_parse("\"safe\"");
  uint8_t dummy = 0;
  char*   url   = eth_build_delegated_block_get_url("eth_getBlockHeader", block, 3, 0, bytes(&dummy, 0), NULL_BYTES);
  TEST_ASSERT_NOT_NULL(url);
  TEST_ASSERT_EQUAL_STRING("proof/eth_getBlockHeader/safe/3/std/0x", url);
  safe_free(url);
}

// -- Data request JSON serialization: the `ttl` field (cache_max_age) --

void test_data_request_ttl_serialized_when_set(void) {
  buffer_t       buf = {0};
  data_request_t req = {0};
  req.chain_id       = 1;
  req.method         = C4_DATA_METHOD_GET;
  req.encoding       = C4_DATA_ENCODING_SSZ;
  req.type           = C4_DATA_TYPE_PROVER;
  req.url            = "proof/eth_getBlockHeader/latest/3/std/0x";
  req.ttl            = 6;
  c4i_add_data_request(&buf, &req, false);
  TEST_ASSERT_NOT_NULL(buf.data.data);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr((char*) buf.data.data, "\"ttl\": 6,"),
                               "ttl must be emitted in the request JSON");
  buffer_free(&buf);
}

void test_data_request_no_ttl_when_zero(void) {
  buffer_t       buf = {0};
  data_request_t req = {0};
  req.chain_id       = 1;
  req.method         = C4_DATA_METHOD_GET;
  req.encoding       = C4_DATA_ENCODING_SSZ;
  req.type           = C4_DATA_TYPE_PROVER;
  req.url            = "proof/eth_getBlockHeader/latest/3/std/0x";
  req.ttl            = 0;
  c4i_add_data_request(&buf, &req, false);
  TEST_ASSERT_NOT_NULL(buf.data.data);
  TEST_ASSERT_NULL_MESSAGE(strstr((char*) buf.data.data, "ttl"),
                           "no \"ttl\" field must be emitted when ttl is 0");
  buffer_free(&buf);
}

// -- Cache-Control mapping (mainnet: 32 slots/epoch, 12s block time) --

void test_cache_control_latest(void) {
  char cc[96] = {0};
  c4_eth_block_cache_control(cc, sizeof cc, "latest", (chain_id_t) 1);
  TEST_ASSERT_EQUAL_STRING("public, max-age=6, stale-while-revalidate=6", cc);
}

void test_cache_control_safe(void) {
  char cc[96] = {0};
  c4_eth_block_cache_control(cc, sizeof cc, "safe", (chain_id_t) 1);
  TEST_ASSERT_EQUAL_STRING("public, max-age=192", cc); // (32/2) * 12
}

void test_cache_control_finalized(void) {
  char cc[96] = {0};
  c4_eth_block_cache_control(cc, sizeof cc, "finalized", (chain_id_t) 1);
  TEST_ASSERT_EQUAL_STRING("public, max-age=384", cc); // 32 * 12
}

void test_cache_control_concrete_block_is_immutable(void) {
  char cc[96] = {0};
  c4_eth_block_cache_control(cc, sizeof cc, "0x1b4", (chain_id_t) 1);
  TEST_ASSERT_EQUAL_STRING("public, max-age=31536000, immutable", cc);
}

// -- Server GET handler validation tests (malformed requests must return 400) --

static void assert_get_status(const char* path, int expected) {
  int   status = 0;
  char* resp   = send_http_request("GET", path, NULL, &status);
  TEST_ASSERT_NOT_NULL(resp);
  TEST_ASSERT_EQUAL(expected, status);
  free(resp);
}

void test_get_handler_missing_segments(void) {
  assert_get_status("/proof/eth_getBlockHeader", 400);
}

void test_get_handler_unsupported_method(void) {
  assert_get_status("/proof/eth_getFoo/latest/3/std/0x", 400);
}

void test_get_handler_invalid_version(void) {
  assert_get_status("/proof/eth_getBlockHeader/latest/notanumber/std/0x", 400);
}

void test_get_handler_invalid_zk_segment(void) {
  assert_get_status("/proof/eth_getBlockHeader/latest/3/maybe/0x", 400);
}

void test_get_handler_invalid_block_identifier(void) {
  // A '!' in the block identifier is rejected to prevent JSON injection into params.
  assert_get_status("/proof/eth_getBlockHeader/lat!est/3/std/0x", 400);
}

void test_get_handler_invalid_c4(void) {
  // odd hex length is invalid
  assert_get_status("/proof/eth_getBlockHeader/latest/3/std/0x123", 400);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_url_builder_header_tag);
  RUN_TEST(test_url_builder_block_by_number_omits_include_tx);
  RUN_TEST(test_url_builder_with_signers_query);
  RUN_TEST(test_url_builder_version_zero);
  RUN_TEST(test_url_builder_zk_with_client_state_and_signers);
  RUN_TEST(test_url_builder_empty_client_state_pointer);
  RUN_TEST(test_cache_control_latest);
  RUN_TEST(test_cache_control_safe);
  RUN_TEST(test_cache_control_finalized);
  RUN_TEST(test_cache_control_concrete_block_is_immutable);
  RUN_TEST(test_data_request_ttl_serialized_when_set);
  RUN_TEST(test_data_request_no_ttl_when_zero);
  RUN_TEST(test_get_handler_missing_segments);
  RUN_TEST(test_get_handler_unsupported_method);
  RUN_TEST(test_get_handler_invalid_version);
  RUN_TEST(test_get_handler_invalid_zk_segment);
  RUN_TEST(test_get_handler_invalid_block_identifier);
  RUN_TEST(test_get_handler_invalid_c4);
  return UNITY_END();
}

#else
int main(void) {
  fprintf(stderr, "test_hybrid_block_get: Skipped (HTTP_SERVER not enabled)\n");
  return 0;
}
#endif
