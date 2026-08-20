/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: MIT
 *
 * Tests for the legacy 2.x proof forwarding gate implemented in
 * `c4_try_forward_legacy_proof` and wired into the POST `/proof` and
 * GET `/proof/...` handlers.
 */

#include "unity.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef HTTP_SERVER

#include "test_server_helper.h"

// Each test manages its own server lifecycle because the v2_prover configuration is fixed at
// c4_test_server_setup() time (memcpy'd into the global http_server struct) and needs to differ
// per test. `tearDown` acts as a safety net when a `TEST_ASSERT_*` failure long-jumps past the
// explicit teardown call at the end of the test body: without it, the server thread would keep
// holding TEST_PORT and every subsequent test's `c4_server_start` would abort the whole binary.
static bool g_server_up = false;

static void setup_with_v2(const char* v2_url) {
  http_server_t config = {0};
  config.port          = TEST_PORT;
  config.host          = TEST_HOST;
  config.chain_id      = 1;
  config.v2_prover     = (char*) v2_url;
  c4_test_server_setup(&config);
  g_server_up = true;
}

static void teardown_if_up(void) {
  if (g_server_up) {
    c4_test_server_teardown();
    g_server_up = false;
  }
}

void setUp(void) {}
void tearDown(void) { teardown_if_up(); }

static int http_status_of(const char* method, const char* path, const char* body) {
  int   status_code = 0;
  char* response    = send_http_request(method, path, body, &status_code);
  if (response) free(response);
  return status_code;
}

// -- POST /proof --

// Legacy client (missing `version` field) + no v2_prover configured -> 503.
void test_post_missing_version_returns_503_without_v2_prover(void) {
  setup_with_v2("");
  c4_test_server_seed_for_test("v2fwd_post_no_version_503");

  const char* body = "{\"method\":\"eth_blockNumber\",\"params\":[]}";
  int         st   = http_status_of("POST", "/proof", body);
  TEST_ASSERT_EQUAL_INT(503, st);

  teardown_if_up();
}

// Legacy client (version 1.1.0 - packed uint32) + no v2_prover configured -> 503.
void test_post_legacy_version_returns_503_without_v2_prover(void) {
  setup_with_v2("");
  c4_test_server_seed_for_test("v2fwd_post_legacy_503");

  // c4_version_number(1,1,0) = (1<<16 | 1<<8) = 65792
  const char* body = "{\"method\":\"eth_blockNumber\",\"params\":[],\"version\":65792}";
  int         st   = http_status_of("POST", "/proof", body);
  TEST_ASSERT_EQUAL_INT(503, st);

  teardown_if_up();
}

// Version boundary: 196607 = c4_version_number(3,0,0) - 1 -> still legacy -> 503.
void test_post_version_boundary_below_3_returns_503(void) {
  setup_with_v2("");
  c4_test_server_seed_for_test("v2fwd_post_boundary_below3");

  const char* body = "{\"method\":\"eth_blockNumber\",\"params\":[],\"version\":196607}";
  int         st   = http_status_of("POST", "/proof", body);
  TEST_ASSERT_EQUAL_INT(503, st);

  teardown_if_up();
}

// 3.x client -> local dispatch, must not be rejected with 503 by the legacy gate and must not
// be accidentally forwarded (which would surface as 502 with v2_prover configured).
void test_post_v3_version_is_not_forwarded(void) {
  setup_with_v2("http://v2-legacy:9999");
  c4_test_server_seed_for_test("v2fwd_post_v3_local");

  // c4_version_number(3,0,0) = 3<<16 = 196608
  const char* body = "{\"method\":\"eth_blockNumber\",\"params\":[],\"version\":196608}";
  int         st   = http_status_of("POST", "/proof", body);
  TEST_ASSERT_NOT_EQUAL(503, st);
  TEST_ASSERT_NOT_EQUAL(502, st);

  teardown_if_up();
}

// Legacy client + configured v2_prover -> forwarding is attempted. There is no mock file for the
// forward URL, so the http_client treats it as HTTP 404 and our callback responds with 502.
void test_post_legacy_forwards_when_v2_prover_configured(void) {
  setup_with_v2("http://v2-legacy:9999");
  c4_test_server_seed_for_test("v2fwd_post_forward_attempt");

  const char* body = "{\"method\":\"eth_blockNumber\",\"params\":[]}";
  int         st   = http_status_of("POST", "/proof", body);
  TEST_ASSERT_NOT_EQUAL(503, st);
  TEST_ASSERT_NOT_EQUAL(400, st);
  TEST_ASSERT_EQUAL_INT(502, st);

  teardown_if_up();
}

