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

#include <stdio.h>
#include <string.h>
#include <unistd.h>

// QEMU Semihosting operations
#define SYS_WRITE0 0x04

// Direct semihosting function using SYS_WRITE0 (simpler than write syscall)
void semi_write(const char* str) {
  // Use the ARM semihosting interface SYS_WRITE0
  asm volatile(
      "mov r0, #0x04\n" // SYS_WRITE0
      "mov r1, %0\n"    // string pointer
      "bkpt #0xAB\n"    // semihosting breakpoint
      : : "r"(str) : "r0", "r1");
}

// Write raw bytes directly to the UART using memory-mapped I/O
void uart_write(const char* str) {
  // QEMU UART at standard address 0x09000000
  volatile unsigned int* uart = (volatile unsigned int*) 0x09000000;

  while (*str) {
    *uart = (unsigned int) (*str++);
  }
  *uart = '\n'; // Add a newline
}

int main(void) {
  // 1. Write using standard printf
  printf("Hello from printf!\n");
  fflush(stdout);

  // 2. Write using semihosting SYS_WRITE0
  semi_write("Hello from semihosting SYS_WRITE0!");

  // 3. Write using direct syscall write() - should go to stdout
  const char* write_msg = "Hello from write syscall!\n";
  write(1, write_msg, strlen(write_msg));

  // 4. Write using UART
  uart_write("Hello from UART!");

  // 5. Print a clear successful test message
  printf("TEST COMPLETED SUCCESSFULLY\n");
  fflush(stdout);

  // 6. Also use semihosting for the success message
  semi_write("Test completed successfully!");

  return 0;
}