/*
 * Copyright (c) 2025 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

// Additional edge-case coverage for the oblivious-node delayed-retry primitives.
// Complements test_oblivious_retry.c by exercising the branches most likely to
// hide real bugs:
//   eth_is_oblivious_unavailable():
//     - error as a JSON string (not object) must NOT match
//     - missing `message` field must NOT match
//     - `code` encoded as a string ("-32001") must NOT match
//     - a non-object top-level response must NOT match
//     - the availability marker is matched as a substring anywhere in the message
//     - exact-length boundary message ("data non availability")
//   c4_state_retry_after():
//     - NULL request is handled gracefully
//     - an exhausted budget (max_retries == 0) must NOT clear the existing
//       response/error and must NOT set the delay (host keeps the failure)
//     - the delay value is refreshed on a subsequent retry

#include "bytes.h"
#include "json.h"
#include "state.h"
#include "unity.h"
#include <string.h>

extern bool eth_is_oblivious_unavailable(json_t response);

void setUp(void) {}
void tearDown(void) {}

// --- detector edge cases -----------------------------------------------------

void test_detector_rejects_string_error(void) {
  // A bare string `error` (not an object) carries no code/message and must not
  // be mistaken for the oblivious availability signal.
  char r[] = "{\"error\":\"Failed due to data non availability\"}";
  TEST_ASSERT_FALSE(eth_is_oblivious_unavailable(json_parse(r)));
}

void test_detector_rejects_missing_message(void) {
  char r[] = "{\"error\":{\"code\":-32001}}";
  TEST_ASSERT_FALSE(eth_is_oblivious_unavailable(json_parse(r)));
}

void test_detector_rejects_numeric_code_as_string(void) {
  // code as a JSON string must not match: the detector requires JSON_TYPE_NUMBER.
  char r[] = "{\"error\":{\"code\":\"-32001\",\"message\":\"data non availability\"}}";
  TEST_ASSERT_FALSE(eth_is_oblivious_unavailable(json_parse(r)));
}

void test_detector_rejects_non_object_response(void) {
  char r[] = "[\"data non availability\"]";
  TEST_ASSERT_FALSE(eth_is_oblivious_unavailable(json_parse(r)));
}

void test_detector_rejects_empty_object(void) {
  char r[] = "{}";
  TEST_ASSERT_FALSE(eth_is_oblivious_unavailable(json_parse(r)));
}

void test_detector_matches_marker_as_substring(void) {
  // The marker may appear with surrounding text; a substring scan must catch it.
  char r[] = "{\"error\":{\"code\":-32001,\"message\":\"request rejected: data non availability for slot 42\"}}";
  TEST_ASSERT_TRUE(eth_is_oblivious_unavailable(json_parse(r)));
}

void test_detector_matches_exact_marker_message(void) {
  // Boundary: message token is exactly the marker (plus the JSON quotes).
  char r[] = "{\"error\":{\"code\":-32001,\"message\":\"data non availability\"}}";
  TEST_ASSERT_TRUE(eth_is_oblivious_unavailable(json_parse(r)));
}

// --- c4_state_retry_after edge cases -----------------------------------------

void test_retry_after_null_request_is_safe(void) {
  TEST_ASSERT_FALSE(c4_state_retry_after(NULL, 3000, 5));
}

void test_retry_after_zero_budget_preserves_failure(void) {
  // With max_retries == 0 the very first call must report exhaustion WITHOUT
  // touching the request: the existing response/error stays so the host can
  // surface the failure, and no spurious delay is scheduled.
  data_request_t req = {0};
  req.response       = bytes_dup(bytes((uint8_t*) "payload", 7));
  req.error          = strdup("boom");

  TEST_ASSERT_FALSE(c4_state_retry_after(&req, 3000, 0));
  TEST_ASSERT_NOT_NULL(req.response.data);
  TEST_ASSERT_EQUAL_UINT32(7, req.response.len);
  TEST_ASSERT_NOT_NULL(req.error);
  TEST_ASSERT_EQUAL_UINT32(0, req.delay);
  TEST_ASSERT_EQUAL_UINT16(0, req.retry_count);

  safe_free(req.response.data);
  safe_free(req.error);
}

void test_retry_after_refreshes_delay(void) {
  data_request_t req = {0};

  TEST_ASSERT_TRUE(c4_state_retry_after(&req, 3000, 5));
  TEST_ASSERT_EQUAL_UINT32(3000, req.delay);

  // A later retry can carry a different delay; the field must be overwritten.
  TEST_ASSERT_TRUE(c4_state_retry_after(&req, 1000, 5));
  TEST_ASSERT_EQUAL_UINT32(1000, req.delay);
  TEST_ASSERT_EQUAL_UINT16(2, req.retry_count);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_detector_rejects_string_error);
  RUN_TEST(test_detector_rejects_missing_message);
  RUN_TEST(test_detector_rejects_numeric_code_as_string);
  RUN_TEST(test_detector_rejects_non_object_response);
  RUN_TEST(test_detector_rejects_empty_object);
  RUN_TEST(test_detector_matches_marker_as_substring);
  RUN_TEST(test_detector_matches_exact_marker_message);
  RUN_TEST(test_retry_after_null_request_is_safe);
  RUN_TEST(test_retry_after_zero_budget_preserves_failure);
  RUN_TEST(test_retry_after_refreshes_delay);
  return UNITY_END();
}
