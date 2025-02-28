/* Simple linker script for QEMU tests */

ENTRY(_start)

/* Define memory regions with different addresses than used in main tests */
MEMORY
{
  RAM (rwx) : ORIGIN = 0x40000000, LENGTH = 1M
}

SECTIONS
{
  /* Vector table and code in RAM (simplifies execution) */
  .text :
  {
    KEEP(*(.isr_vector))  /* Vector table first */
    *(.text._start)       /* Startup code */
    *(.text.Reset_Handler)/* Reset handler */
    *(.text*)             /* All other code */
    *(.rodata*)           /* Read-only data */
    . = ALIGN(4);
  } >RAM

  /* Initialized data */
  .data :
  {
    . = ALIGN(4);
    *(.data*)
    . = ALIGN(4);
  } >RAM

  /* Uninitialized data */
  .bss :
  {
    . = ALIGN(4);
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
  } >RAM

  /* Stack area - define a generous space */
  .stack (NOLOAD) :
  {
    . = ALIGN(8);
    . = . + 0x10000;  /* 64KB of stack space */
    . = ALIGN(8);
  } >RAM

  /* Remove standard libraries - we're not using them */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }
} 