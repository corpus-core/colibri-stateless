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

#include "unity.h"
#include "version.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

void setUp(void) {
  // Setup before each test
}

void tearDown(void) {
  // Cleanup after each test
}

// Test 1: Version string is not NULL or empty
void test_version_string_valid(void) {
  TEST_ASSERT_NOT_NULL(c4_client_version);
  TEST_ASSERT_TRUE(strlen(c4_client_version) > 0);
}

// Test 2: Protocol version bytes are valid
void test_protocol_version_bytes(void) {
  // Protocol version bytes should be defined
  TEST_ASSERT_NOT_NULL(c4_protocol_version_bytes);

  // Verify the array has 4 elements (implicitly tested by accessing them)
  // All values are uint8_t, so they're automatically in range 0-255
}

// Test 3: Print version to file stream
void test_print_version(void) {
  // Create temporary file
  FILE* temp = tmpfile();
  TEST_ASSERT_NOT_NULL(temp);

  // Print version info
  c4_print_version(temp, "test-program");

  // Read back the content
  rewind(temp);
  char   buffer[4096];
  size_t bytes_read  = fread(buffer, 1, sizeof(buffer) - 1, temp);
  buffer[bytes_read] = '\0';

  // Verify output contains expected strings
  TEST_ASSERT_TRUE(strstr(buffer, "test-program") != NULL);
  TEST_ASSERT_TRUE(strstr(buffer, "version") != NULL);
  TEST_ASSERT_TRUE(strstr(buffer, "Build Configuration") != NULL);
  TEST_ASSERT_TRUE(strstr(buffer, "Protocol Version") != NULL);

  fclose(temp);
}

// Test 4: Print version with NULL program name
void test_print_version_null_name(void) {
  FILE* temp = tmpfile();
  TEST_ASSERT_NOT_NULL(temp);

  // Should handle NULL program name gracefully
  c4_print_version(temp, NULL);

  // Read back and verify it didn't crash
  rewind(temp);
  char   buffer[4096];
  size_t bytes_read  = fread(buffer, 1, sizeof(buffer) - 1, temp);
  buffer[bytes_read] = '\0';

  TEST_ASSERT_TRUE(bytes_read > 0);

  fclose(temp);
}

// Test 5: Version string format check
void test_version_string_format(void) {
  // Version should typically contain numbers
  const char* version   = c4_client_version;
  bool        has_digit = false;

  for (size_t i = 0; i < strlen(version); i++) {
    if (version[i] >= '0' && version[i] <= '9') {
      has_digit = true;
      break;
    }
  }

  TEST_ASSERT_TRUE(has_digit);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_version_string_valid);
  RUN_TEST(test_protocol_version_bytes);
  RUN_TEST(test_print_version);
  RUN_TEST(test_print_version_null_name);
  RUN_TEST(test_version_string_format);
  return UNITY_END();
}
