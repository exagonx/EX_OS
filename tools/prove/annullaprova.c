/* =============================================================================
 * tools/prove/annullaprova.c - pthread_cancel cuts a thread's wait short,
 * and FD_CLOEXEC closes at exec (voce 105, kernel 0.349)
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Run by tools/prova_annulla.sh. Prints [OK]/[NO] lines and "annullaprova: N NO".
 * Every wait test: a thread sleeps in the call, the main thread cancels it
 * after 300 ms, join must give PTHREAD_CANCELED within 2 s and the cleanup
 * handler must have run.
 *   a read on an empty pipe        b poll on a pipe, 8 s
 *   c nanosleep 8 s                d sem_wait
 *   e accept on a TCP listener     f poll on the listener (socket path)
 *   g select on a pipe             h sigwait on a blocked SIGUSR1
 *   i waitpid on a child that sleeps 3 s
 *   j cancel disabled: read is not cut, returns the byte written at 600 ms
 *   k a request already pending: read with data ready ends the thread
 *   l FD_CLOEXEC: F_GETFD/F_SETFD, pipe2 O_CLOEXEC, dup clears, F_DUPFD_CLOEXEC,
 *     and after fork + exec the marked descriptors are gone, the others not
 *   m posix_spawn: ENOENT with no child left; posix_spawnp with adddup2 of a
 *     pipe on stdout reads the child's words and then the end (the other
 *     copies closed at exec); clock_nanosleep relative, TIMER_ABSTIME,
 *     ENOTSUP on a CPU clock; strerror_l
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <poll.h>
#include <time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <spawn.h>
#include <locale.h>
#include <netinet/in.h>

#define PORTA 5139

static int g_no = 0;
static int g_tubo[2];
static int g_asc = -1;
static sem_t g_sem;
static pid_t g_figlio;
static volatile int g_pulito;
static volatile int g_letto;

static void esito(int ok, const char *t)
{
    printf("  [%s] %s\n", ok ? "OK" : "NO", t);
    fflush(stdout);
    if (!ok) g_no++;
}

static unsigned int ms_ora(void)
{
    struct timeval t;

    gettimeofday(&t, NULL);
    return (unsigned int)(t.tv_sec * 1000u + t.tv_usec / 1000u);
}

static void pulizia(void *a) { (void)a; g_pulito++; }

static void *in_attesa(void *arg)
{
    int  quale = (int)(long)arg;
    char c;

    pthread_cleanup_push(pulizia, NULL);
    switch (quale) {
    case 'a': read(g_tubo[0], &c, 1); break;
    case 'b': { struct pollfd p = { g_tubo[0], POLLIN, 0 }; poll(&p, 1, 8000); } break;
    case 'c': { struct timespec t = { 8, 0 }; nanosleep(&t, NULL); } break;
    case 'd': sem_wait(&g_sem); break;
    case 'e': accept(g_asc, NULL, NULL); break;
    case 'f': { struct pollfd p = { g_asc, POLLIN, 0 }; poll(&p, 1, 8000); } break;
    case 'g': {
        fd_set l; struct timeval t = { 8, 0 };
        FD_ZERO(&l); FD_SET(g_tubo[0], &l);
        select(g_tubo[0] + 1, &l, NULL, NULL, &t);
    } break;
    case 'h': { sigset_t s; int sig; sigemptyset(&s); sigaddset(&s, SIGUSR1); sigwait(&s, &sig); } break;
    case 'i': { int st; waitpid(g_figlio, &st, 0); } break;
    }
    pthread_cleanup_pop(0);
    return (void *)1;               /* not cancelled */
}

static void prova_attesa(int quale, const char *nome)
{
    pthread_t    t;
    void        *ris = NULL;
    unsigned int t0, dt;
    char         m[200];

    g_pulito = 0;
    if (pthread_create(&t, NULL, in_attesa, (void *)(long)quale) != 0) {
        esito(0, "pthread_create");
        return;
    }
    usleep(300000);
    t0 = ms_ora();
    pthread_cancel(t);
    pthread_join(t, &ris);
    dt = ms_ora() - t0;
    snprintf(m, sizeof m, "%c %s: %s, pulizia %d, %u ms", quale, nome,
             ris == PTHREAD_CANCELED ? "PTHREAD_CANCELED" : "non annullato", g_pulito, dt);
    esito(ris == PTHREAD_CANCELED && g_pulito == 1 && dt < 2000, m);
}

static void *spento(void *arg)
{
    char c = 0;
    int  vecchio;

    (void)arg;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &vecchio);
    g_letto = (int)read(g_tubo[0], &c, 1);
    pthread_setcancelstate(vecchio, NULL);
    pthread_testcancel();
    return (void *)1;
}

