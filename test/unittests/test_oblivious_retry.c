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

// Unit tests for the oblivious-node delayed-retry primitives:
//   - eth_is_oblivious_unavailable(): recognises the oblivious node's
//     "data non availability" (-32001) signal and nothing else.
//   - c4_state_retry_after(): schedules a same-node retry with a delay,
//     bounded by a retry budget, and clears the previous response/error.
//
// The detector lives in the eth verifier (eth_verify.h); it is declared here
// to keep the test independent of the eth verifier's internal include paths.

#include "bytes.h"
#include "json.h"
#include "state.h"
#include "unity.h"
#include <string.h>

extern bool eth_is_oblivious_unavailable(json_t response);

void setUp(void) {}
void tearDown(void) {}

void test_detector_matches_oblivious_unavailable(void) {
  char r[] = "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":-32001,\"message\":\"Failed due to data non availability\"}}";
  TEST_ASSERT_TRUE(eth_is_oblivious_unavailable(json_parse(r)));
}

void test_detector_rejects_other_error_code(void) {
  char r[] = "{\"error\":{\"code\":-32000,\"message\":\"Failed due to data non availability\"}}";
  TEST_ASSERT_FALSE(eth_is_oblivious_unavailable(json_parse(r)));
}

void test_detector_rejects_other_message(void) {
  char r[] = "{\"error\":{\"code\":-32001,\"message\":\"resource not found\"}}";
  TEST_ASSERT_FALSE(eth_is_oblivious_unavailable(json_parse(r)));
}

void test_detector_rejects_valid_result(void) {
  char r[] = "{\"result\":{\"storageProof\":[{\"value\":\"0x01\"}]}}";
  TEST_ASSERT_FALSE(eth_is_oblivious_unavailable(json_parse(r)));
}

void test_retry_after_schedules_and_bounds(void) {
  data_request_t req = {0};

  TEST_ASSERT_TRUE(c4_state_retry_after(&req, 3000, 2));
  TEST_ASSERT_EQUAL_UINT32(3000, req.delay);
  TEST_ASSERT_EQUAL_UINT16(1, req.retry_count);

  TEST_ASSERT_TRUE(c4_state_retry_after(&req, 3000, 2));
  TEST_ASSERT_EQUAL_UINT16(2, req.retry_count);

  // budget exhausted -> no further retry, counter unchanged
  TEST_ASSERT_FALSE(c4_state_retry_after(&req, 3000, 2));
  TEST_ASSERT_EQUAL_UINT16(2, req.retry_count);
}

void test_retry_after_clears_response_and_error(void) {
  data_request_t req = {0};
  req.response       = bytes_dup(bytes((uint8_t*) "x", 1));
  req.error          = strdup("transient error");

  TEST_ASSERT_TRUE(c4_state_retry_after(&req, 1500, 5));
  TEST_ASSERT_NULL(req.response.data);
  TEST_ASSERT_EQUAL_UINT32(0, req.response.len);
  TEST_ASSERT_NULL(req.error);
  TEST_ASSERT_EQUAL_UINT32(1500, req.delay);
}

void test_retry_after_resets_node_selection(void) {
  // A same-node retry must reset the host-side node selection: hosts resume
  // from `response_node_index` and skip excluded nodes, so leaving these set
  // would make the host skip the only oblivious node ("no more nodes to try").
  data_request_t req     = {0};
  req.response_node_index = 1;
  req.node_exclude_mask   = 0x1;

  TEST_ASSERT_TRUE(c4_state_retry_after(&req, 3000, 5));
  TEST_ASSERT_EQUAL_UINT16(0, req.response_node_index);
  TEST_ASSERT_EQUAL_UINT16(0, req.node_exclude_mask);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_detector_matches_oblivious_unavailable);
  RUN_TEST(test_detector_rejects_other_error_code);
  RUN_TEST(test_detector_rejects_other_message);
  RUN_TEST(test_detector_rejects_valid_result);
  RUN_TEST(test_retry_after_schedules_and_bounds);
  RUN_TEST(test_retry_after_clears_response_and_error);
  RUN_TEST(test_retry_after_resets_node_selection);
  return UNITY_END();
}
