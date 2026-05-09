/**
 scheduler.c — Implementação das políticas de escalonamento.

 O scheduler é responsável por decidir qual o próximo comando a executar,
 de acordo com a política configurada no arranque do controller.

 Políticas suportadas:

  SCHED_FCFS (0) — First-Come, First-Served
      Os comandos são executados pela ordem de chegada. Simples e justo
      em termos de ausência de starvation, mas pode causar o efeito
      "convoy" (comandos longos bloqueiam os curtos).

  SCHED_SJF (1) — Shortest Job First
      Privilegia os utilizadores cujos comandos anteriores foram mais
      rápidos (estimativa baseada na média histórica por utilizador).
      Minimiza o tempo médio de espera global, mas utilizadores com
      comandos longos podem sofrer starvation em carga elevada.

  SCHED_PRIORITY (2) — Escalonamento por Prioridade
      O utilizador define explicitamente a urgência com -p <valor>.
      Comandos com maior valor de prioridade são escolhidos primeiro.
      Em caso de empate, o critério é a posição na fila (equivalente a FCFS).

  SCHED_RR (3) — Round Robin por utilizador
      Garante equidade entre utilizadores distintos: serve alternadamente
      um comando de cada utilizador diferente do último servido.
      Se todos os comandos pendentes forem do mesmo utilizador, serve-os
      por ordem de chegada (fallback FCFS).
*/

#include "scheduler.h"
#include "queue.h"
#include <string.h>
#include <limits.h>


// Inicializa o escalonador com a política e fila indicadas.
// s -> ponteiro para o scheduler a inicializar
// policy -> política de escalonamento a usar
// q -> ponteiro para a fila de comandos pendentes (o scheduler irá ler desta fila
void init_scheduler(scheduler_t *s, scheduling_policy_t policy, queue_t *q) {
    s->policy = policy;
    s->queue  = q;
    s->rr_last_user[0] = '\0'; // nenhum user servido ainda
    s->sjf_count = 0; // sem histórico SJF inicialmente
    memset(s->sjf_hist, 0, sizeof(s->sjf_hist));
}

// Histograma SJF

// Devolve (ou cria) a entrada histórica de um utilizador  
static sjf_entry_t *sjf_get_entry(scheduler_t *s, const char *user_id) {
    for (int i = 0; i < s->sjf_count; i++) {
        if (strncmp(s->sjf_hist[i].user_id, user_id, MAX_USER_LEN) == 0) {
            return &s->sjf_hist[i];
        }
    }
    // utilizador novo : criar entrada
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


// Calcula a duração média estimada dos comandos de um utilizador.
static long sjf_avg(scheduler_t *s, const char *user_id) {
    for (int i = 0; i < s->sjf_count; i++) {
        if (strncmp(s->sjf_hist[i].user_id, user_id, MAX_USER_LEN) == 0) {
            if(s->sjf_hist[i].count == 0) return LONG_MAX; // sem histórico → assume duração máxima
            return s->sjf_hist[i].total_duration_ms / s->sjf_hist[i].count;
        }
    }
    return LONG_MAX; // usuário não encontrado → assume duração média máxima
}

//Seleciona e remove o próximo comando a executar.

int next_command(scheduler_t *s, queue_command_t *out) {
    if (is_queue_empty(s->queue)) return 0;
 
    //remove simplesmente o elemento da cabeça da fila (O(1))
    if (s->policy == SCHED_FCFS) {
        return (dequeue_command(s->queue, out) == 0) ? 1 : 0;
    }
 
    /*percorre toda a fila com peek_queue_at() e escolhe o comando
    cujo utilizador tem a menor média histórica de duração. Remove
    com queue_remove_at() (O(n)).*/
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
 
    /*percorre toda a fila e escolhe o comando com maior valor no
    campo priority. Em empate, vence a posição mais baixa (FCFS).
    Remove com queue_remove_at() (O(n)).*/
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
 
    /*percorre a fila à procura do primeiro comando de um utilizador
    diferente de rr_last_user. Atualiza rr_last_user antes de
    devolver. Se todos forem do mesmo utilizador, usa FCFS como
    fallback para evitar bloqueio (O(n)).*/
    if (s->policy == SCHED_RR) {
        int n = get_queue_size(s->queue);
        // procurar o primeiro comando de um utilizador diferente do último servido
        for (int i = 0; i < n; i++) {
            queue_command_t candidate;
            if (!peek_queue_at(s->queue, i, &candidate)) continue;
            if (strncmp(candidate.user_id, s->rr_last_user, MAX_USER_LEN) != 0) {
                strncpy(s->rr_last_user, candidate.user_id, MAX_USER_LEN - 1);
                s->rr_last_user[MAX_USER_LEN - 1] = '\0';
                return queue_remove_at(s->queue, i, out);
            }
        }
        // fallback: todos os comandos pendentes são do mesmo utilizador
        if (dequeue_command(s->queue, out) == 0) {
            strncpy(s->rr_last_user, out->user_id, MAX_USER_LEN - 1);
            s->rr_last_user[MAX_USER_LEN - 1] = '\0';
            return 1;
        }
        return 0;
    }
 
    return 0; // política desconhecida
}


// Notifica o scheduler que um comando terminou.
void command_finished(scheduler_t *s, const char *user_id, long duration_ms) {
    if (s->policy == SCHED_SJF) {
        sjf_entry_t *e = sjf_get_entry(s, user_id);
        if (e) { e->total_duration_ms += duration_ms; e->count++; }
    }
}