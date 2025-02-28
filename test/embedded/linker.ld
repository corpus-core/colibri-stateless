MEMORY
{
    RAM : ORIGIN = 0x00000000, LENGTH = 512K
}

SECTIONS
{
    .text :
    {
        *(.vectors)    /* Vector table from startup.s */
        *(.text)       /* Code */
        *(.rodata)     /* Read-only data */
    } > RAM

    .data : { *(.data) } > RAM   /* Initialized data */
    .bss  : { *(.bss)  } > RAM   /* Uninitialized data */
}