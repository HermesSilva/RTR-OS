/*
 * RTR-OS - the core timer in one-shot mode.
 *
 * There is no periodic tick: the scheduler programs the next instant it
 * cares about, and the core is not interrupted before that.
 */
#ifndef RTR_TIMER_H
#define RTR_TIMER_H

#include <stdint.h>

#include "rtr/abi.h"

void timer_init(void);

/* Programs the next interrupt for the absolute instant `instant`. */
void timer_arm(uint64_t instant);

/*
 * Called on entry to the timer interrupt handler. Returns the current
 * instant and records the delay relative to the programmed instant.
 */
uint64_t timer_irq_enter(void) __attribute__((warn_unused_result));

/* Copies the delay measurement and restarts its window. */
void timer_read_delay(struct rtr_duration *out);

/* Records `value` in a duration: window, worst case and instant of the worst case. */
void duration_record(struct rtr_duration *duration, uint64_t value, uint64_t instant);

/* Copies a duration and restarts its window, keeping the worst case. */
void duration_read(struct rtr_duration *duration, struct rtr_duration *out);

#endif
