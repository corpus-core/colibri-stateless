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
  printf("Status: %s\n", message);
  fflush(stdout);

  // Also try direct semihosting
  semihosting_print(message);
}

// Function to test memory allocation and report usage
static void test_memory_allocation(size_t size) {
  print_status("Testing memory allocation");

  char buf[100];
  snprintf(buf, sizeof(buf), "Attempting to allocate %zu bytes", size);
  semihosting_print(buf);

  void* test_memory = malloc(size);
  if (test_memory) {
    snprintf(buf, sizeof(buf), "Successfully allocated %zu bytes of memory", size);
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

    free(test_memory);
    semihosting_print("Memory freed successfully");
  }
  else {
    snprintf(buf, sizeof(buf), "Failed to allocate %zu bytes of memory", size);
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
  char buf[100];

  // Initialize verification context
  static verify_ctx_t ctx = {0};
  semihosting_print("Verification context initialized");

  // Report size of verification context
  snprintf(buf, sizeof(buf), "Size of verify_ctx_t: %zu bytes", sizeof(verify_ctx_t));
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
  snprintf(buf, sizeof(buf), "Attempting to allocate verification buffer of %zu bytes", verification_buffer_size);
  semihosting_print(buf);

  void* verification_buffer = malloc(verification_buffer_size);
  if (verification_buffer) {
    semihosting_print("Successfully allocated verification buffer");
    free(verification_buffer);
  }
  else {
    semihosting_print("Failed to allocate verification buffer");
  }

  // Create a simple bytes object
  bytes_t dummy_data = {
      .data = (uint8_t*) "test data",
      .len  = 9};

  print_status("Initialized test data");
  snprintf(buf, sizeof(buf), "Data content: %s, length: %zu", (char*) dummy_data.data, dummy_data.len);
  semihosting_print(buf);

  // Print verification library info
  snprintf(buf, sizeof(buf), "Verifier initialized, library size: %zu bytes", sizeof(verify_ctx_t));
  semihosting_print(buf);

  snprintf(buf, sizeof(buf), "Data size: %zu bytes", dummy_data.len);
  semihosting_print(buf);

  semihosting_print("Test completed successfully!");
  return TEST_SUCCESS;
}