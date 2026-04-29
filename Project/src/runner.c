// cliente, pede autorização, executa comandos, reporta fim
// Modos: -e <user-id> "<comando>" | -c | -s


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


// ----- Auxiliar ----------------------------------------------
// constrói o caminho do FIFO de resposta deste runner
void get_runner_fifo(char *buf, size_t size, pid_t pid) {
    // ex: /tmp/runner_12345
    snprintf(buf, size, "%s%d", RUNNER_FIFO_PREFIX, (int)pid);
}

// Escreve uma string formatada no stdout sem printf
static void out(const char *s) {
    write(STDOUT_FILENO, s, strlen(s));
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


// Duplica a string do comando e divide em tokens
int parse_command (const char *cmd_str, Segment segs[MAX_SEGMENTS]) {
    // duplicar a string para poder modificá-la com strtok
    char *copy = strdup(cmd_str);
    if (!copy) return -1;

    int nseg = 0;
    char *seg_str = strtok(copy, "|"); // divide por | → cada parte é um segmento

    while (seg_str != NULL && nseg < MAX_SEGMENTS) {
        Segment *s = &segs[nseg];
        s->argc = 0;
        s->redir_in  = NULL;
        s->redir_out = NULL;
        s->redir_err = NULL;

        
        char *tok = strtok(seg_str, " \t"); //  divide cada segmento por espaços → tokens
        while (tok != NULL && s->argc < MAX_SEG_ARGS - 1) {
            // direcionar
            if (strcmp(tok, ">") == 0) {
                tok = strtok(NULL, " \t");
                if (tok) s->redir_out = strdup(tok);// stdout 

            } else if (strcmp(tok, "<") == 0) {
                tok = strtok(NULL, " \t");
                if (tok) s->redir_in = strdup(tok); //stdin

            } else if (strcmp(tok, "2>") == 0) {
                tok = strtok(NULL, " \t");
                if (tok) s->redir_err = strdup(tok);// stderr

            } else {
                s->argv[s->argc++] = strdup(tok); // argumentos do commando
            }
            tok = strtok(NULL, " \t");
        }
        s->argv[s->argc] = NULL; // terminar argv com NULL

        nseg++;
        seg_str = strtok(NULL, "|");
    }

    free(copy);
    return nseg; // retorna número de segmentos
}


//....
//Execução de pipeline
// é executado npp processo filho e trata dos direcionamentos antes de fazer execvp
void exec_segment (Segment *s, int fd_in, int fd_out) {
    // redirecionar input
    // fd_in e fd_out são os descritores do pipe que vêm do run_pipeline 
    // — se não há pipe, passamos STDIN_FILENO e STDOUT_FILENO diretamente
    if (s->redir_in) {
        int fd = open(s->redir_in, O_RDONLY);
        if (fd < 0) { 
            perror("open redir_in");
            _exit(1); 
        }
        dup2(fd, STDIN_FILENO); //  faz com que o stdin passe a ser o ficheiro/pipe fd
        close(fd);
    } else if (fd_in != STDIN_FILENO) {
        // input vem do pipe anterior
        dup2(fd_in, STDIN_FILENO);
        close(fd_in);
    }

    // redirecionar output
    if (s->redir_out) {
        int fd = open(s->redir_out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { perror("open redir_out"); _exit(1); }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    } else if (fd_out != STDOUT_FILENO) {
        // output vai para o pipe seguinte
        dup2(fd_out, STDOUT_FILENO);
        close(fd_out);
    }

    // redirecionar stderr
    if (s->redir_err) {
        int fd = open(s->redir_err, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { perror("open redir_err"); _exit(1); }
        dup2(fd, STDERR_FILENO);
        close(fd);
    }

    execvp(s->argv[0], s->argv);
    perror("execvp");
    _exit(1);
}

// executa uma pipeline de segmentos, ligando os pipes entre eles
void run_pipeline(Segment segs[], int nseg) {
    int prev_fd = STDIN_FILENO; // input do primeiro segmento é o stdin
    pid_t pids[MAX_SEGMENTS];

    for (int i = 0; i < nseg; i++) {
        int pipefd[2];
        if (i < nseg - 1) { // se não é o último segmento, criar pipe
            if (pipe(pipefd) < 0) {
                perror("pipe");
                exit(1);
            }
        } else {
            pipefd[0] = STDIN_FILENO;
            pipefd[1] = STDOUT_FILENO;
        }

        pid_t pid = fork();
        if (pid == 0) {
            // processo filho executa o segmento
            exec_segment(&segs[i], prev_fd, pipefd[1]);
        } else if (pid > 0) {
            pids[i] = pid;
            if (prev_fd != STDIN_FILENO) close(prev_fd); // fechar input do segmento anterior
            if (pipefd[1] != STDOUT_FILENO) close(pipefd[1]); // fechar output do segmento atual
            prev_fd = pipefd[0]; // input do próximo segmento vem do output deste
        } else {
            perror("fork");
            exit(1);
        }
    }

    // esperar por todos os filhos terminarem
    for (int i = 0; i < nseg; i++) {
        waitpid(pids[i], NULL, 0);
    }
}

// liberar memória alocada para os segmentos
void free_segments(Segment segs[], int nseg) {
    for (int i = 0; i < nseg; i++) {
        for (int j = 0; j < segs[i].argc; j++) {
            free(segs[i].argv[j]);
        }
        if (segs[i].redir_in) free(segs[i].redir_in);
        if (segs[i].redir_out) free(segs[i].redir_out);
        if (segs[i].redir_err) free(segs[i].redir_err);
    }
}


// --------handle_execute ---------------------------------------------------------------

void handle_execute(int argc, char *argv[]) {
    pid_t my_pid = getpid();
    char runner_fifo[64];
    get_runner_fifo(runner_fifo, sizeof(runner_fifo), my_pid);

    // 1. criar o FIFO de resposta
    mkfifo(runner_fifo, 0666);

    // 2. enviar pedido ao controller
    Message msg = {0};
    msg.type = MSG_EXECUTE;
    msg.runner_pid = my_pid;
    strncpy(msg.user_id, argv[2], MAX_USER_LEN - 1);
    strncpy(msg.command, argv[3], MAX_CMD_LEN - 1);

    int fd_ctrl = open(CONTROLLER_FIFO, O_WRONLY);
    write(fd_ctrl, &msg, sizeof(msg));
    close(fd_ctrl);


    // 3. notificar utilizador
    char buf[128];
    snprintf(buf, sizeof(buf), "[runner] command %d submitted\n", (int)my_pid);
    out(buf);

    // 4. aguardar autorização
    int fd_resp = open(runner_fifo, O_RDONLY);
    Message auth;
    read(fd_resp, &auth, sizeof(auth));
    close(fd_resp);
    
    // 5. executar o comando
    snprintf(buf, sizeof(buf), "[runner] executing command %d...\n", (int)my_pid);
    out(buf);

    struct timeval t_start, t_end;   // ← declarar AQUI, antes do fork
    gettimeofday(&t_start, NULL);    // ← iniciar AQUI, antes do fork

    Segment segs[MAX_SEGMENTS];
    int nseg = parse_command(argv[3], segs);
    run_pipeline(segs, nseg);
    free_segments(segs, nseg);

    snprintf(buf, sizeof(buf), "[runner] command %d finished\n", (int)my_pid);
    out(buf);

    // 6. calcular duração e enviar MSG_DONE
    gettimeofday(&t_end, NULL);
    long dur_ms = (t_end.tv_sec  - t_start.tv_sec)  * 1000 + (t_end.tv_usec - t_start.tv_usec) / 1000;

    Message done = {0};
    done.type        = MSG_DONE;
    done.runner_pid  = my_pid;
    done.cmd_id      = auth.cmd_id;
    done.duration_ms = dur_ms;

    fd_ctrl = open(CONTROLLER_FIFO, O_WRONLY);
    if (fd_ctrl >= 0) {
        write(fd_ctrl, &done, sizeof(done));
        close(fd_ctrl);
    }

    // 7. limpar
    unlink(runner_fifo);
}



void handle_query(void) {
    // semelhante ao execute mas envia MSG_QUERY
    // e imprime a resposta no stdout

    pid_t my_pid = getpid();
    char runner_fifo[64];
    get_runner_fifo(runner_fifo, sizeof(runner_fifo), my_pid);

    // 1. criar o FIFO de resposta
    mkfifo(runner_fifo, 0666);
    
    // 2. enviar pedido
    Message msg = {.type = MSG_QUERY, .runner_pid = my_pid};

    int fd_ctrl = open(CONTROLLER_FIFO, O_WRONLY);
    write(fd_ctrl, &msg, sizeof(msg));
    close(fd_ctrl);

    // 3. receber e imprimir resposta
    // o controller envia uma string com a lista
    int fd_resp = open(runner_fifo, O_RDONLY);

    char buf[4096];
    ssize_t n = read(fd_resp, buf, sizeof(buf)-1);
    if (n > 0){
        buf[n]= '\n';
        write(STDOUT_FILENO, buf, n);
    }
    close(fd_resp);

    unlink(runner_fifo);

}


void handle_shutdown() {
    // envia MSG_SHUTDOWN
    // aguarda confirmação MSG_SHUTDOWN_OK do controller

    pid_t my_pid = getpid();
    char runner_fifo[64];
    get_runner_fifo(runner_fifo, sizeof(runner_fifo), my_pid);

    // 1. criar o FIFO de resposta
    mkfifo(runner_fifo, 0666);
    
    // 2. enviar pedido
    Message msg = {.type = MSG_SHUTDOWN, .runner_pid = my_pid};

    int fd_ctrl = open(CONTROLLER_FIFO, O_WRONLY);
    write(fd_ctrl, &msg, sizeof(msg));
    close(fd_ctrl);

    // 3. receber confirmação do controller
    int fd_resp = open(runner_fifo, O_RDONLY);
    Message resp;
    ssize_t n = read(fd_resp, &resp, sizeof(resp));
    if (n == sizeof(resp) && resp.type == MSG_SHUTDOWN_OK) {
        out("[runner] shutdown confirmado pelo controller\n");
    } else {
        out("[runner] erro ao receber confirmação de shutdown\n");
    }
    close(fd_resp);

    unlink(runner_fifo);
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