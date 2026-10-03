/*
 * RTR-OS - interface between the kernel and the programs.
 *
 * This header is shared by both sides and defines everything they have to
 * agree on: the system calls, the program image format and the structures
 * exchanged.
 */
#ifndef RTR_ABI_H
#define RTR_ABI_H

/* Address space of a process. The kernel occupies the first 4 GB. */
#define RTR_USER_BASE           0x100000000     /* program image */
#define RTR_USER_DMA_BASE       0x110000000     /* memory for data exchange with devices */
#define RTR_USER_DEVICE_BASE    0x120000000     /* registers of the assigned devices */
#define RTR_USER_STACK_TOP      0x130000000
#define RTR_USER_END            0x140000000

/* Header at the start of every program image; the address fields are virtual addresses. */
#define RTR_PROGRAM_MAGIC       0x32474F5250525452      /* "RTRPROG2" */
#define RTR_PROGRAM_DESCRIPTION_MAX 64

/* Program flags, set when the program is built. */
#define RTR_PROGRAM_FLAG_REALTIME 1U    /* written for real-time scheduling: may be a real-time process */

/* System calls: number in x8, arguments in x0..x5, result in x0. */
#define RTR_SYS_WAIT_PERIOD     0       /* completes the activation and waits for the next period */
#define RTR_SYS_CONSOLE_WRITE   1       /* (text, size) writes to the debug console */
#define RTR_SYS_REGION_INFO     2       /* (index, struct rtr_region *) describes an assigned region */
#define RTR_SYS_STATS_READ      3       /* (struct rtr_stats *) reads the system statistics */
#define RTR_SYS_PROCESS_CREATE  4       /* (struct rtr_process_spec *, image, size) boot set only, before the seal */
#define RTR_SYS_INSTALL_SEAL    5       /* boot set only: activates the processes created and seals */
#define RTR_SYS_TASK_CONFIGURE  6       /* (struct rtr_process_spec *) boot set only: its own scheduling */
#define RTR_SYS_REBOOT          7       /* boot set only: resets the board */

/* From here on, C only; what is above is also used by assembly. */
#ifndef __ASSEMBLER__

#include <stdint.h>

struct rtr_program_header {
    uint64_t magic;
    uint64_t entry;
    uint64_t text_end;                  /* end of code, start of constants */
    uint64_t rodata_end;                /* end of constants, start of data */
    uint64_t data_end;                  /* end of the data stored in the image */
    uint64_t bss_end;                   /* end of the zeroed data */
    uint64_t flags;                     /* RTR_PROGRAM_FLAG_* */
    char description[RTR_PROGRAM_DESCRIPTION_MAX];  /* one line about the program, NUL-terminated */
};

#define RTR_OK                  0
#define RTR_ERR_ARGUMENT        (-1)
#define RTR_ERR_NOT_FOUND       (-2)
#define RTR_ERR_DENIED          (-3)    /* the caller is not allowed, or the installation is sealed */
#define RTR_ERR_REJECTED        (-4)    /* the process description was refused; the console says why */

#define RTR_CONSOLE_WRITE_MAX   256U
#define RTR_NAME_MAX            16U
#define RTR_SPEC_REGIONS_MAX    4U

/* Region requested in a process description: a device by name, or DMA memory of a given size. */
struct rtr_region_spec {
    uint32_t kind;                      /* RTR_REGION_DEVICE or RTR_REGION_DMA */
    uint32_t reserved;
    uint64_t size;                      /* DMA only, bytes */
    char name[RTR_NAME_MAX];            /* device name, or a label for DMA memory */
};

/* Process description, as read from the manifest. Names are NUL-terminated. */
struct rtr_process_spec {
    char name[RTR_NAME_MAX];
    char program[RTR_NAME_MAX];         /* program name, among the ones available */
    uint64_t argument;
    uint32_t priority;
    uint32_t system;                    /* 1 = system task: scaling respects the floor */
    uint64_t period_us;
    uint64_t limit_us;                  /* 0 = default limit */
    uint64_t floor_us;
    uint64_t stack_pages;
    uint32_t region_count;
    uint32_t realtime;                  /* 1 = real-time process: needs a real-time program */
    uint32_t core;                      /* real-time only: the dedicated core it gets (1 to 3) */
    uint32_t reserved2;
    struct rtr_region_spec regions[RTR_SPEC_REGIONS_MAX];
};

/* Memory region assigned to a process by the permission table. */
#define RTR_REGION_DEVICE       1U      /* device registers */
#define RTR_REGION_DMA          2U      /* contiguous uncached memory with a known physical address */

struct rtr_region {
    uint32_t kind;
    uint32_t reserved;
    uint64_t address;                   /* address in the process space */
    uint64_t physical;                  /* corresponding physical address */
    uint64_t size;
    char name[RTR_NAME_MAX];
};

/* Measured duration: values are in system counter ticks. */
struct rtr_duration {
    uint64_t count;
    uint64_t minimum;
    uint64_t maximum;
    uint64_t sum;
    uint64_t worst;                     /* largest value since boot */
    uint64_t worst_instant;             /* instant at which it happened */
};

#define RTR_TASK_UNUSED         0U
#define RTR_TASK_WAITING        1U      /* activation completed, waiting for the next period */
#define RTR_TASK_READY          2U
#define RTR_TASK_RUNNING        3U
#define RTR_TASK_THROTTLED      4U      /* suspended for exceeding the time limit */
#define RTR_TASK_FAULTED        5U      /* terminated by a fault */

#define RTR_STATS_TASKS_MAX     8U

struct rtr_task_stats {
    char name[RTR_NAME_MAX];
    uint32_t state;
    uint32_t priority;                  /* larger number = higher priority, within its class */
    uint32_t realtime;                  /* real-time processes run before every standard one */
    uint32_t reserved;
    uint64_t period;                    /* ticks */
    uint64_t limit_declared;            /* CPU ticks per period, as declared */
    uint64_t limit_effective;           /* after scaling */
    uint64_t activations;
    uint64_t completions;
    uint64_t overruns;                  /* limit overruns */
    uint64_t unfinished;                /* periods not completed before the next activation */
    uint64_t cpu_total;                 /* CPU ticks since boot */
    uint64_t cpu_period_max;            /* largest CPU use in one period */
    struct rtr_duration response;       /* from activation to completion */
    uint64_t fault_syndrome;            /* filled in if the state is FAULTED */
    uint64_t fault_address;
    uint64_t fault_pc;
};

struct rtr_stats {
    uint64_t instant;                   /* system counter at the time of reading */
    uint64_t counter_hz;
    uint64_t boot_instant;
    uint64_t idle_total;                /* ticks the core spent idle */
    uint64_t context_switches;
    uint64_t interrupts;
    uint64_t interrupts_unhandled;
    uint64_t system_calls;
    uint64_t timer_skipped;
    struct rtr_duration timer_delay;    /* from the programmed instant to handler entry */
    uint64_t memory_total;              /* bytes */
    uint64_t memory_kernel;
    uint64_t memory_processes;
    uint64_t load_declared_ppm;         /* sum of the declared limits, in parts per million */
    uint64_t load_effective_ppm;        /* after scaling */
    uint32_t scaling_applied;
    uint32_t task_count;
    uint32_t console_dropped;
    uint32_t reserved;
    struct rtr_task_stats tasks[RTR_STATS_TASKS_MAX];
};

#endif /* __ASSEMBLER__ */

#endif
