/* RTR-OS - inicialização e conjunto de tarefas de demonstração. */
#include "arch.h"
#include "drivers.h"
#include "kernel.h"
#include "sched.h"

/* Ocupa a CPU até que `us` microssegundos tenham passado no relógio. */
static void burn_us(uint64_t us)
{
    uint64_t end = timer_now() + timer_us_to_ticks(us);

    while (timer_now() < end)
        ;
}

static void burn_job(void *arg)
{
    burn_us((uint64_t)arg);
}

/* Tarefa defeituosa: nunca devolve a CPU por conta própria. */
static void runaway_job(void *arg)
{
    (void)arg;
    for (;;)
        ;
}

static const char *state_name(enum task_state state)
{
    switch (state) {
    case TASK_WAITING:   return "espera";
    case TASK_READY:     return "pronta";
    case TASK_THROTTLED: return "contida";
    default:             return "-";
    }
}

static void report_job(void *arg)
{
    struct sched_info si;
    struct task_info ti;

    (void)arg;
    sched_get_info(&si);

    kprintf("\n[%lu.%03lu s] carga contratada %u.%u%%  ocioso %u.%u%%  atraso max do timer %lu us\n",
            si.uptime_us / 1000000, si.uptime_us / 1000 % 1000,
            si.admitted_ppm / 10000, si.admitted_ppm / 1000 % 10,
            si.idle_ppm / 10000, si.idle_ppm / 1000 % 10,
            si.max_timer_delay_us);
    kprintf("%-10s %-8s %8s %8s %8s %8s %8s %7s %8s %8s\n",
            "tarefa", "estado", "T(us)", "C(us)", "jobs", "feitos", "estouros", "perdas",
            "resp.max", "atr.max");

    for (int id = 0; id < SCHED_MAX_TASKS; id++) {
        if (!sched_task_info(id, &ti))
            continue;
        kprintf("%-10s %-8s %8lu %8lu %8lu %8lu %8lu %7lu %8lu %8lu\n",
                ti.name, state_name(ti.state), ti.period_us, ti.budget_us,
                ti.releases, ti.completions, ti.overruns, ti.misses,
                ti.max_response_us, ti.max_release_delay_us);
    }
}

static void spawn(const char *name, void (*job)(void *), void *arg,
                  uint64_t period_us, uint64_t budget_us)
{
    struct rt_contract contract = { .period_us = period_us, .budget_us = budget_us };
    int id = task_create(name, job, arg, &contract);

    if (id >= 0)
        kprintf("  %-10s T=%lu us C=%lu us: contrato aceito\n", name, period_us, budget_us);
    else if (id == RT_EADMISSION)
        kprintf("  %-10s T=%lu us C=%lu us: RECUSADO, a carga passaria do limite\n",
                name, period_us, budget_us);
    else
        kprintf("  %-10s T=%lu us C=%lu us: erro %d\n", name, period_us, budget_us, id);
}

void kernel_main(void *dtb)
{
    uart_init();
    timer_init();
    gic_init();

    kprintf("\nRTR-OS - kernel de contratos temporais\n");
    kprintf("EL%lu, timer a %lu Hz, dtb em %p\n",
            read_sysreg(CurrentEL) >> 2, timer_hz(), dtb);

    kprintf("contratos:\n");
    spawn("controle", burn_job, (void *)100, 1000, 200);
    spawn("sensor", burn_job, (void *)500, 5000, 1000);
    spawn("travada", runaway_job, NULL, 10000, 2000);
    spawn("relatorio", report_job, NULL, 1000000, 100000);
    spawn("excesso", burn_job, (void *)1000, 10000, 4000);

    sched_start();
}
