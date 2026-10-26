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

#include "rpc_body.h"
#include "unity.h"
#include <string.h>

static bytes_t body_of(const char* json) {
  return bytes((uint8_t*) json, (uint32_t) strlen(json));
}

void setUp(void) {}
void tearDown(void) {}

void test_access_list_body_unchanged_for_other_clients(void) {
  const char* json = "{\"jsonrpc\":\"2.0\",\"method\":\"eth_createAccessList\",\"params\":[{\"to\":\"0x01\"},\"0x10\"],\"id\":1}";
  buffer_t    out  = {0};
  bytes_t     got  = c4_nethermind_access_list_body(body_of(json), false, &out);
  TEST_ASSERT_EQUAL_PTR(json, got.data);
  TEST_ASSERT_NULL(out.data.data);
}

void test_access_list_body_ignores_other_methods(void) {
  const char* json = "{\"jsonrpc\":\"2.0\",\"method\":\"eth_call\",\"params\":[{\"to\":\"0x01\"},\"0x10\"],\"id\":1}";
  buffer_t    out  = {0};
  bytes_t     got  = c4_nethermind_access_list_body(body_of(json), true, &out);
  TEST_ASSERT_EQUAL_PTR(json, got.data);
  TEST_ASSERT_NULL(out.data.data);
}

void test_access_list_body_nethermind_inserts_optimize_false(void) {
  const char* json = "{\"jsonrpc\":\"2.0\",\"method\":\"eth_createAccessList\",\"params\":[{\"to\":\"0x01\"},\"0x10\"],\"id\":1}";
  buffer_t    out  = {0};
  bytes_t     got  = c4_nethermind_access_list_body(body_of(json), true, &out);
  TEST_ASSERT_NOT_NULL(strstr((char*) got.data, "[{\"to\":\"0x01\"},\"0x10\",null,false]"));
  buffer_free(&out);
}

void test_access_list_body_nethermind_keeps_state_override(void) {
  const char* json = "{\"jsonrpc\":\"2.0\",\"method\":\"eth_createAccessList\",\"params\":[{\"to\":\"0x01\"},\"0x10\",{\"0x02\":{\"balance\":\"0x1\"}}],\"id\":1}";
  buffer_t    out  = {0};
  bytes_t     got  = c4_nethermind_access_list_body(body_of(json), true, &out);
  TEST_ASSERT_NOT_NULL(strstr((char*) got.data, "{\"0x02\":{\"balance\":\"0x1\"}},false]"));
  TEST_ASSERT_NULL(strstr((char*) got.data, "null,false"));
  buffer_free(&out);
}

void test_access_list_body_without_trailing_nul(void) {
  const char* json = "{\"jsonrpc\":\"2.0\",\"method\":\"eth_createAccessList\",\"params\":[{\"to\":\"0x01\"},\"0x10\"],\"id\":1}";
  uint32_t    n    = (uint32_t) strlen(json);
  uint8_t*    raw  = safe_malloc(n);
  memcpy(raw, json, n);
  buffer_t out = {0};
  bytes_t  got = c4_nethermind_access_list_body(bytes(raw, n), true, &out);
  TEST_ASSERT_NOT_NULL(strstr((char*) got.data, ",null,false]"));
  buffer_free(&out);
  safe_free(raw);
}

void test_access_list_body_leaves_existing_fourth_param(void) {
  const char* json = "{\"jsonrpc\":\"2.0\",\"method\":\"eth_createAccessList\",\"params\":[{\"to\":\"0x01\"},\"0x10\",null,true],\"id\":1}";
  buffer_t    out  = {0};
  bytes_t     got  = c4_nethermind_access_list_body(body_of(json), true, &out);
  TEST_ASSERT_EQUAL_PTR(json, got.data);
  TEST_ASSERT_NULL(out.data.data);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_access_list_body_unchanged_for_other_clients);
  RUN_TEST(test_access_list_body_ignores_other_methods);
  RUN_TEST(test_access_list_body_nethermind_inserts_optimize_false);
  RUN_TEST(test_access_list_body_nethermind_keeps_state_override);
  RUN_TEST(test_access_list_body_leaves_existing_fourth_param);
  RUN_TEST(test_access_list_body_without_trailing_nul);
  return UNITY_END();
}
