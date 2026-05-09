/* 
 logger.c — Registo persistente de comandos concluídos.

 O logger é responsável por manter um ficheiro de log (controller.log) com
 uma entrada por cada comando que o controller autorizou e que o runner
 notificou como concluído.

 Formato de cada linha:
   user=<user_id> cmd_id=<n> initial_timestamp=<ms> duration=<ms>ms cmd=<comando>

 Design:
  - O ficheiro é aberto em modo append (O_APPEND) para que os registos
    sobrevivam entre execuções do controller e não sejam truncados.
  - O descritor é mantido aberto durante toda a vida do controller para
    evitar o overhead de abrir/fechar a cada escrita.
  - Todas as operações de I/O usam a syscall write() diretamente,
    conforme exigido pelo enunciado (sem fwrite/fprintf).
 */

#include "logger.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

static int log_fd = -1; // descritor global do ficheiro de log

// abrir o ficheiro de log para escrita (criar se não existir, ou truncar se existir)
void init_logger(const char *log_file_path) {
    int fd = open(log_file_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if(fd < 0) {
        perror("Failed to open log file");
        return;
    }
    log_fd = fd;
}

// formatar a entrada de log e escrever no ficheiro
void log_command_execution(log_entry_t entry) {
    char buf[1024]; // buffer local para a linha de log
    int n = snprintf(buf, sizeof(buf), "user=%s cmd_id=%d initial_timestamp=%ld duration=%ldms cmd=%s\n",
                     entry.user_id, entry.cmd_id, entry.initial_timestamp, entry.duration_ms, entry.command);
    if(n < 0) {
        perror("Failed to format log entry");
        return;
    }
    if(log_fd >= 0) {
        if(write(log_fd, buf, n) < 0) {
            perror("Failed to write log entry");
        }
    }
}

// fechar o ficheiro de log e repoe log_fd para -1
void close_logger(void) {
    if (log_fd >= 0) {
        close(log_fd);
        log_fd = -1;
    }
}