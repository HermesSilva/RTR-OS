/* RTR-OS - system halt on an unrecoverable kernel failure. */
#include "panic.h"

#include "arch.h"
#include "console.h"

/* One of the two endless loops allowed in the kernel; the other is the idle loop. */
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
    console_put_text("\n*** RTR-OS PANIC: unexpected exception\n  vector ");
    console_put_dec(vector);
    console_put_text("\n  esr    ");
    console_put_hex(esr);
    console_put_text("\n  elr    ");
    console_put_hex(elr);
    console_put_text("\n  far    ");
    console_put_hex(far);
    console_put_char('\n');
    halt();
}
