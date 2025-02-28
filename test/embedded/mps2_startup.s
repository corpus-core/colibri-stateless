/**
 * Startup code for ARM Cortex-M3 on QEMU MPS2 board
 */
    .syntax unified
    .cpu cortex-m3
    .thumb

/* Global symbols */
    .global Reset_Handler
    .global _start
    .global __StackTop

/* Vector Table */
    .section .vector_table,"a",%progbits
    .type _start, %function
_start:
    .word __StackTop              /* Top of Stack */
    .word Reset_Handler           /* Reset Handler */
    .word 0                       /* NMI Handler */
    .word 0                       /* Hard Fault Handler */
    .word 0                       /* Memory Management Fault Handler */
    .word 0                       /* Bus Fault Handler */
    .word 0                       /* Usage Fault Handler */
    .word 0                       /* Reserved */
    .word 0                       /* Reserved */
    .word 0                       /* Reserved */
    .word 0                       /* Reserved */
    .word 0                       /* SVCall Handler */
    .word 0                       /* Debug Monitor Handler */
    .word 0                       /* Reserved */
    .word 0                       /* PendSV Handler */
    .word 0                       /* SysTick Handler */

    /* External Interrupts */
    .word 0                       /* IRQ0 */
    .word 0                       /* IRQ1 */
    .word 0                       /* IRQ2 */
    .word 0                       /* IRQ3 */
    .word 0                       /* IRQ4 */
    .word 0                       /* IRQ5 */
    .word 0                       /* IRQ6 */
    .word 0                       /* IRQ7 */
    .size _start, .-_start

/* Reset Handler */
    .section .text.Reset_Handler
    .align 2
    .type Reset_Handler, %function
Reset_Handler:
    /* Simple initialization - skip advanced parts for debug */
    
    /* Initialize stack pointer */
    ldr r0, =__StackTop
    mov sp, r0
    
    /* Delay to stabilize */
    movs r0, #0x4000
delay_loop:
    subs r0, r0, #1
    bne delay_loop
    
    /* Jump to main */
    bl main
    
    /* If main returns, loop forever */
hang:
    b hang
    .size Reset_Handler, .-Reset_Handler
    
    .section .stack
    .align 3
    .equ StackSize, 2048
    .space StackSize
    .global __StackTop
__StackTop:
    .size __StackTop, . - __StackTop
    
    .end 