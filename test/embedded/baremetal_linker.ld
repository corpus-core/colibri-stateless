/* Simple linker script for QEMU tests targeting the virt machine */

ENTRY(_start)

/* 
 * Define memory regions with correct addresses for QEMU virt machine
 * QEMU virt machine uses 0x40000000 as the start of RAM
 * We'll place code at 0x40010000 to leave room for stack
 */
MEMORY
{
  /* Main memory for code & data */
  RAM (rwx)   : ORIGIN = 0x40010000, LENGTH = 0xFFF0000
  /* Reserve 64KB for stack at start of RAM */
  STACK (rw)  : ORIGIN = 0x40000000, LENGTH = 0x10000
}

/* Define symbols required by startup code */
__bss_start__ = 0;
__bss_end__ = 0;

/* Define stack addresses explicitly */
__stack_end__ = ORIGIN(STACK);
__stack_size__ = LENGTH(STACK);
__stack_start__ = ORIGIN(STACK) + __stack_size__;

SECTIONS
{
  /* Place code at the beginning of RAM section after stack */
  . = ORIGIN(RAM);

  /* Vector table and code */
  .text : 
  {
    __text_start__ = .;
    
    KEEP(*(.isr_vector))    /* Vector table first */
    . = ALIGN(4);
    *(.text._start)         /* Startup code */
    *(.text.Reset_Handler)  /* Reset handler */
    *(.text*)               /* All other code */
    *(.rodata*)             /* Read-only data */
    
    . = ALIGN(4);
    __text_end__ = .;
  } >RAM

  /* Initialized data */
  .data : 
  {
    . = ALIGN(4);
    __data_start__ = .;
    *(.data*)
    . = ALIGN(4);
    __data_end__ = .;
  } >RAM

  /* Uninitialized data */
  .bss :
  {
    . = ALIGN(4);
    __bss_start__ = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end__ = .;
  } >RAM

  /* Remove standard libraries - we're not using them */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }
  
  /* Debug information */
  .debug_frame (INFO) : { *(.debug_frame) }
} 