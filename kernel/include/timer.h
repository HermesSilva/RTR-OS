/* RTR-OS - interrupção periódica do timer e medição do atraso de tratamento. */
#ifndef RTR_TIMER_H
#define RTR_TIMER_H

#include <stdint.h>

/*
 * Atraso = tempo entre o instante programado e a entrada do tratador,
 * medido pelo contador do sistema. Valores em ticks do contador.
 */
struct timer_stats {
    uint64_t fires;                     /* disparos tratados desde o início */
    uint64_t skipped;                   /* períodos pulados por atraso maior que um período */
    uint64_t window_fires;              /* disparos desde a última leitura */
    uint64_t window_min;
    uint64_t window_max;
    uint64_t window_sum;
    uint64_t worst;                     /* maior atraso desde o início */
};

/* Passa a interromper a cada `period` ticks, em instantes absolutos. */
void timer_start(uint64_t period);

/* Chamada pelo tratador de interrupções. */
void timer_handle_irq(void);

/* Períodos decorridos desde o início: disparos tratados mais os pulados. */
uint64_t timer_periods(void);

/* Copia as estatísticas e reinicia a janela. */
void timer_read_stats(struct timer_stats *out);

#endif
