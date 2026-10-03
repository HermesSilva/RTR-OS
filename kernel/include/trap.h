/* RTR-OS - kernel entries: interrupts, system calls and process faults. */
#ifndef RTR_TRAP_H
#define RTR_TRAP_H

#include <stdint.h>

#include "arch.h"

/*
 * Called by vectors.S. They return the frame of the process that should
 * get the CPU, or NULL for the core to go idle.
 */
struct trap_frame *trap_irq(void);
struct trap_frame *trap_process_sync(struct trap_frame *frame, uint64_t syndrome, uint64_t address);

#endif
