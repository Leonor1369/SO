/*
 * test_runner.c — Testes para o sistema controller/runner
 * Compilar: gcc -Wall -g -Iinclude src/test_runner.c -o bin/test_runner
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <stdarg.h>

#define BIN_CONTROLLER  "./bin/controller"
#define BIN_RUNNER      "./bin/runner"
#define CONTROLLER_FIFO "/tmp/controller_in"
#define LOG_FILE        "./controller.log"


static int g_pass = 0, g_fail = 0;
static pid_t g_ctrl_pid = -1;

/* ── Utilitários ─────────────────────────────────────────────────────────── */

static long now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000L + tv.tv_usec / 1000L;
}

static void pass(const char *name) {
    printf("  [PASS] %s\n", name);
    g_pass++;
}

static void fail(const char *name, const char *reason) {
    printf("  [FAIL] %s -- %s\n", name, reason);
    g_fail++;
}

static void section(const char *title) {
    printf("\n--- %s ---\n", title);
}

static void cleanup(void) {
    if (g_ctrl_pid > 0) {
        kill(g_ctrl_pid, SIGTERM);
        waitpid(g_ctrl_pid, NULL, 0);
        g_ctrl_pid = -1;
    }
    system("pkill -f 'bin/runner' 2>/dev/null");
    system("rm -f /tmp/runner_* 2>/dev/null");
    unlink(CONTROLLER_FIFO);
    usleep(300000);
}

static void start_controller(int parallel, int policy) {
    cleanup();
    char par[8], pol[8];
    snprintf(par, sizeof(par), "%d", parallel);
    snprintf(pol, sizeof(pol), "%d", policy);

    if ((g_ctrl_pid = fork()) == 0) {
        int dn = open("/dev/null", O_WRONLY);
        dup2(dn, STDERR_FILENO);
        close(dn);
        execl(BIN_CONTROLLER, BIN_CONTROLLER, par, pol, NULL);
        _exit(1);
    }
    usleep(400000);
}

/* Corre o runner, captura stdout em buf, devolve tempo em ms */
static long run_runner(char *buf, size_t sz, int timeout_ms, ...) {
    char *args[32] = { BIN_RUNNER };
    int n = 1;
    va_list ap;
    va_start(ap, timeout_ms);
    char *a;
    while ((a = va_arg(ap, char *)) && n < 31)
        args[n++] = a;
    va_end(ap);
    args[n] = NULL;

    int pfd[2];
    pipe(pfd);
    long t0 = now_ms();

    if (fork() == 0) {
        close(pfd[0]);
        dup2(pfd[1], STDOUT_FILENO);
        close(pfd[1]);
        int dn = open("/dev/null", O_WRONLY);
        dup2(dn, STDERR_FILENO);
        close(dn);
        execv(BIN_RUNNER, args);
        _exit(1);
    }
    close(pfd[1]);

    /* Lê com timeout */
    int ms = timeout_ms > 0 ? timeout_ms : 8000;
    size_t total = 0;
    while (buf && total < sz - 1) {
        fd_set rfds; FD_ZERO(&rfds); FD_SET(pfd[0], &rfds);
        struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
        if (select(pfd[0] + 1, &rfds, NULL, NULL, &tv) <= 0) break;
        ssize_t rd = read(pfd[0], buf + total, sz - 1 - total);
        if (rd <= 0) break;
        total += rd;
    }
    if (buf) buf[total] = '\0';
    close(pfd[0]);

    wait(NULL);
    return now_ms() - t0;
}

static void stop_controller(void) {
    char buf[256];
    run_runner(buf, sizeof(buf), 5000, "-s", NULL);
    if (g_ctrl_pid > 0) { waitpid(g_ctrl_pid, NULL, 0); g_ctrl_pid = -1; }
    usleep(200000);
}

