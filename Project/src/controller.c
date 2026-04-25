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

//  gestao de fila de espera -- adicionar á fila 
void queue_push(CmdEntry *e) {
    e-> next = NULL;
    if(queue_tail) queue_tail->next=e;
    else queue_head=e;
    queue_tail =e;
}

// FCFS: tira o primeiro
CmdEntry *queue_pop_fcfs(void){
    if(!queue_head) return NULL;
    CmdEntry *e = queue_head;
    queue_head = e->next;
    if(queue_head) queue_tail =NULL;
    e->next= NULL;
    return e;
}

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
int main(int argc, char *argv[]) {
    // 1. ler argumentos
    g_max_parallel = atoi(argv[1]);
    g_sched_policy = atoi(argv[2]);

    // 2. criar o fifo do controller
    mkfifo(CONTROLLER_FIFO, 0666);

    // 3. abrir para leitura ( bloqueia até 1º runner)
    // Truque : abrir tbm em escrita pra n bloquear
    int fd_ctrl = open(CONTROLLER_FIFO, O_RDONLY | O_NONBLOCK);
    // ou abrir em ler+escrita: O_RDWR
    

    // 4. loop principal
    while (!g_shutdown_rep || g_running >0){
        Message msg;
        ssize_t n = read(fd_ctrl, &msg, sizeof(msg));
        if(n == sizeof(msg)){
            process_message(&msg);
        }
        try_schedule();
    }

    //5. limpar 
    close(fd_ctrl);
    unlink(CONTROLLER_FIFO);
    return 0;
}