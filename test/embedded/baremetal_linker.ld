/* Simple linker script for QEMU tests targeting the virt machine */

ENTRY(_start)

/* Define memory regions with addresses matching QEMU virt machine */
MEMORY
{
  /* QEMU virt machine places RAM at 0x40000000 */
  RAM (rwx) : ORIGIN = 0x40000000, LENGTH = 1M
}

/* Define symbols required by startup code */
__bss_start__ = 0;
__bss_end__ = 0;

SECTIONS
{
  /* Place everything at the start of RAM */
  . = ORIGIN(RAM);

  /* Vector table and code in RAM (simplifies execution) */
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

  /* Stack area - define a generous space */
  .stack (NOLOAD) :
  {
    . = ALIGN(8);
    __stack_start__ = .;
    . = . + 0x10000;  /* 64KB of stack space */
    __stack_end__ = .;
    . = ALIGN(8);
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