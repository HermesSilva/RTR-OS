/*
 * RTR-OS - access to the processor and to memory-mapped registers.
 *
 * Exception to the coding rules: this is the only file in which the kernel
 * uses inline assembly and converts integers to pointers.
 */
#ifndef RTR_ARCH_H
#define RTR_ARCH_H

#include <stdint.h>

#define ARCH_CACHE_LINE     64UL

/*
 * Process state saved on every kernel entry. The layout is mirrored in
 * kernel/boot/vectors.S.
 */
struct trap_frame {
    uint64_t x[31];
    uint64_t pc;
    uint64_t pstate;
    uint64_t sp;
};

_Static_assert(sizeof(struct trap_frame) == 272, "trap_frame out of sync with vectors.S");

#define ARCH_PSTATE_EL0     0x0UL       /* EL0, interrupts enabled */

/* Hands the CPU to the process owning `frame`, or waits idle if it is NULL. In vectors.S. */
void arch_dispatch(struct trap_frame *frame) __attribute__((noreturn));

static inline void mmio_write32(uintptr_t address, uint32_t value)
{
    *(volatile uint32_t *)address = value;
}

static inline uint32_t mmio_read32(uintptr_t address)
{
    return *(volatile uint32_t *)address;
}

/* Converts a physical address in the kernel identity map into a pointer. */
static inline void *arch_physical_pointer(uint64_t physical)
{
    return (void *)(uintptr_t)physical;
}

/* System counter: shared by all four cores, driven by the crystal. */
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

/* Lets processes read the counter directly, without calling the kernel. */
static inline void arch_counter_allow_user(void)
{
    uint64_t value = 3U;

    __asm__ volatile("msr cntkctl_el1, %0\n\tisb" : : "r"(value));
}

/* Programs this core's timer to interrupt when the counter reaches `instant`. */
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

/* Installs the translation table and turns on the MMU and the caches. */
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

/* Switches the address space in use. The kernel part is the same in every table. */
static inline void arch_mmu_switch(uint64_t table)
{
    __asm__ volatile(
        "msr ttbr0_el1, %0\n\t"
        "isb\n\t"
        "tlbi vmalle1\n\t"
        "dsb sy\n\t"
        "isb"
        : : "r"(table));
}

/* Writes the cached contents of [start, start + size) to memory and drops them from the cache. */
static inline void arch_cache_flush(uint64_t start, uint64_t size)
{
    uint64_t line = start & ~(ARCH_CACHE_LINE - 1UL);
    uint64_t end = start + size;

    while (line < end) {
        __asm__ volatile("dc civac, %0" : : "r"(line));
        line += ARCH_CACHE_LINE;
    }
    __asm__ volatile("dsb sy");
}

/* Drops cached instructions; needed after writing code to memory. */
static inline void arch_instruction_cache_flush(void)
{
    __asm__ volatile("ic iallu\n\tdsb sy\n\tisb");
}

#endif
