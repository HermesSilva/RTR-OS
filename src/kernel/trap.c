/* RTR-OS - tratadores de exceção chamados por vectors.S. */
#include "arch.h"
#include "drivers.h"
#include "kernel.h"
#include "sched.h"

#define ESR_EC(esr)     ((esr) >> 26 & 0x3F)
#define ESR_EC_SVC64    0x15

struct trap_frame *trap_irq(struct trap_frame *frame)
{
    uint32_t iar = gic_acknowledge();
    uint32_t irq = iar & 0x3FF;

    if (irq >= IRQ_SPURIOUS)
        return frame;

    if (irq == IRQ_TIMER)
        frame = sched_timer_irq(frame);
    else
        kprintf("irq %u sem tratador\n", irq);

    gic_end_of_irq(iar);
    return frame;
}

struct trap_frame *trap_sync(struct trap_frame *frame, uint64_t esr)
{
    if (ESR_EC(esr) == ESR_EC_SVC64 && (esr & 0xFFFF) == SVC_JOB_DONE)
        return sched_job_done(frame);

    panic("falha em EL1: esr=%lx elr=%lx far=%lx",
          esr, frame->elr, read_sysreg(far_el1));
}

void trap_bad(struct trap_frame *frame, uint64_t vector, uint64_t esr)
{
    panic("vetor %lu inesperado: esr=%lx elr=%lx spsr=%lx",
          vector, esr, frame->elr, frame->spsr);
}
