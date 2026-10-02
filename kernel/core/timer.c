/* RTR-OS - interrupção periódica do timer e medição do atraso de tratamento. */
#include "timer.h"

#include <stddef.h>

#include "arch.h"
#include "board.h"

static uint64_t period_ticks;
static uint64_t next_instant;
static struct timer_stats stats;

static void reset_window(void)
{
    stats.window_fires = 0U;
    stats.window_min = UINT64_MAX;
    stats.window_max = 0U;
    stats.window_sum = 0U;
}

void timer_start(uint64_t period)
{
    if (period == 0U) {
        return;
    }

    period_ticks = period;
    reset_window();

    next_instant = arch_counter_read() + period_ticks;
    arch_timer_set(next_instant);
    gic_enable_irq(BOARD_IRQ_TIMER);
}

void timer_handle_irq(void)
{
    uint64_t now = arch_counter_read();
    uint64_t delay = (now >= next_instant) ? (now - next_instant) : 0U;

    stats.fires++;
    stats.window_fires++;
    stats.window_sum += delay;
    if (delay < stats.window_min) {
        stats.window_min = delay;
    }
    if (delay > stats.window_max) {
        stats.window_max = delay;
    }
    if (delay > stats.worst) {
        stats.worst = delay;
    }

    /*
     * O próximo instante é sempre o anterior mais um período, nunca "agora
     * mais um período": o atraso deste disparo não se acumula no seguinte.
     * Se o kernel ficou parado por mais de um período, pula os que passaram.
     */
    next_instant += period_ticks;
    if (next_instant <= now) {
        uint64_t missed = ((now - next_instant) / period_ticks) + 1U;

        stats.skipped += missed;
        next_instant += missed * period_ticks;
    }
    arch_timer_set(next_instant);
}

uint64_t timer_periods(void)
{
    uint64_t flags = arch_irq_save();
    uint64_t periods = stats.fires + stats.skipped;

    arch_irq_restore(flags);
    return periods;
}

void timer_read_stats(struct timer_stats *out)
{
    uint64_t flags;

    if (out == NULL) {
        return;
    }

    flags = arch_irq_save();
    *out = stats;
    reset_window();
    arch_irq_restore(flags);
}
