.syntax unified
.cpu cortex-a15
.fpu softvfp
.thumb

.global _start
.global Reset_Handler
.global __stack_start__
.global __stack_end__ 

/* External symbols from linker script */
.extern __stack_start__
.extern __stack_end__

/* Vector table - essential for cortex-a in virt machine */
.section .isr_vector,"a",%progbits
.type  g_pfnVectors, %object
.size  g_pfnVectors, .-g_pfnVectors

g_pfnVectors:
    .word  __stack_start__   /* Top of Stack - from linker script */
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

    /* Initialize the stack pointer using symbol from linker script */
    ldr sp, =__stack_start__
    
    /* Validate memory and setup CPU mode */
    mov r0, #0               /* Store test value to memory */
    ldr r1, =__stack_end__   /* Bottom of stack address */
    str r0, [r1]             /* Validate we can write to memory */
    
    /* Try several instructions to see if the CPU is alive */
    mov r0, #1
    mov r1, #1
    add r0, r0, r1           /* Simple operation to validate CPU works */
    
    /* Setup CPU mode */
    mrs r0, cpsr
    bic r0, r0, #0x1F        /* Clear mode bits */
    orr r0, r0, #0x13        /* Set SVC mode */
    msr cpsr_c, r0
    
    /* Simple delay for hardware initialization */
    mov r0, #0x10000         /* Use a smaller delay to get output faster */
delay_loop:
    subs r0, r0, #1
    bne  delay_loop
    
    /* Skip BSS clear for now to simplify debugging */
    
    /* Jump to main function */
    bl  main
    
    /* If main returns, stay in infinite loop */
hang:
    b   hang                 /* Simple infinite loop */
.size  Reset_Handler, .-Reset_Handler

/* Define symbols for linker */
.section .bss
.align 3
.space 4

.end 