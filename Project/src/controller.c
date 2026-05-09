/**
controller.c — Escalonador central do sistema de orquestração de comandos.

 Responsabilidades:
  - Receber pedidos de execução (MSG_EXECUTE) de múltiplos runners via FIFO nomeado.
  - Manter uma fila de comandos pendentes e escaloná-los segundo a política configurada.
  - Autorizar runners a executar (MSG_AUTHORIZE) quando um slot de execução fica livre.
  - Receber notificações de conclusão (MSG_DONE) e atualizar o estado interno.
  - Responder a consultas de estado (MSG_QUERY) sem bloquear o processamento principal.
  - Registar em ficheiro cada comando concluído (utilizador, id, duração, timestamp).
  - Suportar shutdown gracioso: aguarda todos os comandos em curso antes de terminar.

 Comunicação:
  - FIFO de entrada:  /tmp/controller_in  (lido pelo controller)
  - FIFO de resposta: /tmp/runner_<pid>   (escrito pelo controller, lido pelo runner)

 Compilação (via Makefile):
  gcc -Wall -g -Iinclude src/controller.c src/queue.c src/scheduler.c src/logger.c -o bin/controller

 Utilização:
  ./controller <parallel-commands> <sched-policy>
    parallel-commands : número máximo de comandos a executar em simultâneo
    sched-policy      : 0=FCFS, 1=SJF, 2=Priority, 3=RoundRobin
*/
 
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
#include <signal.h>
#include <time.h>

#include "protocol.h"
#include "logger.h"
#include "queue.h"
#include "scheduler.h"

/* Tempo máximo (segundos) que o controller espera após receber MSG_SHUTDOWN
antes de forçar a saída, caso algum runner morra sem enviar MSG_DONE. */
#define SHUTDOWN_TIMEOUT_S 10


// configuração global
static int   g_max_parallel = 1; // slots de execução pararlela
static int   g_sched_policy = 0; // política de escalonamento: 0=FCFS, 1=SJF, 2=Priority, 3=RoundRobin
static int   g_running      = 0; // número de comandos atualmente em execução
static int   g_shutdown_req = 0; // flag: foi recebido o pedido de shitdown
static int   g_cmd_counter  = 1; // cont global para atribuir cmd_id únicos a cada comando recebido
static pid_t g_shutdown_pid = 0; // PID do runne que pediu o shutdown, para enviar confirmação no final
static queue_t     g_queue; // fila de comandos pendentes
static scheduler_t g_scheduler; // escalonar ( encapsula politica + fila)

// lista de execução
 
/*
ExecEntry — nó da lista ligada de comandos atualmente em execução.

*/
typedef struct ExecEntry {
    queue_command_t  cmd; // copia completa do comando
    long submit_time_ms; // timestamp de submissao em ms (para calcular duração)
    struct ExecEntry *next; // proximo no da lista
} ExecEntry;

static ExecEntry *exec_head = NULL;

// --- gestão da lista de execução ---
// adicionar comando à lista de execução
void exec_add_cmd(queue_command_t *cmd) {
    ExecEntry *e = malloc(sizeof(ExecEntry));
    if (!e) { perror("malloc ExecEntry"); return; }
    e->cmd = *cmd;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    e->submit_time_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
    e->next = exec_head;
    exec_head = e;
}

