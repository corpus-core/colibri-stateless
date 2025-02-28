.syntax unified
.cpu cortex-a15
.fpu softvfp
.thumb

.global _start
.global Reset_Handler

/* Define stack end address - using a different area of memory */
.equ  _estack, 0x40000000  /* Use a different memory location to avoid conflicts */

.section .text._start
.type  _start, %function

_start:
    /* Jump directly to the Reset_Handler for simplicity */
    b   Reset_Handler
.size _start, .-_start

.section .text.Reset_Handler
.type  Reset_Handler, %function

Reset_Handler:
    /* Initialize the stack pointer - direct value for maximum compatibility */
    ldr sp, =_estack
    
    /* Simple delay for hardware initialization */
    mov r0, #0x100000
    b   delay_loop
    
delay_loop:
    subs r0, r0, #1
    bne  delay_loop
    
    /* Jump to main function */
    bl  main
    
    /* If main returns, stay in infinite loop */
hang:
    b   hang
.size  Reset_Handler, .-Reset_Handler

/* Simple vector table with just the reset handler */
.section .isr_vector,"a",%progbits
.type  g_pfnVectors, %object
.size  g_pfnVectors, .-g_pfnVectors

g_pfnVectors:
    .word  _estack      /* Top of Stack */
    .word  Reset_Handler /* Reset Handler */

.end 