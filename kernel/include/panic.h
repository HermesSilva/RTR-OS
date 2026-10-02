/* RTR-OS - parada do sistema por falha irrecuperável do kernel. */
#ifndef RTR_PANIC_H
#define RTR_PANIC_H

#include <stdint.h>

void kernel_panic(const char *reason) __attribute__((noreturn));

/* Chamada por vectors.S para qualquer exceção que o kernel não espera. */
void trap_unexpected(uint64_t vector, uint64_t esr, uint64_t elr, uint64_t far)
    __attribute__((noreturn));

#endif
