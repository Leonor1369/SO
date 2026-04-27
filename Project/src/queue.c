#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include "queue.h"

void init_queue(queue_t *q) {
    q->front = 0;
    q->rear  = -1;
    q->size  = 0;
}

int enqueue_command(queue_t *q, queue_command_t cmd) {
    if (is_queue_full(q)) return -1;
    q->rear = (q->rear + 1) % MAX_QUEUE_SIZE;
    q->commands[q->rear] = cmd;
    q->size++;
    return 0;
}

int dequeue_command(queue_t *q, queue_command_t *cmd) {
    if (is_queue_empty(q)) return -1;
    *cmd = q->commands[q->front];
    q->front = (q->front + 1) % MAX_QUEUE_SIZE;
    q->size--;
    return 0;
}

int queue_remove_at(queue_t *q, int i, queue_command_t *out) {
    if (i < 0 || i >= q->size) return 0;
    int idx = (q->front + i) % MAX_QUEUE_SIZE;
    *out = q->commands[idx];
    // deslocar os elementos seguintes uma posição para trás
    for (int j = i; j < q->size - 1; j++) {
        int cur  = (q->front + j)     % MAX_QUEUE_SIZE;
        int next = (q->front + j + 1) % MAX_QUEUE_SIZE;
        q->commands[cur] = q->commands[next];
    }
    q->rear = (q->rear - 1 + MAX_QUEUE_SIZE) % MAX_QUEUE_SIZE;
    q->size--;
    return 1;
}

queue_command_t* peek_queue(queue_t *q) {
    if (is_queue_empty(q)) return NULL;
    return &q->commands[q->front];
}

int peek_queue_at(queue_t *q, int i, queue_command_t *out) {
    if (i < 0 || i >= q->size) return 0;
    int idx = (q->front + i) % MAX_QUEUE_SIZE;
    *out = q->commands[idx];
    return 1;
}

bool is_queue_empty(queue_t *q) { return q->size == 0; }
bool is_queue_full(queue_t *q)  { return q->size == MAX_QUEUE_SIZE; }
int  get_queue_size(queue_t *q) { return q->size; }

void list_queue(queue_t *q) {
    for (int i = 0; i < q->size; i++) {
        int idx = (q->front + i) % MAX_QUEUE_SIZE;
        queue_command_t *c = &q->commands[idx];
        // escrever no stdout para ser apanhado pelo handle_query do controller
        char buf[1024];
        int n = snprintf(buf, sizeof(buf), "user=%s cmd_id=%d cmd=%s\n",
                         c->user_id, c->cmd_id, c->command);
        write(1, buf, n);
    }
}