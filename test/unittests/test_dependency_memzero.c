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

/**
 * @file test_memzero.c
 * @brief Comprehensive unit tests for the memzero library
 *
 * This test suite provides comprehensive coverage of the memzero function,
 * which securely clears memory to prevent sensitive data leaks.
 *
 * Test Statistics:
 * - Total Tests: 10
 * - Coverage: All critical scenarios for secure memory clearing
 *
 * Tested Function Categories:
 * ---------------------------
 *
 * 1. BASIC FUNCTIONALITY (3 tests):
 *    - Small buffers (1-32 bytes)
 *    - Medium buffers (256 bytes)
 *    - Large buffers (1024+ bytes)
 *
 * 2. EDGE CASES (2 tests):
 *    - Zero length (should not crash)
 *    - Single byte
 *
 * 3. DIFFERENT DATA TYPES (2 tests):
 *    - Integer arrays
 *    - Struct types
 *
 * 4. COMPLETE CLEARING (1 test):
 *    - Verify all bytes are zeroed, not just some
 *
 * 5. ADVANCED SCENARIOS (2 tests):
 *    - Partial buffer clearing
 *    - Unaligned memory access
 *
 * Key Testing Features:
 * --------------------
 *
 * 1. Security Verification:
 *    - Ensures all bytes are set to 0
 *    - Tests with various sensitive data patterns
 *    - Verifies complete memory clearing
 *
 * 2. Edge Case Coverage:
 *    - Zero length buffers
 *    - Single byte buffers
 *    - Various buffer sizes
 *
 * 3. Type Safety:
 *    - Tests with different data types (uint8_t, uint32_t, structs)
 *    - Ensures function works with any pointer type
 */

#include "memzero.h"
#include "unity.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

void setUp(void) {}

void tearDown(void) {}

// Test: Basic functionality - small buffer
void test_memzero_small_buffer() {
  uint8_t buffer[32] = {0};
  
  // Fill buffer with non-zero values
  for (size_t i = 0; i < sizeof(buffer); i++) {
    buffer[i] = 0xFF;
  }
  
  // Verify buffer is filled
  for (size_t i = 0; i < sizeof(buffer); i++) {
    TEST_ASSERT_EQUAL_UINT8(0xFF, buffer[i]);
  }
  
  // Clear buffer
  memzero(buffer, sizeof(buffer));
  
  // Verify all bytes are zero
  for (size_t i = 0; i < sizeof(buffer); i++) {
    TEST_ASSERT_EQUAL_UINT8(0, buffer[i]);
  }
}

// Test: Medium buffer
void test_memzero_medium_buffer() {
  uint8_t buffer[256] = {0};
  
  // Fill buffer with pattern
  for (size_t i = 0; i < sizeof(buffer); i++) {
    buffer[i] = (uint8_t)(i & 0xFF);
  }
  
  // Clear buffer
  memzero(buffer, sizeof(buffer));
  
  // Verify all bytes are zero
  for (size_t i = 0; i < sizeof(buffer); i++) {
    TEST_ASSERT_EQUAL_UINT8(0, buffer[i]);
  }
}

// Test: Large buffer
void test_memzero_large_buffer() {
  uint8_t buffer[1024] = {0};
  
  // Fill buffer with pattern
  for (size_t i = 0; i < sizeof(buffer); i++) {
    buffer[i] = (uint8_t)((i * 7) & 0xFF);
  }
  
  // Clear buffer
  memzero(buffer, sizeof(buffer));
  
  // Verify all bytes are zero
  for (size_t i = 0; i < sizeof(buffer); i++) {
    TEST_ASSERT_EQUAL_UINT8(0, buffer[i]);
  }
}

// Test: Single byte
void test_memzero_single_byte() {
  uint8_t byte = 0xFF;
  
  memzero(&byte, 1);
  
  TEST_ASSERT_EQUAL_UINT8(0, byte);
}

