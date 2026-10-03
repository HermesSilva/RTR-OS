/* RTR-OS - tasks and the shared-core scheduler. */
#include "sched.h"

#include <stddef.h>

#include "console.h"
#include "memory.h"
#include "page.h"
#include "timer.h"

#define PPM                 1000000UL
#define START_DELAY_DIVISOR 100UL       /* the first activation happens 10 ms after startup */

extern const uint8_t image_text_start[];
extern const uint8_t image_end[];

static struct task tasks[SCHED_TASKS_MAX];
static uint32_t task_count;
static struct task *current;
static bool sealed;

static uint64_t counter_hz;
static uint64_t boot_instant;
static uint64_t dispatch_instant;       /* when `current` (or idle) got the CPU */
static uint64_t idle_total;
static uint64_t context_switches;
static uint64_t interrupts;
static uint64_t interrupts_unhandled;
static uint64_t system_calls;

static uint64_t load_declared_ppm;
static uint64_t load_effective_ppm;
static bool scaling_applied;

static uint64_t load_ppm(uint64_t limit, uint64_t period)
{
    /* Rounds up, so as never to promise more than there is. */
    return ((limit * PPM) + period - 1U) / period;
}

struct task *sched_task_alloc(void)
{
    struct task *task;

    if (sealed || (task_count >= SCHED_TASKS_MAX)) {
        return NULL;
    }

    task = &tasks[task_count];
    task_count++;
    (void)memset(task, 0, sizeof(*task));
    task->state = RTR_TASK_WAITING;
    task->next_activation = UINT64_MAX;     /* never, until the seal */
    return task;
}

struct task *sched_current(void)
{
    return current;
}

struct task *sched_boot_task(void)
{
    return (task_count != 0U) ? &tasks[0] : NULL;
}

bool sched_sealed(void)
{
    return sealed;
}

bool sched_configure(struct task *task, uint32_t priority, uint64_t period,
                     uint64_t limit, uint64_t floor)
{
    if (sealed || (task == NULL) || (period == 0U) || (limit == 0U) || (limit > period)) {
        return false;
    }
    task->priority = priority;
    task->period = period;
    task->limit_declared = limit;
    task->limit_effective = limit;
    task->limit_floor = floor;
    return true;
}

struct trap_frame *sched_resume(void)
{
    return (current != NULL) ? &current->frame : NULL;
}

void sched_count_interrupt(bool handled)
{
    interrupts++;
    if (!handled) {
        interrupts_unhandled++;
    }
}

void sched_count_system_call(void)
{
    system_calls++;
}

/*
 * Load scaling (D11b, D11c): if the limits add up to more than the ceiling,
 * all of them are reduced in the same proportion. A system task does not go
 * below its floor; what the floor preserves is taken from the others.
 */
static void apply_scaling(void)
{
    bool pinned[SCHED_TASKS_MAX] = { false };
    uint64_t declared = 0U;
    uint64_t pinned_ppm = 0U;
    uint64_t free_ppm = 0U;
    uint64_t available;

    for (uint32_t i = 0U; i < task_count; i++) {
        declared += load_ppm(tasks[i].limit_declared, tasks[i].period);
        tasks[i].limit_effective = tasks[i].limit_declared;
    }
    load_declared_ppm = declared;
    load_effective_ppm = declared;

    if (declared <= SCHED_LOAD_CEILING_PPM) {
        return;
    }
    scaling_applied = true;

    for (uint32_t i = 0U; i < task_count; i++) {
        struct task *task = &tasks[i];
        uint64_t scaled = (task->limit_declared * SCHED_LOAD_CEILING_PPM) / declared;

        if (task->system && (scaled < task->limit_floor)) {
            pinned[i] = true;
            task->limit_effective = (task->limit_floor < task->limit_declared)
                                        ? task->limit_floor : task->limit_declared;
            pinned_ppm += load_ppm(task->limit_effective, task->period);
        } else {
            free_ppm += load_ppm(task->limit_declared, task->period);
        }
    }

    available = (SCHED_LOAD_CEILING_PPM > pinned_ppm) ? (SCHED_LOAD_CEILING_PPM - pinned_ppm) : 0U;
    load_effective_ppm = pinned_ppm;

    for (uint32_t i = 0U; i < task_count; i++) {
        struct task *task = &tasks[i];

        if (!pinned[i] && (free_ppm != 0U)) {
            task->limit_effective = (task->limit_declared * available) / free_ppm;
            if (task->limit_effective == 0U) {
                task->limit_effective = 1U;
            }
            load_effective_ppm += load_ppm(task->limit_effective, task->period);
        }
    }
}

