/*
 * Copyright 2025,2026 corpus.core
 * SPDX-License-Identifier: MIT
 *
 * Tests for the PAP + Hybrid cache-friendly prover requests:
 *  - c4_get_prover_cache_request dispatcher (eth impl)
 *  - server accepts eth_getBlockReceipts as GET proof
 *  - /tx_cache endpoint attaches Cache-Control headers
 */

#include "unity.h"
#include <stdio.h>

#ifdef HTTP_SERVER

#include "../../src/util/json.h"
#include "test_server_helper.h"

// Declared in src/verifier/verify.h (generated dispatcher lives in verifiers.h).
extern bool c4_get_prover_cache_request(chain_id_t chain_id, const char* method, json_t params,
                                        uint32_t version, bool zk_proof, bool light_client,
                                        bytes_t client_state, bytes_t witness_key,
                                        char** url_out, uint32_t* ttl_out);

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

// -- Dispatcher tests --

void test_dispatch_block_header_latest(void) {
  json_t   params = json_parse("[\"latest\"]");
  char*    url    = NULL;
  uint32_t ttl    = 0;
  bool     ok     = c4_get_prover_cache_request(1, "eth_getBlockHeader", params, 3, false, false,
                                                NULL_BYTES, NULL_BYTES, &url, &ttl);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_NOT_NULL(url);
  TEST_ASSERT_EQUAL_STRING("proof/eth_getBlockHeader/latest/3/std/0x", url);
  // Mainnet: block_time=12s, so latest -> 12/2 = 6s
  TEST_ASSERT_EQUAL_UINT32(6, ttl);
  safe_free(url);
}

void test_dispatch_block_by_number_concrete_is_immutable(void) {
  json_t   params = json_parse("[\"0x1b4\",true]");
  char*    url    = NULL;
  uint32_t ttl    = 0;
  bool     ok     = c4_get_prover_cache_request(1, "eth_getBlockByNumber", params, 5, true, false,
                                                NULL_BYTES, NULL_BYTES, &url, &ttl);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_NOT_NULL(url);
  // includeTx (`true`) must be dropped from the URL.
  TEST_ASSERT_EQUAL_STRING("proof/eth_getBlockByNumber/0x1b4/5/zk/0x", url);
  // Concrete block number -> immutable (ttl == 0 signals "no request bound", CDN uses `immutable`).
  TEST_ASSERT_EQUAL_UINT32(0, ttl);
  safe_free(url);
}

void test_dispatch_block_receipts_supported(void) {
  json_t   params = json_parse("[\"0x64\"]");
  char*    url    = NULL;
  uint32_t ttl    = 0;
  bool     ok     = c4_get_prover_cache_request(1, "eth_getBlockReceipts", params, 7, false, false,
                                                NULL_BYTES, NULL_BYTES, &url, &ttl);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_NOT_NULL(url);
  TEST_ASSERT_EQUAL_STRING("proof/eth_getBlockReceipts/0x64/7/std/0x", url);
  TEST_ASSERT_EQUAL_UINT32(0, ttl);
  safe_free(url);
}

void test_dispatch_rejects_tx_hash_method(void) {
  // eth_getTransactionByHash proofs depend on the tx-hash-to-block mapping (PAP-only), so the
  // tx hash itself is the primary cache key. Mixing that into a shared CDN cache would leak the
  // user's transaction. The dispatcher must therefore refuse this method.
  json_t   params = json_parse("[\"0x0000000000000000000000000000000000000000000000000000000000000001\"]");
  char*    url    = NULL;
  uint32_t ttl    = 42;
  bool     ok     = c4_get_prover_cache_request(1, "eth_getTransactionByHash", params, 3, false, false,
                                                NULL_BYTES, NULL_BYTES, &url, &ttl);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_NULL(url);
}

