.syntax unified
.cpu cortex-a15
.fpu softvfp
.thumb

.global  Reset_Handler
.global  Default_Handler

/* Define stack end address */
.equ  _estack, 0x20020000  /* end of 128K RAM */

/* Start address for the initialization values of the .data section */
.word  _sidata
/* Start address for the .data section */
.word  _sdata
/* End address for the .data section */
.word  _edata
/* Start address for the .bss section */
.word  _sbss
/* End address for the .bss section */
.word  _ebss

.section  .text.Reset_Handler
.weak  Reset_Handler
.type  Reset_Handler, %function
Reset_Handler:
  /* Initialize the stack pointer */
  ldr   sp, =_estack

  /* Copy the data segment initializers from flash to SRAM */
  movs  r1, #0
  b  LoopCopyDataInit

CopyDataInit:
  ldr  r3, =_sidata
  ldr  r3, [r3, r1]
  str  r3, [r0, r1]
  adds  r1, r1, #4

LoopCopyDataInit:
  ldr  r0, =_sdata
  ldr  r3, =_edata
  adds  r2, r0, r1
  cmp  r2, r3
  bcc  CopyDataInit

  /* Zero fill the bss segment */
  movs  r1, #0
  b  LoopFillZerobss

FillZerobss:
  ldr  r2, =_sbss
  adds  r2, r1
  movs  r3, #0
  str  r3, [r2]
  adds  r1, r1, #4

LoopFillZerobss:
  ldr  r2, =_ebss
  ldr  r3, =_sbss
  adds  r3, r1
  cmp  r3, r2
  bcc  FillZerobss

  /* Call the application's entry point */
  bl  main
  bx  lr
.size  Reset_Handler, .-Reset_Handler

/* Exception Handlers */
.section  .text.Default_Handler,"ax",%progbits
Default_Handler:
  b  Infinite_Loop
  .size  Default_Handler, .-Default_Handler

/* Vector table */
.section  .isr_vector,"a",%progbits
.type  g_pfnVectors, %object
.size  g_pfnVectors, .-g_pfnVectors

g_pfnVectors:
  .word  _estack                   /* Top of Stack */
  .word  Reset_Handler             /* Reset Handler */
  .word  Default_Handler           /* NMI Handler */
  .word  Default_Handler           /* Hard Fault Handler */
  .word  Default_Handler           /* MPU Fault Handler */
  .word  Default_Handler           /* Bus Fault Handler */
  .word  Default_Handler           /* Usage Fault Handler */
  .word  0                         /* Reserved */
  .word  0                         /* Reserved */
  .word  0                         /* Reserved */
  .word  0                         /* Reserved */
  .word  Default_Handler           /* SVCall Handler */
  .word  Default_Handler           /* Debug Monitor Handler */
  .word  0                         /* Reserved */
  .word  Default_Handler           /* PendSV Handler */
  .word  Default_Handler           /* SysTick Handler */

/* External Interrupts (simplified) */
  .word  Default_Handler           /* Window Watchdog interrupt */
  .word  Default_Handler           /* External Line[9:5] interrupts */
  .word  Default_Handler           /* USB OTG FS global interrupt */

Infinite_Loop:
  b  Infinite_Loop

.end