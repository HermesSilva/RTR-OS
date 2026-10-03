/*
 * RTR-OS - EL1 exception vectors and context switch.
 *
 * The kernel is entered from only two places:
 *
 *   - from a process (EL0), by a system call, a fault or an interrupt;
 *   - from the idle state, by an interrupt.
 *
 * It always runs with interrupts masked and to completion, on the kernel
 * stack, which is reset on every entry. So the kernel never interrupts
 * itself.
 *
 * While a process runs, SP_EL1 points at the end of its trap_frame (the
 * first field of struct task). Entry saves the registers there and exit
 * restores them from there: switching processes is just returning another
 * frame.
 */

#define FRAME_SIZE 272

    /* Save the process state in the frame pointed to by SP_EL1 and move to the kernel stack. */
    .macro enter_from_process
    sub     sp, sp, #FRAME_SIZE
    stp     x0, x1, [sp, #16 * 0]
    stp     x2, x3, [sp, #16 * 1]
    stp     x4, x5, [sp, #16 * 2]
    stp     x6, x7, [sp, #16 * 3]
    stp     x8, x9, [sp, #16 * 4]
    stp     x10, x11, [sp, #16 * 5]
    stp     x12, x13, [sp, #16 * 6]
    stp     x14, x15, [sp, #16 * 7]
    stp     x16, x17, [sp, #16 * 8]
    stp     x18, x19, [sp, #16 * 9]
    stp     x20, x21, [sp, #16 * 10]
    stp     x22, x23, [sp, #16 * 11]
    stp     x24, x25, [sp, #16 * 12]
    stp     x26, x27, [sp, #16 * 13]
    stp     x28, x29, [sp, #16 * 14]
    mrs     x21, elr_el1
    mrs     x22, spsr_el1
    mrs     x23, sp_el0
    stp     x30, x21, [sp, #16 * 15]
    stp     x22, x23, [sp, #16 * 16]
    mov     x0, sp
    ldr     x1, =__boot_stack_top
    mov     sp, x1
    .endm

    .macro unexpected id
    .balign 0x80
    mov     x0, #\id
    b       unexpected_entry
    .endm

    .section ".text", "ax"
    .balign 0x800
    .globl vectors
vectors:
    /* EL1 using SP_EL0 */
    unexpected 0
    unexpected 1
    unexpected 2
    unexpected 3
    /* EL1 using SP_EL1 */
    unexpected 4
    .balign 0x80
    b       idle_irq
    unexpected 6
    unexpected 7
    /* EL0 in AArch64 */
    .balign 0x80
    b       process_sync
    .balign 0x80
    b       process_irq
    unexpected 10
    unexpected 11
    /* EL0 in AArch32 */
    unexpected 12
    unexpected 13
    unexpected 14
    unexpected 15

    .balign 0x80
process_sync:
    enter_from_process
    mrs     x1, esr_el1
    mrs     x2, far_el1
    bl      trap_process_sync           /* returns the frame to resume, or 0 */
    b       arch_dispatch

process_irq:
    enter_from_process
    bl      trap_irq
    b       arch_dispatch

/* Interrupt during the idle state: there is no context to preserve. */
idle_irq:
    ldr     x1, =__boot_stack_top
    mov     sp, x1
    bl      trap_irq
    b       arch_dispatch

/* x0 = frame of the process that gets the CPU, or 0 to go idle. */
    .globl arch_dispatch
arch_dispatch:
    cbz     x0, idle
    mov     sp, x0
    ldp     x22, x23, [sp, #16 * 16]
    ldp     x30, x21, [sp, #16 * 15]
    msr     elr_el1, x21
    msr     spsr_el1, x22
    msr     sp_el0, x23
    ldp     x0, x1, [sp, #16 * 0]
    ldp     x2, x3, [sp, #16 * 1]
    ldp     x4, x5, [sp, #16 * 2]
    ldp     x6, x7, [sp, #16 * 3]
    ldp     x8, x9, [sp, #16 * 4]
    ldp     x10, x11, [sp, #16 * 5]
    ldp     x12, x13, [sp, #16 * 6]
    ldp     x14, x15, [sp, #16 * 7]
    ldp     x16, x17, [sp, #16 * 8]
    ldp     x18, x19, [sp, #16 * 9]
    ldp     x20, x21, [sp, #16 * 10]
    ldp     x22, x23, [sp, #16 * 11]
    ldp     x24, x25, [sp, #16 * 12]
    ldp     x26, x27, [sp, #16 * 13]
    ldp     x28, x29, [sp, #16 * 14]
    add     sp, sp, #FRAME_SIZE
    eret

idle:
    ldr     x1, =__boot_stack_top
    mov     sp, x1
    msr     daifclr, #2
1:  wfi
    b       1b

unexpected_entry:
    mrs     x1, esr_el1
    mrs     x2, elr_el1
    mrs     x3, far_el1
    bl      trap_unexpected             /* does not return */
2:  wfe
    b       2b

    .ltorg
