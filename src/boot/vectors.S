/*
 * RTR-OS - vetores de exceção de EL1.
 *
 * Toda entrada no kernel salva um struct trap_frame na pilha da tarefa
 * interrompida. O tratador em C devolve o frame a restaurar, que pode ser
 * o de outra tarefa: a troca de contexto é só isso.
 */

#define FRAME_SIZE 272                  /* sizeof(struct trap_frame) */

    .macro kernel_entry
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
    stp     x30, x21, [sp, #16 * 15]
    str     x22, [sp, #16 * 16]
    .endm

    .macro vector label
    .balign 0x80
    b       \label
    .endm

    /* Vetor que o kernel não espera receber: despeja o estado e para. */
    .macro bad_vector id
    .balign 0x80
    kernel_entry
    mov     x0, sp
    mov     x1, #\id
    mrs     x2, esr_el1
    b       trap_bad
    .endm

    .section ".text", "ax"
    .balign 0x800
    .globl vectors
vectors:
    /* EL1 usando SP_EL0 (não usado) */
    bad_vector 0
    bad_vector 1
    bad_vector 2
    bad_vector 3
    /* EL1 usando SP_EL1: o caso normal */
    vector  el1_sync
    vector  el1_irq
    bad_vector 6
    bad_vector 7
    /* EL0 em AArch64 (ainda não há modo usuário) */
    bad_vector 8
    bad_vector 9
    bad_vector 10
    bad_vector 11
    /* EL0 em AArch32 */
    bad_vector 12
    bad_vector 13
    bad_vector 14
    bad_vector 15

    .balign 0x80
el1_sync:
    kernel_entry
    mov     x0, sp
    mrs     x1, esr_el1
    bl      trap_sync
    b       trap_return

el1_irq:
    kernel_entry
    mov     x0, sp
    bl      trap_irq

/* x0 = frame a restaurar */
trap_return:
    mov     sp, x0
    ldr     x22, [sp, #16 * 16]
    ldp     x30, x21, [sp, #16 * 15]
    msr     elr_el1, x21
    msr     spsr_el1, x22
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
