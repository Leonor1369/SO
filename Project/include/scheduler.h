#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "queue.h"

#define MAX_USER_LEN 64
#define SJF_MAX_USERS 256

    // ── Estrutura do histograma SJF ───────────────────────────────────────────

typedef struct {
    char user_id[MAX_USER_LEN];
    long total_duration_ms; // soma das durações dos comandos anteriores
    int count;             // número de comandos anteriores
} sjf_entry_t;

    // ── Enum para políticas de escalonamento ───────────────────────────────────────────
typedef enum {
    SCHED_FCFS,   // First-Come, First-Served
    SCHED_SJF,    // Shortest Job First
    SCHED_PRIORITY, // Priority Scheduling
    SCHED_RR       // Round Robin
} scheduling_policy_t;

    // ── Estrutura do escalonador ───────────────────────────────────────────
typedef struct {
    scheduling_policy_t policy;
    queue_t* queue;
    char rr_last_user[MAX_USER_LEN]; // para Round Robin: último user servido
    sjf_entry_t sjf_hist[SJF_MAX_USERS]; // para SJF: histórico de duração por user
    int sjf_count; // número de users no histórico SJF
} scheduler_t;

    // ── Função do Scheduler ───────────────────────────────────────────
void init_scheduler(scheduler_t *s, scheduling_policy_t policy, queue_t *q);
int next_command(scheduler_t *s, queue_command_t *out);
void command_finished(scheduler_t *s, const char *user_id, long duration_ms);

#endif