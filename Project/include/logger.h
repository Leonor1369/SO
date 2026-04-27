#ifndef LOGGER_H
#define LOGGER_H

#define MAX_CMD_LEN 256
#define MAX_USER_LEN 64

    // ── Estrutura do logger ───────────────────────────────────────────
typedef struct {
    char user_id[MAX_USER_LEN];
    int cmd_id;
    char command[MAX_CMD_LEN];
    long initial_timestamp;  // quando o comando foi recebido
    long duration_ms;        // duração da execução
} log_entry_t;

    // ── Função do Logger ───────────────────────────────────────────
    void init_logger(const char* log_file_path);
    void log_command_execution(log_entry_t entry);
    void close_logger();

#endif
