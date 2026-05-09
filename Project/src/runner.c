/*
  runner.c — Cliente do sistema de orquestração de comandos.
 
  O runner é um processo de curta duração que suporta três modos de operação:
 
   -e <user-id> [-p <prioridade>] "<comando>"
       Submete um comando ao controller, aguarda autorização, executa-o
       (com suporte a pipes e redirecionamentos) e notifica a conclusão.
 
   -c
       Consulta o controller sobre os comandos em execução e em espera,
       imprimindo a resposta no stdout.
 
   -s
       Pede ao controller para terminar de forma graciosa (aguarda que
       todos os comandos em curso terminem antes de sair).
 
  Comunicação:
   - Escreve mensagens em /tmp/controller_in  (FIFO do controller).
   - Cria /tmp/runner_<pid> para receber respostas do controller.
     Este FIFO privado é removido no fim da execução do runner.
 
  Compilação (via Makefile):
   gcc -Wall -g -Iinclude src/runner.c -o bin/runner
 *
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
#include "protocol.h"


// ----- Auxiliar ----------------------------------------------
// constrói o caminho do FIFO privado deste runner
//Cada runner tem o seu próprio FIFO para receber respostas do controller,
// identificado pelo PID para evitar colisões entre runners concorrentes.
void get_runner_fifo(char *buf, size_t size, pid_t pid) {
    
    snprintf(buf, size, "%s%d", RUNNER_FIFO_PREFIX, (int)pid);
    // buf -> /tmp/runner_<pid> buffer de destino, 
    // size -> tamanho do buffer, 
    //pid -> PID do runner atual 
}

// Escreve uma string no stdout usando write() 
static void out(const char *s) {
    write(1, s, strlen(s));
}

// ------- divisão do comando: divide string em argv[] -------------------------------

// FAZ: cmd arg1 arg2, pipes (|), redir (>, <, 2>)
// Retorna número de segmentos separados por '|'
// Cada segmento é um array de strings terminado em NULL
 
// Número máximo de segmentos de pipeline suportados (separados por '|')
#define MAX_SEGMENTS 16
// Número máximo de argumentos por segmento (incluindo NULL terminal)
#define MAX_SEG_ARGS 64
 

// representa um segmento de uma piepeline
typedef struct {
    char *argv[MAX_SEG_ARGS]; // array de strings terminado em NULL para passar a execvp()
    int   argc; //número de argumentos válidos em argv (exclui o NULL)
    char *redir_in;           // < ficheiro de redirecionamento de stdin
    char *redir_out;          // > ficheiro de redirecionamento de stdout
    char *redir_err;          // 2> ficheiro de redirecionamento de stderr
} Segment;


//  Divide uma string de comando em segmentos de pipeline e identifica redirecionamentos.
// cmd_str: string de comando completa (ex: "ls -l | grep .c > out.txt")
// segs: array de Segment para preencher com os segmentos encontrados
// Retorna o número de segmentos encontrados ou -1 em caso de erro.

int parse_command (const char *cmd_str, Segment segs[MAX_SEGMENTS]) {
    // duplicar a string para poder modificá-la com strtok_r
    char *copy = strdup(cmd_str);
    if (!copy) return -1;
 
    int nseg = 0;
    char *seg_save; 
    char *seg_str = strtok_r(copy, "|", &seg_save);
 
    // para cada segmento separado por '|', dividir em tokens e identificar redirecionamentos
    while (seg_str != NULL && nseg < MAX_SEGMENTS) {
        Segment *s = &segs[nseg];
        s->argc = 0;
        s->redir_in  = NULL;
        s->redir_out = NULL;
        s->redir_err = NULL;
 
        char *tok_save; // saveptr para o nível dos tokens (espaços) — separado do anterior
        char *tok = strtok_r(seg_str, " \t", &tok_save);
        while (tok != NULL && s->argc < MAX_SEG_ARGS - 1) {
            if (strcmp(tok, ">") == 0) {
                // próximo token é o ficheiro de destino do stdout 
                tok = strtok_r(NULL, " \t", &tok_save);
                if (tok) s->redir_out = strdup(tok); 
 
            } else if (strcmp(tok, "<") == 0) {
                // próximo token é o ficheiro de origem do stdin 
                tok = strtok_r(NULL, " \t", &tok_save); 
                if (tok) s->redir_in = strdup(tok);
 
            } else if (strcmp(tok, "2>") == 0) {
                // próximo token é o ficheiro de destino do stderr
                tok = strtok_r(NULL, " \t", &tok_save);
                if (tok) s->redir_err = strdup(tok);
 
            } else {
                // argumento normal do comando
                s->argv[s->argc++] = strdup(tok);
            }
            tok = strtok_r(NULL," \t", &tok_save);
        }
        s->argv[s->argc] = NULL; // erminar argv com NULL para execvp
 
        nseg++;
        seg_str = strtok_r(NULL, "|", &seg_save);
    }
 
    free(copy);
    return nseg;
}

//Configura redirecionamentos e executa um segmento via execvp
/*Lógica de redirecionamento (por ordem de prioridade):
  - Se redir_in  está definido: abre o ficheiro e redireciona para stdin.
  - Senão, se fd_in != STDIN_FILENO: o stdin vem do pipe anterior.
  - Se redir_out está definido: abre o ficheiro e redireciona stdout.
  - Senão, se fd_out != STDOUT_FILENO: o stdout vai para o pipe seguinte.
  - Se redir_err está definido: abre o ficheiro e redireciona stderr.

  s-> segmento a executar
  fd_in: descritor do pipe para input (ou STDIN_FILENO se não houver pipe)
  fd_out: descritor do pipe para output (ou STDOUT_FILENO se não houver pipe)
*/
void exec_segment (Segment *s, int fd_in, int fd_out) {
    // redirecionamento de stdin

    if (s->redir_in) {
        int fd = open(s->redir_in, O_RDONLY);
        if (fd < 0) { 
            perror("open redir_in");
            _exit(1); 
        }
        dup2(fd, STDIN_FILENO); 
        close(fd);
    } else if (fd_in != 0) {
        dup2(fd_in, 0);
        close(fd_in);
    }

    // redirecionamento de stdout
    if (s->redir_out) {
        int fd = open(s->redir_out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { perror("open redir_out"); _exit(1); }
        dup2(fd, 1);
        close(fd);
    } else if (fd_out != 1) {
        // output vai para o pipe seguinte
        dup2(fd_out, 1);
        close(fd_out);
    }

    // redirecionamento de stderr
    if (s->redir_err) {
        int fd = open(s->redir_err, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { perror("open redir_err"); _exit(1); }
        dup2(fd, 2);
        close(fd);
    }

    execvp(s->argv[0], s->argv);
    perror("execvp");
    _exit(1);
}


// Executa uma pipeline de segmentos ligados por pipes anónimo
/*Gestão de descritores:
 - O lado de escrita do pipe (pipefd[1]) é fechado no pai após o fork(),
   para que o filho seguinte receba EOF quando o filho atual terminar.
 - prev_fd guarda o lado de leitura do pipe anterior para passar ao filho seguinte como fd_in.

segs -> array de segmentos a executar
nseg -> número de segmentos válidos em segs
 */
void run_pipeline(Segment segs[], int nseg) {
    int prev_fd = 0; // stdin do primeiro segmento é o stdin do runner
    pid_t pids[MAX_SEGMENTS];

    for (int i = 0; i < nseg; i++) {
        int pipefd[2];
        if (i < nseg - 1) { 
            // criar um pipe para ligar este segmento ao próximo
            if (pipe(pipefd) < 0) {
                perror("pipe");
                exit(1);
            }
        } else {
            // último segmento : stdout vai para o stdout do runner
            pipefd[0] = 0;
            pipefd[1] = 1;
        }

        pid_t pid = fork();
        if (pid == 0) {
            // processo filho configura descritores e executa segmento
            exec_segment(&segs[i], prev_fd, pipefd[1]);
        } else if (pid > 0) {
            pids[i] = pid;
            if (prev_fd != 0) close(prev_fd); // fechar pipe anterior no pai
            if (pipefd[1] != 1) close(pipefd[1]); // fechar escrita do pipe atual no pai
            prev_fd = pipefd[0]; // leitura do pipe atual é input para o próximo segmento
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



// libertar memória alocada por parse_command para os segmentos e seus argumentos
// segs -> array de segmentos a limpar
// nseg -> número de segmentos válidos em segs
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


// MODOS DE OPERAÇÃO DO RUNNER

// --------handle_execute ---------------------------------------------------------------
// Modo -e: submeter e executar um comando.
// argc Numero de argumentos passados ao runner
// argv Array de strings com os argumentos passados ao runner
void handle_execute(int argc, char *argv[]) {
    char full_cmd[MAX_CMD_LEN] = {0};
    int cmd_start = 3; // indice onde começa o comando em agv
    int priority  = 0; // prioridade por omissao
 
    // verificar se existe a opção de prioridade -p e ajustar cmd_start e priority se necessário
    if (argc > 4 && strcmp(argv[3], "-p") == 0) {
        priority  = atoi(argv[4]);
        cmd_start = 5;
    }
    
    // construir a string completa do comando a partir dos restantes argumentos
    for (int i = cmd_start; i < argc; i++) {
        strncat(full_cmd, argv[i], MAX_CMD_LEN - strlen(full_cmd) - 1);
        if (i < argc - 1) strncat(full_cmd, " ", MAX_CMD_LEN - strlen(full_cmd) - 1);
    }
    
    pid_t my_pid = getpid();
    char runner_fifo[64];
    get_runner_fifo(runner_fifo, sizeof(runner_fifo), my_pid);
 
    //  criar o FIFO privado para receber a resposta do controller
    mkfifo(runner_fifo, 0666);
 
    // construir e encviar MSG_EXECUTE para o controller
    Message msg = {0};
    msg.type = MSG_EXECUTE;
    msg.runner_pid = my_pid;
    msg.priority   = priority;
    strncpy(msg.user_id, argv[2], MAX_USER_LEN - 1);
    strncpy(msg.command, full_cmd, MAX_CMD_LEN - 1);
 
    int fd_ctrl = open(CONTROLLER_FIFO, O_WRONLY);
    write(fd_ctrl, &msg, sizeof(msg));
    close(fd_ctrl);
 
    //  aguardar autorização do controller (bloquente até o controller responder)
    int fd_resp = open(runner_fifo, O_RDONLY);
    Message auth;
    read(fd_resp, &auth, sizeof(auth));
    close(fd_resp);

    // notificar utilizador  agora conhecemos o cmd_id atribuído pelo controller
    char buf[128];
    snprintf(buf, sizeof(buf), "[runner] command %d submitted\n", auth.cmd_id);
    out(buf);
    snprintf(buf, sizeof(buf), "[runner] executing command %d...\n", auth.cmd_id);
    out(buf);
 
    // medir tempo de execução real e correr a pipeline
    struct timeval t_start, t_end;  
    gettimeofday(&t_start, NULL);    
 
    Segment segs[MAX_SEGMENTS];
    int nseg = parse_command(full_cmd, segs);
    run_pipeline(segs, nseg);
    free_segments(segs, nseg);
 
    snprintf(buf, sizeof(buf), "[runner] command %d finished\n", auth.cmd_id);
    out(buf);
    
    gettimeofday(&t_end, NULL);
    long dur_ms = (t_end.tv_sec  - t_start.tv_sec)  * 1000 + (t_end.tv_usec - t_start.tv_usec) / 1000;
 
    // notificar o controller que o comando terminou
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
 
    // 7. limpar FIFO privado 
    unlink(runner_fifo);
}


// Modo -c: consultar comandos em execução e em espera.
/* Fluxo:
 1. Criar FIFO privado para receber a resposta.
 2. Enviar MSG_QUERY ao controller.
 3. Ler a resposta (string formatada) e imprimir no stdout.
 4. Remover o FIFO privado.
*/
void handle_query(void) {

    pid_t my_pid = getpid();
    char runner_fifo[64];
    get_runner_fifo(runner_fifo, sizeof(runner_fifo), my_pid);

    // criar o FIFO de resposta
    mkfifo(runner_fifo, 0666);
    
    //  enviar pedido de consulta 
    Message msg = {.type = MSG_QUERY, .runner_pid = my_pid};

    int fd_ctrl = open(CONTROLLER_FIFO, O_WRONLY);
    write(fd_ctrl, &msg, sizeof(msg));
    close(fd_ctrl);

    // ler e imprimir resposta do controller
    int fd_resp = open(runner_fifo, O_RDONLY);
    char buf[4096];
    ssize_t n = read(fd_resp, buf, sizeof(buf)-1);
    if (n > 0){
        buf[n]= '\n';
        write(1, buf, n);
    }
    close(fd_resp);

    unlink(runner_fifo);

}


//Modo -s: pedir ao controller para terminar.
/* Fluxo:
    1. Criar FIFO privado para receber a confirmação.
    2. Enviar MSG_SHUTDOWN ao controller.
    3. Aguardar MSG_SHUTDOWN_OK (bloqueante; o controller só responde após
todos os comandos em curso terminarem ou o timeout expirar).
    4. Imprimir mensagem de confirmação e remover o FIFO privado.
*/
void handle_shutdown() {
    pid_t my_pid = getpid();
    char runner_fifo[64];
    get_runner_fifo(runner_fifo, sizeof(runner_fifo), my_pid);

    // criar o FIFO privado para receber a confirmação
    mkfifo(runner_fifo, 0666);
    
    // enviar pedido de shutdown
    Message msg = {.type = MSG_SHUTDOWN, .runner_pid = my_pid};

    int fd_ctrl = open(CONTROLLER_FIFO, O_WRONLY);
    write(fd_ctrl, &msg, sizeof(msg));
    close(fd_ctrl);

    out("[runner] sent shutdown notification\n");
    out("[runner] waiting for controller to shutdown...\n");

    // aguardar confirmação do controller
    int fd_resp = open(runner_fifo, O_RDONLY);
    Message resp;
    ssize_t n = read(fd_resp, &resp, sizeof(resp));
    if (n == sizeof(resp) && resp.type == MSG_SHUTDOWN_OK) {
        out("[runner] controller exited.\n");
    } else {
        out("[runner] erro ao receber confirmação de shutdown\n");
    }
    close(fd_resp);

    unlink(runner_fifo);
}


// Valida os argumentos e despacha para o modo de operação correto.
int main(int argc, char *argv[]) {
    if (argc < 2) {
        write(2, "uso: runner -e <user> <cmd> | -c | -s\n", 38);
        exit(1);
    }

    if (strcmp(argv[1], "-e") == 0) {
        if (argc < 4) {
            write(2, "uso: runner -e <user-id> <comando>\n", 35);
            exit(1);
        }
        handle_execute(argc, argv);

    } else if (strcmp(argv[1], "-c") == 0) {
        handle_query();

    } else if (strcmp(argv[1], "-s") == 0) {
        handle_shutdown();

    } else {
        write(2, "opcao invalida\n", 15);
        exit(1);
    }

    return 0;
}