// remove e retorna a entrada de execução com o cmd_id dado, ou NULL se não existir
ExecEntry *exec_remove(int cmd_id) {
    ExecEntry *prev = NULL, *e = exec_head;
    while (e) {
        if (e->cmd.cmd_id == cmd_id) {
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

// --- Envia MSG_AUTHORIZE ao FIFO privado do runner

void authorize_runner_cmd(queue_command_t *cmd) {
    char runner_fifo[64];
    snprintf(runner_fifo, sizeof(runner_fifo), "%s%d", RUNNER_FIFO_PREFIX, (int)cmd->runner_pid);
    Message auth = { .type = MSG_AUTHORIZE, .cmd_id = cmd->cmd_id };
    int fd = open(runner_fifo, O_WRONLY);
    if (fd >= 0) { write(fd, &auth, sizeof(auth)); close(fd); }
}



// escalonamento 


// Tenta preencher os slots de execução disponíveis, autorizando comandos do scheduler
void try_schedule(void) {
    while (g_running < g_max_parallel) {
        queue_command_t cmd;
        if (!next_command(&g_scheduler, &cmd)) break;
        exec_add_cmd(&cmd);
        g_running++;
        authorize_runner_cmd(&cmd);
    }
}

// Responde a um pedido MSG_QUERY de um runner
void handle_query(pid_t runner_pid) {

    char buf[4096];
    int pos = 0;

    // listar comandos em execução
    pos += snprintf(buf + pos, sizeof(buf) - pos, "---\nExecuting\n");
    ExecEntry *e = exec_head;
    while (e && pos < (int)sizeof(buf) - 1) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "user-id %s - command-id %d\n", e->cmd.user_id, e->cmd.cmd_id);  
        e = e->next;
    }
    
    //  listar comandos na fila de espera
    pos += snprintf(buf + pos, sizeof(buf) - pos, "---\nScheduled\n");
    for (int i = 0; i < get_queue_size(&g_queue); i++) {
        queue_command_t cmd;
        if (peek_queue_at(&g_queue, i, &cmd)) {
            pos += snprintf(buf + pos, sizeof(buf) - pos,
            "user-id %s - command-id %d\n", cmd.user_id, cmd.cmd_id);
        }
    }

    // Criar um fork para não bloquear o controller enquanto o runner lê a resposta
    // O filho trata disso de forma assíncrona enquanto o pai continua a processar mensagens
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }else if (pid == 0)
    {
        // filho: abre o FIFO do runner e envia a resposta
        char runner_fifo[64];
        snprintf(runner_fifo, sizeof(runner_fifo), "%s%d", RUNNER_FIFO_PREFIX, (int)runner_pid);

        int fd = open(runner_fifo, O_WRONLY);
        if (fd >= 0) {
            write(fd, buf, pos);
            close(fd);
        }
        _exit(0); // filho termina após enviar a resposta 
    }
    // pai: continua o loop principal imediatamente, sem esperar pelo filho
}

// --- processar mensagem ---


//  Despacha uma mensagem recebida pelo controller
void process_message(Message *msg) {
    switch (msg->type) {
        // Atribui cmd_id, copia campos da mensagem para um
 *      // queue_command_t e insere na fila de espera. O escalonamento
 *      // ocorre depois, em try_schedule().
        case MSG_EXECUTE: {
            // criar entrada na fila com id unico e metadados do pedido
            queue_command_t cmd;
            cmd.cmd_id     = g_cmd_counter++;
            cmd.runner_pid = msg->runner_pid;  
            strncpy(cmd.user_id, msg->user_id, MAX_USER_LEN - 1);
            cmd.user_id[MAX_USER_LEN - 1] = '\0';
            strncpy(cmd.command, msg->command, MAX_CMD_LEN - 1);
            cmd.command[MAX_CMD_LEN - 1] = '\0';

            struct timeval tv;
            gettimeofday(&tv, NULL);
            cmd.entry_time = tv.tv_sec;
            cmd.priority = msg->priority;

            enqueue_command(&g_queue, cmd);
            break;
        }
        
        case MSG_DONE: {
            /* O runner concluiu a execução. 
            Calcular a duração real (tempo desde que o controller recebeu o pedido até agora),
            registar no log e informar o scheduler para atualizar o histórico SJF do utilizador. */
            ExecEntry *e = exec_remove(msg->cmd_id);  
            if (e) {
                struct timeval tv;
                gettimeofday(&tv, NULL);
                long now_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
                long dur = now_ms - e->submit_time_ms;

                log_entry_t entry;
                strncpy(entry.user_id, e->cmd.user_id, MAX_USER_LEN - 1);
                entry.user_id[MAX_USER_LEN - 1] = '\0';
                entry.cmd_id = e->cmd.cmd_id;
                strncpy(entry.command, e->cmd.command, MAX_CMD_LEN - 1);
                entry.command[MAX_CMD_LEN - 1] = '\0';
                entry.initial_timestamp = e->submit_time_ms;
                entry.duration_ms = dur;
                log_command_execution(entry);
                // atualiza histograma SJF com duração real para este utilizador
                command_finished(&g_scheduler, e->cmd.user_id, dur);

                free(e);
                g_running--;
            }
            break;
        }
        
        case MSG_QUERY:
        // delegar numa função separada para manter process_message simples e não bloquear o loop principal
            handle_query(msg->runner_pid);

            break;
        case MSG_SHUTDOWN:
        // registar pedido; o loop principal aguardará o fim dos comandos em execução 
        // e depois enviará a confirmação ao runner que pediu o shutdown
            g_shutdown_req = 1;
            g_shutdown_pid = msg->runner_pid;
            break;
        // mensagens que controller não espera receber do runner — ignorar
        case MSG_AUTHORIZE:
        case MSG_QUERY_RESP:
        case MSG_SHUTDOWN_OK:
            break;
    }
}

