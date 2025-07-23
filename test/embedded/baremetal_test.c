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
 * Bare-metal test for QEMU MPS2 board (ARM Cortex-M3)
 *
 * This test is designed for the QEMU MPS2-AN385 machine model,
 * which simulates an ARM Cortex-M3 processor.
 */

/* UART registers for MPS2 board */
#define UART0_BASE 0x40004000 /* UART0 base address in MPS2 board */
#define UART_DR    (*(volatile unsigned int*) (UART0_BASE + 0x000))
#define UART_STATE (*(volatile unsigned int*) (UART0_BASE + 0x004))
#define UART_CTRL  (*(volatile unsigned int*) (UART0_BASE + 0x008))

/* UART state register bits */
#define UART_STATE_TXFULL (1 << 0)

/* Simple delay function */
static void delay(int count) {
  volatile int i;
  for (i = 0; i < count; i++) {
    /* Do nothing */
  }
}

/* Initialize UART */
void uart_init(void) {
  /* Enable UART TX */
  UART_CTRL = 1;
}

/* Send a character to UART */
void uart_putc(char c) {
  /* Wait for UART to be ready */
  while (UART_STATE & UART_STATE_TXFULL) {
    /* Wait until not full */
  }

  /* Write character */
  UART_DR = c;

  /* If newline, also send carriage return */
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

/* Print a hexadecimal number */
void uart_puthex(unsigned int val) {
  const char hexchars[] = "0123456789ABCDEF";
  int        i;

  uart_puts("0x");
  for (i = 7; i >= 0; i--) {
    uart_putc(hexchars[(val >> (i * 4)) & 0xF]);
  }
}

/**
 * Main function - entry point for the program
 */
int main(void) {
  int i = 0;

  /* Initialize UART */
  uart_init();

  /* Print welcome message */
  uart_puts("\n\n===========================\n");
  uart_puts("MPS2 BAREMETAL TEST STARTING\n");
  uart_puts("===========================\n\n");

  /* Print UART info */
  uart_puts("UART Base: ");
  uart_puthex(UART0_BASE);
  uart_puts("\nUART State: ");
  uart_puthex(UART_STATE);
  uart_puts("\n\n");

  /* Print success message */
  uart_puts("Test successful! UART communication works.\n");
  uart_puts("TEST COMPLETED SUCCESSFULLY\n\n");

  /* Keep printing something to show we're alive */
  while (1) {
    delay(1000000);
    uart_puts("Heartbeat: ");
    uart_puthex(i++);
    uart_puts("\n");
  }

  return 0;
}