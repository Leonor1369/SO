#include "scheduler.h"
#include "queue.h"
#include <string.h>

void init_scheduler(scheduler_t *s, scheduling_policy_t policy, queue_t *q) {
    s->policy = policy;
    s->queue  = q;
    s->rr_last_user[0] = '\0'; // nenhum user servido ainda
}

int next_command(scheduler_t *s, queue_command_t *out) {
    if (is_queue_empty(s->queue)) return 0;

    if (s->policy == SCHED_FCFS
     || s->policy == SCHED_SJF
     || s->policy == SCHED_PRIORITY)
    {
        // FCFS puro — SJF e PRIORITY ficam como FCFS até teres critério de ordenação
        return (dequeue_command(s->queue, out) == 0) ? 1 : 0;
    }

    if (s->policy == SCHED_RR) {
        int n = get_queue_size(s->queue);

        // 1ª passagem: procurar um user diferente do último servido
        for (int i = 0; i < n; i++) {
            queue_command_t candidate;
            if (!peek_queue_at(s->queue, i, &candidate)) continue;

            if (strcmp(candidate.user_id, s->rr_last_user) != 0) {
                strncpy(s->rr_last_user, candidate.user_id, sizeof(s->rr_last_user) - 1);
                s->rr_last_user[sizeof(s->rr_last_user) - 1] = '\0';
                return queue_remove_at(s->queue, i, out);
            }
        }

        // fallback: todos os comandos são do mesmo user — serve o primeiro
        strncpy(s->rr_last_user, out->user_id, sizeof(s->rr_last_user) - 1);
        return (dequeue_command(s->queue, out) == 0) ? 1 : 0;
    }

    return 0;
}

void command_finished(scheduler_t *s, const char *user_id) {
    // reservado para futuras políticas que precisem de reagir a conclusões
    // no RR atual o estado relevante (rr_last_user) é atualizado no next_command
    (void)s;
    (void)user_id;
}