// --- MAIN ---
// Inicializa o sistema e executa o loop principal de eventos.
int main(int argc, char *argv[]) {
    // 1. validar argumentos
    if (argc < 3) {
        write(STDERR_FILENO, "uso: controller <parallel-commands> <sched-policy>\n", 51);
        return 1;
    }
    // 2. inicializar estruturas de dados, logger e FIFO
    g_max_parallel = atoi(argv[1]);
    g_sched_policy = atoi(argv[2]);

    // validar política de escalonamento
    init_logger("controller.log");
    init_queue(&g_queue);
    init_scheduler(&g_scheduler, (scheduling_policy_t)g_sched_policy, &g_queue);

    // criar FIFO de entrada do controller
    mkfifo(CONTROLLER_FIFO, 0666);

    // 3. abrir FIFO em O_RDWR para evitar EOF ao esvaziar o FIFO 
    // (se nenhum runner estiver escrevendo, o controller não bloqueia nem termina)
    int fd_ctrl = open(CONTROLLER_FIFO, O_RDWR);
    if (fd_ctrl < 0) { perror("open controller fifo"); return 1; }

    // tornar fd_ctrl não-bloqueante para usar com select()
    int flags = fcntl(fd_ctrl, F_GETFL, 0);
    fcntl(fd_ctrl, F_SETFL, flags | O_NONBLOCK);

    // 4. loop principal: processar mensagens, escalonar comandos e monitorizar runners
    time_t shutdown_deadline = 0;


    // Condição de saída: sair apenas quando o shutdown foi pedido E
    // não há comandos em execução E a fila está vazia.
    // Isto garante que todos os comandos submetidos antes do shutdown
    // são processados antes de o controller terminar.
    while (!g_shutdown_req || g_running > 0 || !is_queue_empty(&g_queue)) {

        // 4a. watchdog: forçar saída se o timeout de shutdown expirou
        if (g_shutdown_req && shutdown_deadline != 0 && time(NULL) >= shutdown_deadline) {
            write(STDERR_FILENO,
                  "[controller] timeout: runners provavelmente mortos, a sair.\n", 61);
            break;
        }

        //4b. select() com timeout de 500 ms:
        //  - Evita busy-wait quando não há mensagens.
        //  - Permite detetar periodicamente runners mortos via waitpid().
        //  - O timeout curto garante que try_schedule() é chamado frequentemente.
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd_ctrl, &rfds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 500000 };
        int ready = select(fd_ctrl + 1, &rfds, NULL, NULL, &tv);

        // 4c. se houver mensagens, ler e processar
        if (ready > 0) {
            Message msg;
            ssize_t n = read(fd_ctrl, &msg, sizeof(msg));
            if (n == sizeof(msg)) {
                process_message(&msg);
                // ativar deadline na primeira mensagem de shutdown
                if (msg.type == MSG_SHUTDOWN && shutdown_deadline == 0)
                    shutdown_deadline = time(NULL) + SHUTDOWN_TIMEOUT_S;
            }
        }

    // 4d. tentar escalonar comandos sempre que possível (ex: após receber um pedido ou terminar um comando)
        try_schedule();

        // 4e. recolher processos filho:
        //  - Filhos criados por handle_query() para enviar respostas.
        //  - Runners que morreram sem enviar MSG_DONE (detetados pelo PID).
        //    Nesse caso, decrementamos g_running e informamos o scheduler.
        
        pid_t dead;
        while ((dead = waitpid(-1, NULL, WNOHANG)) > 0) {
            // verificar se o pid morto corresponde a um runner em execução
            ExecEntry *prev = NULL, *e = exec_head;
            while (e) {
                if (e->cmd.runner_pid == dead) {
                    write(STDERR_FILENO, "[controller] runner morreu sem MSG_DONE\n", 41);
                    if (prev) prev->next = e->next;
                    else exec_head = e->next;
                    command_finished(&g_scheduler, e->cmd.user_id, 0);
                    free(e);
                    g_running--;
                    break;
                }
                prev = e;
                e = e->next;
            }
        }
    }

    // 5. notificar runner que pediu o shutdown e limpar recursos antes de sair
    char runner_fifo[64];
    snprintf(runner_fifo, sizeof(runner_fifo), "%s%d", RUNNER_FIFO_PREFIX, (int)g_shutdown_pid);
    Message ok = { .type = MSG_SHUTDOWN_OK };
    int fd = open(runner_fifo, O_WRONLY);
    if (fd >= 0) { write(fd, &ok, sizeof(ok)); close(fd); }

    close(fd_ctrl);
    unlink(CONTROLLER_FIFO); // remover o FIFO do sistema de ficheiros
    close_logger();
    return 0;
}