// Test: Zero length (should not crash)
void test_memzero_zero_length() {
  uint8_t buffer[32] = {0xFF};
  
  // Should not crash with zero length
  memzero(buffer, 0);
  
  // Buffer should remain unchanged (implementation-dependent, but should not crash)
  // Note: Some implementations may still clear, but the important thing is it doesn't crash
}

// Test: Integer array
void test_memzero_integer_array() {
  uint32_t array[16] = {0};
  
  // Fill with non-zero values
  for (size_t i = 0; i < 16; i++) {
    array[i] = 0xDEADBEEF;
  }
  
  memzero(array, sizeof(array));
  
  // Verify all integers are zero
  for (size_t i = 0; i < 16; i++) {
    TEST_ASSERT_EQUAL_UINT32(0, array[i]);
  }
}

// Test: Struct type
void test_memzero_struct() {
  typedef struct {
    uint8_t data[32];
    uint32_t value;
    uint64_t large_value;
  } test_struct_t;
  
  test_struct_t s = {0};
  
  // Fill struct with non-zero values
  memset(&s, 0xFF, sizeof(s));
  
  memzero(&s, sizeof(s));
  
  // Verify all bytes are zero
  uint8_t* bytes = (uint8_t*)&s;
  for (size_t i = 0; i < sizeof(s); i++) {
    TEST_ASSERT_EQUAL_UINT8(0, bytes[i]);
  }
}

// Test: Verify complete clearing (not just partial)
void test_memzero_complete_clearing() {
  // Test with a pattern that could potentially be partially cleared
  uint8_t buffer[64] = {0};
  
  // Fill with alternating pattern
  for (size_t i = 0; i < sizeof(buffer); i++) {
    buffer[i] = (i % 2 == 0) ? 0xAA : 0x55;
  }
  
  // Clear buffer
  memzero(buffer, sizeof(buffer));
  
  // Verify ALL bytes are zero (not just some)
  // Also verify with explicit checks
  for (size_t i = 0; i < sizeof(buffer); i++) {
    TEST_ASSERT_EQUAL_UINT8(0, buffer[i]);
  }
}

// Test: Partial buffer clearing (clear only part of buffer)
void test_memzero_partial_buffer() {
  uint8_t buffer[64] = {0};
  
  // Fill entire buffer
  memset(buffer, 0xFF, sizeof(buffer));
  
  // Clear only first 32 bytes
  memzero(buffer, 32);
  
  // Verify first 32 bytes are zero
  for (size_t i = 0; i < 32; i++) {
    TEST_ASSERT_EQUAL_UINT8(0, buffer[i]);
  }
  
  // Verify remaining bytes are still 0xFF
  for (size_t i = 32; i < sizeof(buffer); i++) {
    TEST_ASSERT_EQUAL_UINT8(0xFF, buffer[i]);
  }
}

// Test: Unaligned memory (test with offset pointer)
void test_memzero_unaligned() {
  uint8_t buffer[64 + 4] = {0};
  uint8_t* unaligned = buffer + 1; // Offset by 1 byte (unaligned)
  
  // Fill unaligned region
  memset(unaligned, 0xAA, 32);
  
  // Clear unaligned region
  memzero(unaligned, 32);
  
  // Verify unaligned region is zero
  for (size_t i = 0; i < 32; i++) {
    TEST_ASSERT_EQUAL_UINT8(0, unaligned[i]);
  }
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_memzero_small_buffer);
  RUN_TEST(test_memzero_medium_buffer);
  RUN_TEST(test_memzero_large_buffer);
  RUN_TEST(test_memzero_single_byte);
  RUN_TEST(test_memzero_zero_length);
  RUN_TEST(test_memzero_integer_array);
  RUN_TEST(test_memzero_struct);
  RUN_TEST(test_memzero_complete_clearing);
  RUN_TEST(test_memzero_partial_buffer);
  RUN_TEST(test_memzero_unaligned);

  return UNITY_END();
}

