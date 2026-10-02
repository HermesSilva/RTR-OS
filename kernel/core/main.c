/* RTR-OS - entrada do kernel em C. */
#include <stdint.h>

#include "arch.h"
#include "board.h"
#include "console.h"
#include "mmu.h"
#include "panic.h"
#include "timer.h"
#include "trap.h"

#define TICK_HZ             1000U       /* interrupções do timer por segundo */
#define NANOS_PER_SECOND    1000000000U

void kernel_main(uint64_t dtb_address) __attribute__((noreturn));

static void print_banner(uint64_t dtb_address, uint64_t counter_hz)
{
    console_put_text("\nRTR-OS - Real-time Raspberry Operating System\n");
    console_put_text("CPU ");
    console_put_dec(arch_core_id());
    console_put_text(" em EL");
    console_put_dec(arch_exception_level());
    console_put_text(", contador a ");
    console_put_dec(counter_hz);
    console_put_text(" Hz, dtb em ");
    console_put_hex(dtb_address);
    console_put_char('\n');
}

/* Os atrasos são pequenos, então a multiplicação não estoura 64 bits. */
static uint64_t ticks_to_nanos(uint64_t ticks, uint64_t counter_hz)
{
    return (ticks * NANOS_PER_SECOND) / counter_hz;
}

static void print_report(const struct timer_stats *stats, uint64_t counter_hz)
{
    uint64_t average = (stats->window_fires != 0U) ? (stats->window_sum / stats->window_fires) : 0U;
    uint64_t minimum = (stats->window_fires != 0U) ? stats->window_min : 0U;

    console_put_text("tempo ");
    console_put_dec((stats->fires + stats->skipped) / TICK_HZ);
    console_put_text(" s | disparos ");
    console_put_dec(stats->window_fires);
    console_put_text(" | atraso em ns: min ");
    console_put_dec(ticks_to_nanos(minimum, counter_hz));
    console_put_text(", med ");
    console_put_dec(ticks_to_nanos(average, counter_hz));
    console_put_text(", max ");
    console_put_dec(ticks_to_nanos(stats->window_max, counter_hz));
    console_put_text(", pior ");
    console_put_dec(ticks_to_nanos(stats->worst, counter_hz));
    console_put_text(" | pulados ");
    console_put_dec(stats->skipped);
    console_put_text(" | irqs sem tratador ");
    console_put_dec(trap_unhandled_irqs());
    console_put_text(" | console perdeu ");
    console_put_dec(console_dropped());
    console_put_char('\n');
}

void kernel_main(uint64_t dtb_address)
{
    uint64_t counter_hz = arch_counter_hz();
    uint64_t periods_reported = 0U;

    console_init();
    print_banner(dtb_address, counter_hz);

    if (counter_hz < TICK_HZ) {
        kernel_panic("contador do sistema sem base de tempo");
    }

    mmu_init();
    console_put_text("MMU e caches ligados\n");

#ifdef RTR_FAULT_TEST
    console_put_text("ensaio: gravando no codigo do kernel, deve parar com excecao\n");
    mmu_fault_test();
    console_put_text("ensaio FALHOU: a gravacao foi aceita\n");
#endif

    gic_init();
    timer_start(counter_hz / TICK_HZ);
    arch_irq_enable();
    console_put_text("timer a 1000 interrupcoes por segundo\n");

    /*
     * Laço principal: dorme até a próxima interrupção e, a cada segundo de
     * disparos, relata o atraso de tratamento medido pelo contador.
     */
    for (;;) {
        arch_wait_for_interrupt();

        if ((timer_periods() - periods_reported) >= TICK_HZ) {
            struct timer_stats stats;

            timer_read_stats(&stats);
            periods_reported += TICK_HZ;
            print_report(&stats, counter_hz);
        }
    }
}
