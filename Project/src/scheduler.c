#include "scheduler.h"
#include "queue.h"
#include <string.h>
#include <limits.h>

void init_scheduler(scheduler_t *s, scheduling_policy_t policy, queue_t *q) {
    s->policy = policy;
    s->queue  = q;
    s->rr_last_user[0] = '\0'; // nenhum user servido ainda
    memset(s->sjf_hist, 0, sizeof(s->sjf_hist));
}

// Histograma SJF
static sjf_entry_t *sjf_get_entry(scheduler_t *s, const char *user_id) {
    for (int i = 0; i < s->sjf_count; i++) {
        if (strncmp(s->sjf_hist[i].user_id, user_id, MAX_USER_LEN) == 0) {
            return &s->sjf_hist[i];
        }
    }
    // nova entrada
    if (s->sjf_count < SJF_MAX_USERS) {
        sjf_entry_t *entry = &s->sjf_hist[s->sjf_count++];
        strncpy(entry->user_id, user_id, MAX_USER_LEN - 1);
        entry->user_id[MAX_USER_LEN - 1] = '\0';
        entry->total_duration_ms = 0;
        entry->count = 0;
        return entry;
    }
    return NULL; // limite de usuários no histórico atingido
}

static long sjf_avg(scheduler_t *s, const char *user_id) {
    for (int i = 0; i < s->sjf_count; i++) {
        if (strncmp(s->sjf_hist[i].user_id, user_id, MAX_USER_LEN) == 0) {
            if(s->sjf_hist[i].count == 0) return LONG_MAX; // sem histórico → assume duração máxima
            return s->sjf_hist[i].total_duration_ms / s->sjf_hist[i].count;
        }
    }
    return LONG_MAX; // usuário não encontrado → assume duração média máxima
}

int next_command(scheduler_t *s, queue_command_t *out) {
    if (is_queue_empty(s->queue)) return 0;
 
    if (s->policy == SCHED_FCFS) {
        return (dequeue_command(s->queue, out) == 0) ? 1 : 0;
    }
 
    if (s->policy == SCHED_SJF) {
        int  best_idx = 0;
        long best_avg = LONG_MAX;
        int  n        = get_queue_size(s->queue);
        for (int i = 0; i < n; i++) {
            queue_command_t candidate;
            if (!peek_queue_at(s->queue, i, &candidate)) continue;
            long avg = sjf_avg(s, candidate.user_id);
            if (avg < best_avg) { best_avg = avg; best_idx = i; }
        }
        return queue_remove_at(s->queue, best_idx, out);
    }
 
    if (s->policy == SCHED_PRIORITY) {
        int best_idx  = 0;
        int best_prio = INT_MIN;
        int n         = get_queue_size(s->queue);
        for (int i = 0; i < n; i++) {
            queue_command_t candidate;
            if (!peek_queue_at(s->queue, i, &candidate)) continue;
            if (candidate.priority > best_prio) {
                best_prio = candidate.priority;
                best_idx  = i;
            }
        }
        return queue_remove_at(s->queue, best_idx, out);
    }
 
    if (s->policy == SCHED_RR) {
        int n = get_queue_size(s->queue);
        for (int i = 0; i < n; i++) {
            queue_command_t candidate;
            if (!peek_queue_at(s->queue, i, &candidate)) continue;
            if (strncmp(candidate.user_id, s->rr_last_user, MAX_USER_LEN) != 0) {
                strncpy(s->rr_last_user, candidate.user_id, MAX_USER_LEN - 1);
                s->rr_last_user[MAX_USER_LEN - 1] = '\0';
                return queue_remove_at(s->queue, i, out);
            }
        }
        // fallback: todos do mesmo user
        if (dequeue_command(s->queue, out) == 0) {
            strncpy(s->rr_last_user, out->user_id, MAX_USER_LEN - 1);
            s->rr_last_user[MAX_USER_LEN - 1] = '\0';
            return 1;
        }
        return 0;
    }
 
    return 0;
}


void command_finished(scheduler_t *s, const char *user_id, long duration_ms) {
    if (s->policy == SCHED_SJF) {
        sjf_entry_t *e = sjf_get_entry(s, user_id);
        if (e) { e->total_duration_ms += duration_ms; e->count++; }
    }
}