// The `rpc`/`beacon` proxy fields must be rejected with 403 for legacy clients too, otherwise
// legacy requests could smuggle client-controlled URL lists past `proxy_enabled=false` and let
// the v2 prover use them as backends. This check must happen before the forward gate.
void test_post_legacy_rpc_field_still_blocked_when_proxy_disabled(void) {
  setup_with_v2("http://v2-legacy:9999");
  c4_test_server_seed_for_test("v2fwd_post_legacy_rpc_blocked");

  // proxy_enabled defaults to false; the rpc field must trigger 403 before we forward.
  const char* body =
      "{\"method\":\"eth_blockNumber\",\"params\":[],\"version\":65792,"
      "\"rpc\":\"http://attacker.internal/exfil\"}";
  int st = http_status_of("POST", "/proof", body);
  TEST_ASSERT_EQUAL_INT(403, st);

  teardown_if_up();
}

// A misconfigured `v2_prover` without http/https scheme must not be silently forwarded to.
void test_post_legacy_rejects_invalid_v2_prover_scheme(void) {
  setup_with_v2("v2-legacy:9999");
  c4_test_server_seed_for_test("v2fwd_post_bad_scheme");

  const char* body = "{\"method\":\"eth_blockNumber\",\"params\":[]}";
  int         st   = http_status_of("POST", "/proof", body);
  TEST_ASSERT_EQUAL_INT(500, st);

  teardown_if_up();
}

// -- GET /proof/... --

// Valid GET path with legacy version + no v2_prover -> 503 with Cache-Control: no-store.
void test_get_legacy_version_returns_503_without_v2_prover(void) {
  setup_with_v2("");
  c4_test_server_seed_for_test("v2fwd_get_legacy_503");

  int   status_code = 0;
  char* response    = send_http_request("GET", "/proof/eth_getBlockHeader/latest/3/std/0x", NULL,
                                        &status_code);
  TEST_ASSERT_NOT_NULL(response);
  TEST_ASSERT_EQUAL_INT(503, status_code);
  // proof_get_error attaches `Cache-Control: no-store` so CDNs never cache a 503.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(response, "Cache-Control: no-store"),
                               "503 must not be cacheable");
  free(response);

  teardown_if_up();
}

// GET path with 3.x version + configured v2_prover -> must not be rejected with 503 or 502
// (falls through to the local dispatch).
void test_get_v3_version_is_not_forwarded(void) {
  setup_with_v2("http://v2-legacy:9999");
  c4_test_server_seed_for_test("v2fwd_get_v3_local");

  int st = http_status_of("GET", "/proof/eth_getBlockHeader/latest/196608/std/0x", NULL);
  TEST_ASSERT_NOT_EQUAL(503, st);
  TEST_ASSERT_NOT_EQUAL(502, st);

  teardown_if_up();
}

// Malformed GET (invalid c4 segment) must keep returning 400 even when v2_prover is set,
// so that garbage requests cannot cause the server to blindly forward junk to the legacy
// backend and let the CDN cache a bogus response.
void test_get_invalid_c4_still_400_with_v2_prover(void) {
  setup_with_v2("http://v2-legacy:9999");
  c4_test_server_seed_for_test("v2fwd_get_invalid_c4");

  // odd hex length is invalid
  int st = http_status_of("GET", "/proof/eth_getBlockHeader/latest/3/std/0x123", NULL);
  TEST_ASSERT_EQUAL_INT(400, st);

  teardown_if_up();
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_post_missing_version_returns_503_without_v2_prover);
  RUN_TEST(test_post_legacy_version_returns_503_without_v2_prover);
  RUN_TEST(test_post_version_boundary_below_3_returns_503);
  RUN_TEST(test_post_v3_version_is_not_forwarded);
  RUN_TEST(test_post_legacy_forwards_when_v2_prover_configured);
  RUN_TEST(test_post_legacy_rpc_field_still_blocked_when_proxy_disabled);
  RUN_TEST(test_post_legacy_rejects_invalid_v2_prover_scheme);
  RUN_TEST(test_get_legacy_version_returns_503_without_v2_prover);
  RUN_TEST(test_get_v3_version_is_not_forwarded);
  RUN_TEST(test_get_invalid_c4_still_400_with_v2_prover);
  return UNITY_END();
}

#else
int main(void) {
  fprintf(stderr, "test_v2_proof_forward: Skipped (HTTP_SERVER not enabled)\n");
  return 0;
}
#endif