void test_dispatch_rejects_empty_params(void) {
  json_t   params = json_parse("[]");
  char*    url    = NULL;
  uint32_t ttl    = 0;
  bool     ok     = c4_get_prover_cache_request(1, "eth_getBlockHeader", params, 3, false, false,
                                                NULL_BYTES, NULL_BYTES, &url, &ttl);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_NULL(url);
}

void test_dispatch_rejects_non_string_block(void) {
  // A JSON number in params[0] must be rejected -- block ids are always strings.
  json_t   params = json_parse("[42]");
  char*    url    = NULL;
  uint32_t ttl    = 0;
  bool     ok     = c4_get_prover_cache_request(1, "eth_getBlockHeader", params, 3, false, false,
                                                NULL_BYTES, NULL_BYTES, &url, &ttl);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_NULL(url);
}

void test_dispatch_rejects_unknown_tag(void) {
  // Only latest/safe/justified/finalized/0x... tokens are eligible; anything else must fall back
  // to POST so no shared cache/CDN can pin a bad answer for a rejected tag.
  json_t   params = json_parse("[\"pending\"]");
  char*    url    = NULL;
  uint32_t ttl    = 0;
  bool     ok     = c4_get_prover_cache_request(1, "eth_getBlockHeader", params, 3, false, false,
                                                NULL_BYTES, NULL_BYTES, &url, &ttl);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_NULL(url);
}

void test_dispatch_rejects_block_with_unsafe_characters(void) {
  // Slash / question mark / control chars must never end up in the raw path segment.
  json_t   params = json_parse("[\"latest/../etc\"]");
  char*    url    = NULL;
  uint32_t ttl    = 0;
  bool     ok     = c4_get_prover_cache_request(1, "eth_getBlockHeader", params, 3, false, false,
                                                NULL_BYTES, NULL_BYTES, &url, &ttl);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_NULL(url);
}

void test_dispatch_accepts_odd_length_hex_block_number(void) {
  // JSON-RPC block numbers are minimally encoded (e.g. `0x1b4` = 436), so odd hex lengths must
  // be accepted. Only invalid characters or too-long tokens are rejected.
  json_t   params = json_parse("[\"0x1b4\"]");
  char*    url    = NULL;
  uint32_t ttl    = 0;
  bool     ok     = c4_get_prover_cache_request(1, "eth_getBlockHeader", params, 3, false, false,
                                                NULL_BYTES, NULL_BYTES, &url, &ttl);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING("proof/eth_getBlockHeader/0x1b4/3/std/0x", url);
  TEST_ASSERT_EQUAL_UINT32(0, ttl);
  safe_free(url);
}

void test_dispatch_rejects_hex_with_invalid_nibble(void) {
  // Non-hex characters inside a `0x` token must be rejected (would break the URL path segment).
  json_t   params = json_parse("[\"0x1zz\"]");
  char*    url    = NULL;
  uint32_t ttl    = 0;
  bool     ok     = c4_get_prover_cache_request(1, "eth_getBlockHeader", params, 3, false, false,
                                                NULL_BYTES, NULL_BYTES, &url, &ttl);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_NULL(url);
}

void test_dispatch_finalized_tag_ttl_mainnet(void) {
  json_t   params = json_parse("[\"finalized\"]");
  char*    url    = NULL;
  uint32_t ttl    = 0;
  bool     ok     = c4_get_prover_cache_request(1, "eth_getBlockHeader", params, 3, false, false,
                                                NULL_BYTES, NULL_BYTES, &url, &ttl);
  TEST_ASSERT_TRUE(ok);
  // slots_per_epoch=32, block_time=12 -> 32*12 = 384
  TEST_ASSERT_EQUAL_UINT32(384, ttl);
  safe_free(url);
}

void test_dispatch_justified_tag_same_as_safe(void) {
  json_t   params = json_parse("[\"justified\"]");
  char*    url    = NULL;
  uint32_t ttl    = 0;
  bool     ok     = c4_get_prover_cache_request(1, "eth_getBlockHeader", params, 3, false, false,
                                                NULL_BYTES, NULL_BYTES, &url, &ttl);
  TEST_ASSERT_TRUE(ok);
  // justified shares the safe branch: (32/2) * 12 = 192
  TEST_ASSERT_EQUAL_UINT32(192, ttl);
  safe_free(url);
}

