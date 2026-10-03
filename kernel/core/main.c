/* RTR-OS - kernel entry in C. */
#include <stdint.h>

#include "arch.h"
#include "board.h"
#include "console.h"
#include "mmu.h"
#include "page.h"
#include "panic.h"
#include "process.h"
#include "sched.h"
#include "timer.h"

void kernel_main(uint64_t dtb_address) __attribute__((noreturn));

static void print_banner(uint64_t dtb_address, uint64_t counter_hz)
{
    console_put_text("\nRTR-OS - Real-time Raspberry Operating System\n");
    console_put_text("CPU ");
    console_put_dec(arch_core_id());
    console_put_text(" in EL");
    console_put_dec(arch_exception_level());
    console_put_text(", counter at ");
    console_put_dec(counter_hz);
    console_put_text(" Hz, dtb at ");
    console_put_hex(dtb_address);
    console_put_char('\n');
}

void kernel_main(uint64_t dtb_address)
{
    uint64_t counter_hz = arch_counter_hz();

    console_init();
    print_banner(dtb_address, counter_hz);

    if (counter_hz == 0U) {
        kernel_panic("system counter has no time base");
    }

    mmu_init();
    console_put_text("MMU and caches on\n");

#ifdef RTR_FAULT_TEST
    console_put_text("test: writing to kernel code, must halt with an exception\n");
    mmu_fault_test();
    console_put_text("test FAILED: the write was accepted\n");
#endif

    page_init(dtb_address);
    gic_init();
    timer_init();

    install_load();

    /* From here on the kernel only runs when called: by a process or by an interrupt. */
    sched_start();
}