static pid_t launch_bg(char **args) {
    pid_t pid;
    if ((pid = fork()) == 0) {
        int dn = open("/dev/null", O_WRONLY);
        dup2(dn, STDOUT_FILENO); dup2(dn, STDERR_FILENO);
        close(dn);
        execv(BIN_RUNNER, args);
        _exit(1);
    }
    return pid;
}

/* ── Secções de teste ────────────────────────────────────────────────────── */

static void test_basic(void) {
    section("1. Comandos Básicos");
    start_controller(1, 0);
    char buf[2048];

    run_runner(buf, sizeof(buf), 5000, "-e", "u1", "echo hello_world", NULL);
    if (strstr(buf, "hello_world"))       pass("1.1 echo simples");
    else                                  fail("1.1 echo simples", buf);

    if (strstr(buf, "command submitted") &&
        strstr(buf, "executing command") &&
        strstr(buf, "finished"))          pass("1.2 mensagens corretas");
    else                                  fail("1.2 mensagens", buf);

    run_runner(buf, sizeof(buf), 5000, "-e", "u1", "echo um dois tres", NULL);
    if (strstr(buf, "um dois tres"))      pass("1.3 múltiplos argumentos");
    else                                  fail("1.3 múltiplos argumentos", buf);

    run_runner(buf, sizeof(buf), 5000, "-e", "u1", "ls /caminho_inexistente_xyz", NULL);
    run_runner(buf, sizeof(buf), 5000, "-e", "u1", "echo apos_erro", NULL);
    if (strstr(buf, "apos_erro"))         pass("1.4 continua após erro");
    else                                  fail("1.4 continua após erro", buf);

    stop_controller();
}

