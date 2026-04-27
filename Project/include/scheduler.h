#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "queue.h"

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
    queue_t* ready_queue;
} scheduler_t;

    // ── Função do Scheduler ───────────────────────────────────────────
void schedule_command(scheduler_t* scheduler);

#endif