/*
 * RTR-OS - timer genérico do ARM (timer físico de EL1).
 *
 * Não há tick periódico: o escalonador programa o comparador para o
 * próximo evento que lhe interessa e a CPU não é interrompida antes disso.
 */
#include "arch.h"
#include "drivers.h"

#define CNTP_CTL_ENABLE 1u

static uint64_t hz;

void timer_init(void)
{
    write_sysreg(cntp_ctl_el0, 0);
    hz = read_sysreg(cntfrq_el0);
}

uint64_t timer_hz(void)
{
    return hz;
}

uint64_t timer_now(void)
{
    __asm__ volatile("isb");
    return read_sysreg(cntpct_el0);
}

void timer_set(uint64_t deadline)
{
    write_sysreg(cntp_cval_el0, deadline);
    write_sysreg(cntp_ctl_el0, CNTP_CTL_ENABLE);
}

/* As conversões separam a parte inteira de segundos para não estourar 64 bits. */
uint64_t timer_us_to_ticks(uint64_t us)
{
    return us / 1000000 * hz + us % 1000000 * hz / 1000000;
}

uint64_t timer_ticks_to_us(uint64_t ticks)
{
    return ticks / hz * 1000000 + ticks % hz * 1000000 / hz;
}