static void *gia_chiesto(void *arg)
{
    char c;

    (void)arg;
    pthread_cleanup_push(pulizia, NULL);
    while (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL), g_letto == 0)
        ;                           /* busy until main says the cancel is in */
    read(g_tubo[0], &c, 1);         /* data is there: must not just return it */
    pthread_cleanup_pop(0);
    return (void *)1;
}

static int figlio_exec(int a, int b, int c)
{
    /* after exec: a and c had FD_CLOEXEC, b not */
    int ra = fcntl(a, F_GETFD), ea = errno;
    int rb = fcntl(b, F_GETFD);
    int rc = fcntl(c, F_GETFD), ec = errno;

    printf("    figlio: a %d/%d b %d c %d/%d\n", ra, ea, rb, rc, ec);
    return (ra == -1 && ea == EBADF && rb == 0 && rc == -1 && ec == EBADF) ? 0 : 1;
}

static void prova_cloexec(const char *io)
{
    int  p[2], q[2], d, e, st = -1;
    char m[200], sa[12], sb[12], sc[12];
    pid_t f;

    if (pipe2(p, O_CLOEXEC) != 0 || pipe(q) != 0) { esito(0, "l pipe2/pipe"); return; }
    snprintf(m, sizeof m, "l pipe2 O_CLOEXEC: F_GETFD %d %d, pipe: %d",
             fcntl(p[0], F_GETFD), fcntl(p[1], F_GETFD), fcntl(q[0], F_GETFD));
    esito(fcntl(p[0], F_GETFD) == FD_CLOEXEC && fcntl(p[1], F_GETFD) == FD_CLOEXEC &&
          fcntl(q[0], F_GETFD) == 0, m);

    fcntl(q[1], F_SETFD, FD_CLOEXEC);
    d = fcntl(q[1], F_GETFD);
    fcntl(q[1], F_SETFD, 0);
    snprintf(m, sizeof m, "l F_SETFD acceso %d, spento %d", d, fcntl(q[1], F_GETFD));
    esito(d == FD_CLOEXEC && fcntl(q[1], F_GETFD) == 0, m);

    d = dup(p[0]);
    e = fcntl(q[0], F_DUPFD_CLOEXEC, 20);
    snprintf(m, sizeof m, "l dup toglie il bit: %d; F_DUPFD_CLOEXEC fd %d bit %d",
             fcntl(d, F_GETFD), e, fcntl(e, F_GETFD));
    esito(d >= 0 && fcntl(d, F_GETFD) == 0 && e >= 20 && fcntl(e, F_GETFD) == FD_CLOEXEC, m);

    snprintf(sa, sizeof sa, "%d", p[0]);
    snprintf(sb, sizeof sb, "%d", q[0]);
    snprintf(sc, sizeof sc, "%d", e);
    fflush(stdout);
    f = fork();
    if (f == 0) {
        char *av[] = { (char *)io, "figlio", sa, sb, sc, NULL };
        extern char **environ;

        execve(io, av, environ);
        _exit(9);
    }
    if (f > 0) waitpid(f, &st, 0);
    snprintf(m, sizeof m, "l dopo fork + exec: chiusi quelli con FD_CLOEXEC (uscita %d)",
             WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    esito(f > 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0, m);
}

static void prova_spawn(const char *io)
{
    extern char **environ;
    posix_spawn_file_actions_t az;
    posix_spawnattr_t at;
    char  *av_no[] = { "nessuno", NULL };
    char  *av[] = { (char *)io, "eco", NULL };
    char   buf[64], m[200];
    int    tubo[2], r, n = 0, k, st = -1;
    pid_t  f = 0;
    struct timespec t0, t1, q;
    long   ms;
    sigset_t vuoto;

    r = posix_spawn(&f, "/non/esiste", NULL, NULL, av_no, environ);
    k = waitpid(-1, NULL, WNOHANG);
    snprintf(m, sizeof m, "m posix_spawn di un file che non c'e': %d (ENOENT %d), figli %d", r, ENOENT, k);
    esito(r == ENOENT && k <= 0, m);

    pipe2(tubo, O_CLOEXEC);
    posix_spawn_file_actions_init(&az);
    posix_spawn_file_actions_adddup2(&az, tubo[1], 1);
    posix_spawnattr_init(&at);
    sigemptyset(&vuoto);
    posix_spawnattr_setsigmask(&at, &vuoto);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSIGMASK);
    r = posix_spawnp(&f, io, &az, &at, av, environ);
    close(tubo[1]);
    while (r == 0 && n < (int)sizeof buf - 1 && (k = (int)read(tubo[0], buf + n, sizeof buf - 1 - n)) > 0)
        n += k;
    buf[n] = 0;
    if (r == 0) waitpid(f, &st, 0);
    close(tubo[0]);
    posix_spawn_file_actions_destroy(&az);
    posix_spawnattr_destroy(&at);
    snprintf(m, sizeof m, "m posix_spawnp + adddup2: %d, letto \"%s\" e la fine, uscita %d",
             r, buf, WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    esito(r == 0 && strcmp(buf, "ciao") == 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0, m);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    q.tv_sec = 0; q.tv_nsec = 200000000L;
    r = clock_nanosleep(CLOCK_MONOTONIC, 0, &q, NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    snprintf(m, sizeof m, "m clock_nanosleep 200 ms: %d, %ld ms", r, ms);
    esito(r == 0 && ms >= 190 && ms < 500, m);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    q = t0; q.tv_nsec += 300000000L;
    if (q.tv_nsec >= 1000000000L) { q.tv_nsec -= 1000000000L; q.tv_sec++; }
    r = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &q, NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    k = (t1.tv_sec > q.tv_sec || (t1.tv_sec == q.tv_sec && t1.tv_nsec >= q.tv_nsec));
    snprintf(m, sizeof m, "m clock_nanosleep TIMER_ABSTIME +300 ms: %d, %ld ms, passato %d", r, ms, k);
    esito(r == 0 && k && ms < 600, m);

    r = clock_nanosleep(CLOCK_PROCESS_CPUTIME_ID, 0, &q, NULL);
    snprintf(m, sizeof m, "m clock_nanosleep su un orologio di CPU: %d (ENOTSUP %d)", r, ENOTSUP);
    esito(r == ENOTSUP, m);

    snprintf(m, sizeof m, "m strerror_l(ENOENT): \"%s\"", strerror_l(ENOENT, LC_GLOBAL_LOCALE));
    esito(strcmp(strerror_l(ENOENT, LC_GLOBAL_LOCALE), strerror(ENOENT)) == 0, m);
}

int main(int argc, char **argv)
{
    struct sockaddr_in a;
    pthread_t t;
    void     *ris;
    sigset_t  s;
    char      m[200];
    int       uno = 1;

    if (argc == 2 && strcmp(argv[1], "eco") == 0) {
        fputs("ciao", stdout);
        return 0;
    }
    if (argc == 5 && strcmp(argv[1], "figlio") == 0)
        return figlio_exec(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]));

    if (pipe(g_tubo) != 0) { printf("annullaprova: pipe\nannullaprova: 1 NO\n"); return 1; }
    sem_init(&g_sem, 0, 0);
    sigemptyset(&s); sigaddset(&s, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &s, NULL);           /* the threads inherit it */

    prova_attesa('a', "read su un tubo vuoto");
    prova_attesa('b', "poll su un tubo");
    prova_attesa('c', "nanosleep 8 s");
    prova_attesa('d', "sem_wait");

    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(PORTA); a.sin_addr.s_addr = htonl(0x7F000001);
    g_asc = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(g_asc, SOL_SOCKET, SO_REUSEADDR, &uno, sizeof uno);
    if (g_asc < 0 || bind(g_asc, (struct sockaddr *)&a, sizeof a) != 0 || listen(g_asc, 4) != 0) {
        esito(0, "e/f socket/bind/listen (ip.drv?)");
    } else {
        prova_attesa('e', "accept");
        prova_attesa('f', "poll sul socket in ascolto");
    }
    prova_attesa('g', "select su un tubo");
    prova_attesa('h', "sigwait");

    g_figlio = fork();
    if (g_figlio == 0) { sleep(3); _exit(0); }
    prova_attesa('i', "waitpid");
    if (g_figlio > 0) waitpid(g_figlio, NULL, 0);

    /* j */
    g_letto = 0;
    pthread_create(&t, NULL, spento, NULL);
    usleep(300000);
    pthread_cancel(t);
    usleep(300000);
    write(g_tubo[1], "J", 1);
    ris = NULL;
    pthread_join(t, &ris);
    snprintf(m, sizeof m, "j annullamento spento: read %d, poi testcancel %s",
             g_letto, ris == PTHREAD_CANCELED ? "PTHREAD_CANCELED" : "non annullato");
    esito(g_letto == 1 && ris == PTHREAD_CANCELED, m);

    /* k */
    g_letto = 0; g_pulito = 0;
    write(g_tubo[1], "K", 1);
    pthread_create(&t, NULL, gia_chiesto, NULL);
    pthread_cancel(t);
    g_letto = 1;
    ris = NULL;
    pthread_join(t, &ris);
    {
        struct pollfd p = { g_tubo[0], POLLIN, 0 };
        int resta = poll(&p, 1, 0);

        snprintf(m, sizeof m, "k richiesta gia' fatta, dato pronto: %s, il dato resta %d",
                 ris == PTHREAD_CANCELED ? "PTHREAD_CANCELED" : "non annullato", resta);
        esito(ris == PTHREAD_CANCELED && g_pulito == 1 && resta == 1, m);
    }

    prova_cloexec(argv[0]);
    prova_spawn(argv[0]);

    printf("annullaprova: %d NO\n", g_no);
    return g_no ? 1 : 0;
}
