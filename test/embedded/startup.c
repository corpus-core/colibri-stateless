#include <stdint.h>

// Minimal vector table for Cortex-M
__attribute__((section(".vectors"))) void (*const vector_table[])(void) = {
    0,                     // Stack pointer (will be set by linker script)
    (void (*)(void)) main, // Reset handler (points to main)
};

// Startup code
void _start(void) {
  // Call main directly
  main();

  // Should never return
  while (1);
}