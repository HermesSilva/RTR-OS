/*
 * RTR-OS - tasks and the shared-core scheduler.
 *
 * Fixed priority with preemption: the ready task with the highest priority
 * runs (a larger number means a higher priority). Every task is periodic
 * and has a CPU time limit per period; one that exceeds it is suspended
 * until the next period. If the limits add up to more than the ceiling,
 * they are scaled down.
 */
#ifndef RTR_SCHED_H
#define RTR_SCHED_H

#include <stdbool.h>
#include <stdint.h>

#include "arch.h"
#include "rtr/abi.h"

#define SCHED_TASKS_MAX         RTR_STATS_TASKS_MAX
#define SCHED_REGIONS_MAX       4U

/* Fraction of the CPU that may be promised; the rest covers the kernel's own cost. */
#define SCHED_LOAD_CEILING_PPM  950000UL

/* Factory limit for a task that does not declare one: 10% of its period. */
#define SCHED_DEFAULT_LIMIT_PPM 100000UL

struct task {
    struct trap_frame frame;            /* MUST be the first field: vectors.S saves the context here */
    uint64_t space;                     /* process translation table */
    char name[RTR_NAME_MAX];
    uint32_t state;
    uint32_t priority;
    bool system;                        /* system task: scaling respects the floor */
    bool realtime;                      /* real-time class: runs before every standard task */

    uint64_t period;                    /* everything in counter ticks */
    uint64_t limit_declared;
    uint64_t limit_effective;
    uint64_t limit_floor;

    uint64_t next_activation;           /* nominal instant of the next activation */
    uint64_t activation_instant;        /* nominal instant of the current activation */
    uint64_t remaining;                 /* CPU time left in this period */
    uint64_t used_in_period;

    uint64_t activations;
    uint64_t completions;
    uint64_t overruns;
    uint64_t unfinished;
    uint64_t skipped;
    uint64_t cpu_total;
    uint64_t cpu_period_max;
    struct rtr_duration response;

    uint64_t fault_syndrome;
    uint64_t fault_address;
    uint64_t fault_pc;

    uint32_t region_count;
    struct rtr_region regions[SCHED_REGIONS_MAX];
    uint64_t memory_bytes;
};

/* Reserves a task table entry, or NULL if it is full or the installation is sealed. */
struct task *sched_task_alloc(void) __attribute__((warn_unused_result));

/* Hands the CPU to the boot set. The other tasks only run after sched_seal. */
void sched_start(void) __attribute__((noreturn));

/*
 * Seals the installation: applies the scaling, schedules the first
 * activation of every task created since the start, and refuses any
 * further process creation.
 */
void sched_seal(void);
bool sched_sealed(void);

/* Changes the scheduling parameters of a task; only before the seal. */
bool sched_configure(struct task *task, uint32_t priority, uint64_t period,
                     uint64_t limit, uint64_t floor) __attribute__((warn_unused_result));

struct task *sched_current(void);

/* The boot set: the first task created, the only one allowed to change the installation. */
struct task *sched_boot_task(void);

/* Exception handling entry points. They return the frame to resume, or NULL to go idle. */
struct trap_frame *sched_resume(void);
struct trap_frame *sched_timer_irq(uint64_t now);
struct trap_frame *sched_wait_period(void);
struct trap_frame *sched_fault(uint64_t syndrome, uint64_t address);

void sched_count_interrupt(bool handled);
void sched_count_system_call(void);

/* Fills in the system statistics and restarts the measurement windows. */
void sched_read_stats(struct rtr_stats *out);

#endif
