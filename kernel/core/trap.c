/* RTR-OS - kernel entries: interrupts, system calls and process faults. */
#include "trap.h"

#include <stdbool.h>
#include <stddef.h>

#include "board.h"
#include "console.h"
#include "memory.h"
#include "mmu.h"
#include "process.h"
#include "rtr/abi.h"
#include "sched.h"
#include "timer.h"

#define SYNDROME_CLASS_SHIFT    26U
#define SYNDROME_CLASS_MASK     0x3FUL
#define SYNDROME_CLASS_SVC      0x15UL  /* system call from AArch64 */
#define MICROS_PER_SECOND       1000000UL
#define PROGRAM_IMAGE_MAX       0x04000000UL    /* 64 MB */

struct trap_frame *trap_irq(void)
{
    uint32_t token = 0U;
    uint32_t irq = gic_acknowledge(&token);
    struct trap_frame *resume = sched_resume();

    if (irq == GIC_IRQ_NONE) {
        return resume;
    }

    if (irq == BOARD_IRQ_TIMER) {
        sched_count_interrupt(true);
        resume = sched_timer_irq(timer_irq_enter());
    } else {
        sched_count_interrupt(false);
    }
    gic_end_of_irq(token);
    return resume;
}

/* A pointer coming from a process is only used after being checked against its address space. */
static bool user_range_ok(const struct task *task, uint64_t address, uint64_t size, bool write)
{
    return mmu_space_check(task->space, address, size, write);
}

static int64_t sys_console_write(const struct task *task, uint64_t text, uint64_t size)
{
    const char *source = (const char *)(uintptr_t)text;

    if ((size == 0U) || (size > RTR_CONSOLE_WRITE_MAX) || !user_range_ok(task, text, size, false)) {
        return RTR_ERR_ARGUMENT;
    }
    for (uint64_t i = 0U; i < size; i++) {
        console_put_char(source[i]);
    }
    return RTR_OK;
}

static int64_t sys_region_info(const struct task *task, uint64_t index, uint64_t destination)
{
    struct rtr_region *out = (struct rtr_region *)(uintptr_t)destination;

    if (index >= task->region_count) {
        return RTR_ERR_NOT_FOUND;
    }
    if (((destination % _Alignof(struct rtr_region)) != 0U) ||
        !user_range_ok(task, destination, sizeof(*out), true)) {
        return RTR_ERR_ARGUMENT;
    }
    *out = task->regions[index];
    return RTR_OK;
}

static int64_t sys_stats_read(const struct task *task, uint64_t destination)
{
    struct rtr_stats *out = (struct rtr_stats *)(uintptr_t)destination;

    if (((destination % _Alignof(struct rtr_stats)) != 0U) ||
        !user_range_ok(task, destination, sizeof(*out), true)) {
        return RTR_ERR_ARGUMENT;
    }
    sched_read_stats(out);
    return RTR_OK;
}

/* Copies a process description from the caller's memory, checking the pointer first. */
static bool copy_spec(const struct task *task, uint64_t source, struct rtr_process_spec *spec)
{
    if (((source % _Alignof(struct rtr_process_spec)) != 0U) ||
        !user_range_ok(task, source, sizeof(*spec), false)) {
        return false;
    }
    (void)memcpy(spec, (const void *)(uintptr_t)source, sizeof(*spec));
    return true;
}

/* Installation calls: only the boot set may make them, and only before the seal. */
static int64_t sys_install(struct task *task, uint64_t number, uint64_t argument,
                           uint64_t image, uint64_t image_size)
{
    struct rtr_process_spec spec;
    uint64_t hz = arch_counter_hz();

    if (task != sched_boot_task()) {
        return RTR_ERR_DENIED;
    }
    if (number == RTR_SYS_REBOOT) {
        console_put_text("reboot requested by the boot set\n");
        board_reboot();
    }
    if (sched_sealed()) {
        return RTR_ERR_DENIED;
    }

    switch (number) {
    case RTR_SYS_PROCESS_CREATE:
        /* The image stays in the caller's memory; it is read while that space is current. */
        if (!copy_spec(task, argument, &spec) || (image_size == 0U) ||
            (image_size > PROGRAM_IMAGE_MAX) || !user_range_ok(task, image, image_size, false)) {
            return RTR_ERR_ARGUMENT;
        }
        return process_create_from_spec(&spec, (const uint8_t *)(uintptr_t)image, image_size)
                   ? RTR_OK : RTR_ERR_REJECTED;
    case RTR_SYS_TASK_CONFIGURE:
        if (!copy_spec(task, argument, &spec)) {
            return RTR_ERR_ARGUMENT;
        }
        if ((spec.period_us == 0U) || (spec.limit_us > spec.period_us)) {
            return RTR_ERR_REJECTED;
        }
        if (!sched_configure(task, spec.priority, (spec.period_us * hz) / MICROS_PER_SECOND,
                             (spec.limit_us * hz) / MICROS_PER_SECOND,
                             (spec.floor_us * hz) / MICROS_PER_SECOND)) {
            return RTR_ERR_REJECTED;
        }
        task->system = (spec.system != 0U);
        task->realtime = (spec.realtime != 0U);
        return RTR_OK;
    case RTR_SYS_INSTALL_SEAL:
        sched_seal();
        return RTR_OK;
    default:
        return RTR_ERR_ARGUMENT;
    }
}

struct trap_frame *trap_process_sync(struct trap_frame *frame, uint64_t syndrome, uint64_t address)
{
    struct task *task = sched_current();
    int64_t result = RTR_ERR_ARGUMENT;

    if ((task == NULL) || (frame == NULL)) {
        return sched_resume();
    }

    /* Any exception that is not a system call terminates the process. */
    if (((syndrome >> SYNDROME_CLASS_SHIFT) & SYNDROME_CLASS_MASK) != SYNDROME_CLASS_SVC) {
        return sched_fault(syndrome, address);
    }

    sched_count_system_call();

    switch (frame->x[8]) {
    case RTR_SYS_WAIT_PERIOD:
        frame->x[0] = (uint64_t)RTR_OK;
        return sched_wait_period();
    case RTR_SYS_CONSOLE_WRITE:
        result = sys_console_write(task, frame->x[0], frame->x[1]);
        break;
    case RTR_SYS_REGION_INFO:
        result = sys_region_info(task, frame->x[0], frame->x[1]);
        break;
    case RTR_SYS_STATS_READ:
        result = sys_stats_read(task, frame->x[0]);
        break;
    case RTR_SYS_PROCESS_CREATE:
    case RTR_SYS_INSTALL_SEAL:
    case RTR_SYS_TASK_CONFIGURE:
    case RTR_SYS_REBOOT:
        result = sys_install(task, frame->x[8], frame->x[0], frame->x[1], frame->x[2]);
        break;
    default:
        break;
    }

    frame->x[0] = (uint64_t)result;
    return frame;
}
