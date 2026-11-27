/*
 * Copyright (c) 2025,2026 corpus.core GmbH
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

// Issue #406: standard eth_createAccessList first; if the list is empty, retry
// with Nethermind optimize=false.

#include "eth_req.h"
#include "json.h"
#include "prover.h"
#include "state.h"
#include "unity.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void set_json_response(data_request_t* req, const char* json) {
  size_t   n    = strlen(json);
  uint8_t* copy = (uint8_t*) safe_malloc(n + 1);
  memcpy(copy, json, n + 1);
  safe_free(req->response.data);
  req->response = bytes(copy, (uint32_t) n);
  req->error    = NULL;
}

void test_create_access_list_standard_first(void) {
  char         tx_json[] = "{\"to\":\"0x01\",\"data\":\"0x01\"}";
  prover_ctx_t ctx       = {0};
  json_t       tx        = json_parse(tx_json);
  json_t       trace     = {0};

  TEST_ASSERT_EQUAL_INT(C4_PENDING, eth_create_access_list(&ctx, tx, &trace, 1, (json_t) {0}));
  data_request_t* req = c4_state_get_pending_request(&ctx.state);
  TEST_ASSERT_NOT_NULL(strstr((char*) req->payload.data, "\"eth_createAccessList\""));
  TEST_ASSERT_NULL(strstr((char*) req->payload.data, ",false]"));
  c4_state_free(&ctx.state);
}

void test_create_access_list_retries_optimize_false_when_empty(void) {
  char         tx_json[] = "{\"to\":\"0x01\",\"data\":\"0x01\"}";
  prover_ctx_t ctx       = {0};
  json_t       tx        = json_parse(tx_json);
  json_t       trace     = {0};

  TEST_ASSERT_EQUAL_INT(C4_PENDING, eth_create_access_list(&ctx, tx, &trace, 1, (json_t) {0}));
  data_request_t* req1 = c4_state_get_pending_request(&ctx.state);
  set_json_response(req1, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"accessList\":[],\"gasUsed\":\"0x1\"}}");

  TEST_ASSERT_EQUAL_INT(C4_PENDING, eth_create_access_list(&ctx, tx, &trace, 1, (json_t) {0}));
  data_request_t* req2 = c4_state_get_pending_request(&ctx.state);
  TEST_ASSERT_NOT_EQUAL(req1, req2);
  TEST_ASSERT_NOT_NULL(strstr((char*) req2->payload.data, ",null,false]"));

  set_json_response(req2,
                    "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"accessList\":[{"
                    "\"address\":\"0x0000000000000000000000000000000000000001\","
                    "\"storageKeys\":[\"0x0000000000000000000000000000000000000000000000000000000000000002\"]"
                    "}],\"gasUsed\":\"0x1\"}}");
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, eth_create_access_list(&ctx, tx, &trace, 1, (json_t) {0}),
                                ctx.state.error ? ctx.state.error : "expected success");
  TEST_ASSERT_EQUAL_INT(1, (int) json_len(json_get(trace, "accessList")));
  c4_state_free(&ctx.state);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_create_access_list_standard_first);
  RUN_TEST(test_create_access_list_retries_optimize_false_when_empty);
  return UNITY_END();
}
