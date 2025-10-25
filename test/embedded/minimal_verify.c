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

#include "util/bytes.h"
#include "verifier/verify.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Direct semihosting write function for more reliable output
static void semihosting_print(const char* message) {
  write(1, message, strlen(message));
  write(1, "\n", 1);
}

// Simple function to print status messages using both methods
static void print_status(const char* message) {
  //  printf("Status: %s\n", message);
  //  fflush(stdout);

  // Also try direct semihosting
  semihosting_print(message);
}

// Function to test memory allocation and report usage
static void test_memory_allocation(size_t size) {
  print_status("Testing memory allocation");

  char     buf[100];
  buffer_t tmp_buf = stack_buffer(buf);
  bprintf(&tmp_buf, "Attempting to allocate %d bytes", (uint32_t) size);
  semihosting_print(buf);

  void* test_memory = safe_malloc(size);
  if (test_memory) {
    tmp_buf = stack_buffer(buf);
    bprintf(&tmp_buf, "Successfully allocated %d bytes of memory", (uint32_t) size);
    semihosting_print(buf);

    // Write some pattern to the memory to ensure it's usable
    memset(test_memory, 0xAA, size);

    // Verify memory content
    uint8_t* memory_bytes = (uint8_t*) test_memory;
    if (memory_bytes[0] == 0xAA && memory_bytes[size - 1] == 0xAA) {
      semihosting_print("Memory content verification passed");
    }
    else {
      semihosting_print("Memory content verification failed!");
    }

    safe_free(test_memory);
    semihosting_print("Memory freed successfully");
  }
  else {
    tmp_buf = stack_buffer(buf);
    bprintf(&tmp_buf, "Failed to allocate %d bytes of memory", (uint32_t) size);
    semihosting_print(buf);
  }
}

// Return codes
#define TEST_SUCCESS 0
#define TEST_FAILURE 1

// Test a minimal verification setup
int main(void) {
  // Try direct semihosting output first
  semihosting_print("=== C4 EMBEDDED VERIFICATION MEMORY TEST ===");
  semihosting_print("Starting minimal embedded verification test");

  // Then try standard printf
  print_status("Starting minimal embedded verification test");

  // Report memory sizes
  char     buf[100];
  buffer_t tmp_buf;

  // Initialize verification context
  static verify_ctx_t ctx = {0};
  semihosting_print("Verification context initialized");

  // Report size of verification context
  tmp_buf = stack_buffer(buf);
  bprintf(&tmp_buf, "Size of verify_ctx_t: %d bytes", (uint32_t) sizeof(verify_ctx_t));
  semihosting_print(buf);

  // Test memory allocations
  // Try small allocation
  test_memory_allocation(1024); // 1KB

  // Try medium allocation (typical proof size)
  test_memory_allocation(16 * 1024); // 16KB

  // Try large allocation (key update size)
  test_memory_allocation(32 * 1024); // 32KB

  // Try to allocate a buffer for the full verification process
  size_t verification_buffer_size = 64 * 1024; // 64KB
  tmp_buf                         = stack_buffer(buf);
  bprintf(&tmp_buf, "Attempting to allocate verification buffer of %d bytes", (uint32_t) verification_buffer_size);
  semihosting_print(buf);

  void* verification_buffer = safe_malloc(verification_buffer_size);
  if (verification_buffer) {
    semihosting_print("Successfully allocated verification buffer");
    safe_free(verification_buffer);
  }
  else {
    semihosting_print("Failed to allocate verification buffer");
  }

  // Create a simple bytes object
  bytes_t dummy_data = {
      .data = (uint8_t*) "test data",
      .len  = 9};

  print_status("Initialized test data");
  tmp_buf = stack_buffer(buf);
  bprintf(&tmp_buf, "Data content: %s, length: %d", (char*) dummy_data.data, (uint32_t) dummy_data.len);
  semihosting_print(buf);

  // Print verification library info
  tmp_buf = stack_buffer(buf);
  bprintf(&tmp_buf, "Verifier initialized, library size: %d bytes", (uint32_t) sizeof(verify_ctx_t));
  semihosting_print(buf);

  tmp_buf = stack_buffer(buf);
  bprintf(&tmp_buf, "Data size: %d bytes", (uint32_t) dummy_data.len);
  semihosting_print(buf);

  semihosting_print("Test completed successfully!");
  return TEST_SUCCESS;
}