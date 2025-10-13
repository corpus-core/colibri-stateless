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

// datei: test_addiere.c
#include "beacon_types.h"
#include "bytes.h"
#include "c4_assert.h"
#include "ssz.h"
#include "unity.h"
void setUp(void) {
}

void tearDown(void) {
}

void test_json() {

  // simple parser
  buffer_t buffer = {0};
  json_t   json   = json_parse("{\"name\": \"John\", \"age\": 30}");
  TEST_ASSERT_EQUAL_STRING("John", json_as_string(json_get(json, "name"), &buffer));
  TEST_ASSERT_EQUAL_INT(30, json_as_uint32(json_get(json, "age")));

  // escaped strings
  json = json_parse("{\"name\": \"John\\\"\", \"age\": 30}");
  TEST_ASSERT_EQUAL_STRING("John\"", json_as_string(json_get(json, "name"), &buffer));

  buffer_reset(&buffer);
  TEST_ASSERT_EQUAL_STRING("{\"name\": \"John\\\"\"}", bprintf(&buffer, "{\"name\": \"%S\"}", "John\""));

  // cleanup
  buffer_free(&buffer);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_json);
  return UNITY_END();
}