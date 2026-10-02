/*
 * RTR-OS - escalonador de contratos temporais.
 *
 * Política: EDF (o job com o deadline mais próximo roda) com orçamento
 * imposto pelo kernel. Tudo aqui roda com interrupções mascaradas e em
 * tempo limitado por SCHED_MAX_TASKS, nunca pela carga do sistema.
 */
#include "sched.h"

#include "drivers.h"
#include "kernel.h"

#define PPM             1000000u
#define MAX_PERIOD_US   3600000000UL    /* 1 hora */
#define START_DELAY_US  10000

struct task {
    const char *name;
    enum task_state state;
    struct trap_frame *frame;           /* contexto salvo enquanto não está rodando */
    struct rt_contract contract;

    uint64_t period;                    /* contrato convertido para ticks do timer */
    uint64_t budget;
    uint64_t deadline;

    uint64_t next_release;              /* instante nominal da próxima liberação */
    uint64_t job_release;               /* instante nominal em que o job atual foi liberado */
    uint64_t job_deadline;
    uint64_t remaining;                 /* orçamento que resta ao job atual */

    uint64_t releases;
    uint64_t completions;
    uint64_t overruns;
    uint64_t misses;
    uint64_t skipped;
    uint64_t max_response;
    uint64_t max_release_delay;
};

static struct task tasks[SCHED_MAX_TASKS];
static uint8_t stacks[SCHED_MAX_TASKS][SCHED_STACK_SIZE] __attribute__((aligned(16)));

/* A tarefa ociosa é o próprio fluxo de boot, que continua na pilha de boot. */
static struct task idle = { .name = "idle", .state = TASK_READY };
static struct task *current = &idle;

static bool started;
static uint32_t admitted_ppm;
static uint64_t start_time;
static uint64_t dispatch_time;          /* quando `current` recebeu a CPU */
static uint64_t idle_ticks;
static uint64_t timer_target = UINT64_MAX;
static uint64_t max_timer_delay;

static void task_trampoline(void (*job)(void *), void *arg)
{
    for (;;) {
        job(arg);
        __asm__ volatile("svc #0");     /* SVC_JOB_DONE */
    }
}

int task_create(const char *name, void (*job)(void *), void *arg, const struct rt_contract *contract)
{
    uint64_t deadline_us = contract->deadline_us ? contract->deadline_us : contract->period_us;
    struct trap_frame *frame;
    struct task *t = NULL;
    uint64_t density;
    int id;

    if (started)
        return RT_ESTARTED;

    if (!job || contract->budget_us == 0 || contract->budget_us > deadline_us ||
        deadline_us > contract->period_us || contract->period_us > MAX_PERIOD_US)
        return RT_EINVAL;

    for (id = 0; id < SCHED_MAX_TASKS; id++) {
        if (tasks[id].state == TASK_UNUSED) {
            t = &tasks[id];
            break;
        }
    }
    if (!t)
        return RT_ENOSLOT;

    /*
     * Teste de admissão: com EDF, o conjunto é escalonável se a soma das
     * densidades (orçamento / deadline) não passar de 1. Arredonda para cima
     * para nunca prometer mais do que existe.
     */
    density = (contract->budget_us * PPM + deadline_us - 1) / deadline_us;
    if (admitted_ppm + density > SCHED_LOAD_LIMIT_PPM)
        return RT_EADMISSION;
    admitted_ppm += (uint32_t)density;

    memset(t, 0, sizeof(*t));
    t->name = name;
    t->contract = *contract;
    t->contract.deadline_us = deadline_us;
    t->period = timer_us_to_ticks(contract->period_us);
    t->budget = timer_us_to_ticks(contract->budget_us);
    t->deadline = timer_us_to_ticks(deadline_us);

    /* Monta o contexto inicial como se a tarefa tivesse sido interrompida no trampolim. */
    frame = (struct trap_frame *)(stacks[id] + SCHED_STACK_SIZE - sizeof(*frame));
    memset(frame, 0, sizeof(*frame));
    frame->x[0] = (uint64_t)job;
    frame->x[1] = (uint64_t)arg;
    frame->elr = (uint64_t)task_trampoline;
    frame->spsr = SPSR_EL1H;
    t->frame = frame;

    t->state = TASK_WAITING;
    return id;
}

