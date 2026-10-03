/*
 * RTR-OS - demonstration and test program.
 *
 * The same binary serves three processes, depending on the argument:
 *
 *   0  reports the system statistics on the console once per period;
 *   1  never yields the CPU, to be contained by the time limit;
 *   2  writes to kernel memory, to be terminated by a fault.
 */
#include <rtr/user.h>

#define MODE_REPORT     0U
#define MODE_RUNAWAY    1U
#define MODE_FAULT      2U

#define KERNEL_ADDRESS  0x80000UL
#define NANOS           1000000000UL
#define PERCENT_X10     1000UL

int program_main(uint64_t mode);

static struct rtr_stats stats;
static uint64_t last_idle;
static uint64_t last_instant;

static const char *state_name(uint32_t state)
{
    switch (state) {
    case RTR_TASK_WAITING:   return "waiting";
    case RTR_TASK_READY:     return "ready";
    case RTR_TASK_RUNNING:   return "running";
    case RTR_TASK_THROTTLED: return "throttled";
    case RTR_TASK_FAULTED:   return "faulted";
    default:                 return "-";
    }
}

static uint64_t to_nanos(uint64_t ticks)
{
    return (ticks * NANOS) / stats.counter_hz;
}

static void report(void)
{
    uint64_t elapsed;
    uint64_t idle_x10;
    uint64_t average;

    if (rtr_stats_read(&stats) != RTR_OK) {
        rtr_print("report: statistics read refused\n");
        return;
    }

    elapsed = stats.instant - last_instant;
    idle_x10 = (elapsed != 0U) ? (((stats.idle_total - last_idle) * PERCENT_X10) / elapsed) : 0U;
    average = (stats.timer_delay.count != 0U) ? (stats.timer_delay.sum / stats.timer_delay.count) : 0U;
    last_instant = stats.instant;
    last_idle = stats.idle_total;

    rtr_print("\n[");
    rtr_print_dec((stats.instant - stats.boot_instant) / stats.counter_hz);
    rtr_print(" s] idle ");
    rtr_print_dec(idle_x10 / 10U);
    rtr_print(".");
    rtr_print_dec(idle_x10 % 10U);
    rtr_print("% | timer delay in ns: min ");
    rtr_print_dec(to_nanos(stats.timer_delay.minimum));
    rtr_print(", avg ");
    rtr_print_dec(to_nanos(average));
    rtr_print(", max ");
    rtr_print_dec(to_nanos(stats.timer_delay.maximum));
    rtr_print(", worst ");
    rtr_print_dec(to_nanos(stats.timer_delay.worst));
    rtr_print(" | switches ");
    rtr_print_dec(stats.context_switches);
    rtr_print(" | syscalls ");
    rtr_print_dec(stats.system_calls);
    rtr_print(" | unhandled irqs ");
    rtr_print_dec(stats.interrupts_unhandled);
    rtr_print("\n");

    for (uint32_t i = 0U; (i < stats.task_count) && (i < RTR_STATS_TASKS_MAX); i++) {
        const struct rtr_task_stats *task = &stats.tasks[i];

        rtr_print("  ");
        rtr_print(task->name);
        rtr_print(": ");
        rtr_print(state_name(task->state));
        rtr_print(", activations ");
        rtr_print_dec(task->activations);
        rtr_print(", completed ");
        rtr_print_dec(task->completions);
        rtr_print(", overruns ");
        rtr_print_dec(task->overruns);
        rtr_print(", unfinished ");
        rtr_print_dec(task->unfinished);
        rtr_print(", max cpu per period ");
        rtr_print_dec(to_nanos(task->cpu_period_max) / 1000U);
        rtr_print(" us\n");
    }
}

int program_main(uint64_t mode)
{
    if (mode == MODE_RUNAWAY) {
        for (;;) {
        }
    }

    if (mode == MODE_FAULT) {
        volatile uint64_t *kernel = (volatile uint64_t *)KERNEL_ADDRESS;

        *kernel = 0U;
        rtr_print("intruder: the write to kernel memory was ACCEPTED\n");
        return 1;
    }

    for (;;) {
        report();
        rtr_wait_period();
    }
}
