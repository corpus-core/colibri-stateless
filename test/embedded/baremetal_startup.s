.syntax unified
.cpu cortex-a15
.fpu softvfp
.thumb

.global _start
.global Reset_Handler

/* Define stack end address - using a different area of memory */
.equ  _estack, 0x40000000  /* Use a different memory location to avoid conflicts */

/* Vector table - essential for cortex-a in virt machine */
.section .isr_vector,"a",%progbits
.type  g_pfnVectors, %object
.size  g_pfnVectors, .-g_pfnVectors

g_pfnVectors:
    .word  _estack           /* Top of Stack */
    .word  Reset_Handler     /* Reset Handler */
    .word  0                 /* NMI Handler */
    .word  0                 /* Hard Fault Handler */
    .word  0                 /* Reserved */
    .word  0                 /* Reserved */
    .word  0                 /* Reserved */
    .word  0                 /* Reserved */
    .word  0                 /* Reserved */
    .word  0                 /* Reserved */
    .word  0                 /* Reserved */
    .word  0                 /* SVCall Handler */
    .word  0                 /* Debug Monitor Handler */
    .word  0                 /* Reserved */
    .word  0                 /* PendSV Handler */
    .word  0                 /* SysTick Handler */

.section .text._start
.type  _start, %function

_start:
    /* Jump directly to the Reset_Handler */
    b   Reset_Handler
.size _start, .-_start

.section .text.Reset_Handler
.type  Reset_Handler, %function

Reset_Handler:
    /* Disable interrupts before starting initialization */
    cpsid i

    /* Initialize the stack pointer - direct value for maximum compatibility */
    ldr sp, =_estack
    
    /* Setup CPU mode and enable FPU if available */
    mrs r0, cpsr
    bic r0, r0, #0x1F        /* Clear mode bits */
    orr r0, r0, #0x13        /* Set SVC mode */
    msr cpsr_c, r0
    
    /* Simple delay for hardware initialization */
    mov r0, #0x200000
delay_loop:
    subs r0, r0, #1
    bne  delay_loop
    
    /* Clear BSS section (zero init) */
    ldr r0, =__bss_start__
    ldr r1, =__bss_end__
    mov r2, #0
bss_clear_loop:
    cmp r0, r1
    bge bss_clear_done
    str r2, [r0], #4
    b bss_clear_loop
bss_clear_done:
    
    /* Jump to main function */
    bl  main
    
    /* If main returns, stay in infinite loop */
hang:
    wfe                     /* Wait for event (power efficient) */
    b   hang
.size  Reset_Handler, .-Reset_Handler

/* Define symbols for linker */
.section .bss
.align 3
__bss_start__:
.space 4
__bss_end__:

.end 