static void release_job(struct task *t, uint64_t now)
{
    uint64_t delay = now - t->next_release;

    /* O kernel ficou parado por mais de um período: pula os que já passaram. */
    if (delay >= t->period) {
        uint64_t n = delay / t->period;

        t->skipped += n;
        t->next_release += n * t->period;
        delay = now - t->next_release;
    }
    if (delay > t->max_release_delay)
        t->max_release_delay = delay;

    /* O job anterior tinha orçamento e mesmo assim não terminou a tempo. */
    if (t->state == TASK_READY)
        t->misses++;

    t->job_release = t->next_release;
    t->job_deadline = t->job_release + t->deadline;
    t->remaining = t->budget;
    t->next_release += t->period;
    t->state = TASK_READY;
    t->releases++;
}

/*
 * Coração do kernel: cobra o tempo usado por quem estava rodando, libera
 * os jobs cujo período chegou, escolhe o deadline mais próximo e programa
 * o timer para o próximo evento (uma liberação ou o fim do orçamento).
 */
static struct trap_frame *reschedule(struct trap_frame *frame, uint64_t now)
{
    struct task *next = &idle;
    uint64_t event = UINT64_MAX;
    uint64_t ran = now - dispatch_time;

    current->frame = frame;

    if (current == &idle) {
        idle_ticks += ran;
    } else if (ran < current->remaining) {
        current->remaining -= ran;
    } else {
        current->remaining = 0;
        if (current->state == TASK_READY) {
            current->state = TASK_THROTTLED;
            current->overruns++;
        }
    }

    for (int i = 0; i < SCHED_MAX_TASKS; i++) {
        struct task *t = &tasks[i];

        if (t->state == TASK_UNUSED)
            continue;
        if (now >= t->next_release)
            release_job(t, now);
        if (t->state == TASK_READY && (next == &idle || t->job_deadline < next->job_deadline))
            next = t;
        if (t->next_release < event)
            event = t->next_release;
    }

    if (next != &idle && now + next->remaining < event)
        event = now + next->remaining;

    timer_target = event;
    timer_set(event);

    dispatch_time = now;
    current = next;
    return next->frame;
}

struct trap_frame *sched_timer_irq(struct trap_frame *frame)
{
    uint64_t now = timer_now();

    if (now >= timer_target && now - timer_target > max_timer_delay)
        max_timer_delay = now - timer_target;

    return reschedule(frame, now);
}

struct trap_frame *sched_job_done(struct trap_frame *frame)
{
    uint64_t now = timer_now();
    struct task *t = current;
    uint64_t response = now - t->job_release;

    if (t == &idle)
        panic("SVC_JOB_DONE fora de uma tarefa");

    t->completions++;
    if (response > t->max_response)
        t->max_response = response;
    if (t->state == TASK_READY && now > t->job_deadline)
        t->misses++;
    t->state = TASK_WAITING;

    return reschedule(frame, now);
}

void sched_start(void)
{
    uint64_t now = timer_now();
    uint64_t first = now + timer_us_to_ticks(START_DELAY_US);

    /* Todas as tarefas liberam juntas: é o pior caso para o escalonador. */
    for (int i = 0; i < SCHED_MAX_TASKS; i++)
        tasks[i].next_release = first;

    started = true;
    start_time = now;
    dispatch_time = now;
    timer_target = first;
    timer_set(first);

    gic_enable_irq(IRQ_TIMER);
    irq_enable();

    for (;;)
        cpu_wait_irq();
}

bool sched_task_info(int id, struct task_info *out)
{
    const struct task *t;
    uint64_t flags;

    if (id < 0 || id >= SCHED_MAX_TASKS || tasks[id].state == TASK_UNUSED)
        return false;

    t = &tasks[id];
    flags = irq_save();
    out->name = t->name;
    out->state = t->state;
    out->period_us = t->contract.period_us;
    out->budget_us = t->contract.budget_us;
    out->deadline_us = t->contract.deadline_us;
    out->releases = t->releases;
    out->completions = t->completions;
    out->overruns = t->overruns;
    out->misses = t->misses;
    out->skipped = t->skipped;
    out->max_response_us = t->max_response;
    out->max_release_delay_us = t->max_release_delay;
    irq_restore(flags);

    out->max_response_us = timer_ticks_to_us(out->max_response_us);
    out->max_release_delay_us = timer_ticks_to_us(out->max_release_delay_us);
    return true;
}

void sched_get_info(struct sched_info *out)
{
    uint64_t flags = irq_save();
    uint64_t elapsed = timer_now() - start_time;
    uint64_t idle_now = idle_ticks;
    uint64_t delay = max_timer_delay;

    irq_restore(flags);

    out->uptime_us = timer_ticks_to_us(elapsed);
    out->admitted_ppm = admitted_ppm;
    out->idle_ppm = elapsed >= 1000 ? (uint32_t)(idle_now * 1000 / (elapsed / 1000)) : 0;
    out->max_timer_delay_us = timer_ticks_to_us(delay);
}