static uint64_t ticks_to_micros(uint64_t ticks)
{
    return (ticks * PPM) / counter_hz;
}

static void print_tasks(void)
{
    console_put_text("installation sealed; tasks:\n");
    for (uint32_t i = 0U; i < task_count; i++) {
        const struct task *task = &tasks[i];

        console_put_text("  ");
        console_put_text(task->name);
        console_put_text(task->realtime ? ": real-time, priority " : ": priority ");
        console_put_dec(task->priority);
        console_put_text(", period ");
        console_put_dec(ticks_to_micros(task->period));
        console_put_text(" us, limit ");
        console_put_dec(ticks_to_micros(task->limit_effective));
        console_put_text(" us");
        if (task->limit_effective != task->limit_declared) {
            console_put_text(" (declared ");
            console_put_dec(ticks_to_micros(task->limit_declared));
            console_put_text(" us)");
        }
        console_put_char('\n');
    }

    console_put_text("declared load ");
    console_put_dec(load_declared_ppm / 10000U);
    console_put_text("%, effective ");
    console_put_dec(load_effective_ppm / 10000U);
    console_put_text(scaling_applied ? "%, scaled down\n" : "%, no scaling\n");
}

/* Scheduling order: the real-time class first, then the priority within the class. */
static uint32_t rank(const struct task *task)
{
    return (task->realtime ? (1U << 8U) : 0U) | (task->priority & 0xFFU);
}

/* Opens a new period for the task: new time limit and new accounting. */
static void activate(struct task *task, uint64_t now)
{
    uint64_t late = now - task->next_activation;

    /* The kernel was stalled for more than a period: skip the ones that passed. */
    if (late >= task->period) {
        uint64_t missed = late / task->period;

        task->skipped += missed;
        task->next_activation += missed * task->period;
    }

    /* The previous activation had time left and still was not completed. */
    if (task->state == RTR_TASK_READY) {
        task->unfinished++;
    }
    if (task->used_in_period > task->cpu_period_max) {
        task->cpu_period_max = task->used_in_period;
    }

    task->used_in_period = 0U;
    task->activation_instant = task->next_activation;
    task->next_activation += task->period;
    task->remaining = task->limit_effective;
    task->state = RTR_TASK_READY;
    task->activations++;
}

/*
 * Heart of the scheduler: charges the time used by whoever was running,
 * opens the periods that have arrived, picks the ready task with the
 * highest priority and arms the timer for the next event (an activation or
 * the end of a limit).
 */
static struct trap_frame *reschedule(uint64_t now)
{
    struct task *next = NULL;
    uint64_t event = UINT64_MAX;
    uint64_t ran = now - dispatch_instant;

    if (current == NULL) {
        idle_total += ran;
    } else {
        current->cpu_total += ran;
        current->used_in_period += ran;

        if (current->state == RTR_TASK_RUNNING) {
            if (ran >= current->remaining) {
                current->remaining = 0U;
                current->state = RTR_TASK_THROTTLED;
                current->overruns++;
            } else {
                current->remaining -= ran;
                current->state = RTR_TASK_READY;
            }
        }
    }

    for (uint32_t i = 0U; i < task_count; i++) {
        struct task *task = &tasks[i];

        if (task->state == RTR_TASK_FAULTED) {
            continue;
        }
        if (now >= task->next_activation) {
            activate(task, now);
        }
        if ((task->state == RTR_TASK_READY) && ((next == NULL) || (rank(task) > rank(next)))) {
            next = task;
        }
        if (task->next_activation < event) {
            event = task->next_activation;
        }
    }

    if (next != NULL) {
        if ((now + next->remaining) < event) {
            event = now + next->remaining;
        }
        next->state = RTR_TASK_RUNNING;
    }
    timer_arm(event);

    if (next != current) {
        context_switches++;
        if (next != NULL) {
            arch_mmu_switch(next->space);
        }
    }

    current = next;
    dispatch_instant = now;
    return sched_resume();
}

struct trap_frame *sched_timer_irq(uint64_t now)
{
    return reschedule(now);
}

struct trap_frame *sched_wait_period(void)
{
    uint64_t now = arch_counter_read();

