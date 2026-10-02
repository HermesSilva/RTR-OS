/*
 * RTR-OS - acesso ao processador e a registradores mapeados em memória.
 *
 * Exceção às regras de codificação: este é o único arquivo em que o kernel
 * usa assembly embutido e converte inteiro em ponteiro.
 */
#ifndef RTR_ARCH_H
#define RTR_ARCH_H

#include <stdint.h>

static inline void mmio_write32(uintptr_t address, uint32_t value)
{
    *(volatile uint32_t *)address = value;
}

static inline uint32_t mmio_read32(uintptr_t address)
{
    return *(volatile uint32_t *)address;
}

/* Contador do sistema: o mesmo para os quatro núcleos, movido pelo cristal. */
static inline uint64_t arch_counter_read(void)
{
    uint64_t value;

    __asm__ volatile("isb\n\tmrs %0, cntpct_el0" : "=r"(value));
    return value;
}

static inline uint64_t arch_counter_hz(void)
{
    uint64_t value;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
    return value;
}

/* Programa o timer deste núcleo para interromper quando o contador chegar a `instant`. */
static inline void arch_timer_set(uint64_t instant)
{
    uint64_t enable = 1U;

    __asm__ volatile("msr cntp_cval_el0, %0\n\tmsr cntp_ctl_el0, %1" : : "r"(instant), "r"(enable));
}

static inline uint64_t arch_exception_level(void)
{
    uint64_t value;

    __asm__ volatile("mrs %0, CurrentEL" : "=r"(value));
    return (value >> 2U) & 3U;
}

static inline uint64_t arch_core_id(void)
{
    uint64_t value;

    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(value));
    return value & 0xFFU;
}

static inline void arch_wait_for_event(void)
{
    __asm__ volatile("wfe");
}

static inline void arch_wait_for_interrupt(void)
{
    __asm__ volatile("wfi");
}

static inline void arch_irq_enable(void)
{
    __asm__ volatile("msr daifclr, #2");
}

/* Mascara as interrupções e devolve o estado anterior, para arch_irq_restore. */
static inline uint64_t arch_irq_save(void)
{
    uint64_t flags;

    __asm__ volatile("mrs %0, daif\n\tmsr daifset, #2" : "=r"(flags));
    return flags;
}

static inline void arch_irq_restore(uint64_t flags)
{
    __asm__ volatile("msr daif, %0" : : "r"(flags));
}

/* Instala a tabela de tradução e liga MMU e caches. */
static inline void arch_mmu_enable(uint64_t table, uint64_t tcr, uint64_t mair, uint64_t sctlr)
{
    __asm__ volatile(
        "dsb sy\n\t"
        "msr mair_el1, %[mair]\n\t"
        "msr tcr_el1, %[tcr]\n\t"
        "msr ttbr0_el1, %[table]\n\t"
        "isb\n\t"
        "tlbi vmalle1\n\t"
        "ic iallu\n\t"
        "dsb sy\n\t"
        "isb\n\t"
        "msr sctlr_el1, %[sctlr]\n\t"
        "isb"
        : : [table] "r"(table), [tcr] "r"(tcr), [mair] "r"(mair), [sctlr] "r"(sctlr));
}

#endif
