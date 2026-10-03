/* RTR-OS - program library: system calls and utilities. */
#ifndef RTR_USER_H
#define RTR_USER_H

#include <stddef.h>
#include <stdint.h>

#include <rtr/abi.h>

static inline int64_t rtr_syscall(uint64_t number, uint64_t arg0, uint64_t arg1)
{
    register uint64_t x8 __asm__("x8") = number;
    register uint64_t x0 __asm__("x0") = arg0;
    register uint64_t x1 __asm__("x1") = arg1;

    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1) : "memory");
    return (int64_t)x0;
}

/* Completes the current activation; returns at the start of the next period. */
static inline void rtr_wait_period(void)
{
    (void)rtr_syscall(RTR_SYS_WAIT_PERIOD, 0U, 0U);
}

static inline int64_t rtr_region_info(uint64_t index, struct rtr_region *out)
{
    return rtr_syscall(RTR_SYS_REGION_INFO, index, (uint64_t)(uintptr_t)out);
}

static inline int64_t rtr_stats_read(struct rtr_stats *out)
{
    return rtr_syscall(RTR_SYS_STATS_READ, (uint64_t)(uintptr_t)out, 0U);
}

/* Installation calls: boot set only. */
static inline int64_t rtr_syscall3(uint64_t number, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    register uint64_t x8 __asm__("x8") = number;
    register uint64_t x0 __asm__("x0") = arg0;
    register uint64_t x1 __asm__("x1") = arg1;
    register uint64_t x2 __asm__("x2") = arg2;

    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return (int64_t)x0;
}

/* `image` is the program file as read from the card; it is only needed during the call. */
static inline int64_t rtr_process_create(const struct rtr_process_spec *spec,
                                         const void *image, uint64_t image_size)
{
    return rtr_syscall3(RTR_SYS_PROCESS_CREATE, (uint64_t)(uintptr_t)spec,
                        (uint64_t)(uintptr_t)image, image_size);
}

static inline int64_t rtr_task_configure(const struct rtr_process_spec *spec)
{
    return rtr_syscall(RTR_SYS_TASK_CONFIGURE, (uint64_t)(uintptr_t)spec, 0U);
}

static inline int64_t rtr_install_seal(void)
{
    return rtr_syscall(RTR_SYS_INSTALL_SEAL, 0U, 0U);
}

static inline void rtr_reboot(void)
{
    (void)rtr_syscall(RTR_SYS_REBOOT, 0U, 0U);
}

/* The system counter is read directly, without entering the kernel. */
static inline uint64_t rtr_counter(void)
{
    uint64_t value;

    __asm__ volatile("isb\n\tmrs %0, cntpct_el0" : "=r"(value));
    return value;
}

static inline uint64_t rtr_counter_hz(void)
{
    uint64_t value;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
    return value;
}

/* Text output on the kernel debug console. */
void rtr_print(const char *text);
void rtr_print_dec(uint64_t value);
void rtr_print_hex(uint64_t value);

#endif
