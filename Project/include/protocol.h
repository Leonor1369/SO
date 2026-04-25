#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <unistd.h>

#define CONTROLLER_FIFO "/tmp/controller_in"
#define RUNNER_FIFO_PREFIX "/tmp/runner_"


#define MAX_CMD_LEN 256
#define MAX_USER_LEN 64
#define MAX_ARGS 32

typedef enum {
    MSG_EXECUTE,    // runner pede para executar
    MSG_AUTHORIZE,  // controller autoriza execução
    MSG_DONE,       // runner notifica que terminou
    MSG_QUERY,      // runner pede lista de comandos
    MSG_QUERY_RESP, // controller responde à consulta
    MSG_SHUTDOWN,    // runner pede para desligar
    MSG_SHUTDOWN_OK  // controlle confirma shutdown
} MsgType;

typedef struct {
    MsgType type;
    pid_t   runner_pid;       // para o controller saber a quem responder
    int     cmd_id;           // identificador único do comando
    char    user_id[MAX_USER_LEN];  // id do utilizador
    char    command[MAX_CMD_LEN]; // comando completo (string)
    long    duration_ms;        // duração em ms 
} Message;

// Estrutura para entrada na fila/lista do controller
typedef struct CmdEntry {
    int             cmd_id;
    pid_t           runner_pid;
    char            user_id[MAX_USER_LEN];
    char            command[MAX_CMD_LEN];
    long            submit_time_ms;  // quando o pedido chegou
    struct CmdEntry *next;
} CmdEntry;

#endif