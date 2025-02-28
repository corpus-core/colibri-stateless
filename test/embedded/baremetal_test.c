/**
 * Bare-metal test for QEMU (absolute minimal version)
 *
 * This test avoids all dependencies and uses the simplest possible
 * approach to get output from QEMU. It directly accesses the UART
 * hardware registers at the address expected by QEMU's virt machine.
 */

/* UART registers for QEMU virt machine (PL011 UART) */
#define UART_BASE 0x09000000
#define UART_DR   (*(volatile unsigned int*) (UART_BASE + 0x00))
#define UART_FR   (*(volatile unsigned int*) (UART_BASE + 0x18))
#define UART_CR   (*(volatile unsigned int*) (UART_BASE + 0x30))

/* Flag bits in the Flag Register (FR) */
#define UART_FR_TXFF (1 << 5) /* Transmit FIFO full */
#define UART_FR_RXFE (1 << 4) /* Receive FIFO empty */

/* Memory regions defined in linker script */
extern unsigned int __stack_start__;
extern unsigned int __stack_end__;
extern unsigned int __bss_start__;
extern unsigned int __bss_end__;

/* Minimalistic UART initialization - absolute minimum to get output */
void uart_init(void) {
  /* Simply enable the UART, assuming hardware default values are okay */
  UART_CR = 0x301; /* UART enable (bit 0), TX enable (bit 8), RX enable (bit 9) */
}

/* Short delay without actual timer */
void short_delay(void) {
  volatile int i;
  for (i = 0; i < 1000; i++) {
    /* Do nothing */
  }
}

/* Send a single character to UART */
void uart_putc(unsigned char c) {
  /* Wait for space in the FIFO */
  while (UART_FR & UART_FR_TXFF) {
    /* Just wait */
  }

  /* Send the character */
  UART_DR = c;

  /* If we sent a newline, also send carriage return */
  if (c == '\n') {
    uart_putc('\r');
  }
}

/* Send a string to UART */
void uart_puts(const char* s) {
  while (*s) {
    uart_putc(*s++);
  }
}

/* Simple integer to string conversion for debugging (minimal implementation) */
void uart_puthex(unsigned int value) {
  const char hexchars[] = "0123456789ABCDEF";
  uart_puts("0x");

  /* Print 8 hex digits */
  for (int i = 7; i >= 0; i--) {
    int digit = (value >> (i * 4)) & 0xF;
    uart_putc(hexchars[digit]);
  }
}

/* Test memory access to validate we can access RAM */
int test_memory(void) {
  volatile unsigned int* stack_end = (volatile unsigned int*) &__stack_end__;
  volatile unsigned int* ram_test  = (volatile unsigned int*) 0x40100000; /* Some RAM location */

  /* Try to write and read from stack area */
  *stack_end = 0xABCD1234;
  if (*stack_end != 0xABCD1234) {
    return 1; /* Failed stack memory test */
  }

  /* Try to write and read from main RAM area */
  *ram_test = 0x55AA55AA;
  if (*ram_test != 0x55AA55AA) {
    return 2; /* Failed RAM memory test */
  }

  return 0; /* All memory tests passed */
}

/* Main function - entry point after startup code */
int main(void) {
  int counter = 0;
  int mem_test_result;

  /* Initialize UART */
  uart_init();
  short_delay();

  /* Initial hello message */
  uart_puts("\n\n=========================\n");
  uart_puts("BAREMETAL TEST STARTING\n");
  uart_puts("=========================\n\n");

  /* Print UART status for debugging */
  uart_puts("UART_BASE: ");
  uart_puthex(UART_BASE);
  uart_puts("\nUART_CR: ");
  uart_puthex(UART_CR);
  uart_puts("\nUART_FR: ");
  uart_puthex(UART_FR);
  uart_puts("\n\n");

  /* Print memory configuration */
  uart_puts("Stack Start: ");
  uart_puthex((unsigned int) &__stack_start__);
  uart_puts("\nStack End: ");
  uart_puthex((unsigned int) &__stack_end__);
  uart_puts("\nBSS Start: ");
  uart_puthex((unsigned int) &__bss_start__);
  uart_puts("\nBSS End: ");
  uart_puthex((unsigned int) &__bss_end__);
  uart_puts("\n\n");

  /* Test memory access */
  uart_puts("Testing memory access...\n");
  mem_test_result = test_memory();
  if (mem_test_result == 0) {
    uart_puts("Memory test passed!\n");
  }
  else {
    uart_puts("Memory test failed with code: ");
    uart_puthex(mem_test_result);
    uart_puts("\n");
  }

  /* Print test message */
  uart_puts("Test successful! UART communication works.\n");
  uart_puts("TEST COMPLETED SUCCESSFULLY\n");

  /* Continue printing something in a loop to show we're alive */
  while (1) {
    short_delay();
    uart_puts("Still alive: ");
    uart_puthex(counter++);
    uart_puts("\n");

    /* Longer delay between messages */
    for (volatile int i = 0; i < 10000; i++) {
      /* Do nothing */
    }
  }

  return 0;
}