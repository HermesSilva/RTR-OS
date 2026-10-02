/*
 * RTR-OS - escalonador de contratos temporais.
 *
 * Uma tarefa não tem prioridade: ela declara um contrato (a cada período,
 * precisa de até `budget` de CPU, entregue antes de `deadline`). O kernel
 * só aceita o contrato se puder honrá-lo junto com os já aceitos, e depois
 * disso impede que qualquer tarefa consuma mais do que declarou.
 */
#ifndef RTR_SCHED_H
#define RTR_SCHED_H

#include <stdbool.h>
#include <stdint.h>

#include "arch.h"

#define SCHED_MAX_TASKS     8
#define SCHED_STACK_SIZE    8192

/* Fração da CPU que pode ser prometida; o resto cobre o custo do próprio kernel. */
#define SCHED_LOAD_LIMIT_PPM 950000u

#define SVC_JOB_DONE        0

struct rt_contract {
    uint64_t period_us;
    uint64_t budget_us;
    uint64_t deadline_us;               /* 0 = igual ao período */
};

enum {
    RT_EINVAL    = -1,                  /* contrato malformado */
    RT_ENOSLOT   = -2,                  /* tabela de tarefas cheia */
    RT_EADMISSION = -3,                 /* aceitar quebraria as garantias já dadas */
    RT_ESTARTED  = -4,                  /* o escalonador já está rodando */
};

enum task_state {
    TASK_UNUSED,
    TASK_WAITING,                       /* job concluído, aguardando o próximo período */
    TASK_READY,
    TASK_THROTTLED,                     /* orçamento esgotado antes de concluir o job */
};

struct task_info {
    const char *name;
    enum task_state state;
    uint64_t period_us;
    uint64_t budget_us;
    uint64_t deadline_us;
    uint64_t releases;                  /* jobs liberados */
    uint64_t completions;               /* jobs concluídos */
    uint64_t overruns;                  /* jobs que estouraram o orçamento (culpa da tarefa) */
    uint64_t misses;                    /* jobs dentro do orçamento que perderam o deadline (culpa do kernel) */
    uint64_t skipped;                   /* períodos pulados por atraso do kernel */
    uint64_t max_response_us;           /* pior tempo entre liberação e conclusão */
    uint64_t max_release_delay_us;      /* pior atraso entre o instante nominal e a liberação efetiva */
};

struct sched_info {
    uint64_t uptime_us;
    uint32_t admitted_ppm;              /* carga prometida em contratos */
    uint32_t idle_ppm;                  /* fração do tempo em que a CPU ficou ociosa */
    uint64_t max_timer_delay_us;        /* pior atraso entre o disparo programado e o tratamento */
};

/*
 * Cria uma tarefa periódica. `job` é chamada uma vez por período e deve
 * retornar ao terminar o trabalho daquele período. Devolve o identificador
 * da tarefa (>= 0) ou um dos erros RT_E*.
 */
int task_create(const char *name, void (*job)(void *), void *arg, const struct rt_contract *contract);

void sched_start(void) __attribute__((noreturn));

bool sched_task_info(int id, struct task_info *out);
void sched_get_info(struct sched_info *out);

/* Pontos de entrada usados pelos tratadores de exceção */
struct trap_frame *sched_timer_irq(struct trap_frame *frame);
struct trap_frame *sched_job_done(struct trap_frame *frame);

#endif
