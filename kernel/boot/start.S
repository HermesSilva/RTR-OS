/*
 * RTR-OS - entry point (Raspberry Pi 4, AArch64).
 *
 * The firmware loads kernel8.img at 0x80000 and enters here in EL2 with the
 * device tree address in x0. Only core 0 goes on; the others are parked.
 */

#define BOOT_STACK_SIZE 0x4000

    .section ".text.boot", "ax"
    .globl _start
_start:
    mov     x20, x0                     /* keep the DTB address */

    mrs     x1, mpidr_el1
    and     x1, x1, #3
    cbnz    x1, park

    mrs     x1, CurrentEL
    lsr     x1, x1, #2
    cmp     x1, #2
    b.eq    from_el2
    cmp     x1, #1
    b.eq    in_el1
    b       park                        /* EL3 is not supported */

from_el2:
    mov     x1, #(1 << 31)              /* HCR_EL2.RW: EL1 runs AArch64 */
    msr     hcr_el2, x1

    mrs     x1, cnthctl_el2             /* give EL1 the counter and the physical timer */
    orr     x1, x1, #3
    msr     cnthctl_el2, x1
    msr     cntvoff_el2, xzr

    mov     x1, #0x33ff                 /* no FP/SIMD traps to EL2 */
    msr     cptr_el2, x1

    ldr     x1, =0x30d01800             /* SCTLR_EL1: RES1 + instruction cache, MMU off */
    msr     sctlr_el1, x1

    mov     x1, #0x3c5                  /* EL1h with DAIF masked */
    msr     spsr_el2, x1
    adr     x1, in_el1
    msr     elr_el2, x1
    eret

in_el1:
    msr     SPSel, #1
    ldr     x1, =__boot_stack_top
    mov     sp, x1

    ldr     x1, =__bss_start
    ldr     x2, =__bss_end
1:  cmp     x1, x2
    b.hs    2f
    str     xzr, [x1], #8
    b       1b

2:  ldr     x1, =vectors
    msr     vbar_el1, x1
    isb

    mov     x0, x20
    bl      kernel_main

park:
    wfe
    b       park

    .ltorg

    .section ".bss.boot_stack", "aw", %nobits
    .balign 16
    .skip   BOOT_STACK_SIZE
    .globl __boot_stack_top
__boot_stack_top:
