/**
 * Linker script for ARM Cortex-M3 on QEMU MPS2 board
 */

ENTRY(Reset_Handler)

/* Memory layout for MPS2 */
MEMORY
{
  FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 512K
  RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 512K
}

/* Stack size */
__stack_size__ = 2048;

SECTIONS
{
    /* The vector table goes at the start of flash */
    .vector_table :
    {
        . = ALIGN(4);
        KEEP(*(.vector_table))
        . = ALIGN(4);
    } > FLASH

    /* The program code */
    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    /* Read-write initialized data */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    /* Uninitialized data */
    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /* Stack space */
    .stack (NOLOAD):
    {
        . = ALIGN(8);
        . = . + __stack_size__;
        . = ALIGN(8);
    } > RAM

    /* Discard debug sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.ARM.attributes)
    }
}

/* Compute the top of stack address */
__StackTop = ORIGIN(RAM) + LENGTH(RAM);
__StackLimit = __StackTop - __stack_size__; 