    if (current != NULL) {
        current->state = RTR_TASK_WAITING;
        current->completions++;
        duration_record(&current->response, now - current->activation_instant, now);
    }
    return reschedule(now);
}

struct trap_frame *sched_fault(uint64_t syndrome, uint64_t address)
{
    uint64_t now = arch_counter_read();

    if (current != NULL) {
        current->state = RTR_TASK_FAULTED;
        current->fault_syndrome = syndrome;
        current->fault_address = address;
        current->fault_pc = current->frame.pc;

        console_put_text("process ");
        console_put_text(current->name);
        console_put_text(" terminated by fault: esr ");
        console_put_hex(syndrome);
        console_put_text(", address ");
        console_put_hex(address);
        console_put_text(", pc ");
        console_put_hex(current->frame.pc);
        console_put_char('\n');
    }
    return reschedule(now);
}

/* Gives every task still without an activation the same first instant: the worst case for the scheduler. */
static uint64_t schedule_first_activation(uint64_t now)
{
    uint64_t first = now + (counter_hz / START_DELAY_DIVISOR);

    for (uint32_t i = 0U; i < task_count; i++) {
        if (tasks[i].next_activation == UINT64_MAX) {
            tasks[i].next_activation = first;
        }
    }
    return first;
}

void sched_start(void)
{
    uint64_t now;
    uint64_t first;

    counter_hz = arch_counter_hz();
    now = arch_counter_read();

    /* Only the boot set runs for now; the installation it describes is sealed later. */
    for (uint32_t i = 0U; i < task_count; i++) {
        tasks[i].limit_effective = tasks[i].limit_declared;
    }
    first = schedule_first_activation(now);

    boot_instant = now;
    dispatch_instant = now;
    current = NULL;
    timer_arm(first);

    arch_dispatch(NULL);
}

void sched_seal(void)
{
    if (sealed) {
        return;
    }
    sealed = true;
    apply_scaling();
    print_tasks();
    (void)schedule_first_activation(arch_counter_read());
}

static void copy_name(char *to, const char *from)
{
    for (uint32_t i = 0U; i < RTR_NAME_MAX; i++) {
        to[i] = from[i];
    }
    to[RTR_NAME_MAX - 1U] = '\0';
}

void sched_read_stats(struct rtr_stats *out)
{
    uint64_t process_bytes = 0U;
    uint64_t skipped = 0U;
    uint64_t image_bytes = (uint64_t)(uintptr_t)image_end - (uint64_t)(uintptr_t)image_text_start;

    if (out == NULL) {
        return;
    }
    (void)memset(out, 0, sizeof(*out));

    for (uint32_t i = 0U; i < task_count; i++) {
        struct task *task = &tasks[i];
        struct rtr_task_stats *stats = &out->tasks[i];

        copy_name(stats->name, task->name);
        stats->state = task->state;
        stats->priority = task->priority;
        stats->realtime = task->realtime ? 1U : 0U;
        stats->period = task->period;
        stats->limit_declared = task->limit_declared;
        stats->limit_effective = task->limit_effective;
        stats->activations = task->activations;
        stats->completions = task->completions;
        stats->overruns = task->overruns;
        stats->unfinished = task->unfinished;
        stats->cpu_total = task->cpu_total;
        stats->cpu_period_max = task->cpu_period_max;
        duration_read(&task->response, &stats->response);
        stats->fault_syndrome = task->fault_syndrome;
        stats->fault_address = task->fault_address;
        stats->fault_pc = task->fault_pc;

        process_bytes += task->memory_bytes;
        skipped += task->skipped;
    }

    out->instant = arch_counter_read();
    out->counter_hz = counter_hz;
    out->boot_instant = boot_instant;
    out->idle_total = idle_total;
    out->context_switches = context_switches;
    out->interrupts = interrupts;
    out->interrupts_unhandled = interrupts_unhandled;
    out->system_calls = system_calls;
    out->timer_skipped = skipped;
    timer_read_delay(&out->timer_delay);
    out->memory_total = 0U;             /* not read from the device tree yet */
    out->memory_processes = process_bytes;
    out->memory_kernel = image_bytes + page_bytes_used() - process_bytes;
    out->load_declared_ppm = load_declared_ppm;
    out->load_effective_ppm = load_effective_ppm;
    out->scaling_applied = scaling_applied ? 1U : 0U;
    out->task_count = task_count;
    out->console_dropped = console_dropped();
}
