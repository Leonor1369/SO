// escalonador central, recebe pedidos e autoriza execuções

// controller.c — escalonador central
// Recebe pedidos de runners via FIFO, escalonamento FIFO, autoriza execuções,
// regista log, responde a queries e suporta shutdown gracioso.
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <errno.h>
#include "protocol.h"

// configuração global
static int  g_max_parallel   = 1;   // parallel-commands (arg 1)
static int  g_sched_policy   = 0;   // 0 = FCFS, 1 = Round-Robin por user
static int  g_running        = 0;   // comandos a executar agora
static int  g_shutdown_req   = 0;   // alguém pediu shutdown
static int  g_cmd_counter    = 1;   // gerador de cmd_id único

// filas
static CmdEntry *queue_head  = NULL; // fila de espera
static CmdEntry *queue_tail  = NULL;
static CmdEntry *exec_head   = NULL; // a executar agora

// void queue_push(CmdEntry *e) - gestao de fila de espera

// CmdEntry *queue_pop_fcfs -- FCFS: tira o primeiro

// CmdEntry *queue_pop_rr(void) --  // Round-Robin: tira o primeiro cujo user_id não esteja já a executar

// CmdEntry *queue_pop

// Gestão da lista de execução
/*void exec_add(CmdEntry *e) ;
CmdEntry *exec_remove(int cmd_id);*/

//Log persistente --void log_command(const char *user_id, int cmd_id,const char *command, long duration_ms)

// void authorize_runner(CmdEntry *e) -- enviar autorização ao runner

// void try_schedule(void) -- Escalonar próximos comandos (chamado após cada mudança de estado)

// void handle_query(pid_t runner_pid) -- Responder a um pedido de query

// void process_message(Message *msg) -- Processar mensagem recebida

// MAIN