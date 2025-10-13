/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: MIT
 *
 * Test storage cache isolation between tests
 */

#include "unity.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef HTTP_SERVER

#include "test_server_helper.h"

// Unity setup - called before each test
void setUp(void) {
  c4_test_server_setup(NULL);
}

// Unity teardown - called after each test
void tearDown(void) {
  c4_test_server_teardown();
}

// Test 1: Verify C4_STATES_DIR is set correctly
void test_states_dir_isolation(void) {
  c4_test_server_seed_for_test("test_states_dir_isolation");

  // Verify environment variable is set
  const char* states_dir = getenv("C4_STATES_DIR");
  TEST_ASSERT_NOT_NULL_MESSAGE(states_dir, "C4_STATES_DIR should be set");

  // Should contain test name
  TEST_ASSERT_TRUE_MESSAGE(strstr(states_dir, "test_states_dir_isolation") != NULL,
                           "C4_STATES_DIR should contain test name");

  fprintf(stderr, "C4_STATES_DIR = %s\n", states_dir);
}

// Test 2: Verify different tests get different directories
void test_different_test_different_dir(void) {
  c4_test_server_seed_for_test("test_different_test_different_dir");

  const char* states_dir = getenv("C4_STATES_DIR");
  TEST_ASSERT_NOT_NULL(states_dir);
  TEST_ASSERT_TRUE(strstr(states_dir, "test_different_test_different_dir") != NULL);

  fprintf(stderr, "C4_STATES_DIR = %s\n", states_dir);
}

// Main test runner
int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_states_dir_isolation);
  RUN_TEST(test_different_test_different_dir);

  return UNITY_END();
}

#else // !HTTP_SERVER

int main(void) {
  fprintf(stderr, "test_storage_isolation: Skipped (HTTP_SERVER not enabled)\n");
  return 0;
}

#endif // HTTP_SERVER
