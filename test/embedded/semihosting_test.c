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
  buffer_t buf = {0};
  bprintf(&buf, "Status: %s\n", message);
  fwrite(buf.data.data, 1, buf.data.len, stdout);
  buffer_free(&buf);
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
    char     buf[100];
    buffer_t tmp_buf = stack_buffer(buf);
    bprintf(&tmp_buf, "Counter: %d", (uint32_t) i);
    semihosting_write(buf);
  }

  semihosting_write("Test completed successfully!");
  return 0;
}