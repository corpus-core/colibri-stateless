/**
 * Bare-metal test for QEMU (no dependencies, direct hardware access)
 *
 * This test avoids semihosting and even stdio/libc dependencies
 * to ensure it works in the most minimalist environment possible.
 */

/* Direct UART registers for QEMU virt machine */
#define UART0_BASE    0x09000000
#define UART0_DR      (*(volatile unsigned int*) (UART0_BASE))
#define UART0_FR      (*(volatile unsigned int*) (UART0_BASE + 0x18))
#define UART0_FR_TXFF 0x20

/* Simple delay function */
void delay(int count) {
  for (volatile int i = 0; i < count; i++) {
    /* Do nothing */
  }
}

/* Write a character to the UART */
void uart_putc(char c) {
  /* Wait for UART to be ready */
  while (UART0_FR & UART0_FR_TXFF) {}

  /* Write character */
  UART0_DR = c;
}

/* Write a string to the UART */
void uart_puts(const char* str) {
  while (*str) {
    uart_putc(*str++);
  }
}

/* Main function */
int main(void) {
  /* Direct hardware initialization */
  delay(1000);

  /* Write a message directly to the UART - no dependencies */
  uart_puts("\r\n\r\n============================\r\n");
  uart_puts("BARE METAL TEST STARTING\r\n");
  uart_puts("============================\r\n\r\n");

  /* Short delay */
  delay(100000);

  /* Print a message that will be visible */
  uart_puts("Test step 1: Basic UART output\r\n");
  delay(10000);

  /* Print a successful test message */
  uart_puts("\r\n>> TEST COMPLETED SUCCESSFULLY <<\r\n");
  uart_puts("Test completed successfully!\r\n");

  /* Infinite loop to prevent return */
  while (1) {
    uart_puts(".");
    delay(5000000);
  }

  return 0;
}