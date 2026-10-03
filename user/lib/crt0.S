/*
 * RTR-OS - start of every program.
 *
 * The image begins with the header (header.S) the kernel reads to load the
 * program. The kernel enters _start with the process argument in x0 and the
 * stack ready.
 */
#include <rtr/abi.h>

    .section ".text.start", "ax"
    .globl _start
_start:
    mov     x29, #0
    mov     x30, #0
    bl      program_main

    /* A program that returns from program_main has nothing left to do: it only yields the CPU. */
1:  mov     x8, #RTR_SYS_WAIT_PERIOD
    svc     #0
    b       1b
