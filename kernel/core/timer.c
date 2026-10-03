/* RTR-OS - the core timer in one-shot mode. */
#include "timer.h"

#include <stddef.h>

#include "arch.h"
#include "board.h"

static uint64_t armed_instant = UINT64_MAX;
static struct rtr_duration delay;

void duration_record(struct rtr_duration *duration, uint64_t value, uint64_t instant)
{
    if (duration == NULL) {
        return;
    }

    if ((duration->count == 0U) || (value < duration->minimum)) {
        duration->minimum = value;
    }
    if (value > duration->maximum) {
        duration->maximum = value;
    }
    duration->count++;
    duration->sum += value;

    if (value > duration->worst) {
        duration->worst = value;
        duration->worst_instant = instant;
    }
}

void duration_read(struct rtr_duration *duration, struct rtr_duration *out)
{
    if ((duration == NULL) || (out == NULL)) {
        return;
    }

    *out = *duration;
    duration->count = 0U;
    duration->minimum = 0U;
    duration->maximum = 0U;
    duration->sum = 0U;
}

void timer_init(void)
{
    arch_counter_allow_user();
    gic_enable_irq(BOARD_IRQ_TIMER);
}

void timer_arm(uint64_t instant)
{
    armed_instant = instant;
    arch_timer_set(instant);
}

uint64_t timer_irq_enter(void)
{
    uint64_t now = arch_counter_read();

    if (now >= armed_instant) {
        duration_record(&delay, now - armed_instant, now);
    }
    return now;
}

void timer_read_delay(struct rtr_duration *out)
{
    duration_read(&delay, out);
}
