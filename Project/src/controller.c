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
#include "logger.h"
#include "queue.h"
#include "scheduler.h"

// configuração global
static int   g_max_parallel = 1;
static int   g_sched_policy = 0;
static int   g_running      = 0;
static int   g_shutdown_req = 0;
static int   g_cmd_counter  = 1;
static pid_t g_shutdown_pid = 0;
static queue_t     g_queue;
static scheduler_t g_scheduler;

// lista de execução
typedef struct ExecEntry {
    queue_command_t  cmd;
    long             submit_time_ms;
    struct ExecEntry *next;
} ExecEntry;

static ExecEntry *exec_head = NULL;

// --- gestão da lista de execução ---

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

// --- enviar autorização ao runner ---

void authorize_runner_cmd(queue_command_t *cmd) {
    char runner_fifo[64];
    snprintf(runner_fifo, sizeof(runner_fifo), "%s%d", RUNNER_FIFO_PREFIX, (int)cmd->runner_pid);
    Message auth = { .type = MSG_AUTHORIZE, .cmd_id = cmd->cmd_id };
    int fd = open(runner_fifo, O_WRONLY);
    if (fd >= 0) { write(fd, &auth, sizeof(auth)); close(fd); }
}

// --- escalonar próximos comandos ---

void try_schedule(void) {
    while (g_running < g_max_parallel) {
        queue_command_t cmd;
        if (!next_command(&g_scheduler, &cmd)) break;
        exec_add_cmd(&cmd);
        g_running++;
        authorize_runner_cmd(&cmd);
    }
}

// --- responder a query ---

void handle_query(pid_t runner_pid) {
    char runner_fifo[64];
    snprintf(runner_fifo, sizeof(runner_fifo), "%s%d", RUNNER_FIFO_PREFIX, (int)runner_pid);

    char buf[4096];
    int pos = 0;

    pos += snprintf(buf + pos, sizeof(buf) - pos, "---\nExecuting\n");
    ExecEntry *e = exec_head;
    while (e && pos < (int)sizeof(buf) - 1) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "user-id %s - command-id %d\n", e->cmd.user_id, e->cmd.cmd_id);  // FIX: e->cmd.user_id
        e = e->next;
    }

    pos += snprintf(buf + pos, sizeof(buf) - pos, "---\nScheduled\n");
    for (int i = 0; i < get_queue_size(&g_queue); i++) {
        queue_command_t cmd;
        if (peek_queue_at(&g_queue, i, &cmd)) {
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                "user-id %s - command-id %d\n", cmd.user_id, cmd.cmd_id);
        }
    }

    int fd = open(runner_fifo, O_WRONLY);
    if (fd < 0) { perror("query open"); return; }
    write(fd, buf, pos);
    close(fd);
}

// --- processar mensagem ---

void process_message(Message *msg) {
    switch (msg->type) {
        case MSG_EXECUTE: {
            queue_command_t cmd;
            cmd.cmd_id     = g_cmd_counter++;
            cmd.runner_pid = msg->runner_pid;   // FIX: msg-> em vez de msg.
            strncpy(cmd.user_id, msg->user_id, MAX_USER_LEN - 1);
            cmd.user_id[MAX_USER_LEN - 1] = '\0';
            strncpy(cmd.command, msg->command, MAX_CMD_SIZE - 1);
            cmd.command[MAX_CMD_SIZE - 1] = '\0';

            struct timeval tv;
            gettimeofday(&tv, NULL);
            cmd.entry_time = tv.tv_sec;

            enqueue_command(&g_queue, cmd);
            break;
        }

        case MSG_DONE: {
            ExecEntry *e = exec_remove(msg->cmd_id);  // FIX: agora exec_remove está definida
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
                command_finished(&g_scheduler, e->cmd.user_id);

                free(e);
                g_running--;
            }
            break;
        }

        case MSG_QUERY:
            handle_query(msg->runner_pid);
            break;

        case MSG_SHUTDOWN:
            g_shutdown_req = 1;
            g_shutdown_pid = msg->runner_pid;
            break;

        case MSG_AUTHORIZE:
        case MSG_QUERY_RESP:
        case MSG_SHUTDOWN_OK:
            break;
    }
}

// --- MAIN ---

int main(int argc, char *argv[]) {
    if (argc < 3) {
        write(STDERR_FILENO, "uso: controller <parallel-commands> <sched-policy>\n", 51);
        return 1;
    }

    g_max_parallel = atoi(argv[1]);
    g_sched_policy = atoi(argv[2]);

    init_logger("controller.log");
    init_queue(&g_queue);
    init_scheduler(&g_scheduler, (scheduling_policy_t)g_sched_policy, &g_queue);

    mkfifo(CONTROLLER_FIFO, 0666);

    int fd_ctrl = open(CONTROLLER_FIFO, O_RDWR);
    if (fd_ctrl < 0) { perror("open controller fifo"); return 1; }

    while (!g_shutdown_req || g_running > 0 || !is_queue_empty(&g_queue)) {
        Message msg;
        ssize_t n = read(fd_ctrl, &msg, sizeof(msg));
        if (n == sizeof(msg)) {
            process_message(&msg);
        }
        try_schedule();
    }

    // shutdown: notificar runner que pediu
    char runner_fifo[64];
    snprintf(runner_fifo, sizeof(runner_fifo), "%s%d", RUNNER_FIFO_PREFIX, (int)g_shutdown_pid);
    Message ok = { .type = MSG_SHUTDOWN_OK };
    int fd = open(runner_fifo, O_WRONLY);
    if (fd >= 0) { write(fd, &ok, sizeof(ok)); close(fd); }

    close(fd_ctrl);
    unlink(CONTROLLER_FIFO);
    close_logger();
    return 0;
}