void test_dispatch_latest_light_client_uses_full_block_time(void) {
  json_t   params = json_parse("[\"latest\"]");
  char*    url    = NULL;
  uint32_t ttl    = 0;
  // light_client=true doubles the "latest" bound to the full block_time (12s on mainnet).
  bool     ok     = c4_get_prover_cache_request(1, "eth_getBlockHeader", params, 3, false, true,
                                                NULL_BYTES, NULL_BYTES, &url, &ttl);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_UINT32(12, ttl);
  safe_free(url);
}

void test_dispatch_latest_gnosis(void) {
  json_t   params = json_parse("[\"latest\"]");
  char*    url    = NULL;
  uint32_t ttl    = 0;
  bool     ok     = c4_get_prover_cache_request(C4_CHAIN_GNOSIS, "eth_getBlockHeader", params,
                                                3, false, false,
                                                NULL_BYTES, NULL_BYTES, &url, &ttl);
  TEST_ASSERT_TRUE(ok);
  // Gnosis block_time=5s -> latest bound = 5/2 = 2s
  TEST_ASSERT_EQUAL_UINT32(2, ttl);
  safe_free(url);
}

void test_dispatch_finalized_gnosis(void) {
  json_t   params = json_parse("[\"finalized\"]");
  char*    url    = NULL;
  uint32_t ttl    = 0;
  bool     ok     = c4_get_prover_cache_request(C4_CHAIN_GNOSIS, "eth_getBlockHeader", params,
                                                3, false, false,
                                                NULL_BYTES, NULL_BYTES, &url, &ttl);
  TEST_ASSERT_TRUE(ok);
  // Gnosis: slots_per_epoch=16, block_time=5 -> 16 * 5 = 80
  TEST_ASSERT_EQUAL_UINT32(80, ttl);
  safe_free(url);
}

void test_dispatch_safe_tag_ttl(void) {
  json_t   params = json_parse("[\"safe\"]");
  char*    url    = NULL;
  uint32_t ttl    = 0;
  bool     ok     = c4_get_prover_cache_request(1, "eth_getBlockHeader", params, 3, false, false,
                                                NULL_BYTES, NULL_BYTES, &url, &ttl);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING("proof/eth_getBlockHeader/safe/3/std/0x", url);
  // (32/2) * 12 = 192
  TEST_ASSERT_EQUAL_UINT32(192, ttl);
  safe_free(url);
}

// -- Server GET handler: eth_getBlockReceipts must be accepted --

static void assert_get_status(const char* path, int expected) {
  int   status = 0;
  char* resp   = send_http_request("GET", path, NULL, &status);
  TEST_ASSERT_NOT_NULL(resp);
  TEST_ASSERT_EQUAL(expected, status);
  free(resp);
}

void test_get_handler_accepts_receipts(void) {
  // Malformed c4 must still take precedence over the method whitelist: proves that receipts pass
  // the "Unsupported method" check (400 comes from the c4 validator).
  int   status = 0;
  char* resp   = send_http_request("GET", "/proof/eth_getBlockReceipts/0x64/3/std/0xZZ", NULL, &status);
  TEST_ASSERT_NOT_NULL(resp);
  TEST_ASSERT_EQUAL(400, status);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(resp, "Invalid c4"), "must fail on c4 hex, not on method whitelist");
  free(resp);
}

void test_get_handler_rejects_unrelated_method(void) {
  // Sanity: an unrelated method still returns "Unsupported method" (regression: whitelist wiring).
  int   status = 0;
  char* resp   = send_http_request("GET", "/proof/eth_getTransactionByHash/0x64/3/std/0x", NULL, &status);
  TEST_ASSERT_NOT_NULL(resp);
  TEST_ASSERT_EQUAL(400, status);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(resp, "Unsupported method"), "unrelated method must be rejected");
  free(resp);
}

