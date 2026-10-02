/* RTR-OS - tratamento de interrupções. */
#ifndef RTR_TRAP_H
#define RTR_TRAP_H

#include <stdint.h>

/* Chamada por vectors.S a cada interrupção tomada pelo kernel. */
void trap_irq(void);

/* Interrupções recebidas para as quais não há tratador. */
uint64_t trap_unhandled_irqs(void);

#endif