static void test_pipes(void) {
    section("2. Pipes e Redirecionamentos");
    start_controller(1, 0);
    char buf[2048];

    run_runner(buf, sizeof(buf), 5000, "-e", "u1", "echo a b c | wc -w", NULL);
    if (strstr(buf, "3"))   pass("2.1 pipe wc -w = 3");
    else                    fail("2.1 pipe wc", buf);

    run_runner(buf, sizeof(buf), 5000, "-e", "u1", "ls /etc | grep passwd", NULL);
    if (strstr(buf, "passwd")) pass("2.2 pipe grep passwd");
    else                       fail("2.2 pipe grep", buf);

    unlink("/tmp/t_out.txt");
    run_runner(buf, sizeof(buf), 5000, "-e", "u1", "echo escrita > /tmp/t_out.txt", NULL);
    int fd = open("/tmp/t_out.txt", O_RDONLY);
    char rbuf[128] = {0};
    if (fd >= 0) { read(fd, rbuf, sizeof(rbuf)-1); close(fd); }
    if (strstr(rbuf, "escrita")) pass("2.3 redir >");
    else                         fail("2.3 redir >", "conteúdo inesperado");

    fd = open("/tmp/t_in.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    write(fd, "leitura\n", 8); close(fd);
    run_runner(buf, sizeof(buf), 5000, "-e", "u1", "cat < /tmp/t_in.txt", NULL);
    if (strstr(buf, "leitura")) pass("2.4 redir <");
    else                        fail("2.4 redir <", buf);

    unlink("/tmp/t_err.txt");
    run_runner(buf, sizeof(buf), 5000, "-e", "u1", "ls /nao_existe 2> /tmp/t_err.txt", NULL);
    struct stat st;
    if (stat("/tmp/t_err.txt", &st) == 0 && st.st_size > 0) pass("2.5 redir 2>");
    else fail("2.5 redir 2>", "ficheiro de erro vazio");

    run_runner(buf, sizeof(buf), 5000, "-e", "u1", "echo a b c d | wc -w", NULL);
    if (strstr(buf, "4")) pass("2.6 pipeline 2 segmentos = 4");
    else                  fail("2.6 pipeline", buf);

    stop_controller();
}

static void test_parallel(void) {
    section("3. Paralelismo");
    char buf[256];

    /* parallel=2: dois sleep 0.5 devem terminar em ~500ms */
    start_controller(2, 0);
    char *a1[] = { BIN_RUNNER, "-e", "u1", "sleep 0.5", NULL };
    char *a2[] = { BIN_RUNNER, "-e", "u2", "sleep 0.5", NULL };
    long t0 = now_ms();
    pid_t p1 = launch_bg(a1), p2 = launch_bg(a2);
    waitpid(p1, NULL, 0); waitpid(p2, NULL, 0);
    long el = now_ms() - t0;
    if (el < 900) pass("3.1 parallel=2 executa em simultâneo");
    else          fail("3.1 parallel=2", "demorou demasiado");

    /* query mostra Executing + Scheduled */
    char *b1[] = { BIN_RUNNER, "-e", "u1", "sleep 0.8", NULL };
    char *b2[] = { BIN_RUNNER, "-e", "u2", "sleep 0.8", NULL };
    char *b3[] = { BIN_RUNNER, "-e", "u3", "echo x",    NULL };
    p1 = launch_bg(b1); p2 = launch_bg(b2); pid_t p3 = launch_bg(b3);
    usleep(300000);
    run_runner(buf, sizeof(buf), 3000, "-c", NULL);
    waitpid(p1, NULL, 0); waitpid(p2, NULL, 0); waitpid(p3, NULL, 0);
    if (strstr(buf, "Executing") && strstr(buf, "Scheduled"))
        pass("3.2 query: Executing + Scheduled");
    else
        fail("3.2 query", buf);
    stop_controller();

    /* parallel=1: dois sleep 0.5 devem demorar ~1s */
    start_controller(1, 0);
    char *c1[] = { BIN_RUNNER, "-e", "u1", "sleep 0.5", NULL };
    char *c2[] = { BIN_RUNNER, "-e", "u2", "sleep 0.5", NULL };
    t0 = now_ms();
    p1 = launch_bg(c1); p2 = launch_bg(c2);
    waitpid(p1, NULL, 0); waitpid(p2, NULL, 0);
    el = now_ms() - t0;
    if (el > 900) pass("3.3 parallel=1 serializa");
    else          fail("3.3 parallel=1", "não serializou");
    stop_controller();
}

static void test_query(void) {
    section("4. Consulta de Estado (-c)");
    start_controller(1, 0);
    char buf[2048];

    char *a1[] = { BIN_RUNNER, "-e", "u1", "sleep 0.8", NULL };
    char *a2[] = { BIN_RUNNER, "-e", "u2", "echo segundo", NULL };
    pid_t p1 = launch_bg(a1), p2 = launch_bg(a2);
    usleep(300000);
    run_runner(buf, sizeof(buf), 3000, "-c", NULL);

    if (strstr(buf, "Executing") && strstr(buf, "Scheduled"))
        pass("4.1 query tem Executing e Scheduled");
    else
        fail("4.1 query", buf);

    if (strstr(buf, "user-id") && strstr(buf, "command-id"))
        pass("4.2 query tem user-id e command-id");
    else
        fail("4.2 query formato", buf);

    waitpid(p1, NULL, 0); waitpid(p2, NULL, 0);

    char *aq[] = { BIN_RUNNER, "-c", NULL };
    pid_t pq = launch_bg(aq);
    run_runner(buf, sizeof(buf), 5000, "-e", "u3", "echo sem_bloqueio", NULL);
    waitpid(pq, NULL, 0);
    if (strstr(buf, "sem_bloqueio")) pass("4.3 query não bloqueia");
    else                             fail("4.3 query bloqueia", buf);

    stop_controller();
}

static void test_policies(void) {
    section("5. Políticas de Escalonamento");
    char buf[1024];

    start_controller(1, 0);
    run_runner(buf, sizeof(buf), 5000, "-e", "u1", "echo FCFS_OK", NULL);
    if (strstr(buf, "FCFS_OK")) pass("5.1 FCFS (pol=0)");
    else                        fail("5.1 FCFS", buf);
    stop_controller();

    start_controller(1, 1);
    run_runner(buf, sizeof(buf), 5000, "-e", "u1", "echo SJF_OK", NULL);
    if (strstr(buf, "SJF_OK")) pass("5.2 SJF (pol=1)");
    else                       fail("5.2 SJF", buf);
    stop_controller();

    start_controller(1, 2);
    char *al[] = { BIN_RUNNER, "-e", "u1", "sleep 0.8", NULL };
    pid_t pl = launch_bg(al);
    usleep(200000);
    char *lo[] = { BIN_RUNNER, "-e", "u1", "-p", "1", "echo BAIXA", NULL };
    char *hi[] = { BIN_RUNNER, "-e", "u1", "-p", "9", "echo ALTA",  NULL };
    pid_t plo = launch_bg(lo), phi = launch_bg(hi);
    usleep(200000);
    run_runner(buf, sizeof(buf), 3000, "-c", NULL);
    waitpid(pl, NULL, 0); waitpid(plo, NULL, 0); waitpid(phi, NULL, 0);
    if (strstr(buf, "Scheduled")) pass("5.3 Priority (pol=2) — fila presente");
    else                          fail("5.3 Priority", buf);
    stop_controller();

    start_controller(2, 3);
    char *rr1[] = { BIN_RUNNER, "-e", "alice", "echo A1", NULL };
    char *rr2[] = { BIN_RUNNER, "-e", "bob",   "echo B1", NULL };
    char *rr3[] = { BIN_RUNNER, "-e", "alice", "echo A2", NULL };
    char *rr4[] = { BIN_RUNNER, "-e", "bob",   "echo B2", NULL };
    pid_t pp[4] = { launch_bg(rr1), launch_bg(rr2), launch_bg(rr3), launch_bg(rr4) };
    for (int i = 0; i < 4; i++) waitpid(pp[i], NULL, 0);
    pass("5.4 Round Robin (pol=3) — todos executaram");
    stop_controller();
}

static void test_shutdown(void) {
    section("6. Shutdown");
    char buf[512];

    start_controller(1, 0);
    run_runner(buf, sizeof(buf), 5000, "-s", NULL);
    if (g_ctrl_pid > 0) { waitpid(g_ctrl_pid, NULL, 0); g_ctrl_pid = -1; }
    if (strstr(buf, "sent shutdown notification") && strstr(buf, "controller exited"))
        pass("6.1 mensagens de shutdown corretas");
    else
        fail("6.1 mensagens shutdown", buf);

    start_controller(1, 0);
    char *args[] = { BIN_RUNNER, "-e", "u1", "sleep 0.8", NULL };
    pid_t p = launch_bg(args);
    usleep(200000);
    long t0 = now_ms();
    run_runner(buf, sizeof(buf), 5000, "-s", NULL);
    long el = now_ms() - t0;
    waitpid(p, NULL, 0);
    if (g_ctrl_pid > 0) { waitpid(g_ctrl_pid, NULL, 0); g_ctrl_pid = -1; }
    if (el > 300) pass("6.2 shutdown aguarda execução em curso");
    else          fail("6.2 shutdown não esperou", "terminou demasiado rápido");
}

static void test_logger(void) {
    section("7. Logger");
    unlink(LOG_FILE);
    start_controller(1, 0);
    char buf[256];
    run_runner(buf, sizeof(buf), 5000, "-e", "utilizador1", "echo log_test", NULL);
    stop_controller();

    struct stat st;
    if (stat(LOG_FILE, &st) != 0) { fail("7.1 controller.log não existe", ""); return; }
    pass("7.1 controller.log criado");

    int fd = open(LOG_FILE, O_RDONLY);
    char log[1024] = {0};
    if (fd >= 0) { read(fd, log, sizeof(log)-1); close(fd); }

    if (strstr(log, "utilizador1")) pass("7.2 log contém user-id");
    else                            fail("7.2 log sem user-id", log);

    if (strstr(log, "duration="))   pass("7.3 log contém duration=");
    else                            fail("7.3 log sem duration=", log);

    if (strstr(log, "cmd_id="))     pass("7.4 log contém cmd_id=");
    else                            fail("7.4 log sem cmd_id=", log);
}

static void test_performance(void) {
    section("8. Avaliação de Desempenho");
    printf("  %-16s  %-8s  Tempo\n", "Politica", "Parallel");
    printf("  ----------------------------------\n");

    const char *names[] = { "FCFS", "SJF", "Priority", "RoundRobin" };

    for (int par = 1; par <= 4; par *= 2) {
        cleanup();
        char par_s[16], pol_s[16];
        snprintf(par_s, sizeof(par_s), "%d", par);
        snprintf(pol_s, sizeof(pol_s), "0");

        pid_t cp;
        if ((cp = fork()) == 0) {
            int dn = open("/dev/null", O_WRONLY);
            dup2(dn, STDERR_FILENO); close(dn);
            execl(BIN_CONTROLLER, BIN_CONTROLLER, par_s, pol_s, NULL);
            _exit(1);
        }
        g_ctrl_pid = cp;
        usleep(400000);

        char *ca[] = { BIN_RUNNER, "-e", "alice", "sleep 0.1", NULL };
        char *cb[] = { BIN_RUNNER, "-e", "bob",   "sleep 0.2", NULL };
        char *cc[] = { BIN_RUNNER, "-e", "carol", "echo x",    NULL };
        pid_t pids[9]; int n = 0;
        long t0 = now_ms();
        for (int i = 0; i < 3; i++) {
            pids[n++] = launch_bg(ca);
            pids[n++] = launch_bg(cb);
            pids[n++] = launch_bg(cc);
        }
        for (int i = 0; i < n; i++) waitpid(pids[i], NULL, 0);
        printf("  %-16s  par=%-4d  %ldms\n", "FCFS", par, now_ms() - t0);
        stop_controller();
    }

    for (int i = 1; i < 4; i++) {
        cleanup();
        char pol_s[8]; snprintf(pol_s, sizeof(pol_s), "%d", i);
        pid_t cp;
        if ((cp = fork()) == 0) {
            int dn = open("/dev/null", O_WRONLY);
            dup2(dn, STDERR_FILENO); close(dn);
            execl(BIN_CONTROLLER, BIN_CONTROLLER, "2", pol_s, NULL);
            _exit(1);
        }
        g_ctrl_pid = cp;
        usleep(400000);

        char *ca[] = { BIN_RUNNER, "-e", "alice", "sleep 0.1", NULL };
        char *cb[] = { BIN_RUNNER, "-e", "bob",   "sleep 0.2", NULL };
        char *cc[] = { BIN_RUNNER, "-e", "carol", "echo x",    NULL };
        pid_t pids[9]; int n = 0;
        long t0 = now_ms();
        for (int j = 0; j < 3; j++) {
            pids[n++] = launch_bg(ca);
            pids[n++] = launch_bg(cb);
            pids[n++] = launch_bg(cc);
        }
        for (int j = 0; j < n; j++) waitpid(pids[j], NULL, 0);
        printf("  %-16s  par=%-4d  %ldms\n", names[i], 2, now_ms() - t0);
        stop_controller();
    }

    pass("8.1 medições concluídas");
}

/* ── Main ────────────────────────────────────────────────────────────────── */

int main(void) {
    printf("\n=== Testes Controller/Runner ===\n\n");

    test_basic();
    test_pipes();
    test_parallel();
    test_query();
    test_policies();
    test_shutdown();
    test_logger();
    test_performance();

    section("Resumo");
    printf("\n  Total: %d   Passou: %d   Falhou: %d\n\n",
           g_pass + g_fail, g_pass, g_fail);
    if (g_fail == 0)
        printf("  Todos os testes passaram!\n\n");
    else
        printf("  %d teste(s) falharam.\n\n", g_fail);

    cleanup();
    return g_fail > 0 ? 1 : 0;
}