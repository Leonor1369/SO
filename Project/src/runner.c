// cliente, pede autorização, executa comandos, reporta fim
// Modos: -e <user-id> "<comando>" | -c | -s

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include "protocol.h"


// ----- Auxiliar ----------------------------------------------
// constrói o caminho do FIFO de resposta deste runner
void get_runner_fifo(char *buf, size_t size pid_t pid) {
    // ex: /tmp/runner_12345
    snprintf(buf, size, "%s%d", RUNNER_FIFO_PREFIX, (int)pid);
}

// Escreve uma string formatada no stdout sem printf
static void out(const char *s) {
    write(STDOUT_FILENO, s, strlen(s));
}

// Escreve um int no stdout
static void out_int(long v) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%ld", v);
    write(STDOUT_FILENO, buf, n);
}

// ------- divisão do comando: divide string em argv[] -------------------------------
// FAZ: cmd arg1 arg2, pipes (|), redir (>, <, 2>)
// Retorna número de segmentos separados por '|'
// Cada segmento é um array de strings terminado em NULL
 
#define MAX_SEGMENTS 16
#define MAX_SEG_ARGS 64
 
typedef struct {
    char *argv[MAX_SEG_ARGS]; // argumentos do segmento
    int   argc;
    char *redir_in;           // < ficheiro
    char *redir_out;          // > ficheiro
    char *redir_err;          // 2> ficheiro
} Segment;


// int parse_command  -- Duplica a string do comando e divide em tokens



//....
//void exec_segment -- Execução de pipeline
//void run_pipeline -- Executa nseg segmentos em pipeline; retorna quando todos terminam.
//void free_segments --  Libertar segmentosLibertar segmentos


// --------handle_execute ---------------------------------------------------------------

void handle_execute(int argc, char *argv[]) {
    // argv[2] = user_id
    // argv[3] = comando
    // argv[4..] = argumentos do comando

    pid_t my_pid = getpid();
    char runner_fifo[64];
    get_runner_fifo(runner_fifo, my_pid);

    // 1. criar o FIFO de resposta
    mkfifo(runner_fifo, 0666);
    write(1, "FIFO criado\n", 12);
    
    // 2. abrir o FIFO do controller para escrita
    int fd_ctrl = open(CONTROLLER_FIFO, O_WRONLY);

    // 3. preencher a mensagem e enviar
    Message msg = { .type = MSG_EXECUTE, .runner_pid = my_pid, .user_id = atoi(argv[2]) };
    write(fd_ctrl, &msg, sizeof(msg));
    close(fd_ctrl);

    // 4. notificar utilizador
    write(STDOUT_FILENO, "[runner] command X submitted\n", 30);

    // 5. abrir o FIFO de resposta e aguardar autorização
    int fd_resp = open(runner_fifo, O_RDONLY);
    Message auth;
    read(fd_resp, &auth, sizeof(auth));

    // 6. notificar utilizador que vai executar
    write(STDOUT_FILENO, "[runner] executing command X...\n", 33);

    // 7. fork + exec
    pid_t child = fork();
    // if (child == 0) { execvp(...); }
    // else { waitpid(child, NULL, 0); }

    // 8. notificar utilizador que terminou
    // write(STDOUT_FILENO, "[runner] command X finished\n", ...);

    // 9. enviar MSG_DONE ao controller
    // ...

    // 10. limpar o FIFO de resposta
    // unlink(runner_fifo);
}



void handle_query() {
    // semelhante ao execute mas envia MSG_QUERY
    // e imprime a resposta no stdout
}


void handle_shutdown() {
    // envia MSG_SHUTDOWN
    // aguarda confirmação do controller
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        write(STDERR_FILENO, "uso: runner -e <user> <cmd> | -c | -s\n", 38);
        exit(1);
    }

    if (strcmp(argv[1], "-e") == 0) {
        if (argc < 4) {
            write(STDERR_FILENO, "uso: runner -e <user-id> <comando>\n", 35);
            exit(1);
        }
        handle_execute(argc, argv);

    } else if (strcmp(argv[1], "-c") == 0) {
        handle_query();

    } else if (strcmp(argv[1], "-s") == 0) {
        handle_shutdown();

    } else {
        write(STDERR_FILENO, "opcao invalida\n", 15);
        exit(1);
    }

    return 0;
}