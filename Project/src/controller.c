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
static pid_t g_shutdown_pid = 0;

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
    if(!queue_head) queue_tail =NULL;
    e->next= NULL;
    return e;
}

static int is_user_executing(const char *user_id) {
    CmdEntry *e = exec_head;
    while (e) {
        if (strcmp(e->user_id, user_id) == 0) return 1;
        e = e->next;
    }
    return 0;
}

//  Round-Robin: tira o primeiro cujo user_id não esteja já a executar
CmdEntry *queue_pop_rr(void) {
    CmdEntry *prev = NULL, *e = queue_head;
    while (e) {
        if (!is_user_executing(e->user_id)) {
            // remover e da fila
            if (prev) prev->next = e->next;
            else queue_head = e->next;
            if (e == queue_tail) queue_tail = prev;
            e->next = NULL;
            return e;
        }
        prev = e;
        e = e->next;
    }
    // todos os users já têm algo a executar — cair para FCFS
    return queue_pop_fcfs();
}

// Gestão de listas de execuçao
void exec_add(CmdEntry *e){
    e -> next = exec_head;
    exec_head = e;
}

CmdEntry *exec_remove(int cmd_id) {
    CmdEntry *prev = NULL, *e = exec_head;
    while (e) {
        if (e->cmd_id == cmd_id) {
            if (prev) prev->next = e->next;
            else exec_head = e->next;
            e->next = NULL;
            return e;
        }
        prev = e;
        e = e->next;
    }
    return NULL;
}

// CmdEntry *queue_pop


//Log persistente 
void log_command(const char *user_id, int cmd_id,const char *command, long duration_ms){
    int fd = open("controller.log", O_WRONLY | O_CREAT | O_APPEND, 0644);

    char buf[512];
    int n = snprintf(buf, sizeof(buf), "user=%s cmd_id=%d dutation=%ldms cmd=%s\n", user_id, cmd_id, duration_ms, command);

    write(fd, buf, n);
    close(fd);
}

// enviar autorização ao runner

void authorize_runner(CmdEntry *e) {
    char runner_fifo[64];

    snprintf(runner_fifo, sizeof(runner_fifo), "%s%d", RUNNER_FIFO_PREFIX, (int) e->runner_pid);

    Message auth = {
        .type = MSG_AUTHORIZE,
        .cmd_id = e->cmd_id
    };

    int fd = open(runner_fifo, O_WRONLY);
    write(fd, &auth, sizeof(auth));
    close(fd);
}

//  Escalonar próximos comandos (chamado após cada mudança de estado)
//verificar se pode autorizar mais comandos
void try_schedule(void) {
    void try_schedule(void) {
    while (g_running < g_max_parallel) {
        CmdEntry *e = (g_sched_policy == 0)
                        ? queue_pop_fcfs()
                        : queue_pop_rr();
        if (!e) break;

        exec_add(e);
        g_running++;
        authorize_runner(e);
    }
}

//  Responder a um pedido de query
void handle_query(pid_t runner_pid){
    char runner_fifo[64];
    snprintf(runner_fifo, sizeof(runner_fifo), "%s%d", RUNNER_FIFO_PREFIX, (int)runner_pid);

    // construir resposta
    char buf[4096];
    int pos = 0;
 
    pos += snprintf(buf + pos, sizeof(buf) - pos, "---\nExecuting\n");
    CmdEntry *e = exec_head;
    while (e && pos < (int)sizeof(buf) - 1) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,"user-id %s - command-id %d\n", e->user_id, e->cmd_id);
        e = e->next;
    }

    pos += snprintf(buf + pos, sizeof(buf) - pos, "---\nScheduled\n");
    e = queue_head;
    while (e && pos < (int)sizeof(buf) - 1) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,"user-id %s - command-id %d\n", e->user_id, e->cmd_id);
        e = e->next;
    }
 
    int fd = open(runner_fifo, O_WRONLY);
    if (fd < 0) { perror("query open"); return; }
    write(fd, buf, pos);
    close(fd);
}

//  Processar mensagem recebida -- ainda n acabado
void process_message(Message *msg){
    switch(msg->type){
        case MSG_EXECUTE: {
            // criar entrada na fila
            CmdEntry *e = malloc (sizeof(CmdEntry));
            if (!e) { perror("malloc"); return; }
            e-> cmd_id = g_cmd_counter++;
            e->runner_pid = msg->runner_pid;
            strncpy(e->user_id, msg->user_id, MAX_USER_LEN - 1);
            e->user_id[MAX_USER_LEN - 1] = '\0';
            strncpy(e->command, msg->command, MAX_CMD_LEN - 1);
            e->command[MAX_CMD_LEN - 1] = '\0';

            //guardar tempo de chegada
            struct timeval tv;
            gettimeofday(&tv, NULL);
            e->submit_time_ms = tv.tv_sec*1000 + tv.tv_usec/1000;
            e->next = NULL;

            queue_push(e);
            break;

        }

        case MSG_DONE: {
            //remover da lista de execução e registar log
            CmdEntry *e = exec_remove(msg->cmd_id);
            if (e) {
                struct timeval tv;
                gettimeofday(&tv, NULL);
                long now_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
                long dur    = now_ms - e->submit_time_ms;
                log_command(e->user_id, e->cmd_id, e->command, dur);
                free(e);
                g_running--;
            }
            break;
        }

        case MSG_QUERY:{
            handle_query(msg->runner_pid);
            break;
        }

        case MSG_SHUTDOWN: {
            g_shutdown_req = 1;
            // responder MSG_SHUTDOWN_OK quando n houver pendentes
            g_shutdown_pid = msg.runner_pid;
            break;
        }
        
        // Estes casos não chegam ao controller — silenciar warnings
        case MSG_AUTHORIZE:
        case MSG_QUERY_RESP:
        case MSG_SHUTDOWN_OK:
            break;
        

    }
}

// MAIN
int main(int argc, char *argv[]) {
    if (argc < 3) {
        write(STDERR_FILENO,"uso: controller <parallel-commands> <sched-policy>\n", 51);
        return 1;
    }
    // 1. ler argumentos
    g_max_parallel = atoi(argv[1]);
    g_sched_policy = atoi(argv[2]);

    // 2. criar o fifo do controller
    mkfifo(CONTROLLER_FIFO, 0666);

    // 3. abrir para leitura ( bloqueia até 1º runner)
    // Truque : abrir tbm em escrita pra n bloquear
    int fd_ctrl = open(CONTROLLER_FIFO, O_RDWR);
    // ou abrir em ler+escrita: O_RDWR
    if (fd_ctrl < 0) { perror("open controller fifo"); return 1; }

    // 4. loop principal
    while (!g_shutdown_req || g_running >0 || queue_head != NULL){
        Message msg;
        ssize_t n = read(fd_ctrl, &msg, sizeof(msg));
        if(n == sizeof(msg)){
            process_message(&msg);
        }
        try_schedule();
    }

    // aqui chegamos quando: shutdown pedido + fila vazia + nada a executar
    char runner_fifo[64];
    snprintf(runner_fifo, sizeof(runner_fifo), "%s%d", RUNNER_FIFO_PREFIX, (int)g_shutdown_pid);
    
    Message ok = { .type = MSG_SHUTDOWN_OK };
    int fd = open(runner_fifo, O_WRONLY);
    if (fd >= 0) { 
        write(fd, &ok, sizeof(ok)); close(fd);
    }
    
    //5. limpar 
    close(fd_ctrl);
    unlink(CONTROLLER_FIFO);
    return 0;
}