// -- Server Cache-Control mapping regression (M2) --

extern void c4_eth_block_cache_control(char* out, size_t cap, const char* block, chain_id_t chain_id);

void test_cache_control_unknown_alnum_token_is_not_cacheable(void) {
  // Unknown alphanumeric block tokens (e.g. `pending`) must NOT be marked `immutable`, or a
  // shared CDN could pin a bad response for up to a year. Regression for M2.
  char cc[96] = {0};
  c4_eth_block_cache_control(cc, sizeof cc, "pending", (chain_id_t) 1);
  TEST_ASSERT_EQUAL_STRING("no-store", cc);
}

void test_cache_control_hex_block_still_immutable(void) {
  // The 0x-prefix path must still yield the immutable long-max-age response.
  char cc[96] = {0};
  c4_eth_block_cache_control(cc, sizeof cc, "0x1b4", (chain_id_t) 1);
  TEST_ASSERT_EQUAL_STRING("public, max-age=31536000, immutable", cc);
}

// -- tx_cache: Cache-Control on responses --

void test_tx_cache_get_has_cache_control(void) {
  int   status = 0;
  char* resp   = send_http_request("GET", "/tx_cache", NULL, &status);
  TEST_ASSERT_NOT_NULL(resp);
  TEST_ASSERT_EQUAL(200, status);
  // Response headers are included in `resp` (curl `CURLOPT_HEADER=1`).
  // For mainnet (block_time=12s): max-age=6.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(resp, "Cache-Control: public, max-age=6"),
                               "tx_cache must attach a bounded max-age header");
  free(resp);
}

void test_tx_cache_post_is_no_store(void) {
  int   status = 0;
  char* resp   = send_http_request("POST", "/tx_cache", "{}", &status);
  TEST_ASSERT_NOT_NULL(resp);
  TEST_ASSERT_EQUAL(405, status);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(resp, "Cache-Control: no-store"),
                               "tx_cache errors must not be cacheable");
  free(resp);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_dispatch_block_header_latest);
  RUN_TEST(test_dispatch_block_by_number_concrete_is_immutable);
  RUN_TEST(test_dispatch_block_receipts_supported);
  RUN_TEST(test_dispatch_rejects_tx_hash_method);
  RUN_TEST(test_dispatch_rejects_empty_params);
  RUN_TEST(test_dispatch_rejects_non_string_block);
  RUN_TEST(test_dispatch_rejects_unknown_tag);
  RUN_TEST(test_dispatch_rejects_block_with_unsafe_characters);
  RUN_TEST(test_dispatch_accepts_odd_length_hex_block_number);
  RUN_TEST(test_dispatch_rejects_hex_with_invalid_nibble);
  RUN_TEST(test_dispatch_safe_tag_ttl);
  RUN_TEST(test_dispatch_finalized_tag_ttl_mainnet);
  RUN_TEST(test_dispatch_justified_tag_same_as_safe);
  RUN_TEST(test_dispatch_latest_light_client_uses_full_block_time);
  RUN_TEST(test_dispatch_latest_gnosis);
  RUN_TEST(test_dispatch_finalized_gnosis);
  RUN_TEST(test_get_handler_accepts_receipts);
  RUN_TEST(test_get_handler_rejects_unrelated_method);
  RUN_TEST(test_cache_control_unknown_alnum_token_is_not_cacheable);
  RUN_TEST(test_cache_control_hex_block_still_immutable);
  RUN_TEST(test_tx_cache_get_has_cache_control);
  RUN_TEST(test_tx_cache_post_is_no_store);
  return UNITY_END();
}

#else
int main(void) {
  fprintf(stderr, "test_pap_cache_get: Skipped (HTTP_SERVER not enabled)\n");
  return 0;
}
#endif
