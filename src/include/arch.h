/* RTR-OS - acesso a registradores do processador (AArch64, EL1). */
#ifndef RTR_ARCH_H
#define RTR_ARCH_H

#include <stdint.h>

#define read_sysreg(reg) ({                                 \
    uint64_t _v;                                            \
    __asm__ volatile("mrs %0, " #reg : "=r"(_v));           \
    _v;                                                     \
})

#define write_sysreg(reg, val) do {                         \
    uint64_t _v = (val);                                    \
    __asm__ volatile("msr " #reg ", %0" : : "r"(_v));       \
} while (0)

#define SPSR_EL1H 0x5u                  /* EL1 com SP_EL1, interrupções abertas */

/* Estado salvo em toda entrada no kernel; o layout é espelhado em vectors.S. */
struct trap_frame {
    uint64_t x[31];
    uint64_t elr;
    uint64_t spsr;
    uint64_t reserved;
};

_Static_assert(sizeof(struct trap_frame) == 272, "trap_frame fora de sincronia com vectors.S");

static inline void mmio_write(uintptr_t addr, uint32_t val)
{
    *(volatile uint32_t *)addr = val;
}

static inline uint32_t mmio_read(uintptr_t addr)
{
    return *(volatile uint32_t *)addr;
}

static inline uint64_t irq_save(void)
{
    uint64_t flags = read_sysreg(daif);
    __asm__ volatile("msr daifset, #2");
    return flags;
}

static inline void irq_restore(uint64_t flags)
{
    write_sysreg(daif, flags);
}

static inline void irq_enable(void)
{
    __asm__ volatile("msr daifclr, #2");
}

static inline void cpu_wait_irq(void)
{
    __asm__ volatile("wfi");
}

#endif
