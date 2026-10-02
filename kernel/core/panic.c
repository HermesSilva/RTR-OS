/* RTR-OS - parada do sistema por falha irrecuperável do kernel. */
#include "panic.h"

#include "arch.h"
#include "console.h"

/* Um dos dois laços sem fim permitidos no kernel; o outro é o laço principal. */
static void halt(void) __attribute__((noreturn));

static void halt(void)
{
    for (;;) {
        arch_wait_for_event();
    }
}

void kernel_panic(const char *reason)
{
    console_put_text("\n*** RTR-OS PANIC: ");
    console_put_text(reason);
    console_put_char('\n');
    halt();
}

void trap_unexpected(uint64_t vector, uint64_t esr, uint64_t elr, uint64_t far)
{
    console_put_text("\n*** RTR-OS PANIC: excecao inesperada\n  vetor ");
    console_put_dec(vector);
    console_put_text("\n  esr   ");
    console_put_hex(esr);
    console_put_text("\n  elr   ");
    console_put_hex(elr);
    console_put_text("\n  far   ");
    console_put_hex(far);
    console_put_char('\n');
    halt();
}
