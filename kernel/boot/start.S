/*
 * RTR-OS - ponto de entrada (Raspberry Pi 4, AArch64).
 *
 * O firmware carrega kernel8.img em 0x80000 e entra aqui em EL2, com o
 * endereço do device tree em x0. Só o núcleo 0 segue adiante; os demais
 * ficam estacionados.
 */

#define BOOT_STACK_SIZE 0x4000

    .section ".text.boot", "ax"
    .globl _start
_start:
    mov     x20, x0                     /* preserva o endereço do DTB */

    mrs     x1, mpidr_el1
    and     x1, x1, #3
    cbnz    x1, park

    mrs     x1, CurrentEL
    lsr     x1, x1, #2
    cmp     x1, #2
    b.eq    from_el2
    cmp     x1, #1
    b.eq    in_el1
    b       park                        /* EL3 não é suportado */

from_el2:
    mov     x1, #(1 << 31)              /* HCR_EL2.RW: EL1 em AArch64 */
    msr     hcr_el2, x1

    mrs     x1, cnthctl_el2             /* libera o contador e o timer físico para EL1 */
    orr     x1, x1, #3
    msr     cnthctl_el2, x1
    msr     cntvoff_el2, xzr

    mov     x1, #0x33ff                 /* sem traps de FP/SIMD para EL2 */
    msr     cptr_el2, x1

    ldr     x1, =0x30d01800             /* SCTLR_EL1: RES1 + cache de instruções, MMU desligada */
    msr     sctlr_el1, x1

    mov     x1, #0x3c5                  /* EL1h com DAIF mascarado */
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
