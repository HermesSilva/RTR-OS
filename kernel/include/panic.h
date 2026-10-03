/* RTR-OS - system halt on an unrecoverable kernel failure. */
#ifndef RTR_PANIC_H
#define RTR_PANIC_H

#include <stdint.h>

void kernel_panic(const char *reason) __attribute__((noreturn));

/* Called by vectors.S for any exception the kernel does not expect. */
void trap_unexpected(uint64_t vector, uint64_t esr, uint64_t elr, uint64_t far)
    __attribute__((noreturn));

#endif
