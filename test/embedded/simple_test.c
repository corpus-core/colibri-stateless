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