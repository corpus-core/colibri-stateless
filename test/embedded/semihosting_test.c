#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ARM semihosting operations
#define SYS_WRITE0 0x04 // Write a null-terminated string
#define SYS_WRITEC 0x03 // Write a character
#define SYS_WRITE  0x05 // Write data to a file

// Direct ARM semihosting call
static inline int semihosting_call(int operation, void* args) {
  register int   r0 __asm__("r0") = operation;
  register void* r1 __asm__("r1") = args;

  __asm__ volatile(
      "bkpt 0xAB"
      : "=r"(r0)
      : "r"(r0), "r"(r1)
      : "memory");

  return r0;
}

// Write a string using semihosting SYS_WRITE0
static void sh_write0(const char* string) {
  semihosting_call(SYS_WRITE0, (void*) string);
}

// Write a character using semihosting SYS_WRITEC
static void sh_writec(char c) {
  semihosting_call(SYS_WRITEC, &c);
}

// Write a string with newline using direct semihosting
static void semihosting_write(const char* message) {
  sh_write0(message);
  sh_writec('\n');
}

// Simple function to print status messages
static void print_status(const char* message) {
  printf("Status: %s\n", message);
  fflush(stdout);
}

int main(void) {
  // Try direct semihosting output
  semihosting_write("=== SEMIHOSTING TEST PROGRAM ===");
  semihosting_write("This is a direct semihosting write test");

  // Try standard printf
  print_status("This is a printf test");

  // Test memory allocation
  void* test_memory = safe_malloc(1024);
  if (test_memory) {
    semihosting_write("Successfully allocated 1KB of memory");
    safe_free(test_memory);
  }
  else {
    semihosting_write("Failed to allocate memory");
  }

  // Print some numbers
  for (int i = 0; i < 5; i++) {
    char buf[100];
    snprintf(buf, sizeof(buf), "Counter: %d", i);
    semihosting_write(buf);
  }

  semihosting_write("Test completed successfully!");
  return 0;
}