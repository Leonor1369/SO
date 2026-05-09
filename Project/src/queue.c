// queue.c - implementação da fila circular de comandos 

/*A fila é a estrutura de dados central do controller: todos os comandos
 submetidos pelos runners são inseridos aqui e retirados pelo scheduler
 de acordo com a política configurada.

 Estrutura interna:
  A fila é implementada como um array circular de tamanho fixo MAX_QUEUE_SIZE.
  Os índices front e rear marcam, respetivamente, o primeiro elemento válido
  e a posição onde o próximo elemento será inserido.

  Invariantes:
   - size == 0 ↔ fila vazia
   - size == MAX_QUEUE_SIZE ↔ fila cheia
   - Os elementos válidos ocupam as posições:
       (front + 0) % MAX_QUEUE_SIZE
       (front + 1) % MAX_QUEUE_SIZE
       ...
       (front + size - 1) % MAX_QUEUE_SIZE

 Complexidade:
  - enqueue_command  : O(1)
  - dequeue_command  : O(1)
  - peek_queue       : O(1)
  - peek_queue_at    : O(1)
  - queue_remove_at  : O(n) — desloca os elementos seguintes
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include "queue.h"

// Inicializa a fila para o estado vazio
void init_queue(queue_t *q) {
    q->front = 0;
    q->rear  = -1;
    q->size  = 0;
}
// Insere um comando no fim da fila (FIFO)
int enqueue_command(queue_t *q, queue_command_t cmd) {
    if (is_queue_full(q)) return -1;
    q->rear = (q->rear + 1) % MAX_QUEUE_SIZE;
    q->commands[q->rear] = cmd;
    q->size++;
    return 0;
}
// remove e devolve o comando mais antigo da fila e copia para *cmd, ou retorna -1 se a fila estiver vazia
int dequeue_command(queue_t *q, queue_command_t *cmd) {
    if (is_queue_empty(q)) return -1;
    *cmd = q->commands[q->front];
    q->front = (q->front + 1) % MAX_QUEUE_SIZE;
    q->size--;
    return 0;
}
// Remove o elemento na posição lógica i (0 = mais antigo)
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
// Devolve um ponteiro para o elemento mais antigo sem o remover
queue_command_t* peek_queue(queue_t *q) {
    if (is_queue_empty(q)) return NULL;
    return &q->commands[q->front];
}
// Copia o elemento na posição lógica i sem o remover, ou retorna 0 se i for inválido
int peek_queue_at(queue_t *q, int i, queue_command_t *out) {
    if (i < 0 || i >= q->size) return 0;
    int idx = (q->front + i) % MAX_QUEUE_SIZE;
    *out = q->commands[idx];
    return 1;
}

// helper para imprimir o conteúdo da fila (para debug)

bool is_queue_empty(queue_t *q) { return q->size == 0; } // Verifica se a fila está vazia
bool is_queue_full(queue_t *q)  { return q->size == MAX_QUEUE_SIZE; }  // Verifica se a fila está cheia
int  get_queue_size(queue_t *q) { return q->size; }  // Devolve o número atual de elementos na fila.

// imprimir a fila no stdout (para o handle_query do controller)
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