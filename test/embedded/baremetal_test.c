/**
 * Bare-metal test for QEMU (no dependencies, direct hardware access)
 *
 * This test avoids semihosting and even stdio/libc dependencies
 * to ensure it works in the most minimalist environment possible.
 */

/* Direct UART registers for QEMU virt machine
 * QEMU virt machine uses PL011 UART at this address
 */
#define UART0_BASE    0x09000000
#define UART0_DR      (*(volatile unsigned int*) (UART0_BASE))
#define UART0_FR      (*(volatile unsigned int*) (UART0_BASE + 0x18))
#define UART0_IBRD    (*(volatile unsigned int*) (UART0_BASE + 0x24))
#define UART0_FBRD    (*(volatile unsigned int*) (UART0_BASE + 0x28))
#define UART0_LCRH    (*(volatile unsigned int*) (UART0_BASE + 0x2C))
#define UART0_CR      (*(volatile unsigned int*) (UART0_BASE + 0x30))
#define UART0_FR_TXFF 0x20

/* Initialize UART for QEMU virt machine (PL011) */
void uart_init(void) {
  /* Disable UART during configuration */
  UART0_CR = 0;

  /* Configure baud rate (115200) - assuming 24MHz clock
   * Divider = 24000000 / (16 * 115200) = 13.0208
   * Integer part = 13
   * Fractional part = 0.0208 * 64 = 1.33 ≈ 1
   */
  UART0_IBRD = 13;
  UART0_FBRD = 1;

  /* Enable FIFO, 8 data bits, 1 stop bit, no parity */
  UART0_LCRH = 0x70; /* 8N1 + FIFO enable */

  /* Enable UART, transmit, receive */
  UART0_CR = 0x301; /* UART enable, TX enable, RX enable */
}

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

  /* Special handling for newline */
  if (c == '\n') {
    uart_putc('\r');
  }
}

/* Write a string to the UART */
void uart_puts(const char* str) {
  while (*str) {
    uart_putc(*str++);
  }
}

/* Main function */
int main(void) {
  /* Initialize UART hardware explicitly */
  uart_init();

  /* Direct hardware initialization */
  delay(100000);

  /* Write a message directly to the UART - no dependencies */
  uart_puts("\n\n============================\n");
  uart_puts("BARE METAL TEST STARTING\n");
  uart_puts("============================\n\n");

  /* Short delay */
  delay(100000);

  /* Print a message that will be visible */
  uart_puts("Test step 1: Basic UART output\n");
  delay(10000);

  /* Print more debug information */
  uart_puts("Test step 2: Secondary output\n");
  delay(10000);

  /* Print a successful test message */
  uart_puts("\n>> TEST COMPLETED SUCCESSFULLY <<\n");
  uart_puts("Test completed successfully!\n");

  /* Infinite loop to prevent return */
  while (1) {
    uart_puts(".");
    delay(1000000);
  }

  return 0;
}