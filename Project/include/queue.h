#ifndef QUEUE_H
#define QUEUE_H

#include <sys/types.h>
#include <stdbool.h>

#define MAX_USER_LEN 64
#define MAX_CMD_SIZE 256
#define MAX_QUEUE_SIZE 1024  

    // ── Estrutura de Comando na Fila ───────────────────────────────────────────
typedef struct {
    char user_id[MAX_USER_LEN];  // id do utilizador
    int cmd_id;             // identificador do comando
    pid_t runner_pid;       // PID do runner
    char command[MAX_CMD_SIZE]; // comando a executar
    time_t entry_time;         // tempo de entrada na fila (timestamp)
    int priority;            // prioridade do comando (maior valor = maior priority)
} queue_command_t;


    // ── Estrutura da Fila ───────────────────────────────────────────
typedef struct {
    queue_command_t commands[MAX_QUEUE_SIZE];
    int front;   // índice do primeiro elemento
    int rear;    // índice do último elemento
    int size;    // número atual de elementos
} queue_t;
    
    // ── Funçoes da Fila ───────────────────────────────────────────
// Inicialização
void init_queue(queue_t *q);

// Inserir comando
int enqueue_command(queue_t *q, queue_command_t cmd);

// Remover comando (FIFO)
int dequeue_command(queue_t *q, queue_command_t *cmd);

// Consultar sem remover
queue_command_t* peek_queue(queue_t *q);

// Consultar elemento na posição i sem remover
int peek_queue_at(queue_t *q, int i, queue_command_t *out);

// remover elemento na posição i (não necessariamente o front)
int queue_remove_at(queue_t *q, int i, queue_command_t *out);

// Estado
bool is_queue_empty(queue_t *q);
bool is_queue_full(queue_t *q);
int get_queue_size(queue_t *q);

// Listar todos os elementos (para -c)
void list_queue(queue_t *q);

#endif