#include "logger.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

static int log_fd = -1; // descritor global do ficheiro de log

// abrir o ficheiro de log para escrita (criar se não existir, ou truncar se existir)
void init_logger(const char *log_file_path) {
    int fd = open(log_file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
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

// fechar o ficheiro de log
void close_logger(void) {
    if (log_fd >= 0) {
        close(log_fd);
        log_fd = -1;
    }
}