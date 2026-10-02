/* RTR-OS - tratamento de interrupções, chamado por vectors.S. */
#include "trap.h"

#include "board.h"
#include "timer.h"

static uint64_t unhandled;

void trap_irq(void)
{
    uint32_t token = 0U;
    uint32_t irq = gic_acknowledge(&token);

    if (irq == GIC_IRQ_NONE) {
        return;
    }

    if (irq == BOARD_IRQ_TIMER) {
        timer_handle_irq();
    } else {
        unhandled++;
    }
    gic_end_of_irq(token);
}

uint64_t trap_unhandled_irqs(void)
{
    return unhandled;
}
