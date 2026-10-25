/* =============================================================================
 * tools/prove/sigwaitprova.c — sigwait, sigtimedwait, sigsuspend (kernel 0.279)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     sigwaitprova             one line per check, then "SEGNALI fine <errors>"
 *
 * What a server like MariaDB does with signals: every thread blocks SIGTERM
 * and SIGHUP, one thread waits for them with sigwait; readline waits with
 * sigsuspend. The child cases are the service being stopped from outside:
 * a SIGTERM sent by another process to a program that blocks it must stay
 * pending for sigwait instead of ending the program.
 *   SEGNALI ok <what>    /   SEGNALI NO <what>
 * ============================================================================= */
#include <libc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>

static int g_male;

static void prova(const char *cosa, int giusto)
{
    printf("SEGNALI %s %s\n", giusto ? "ok" : "NO", cosa);
    if (!giusto) g_male++;
}

static void dormi_ms(long ms)
{
    struct timespec t;
    t.tv_sec = ms / 1000; t.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&t, NULL);
}

/* the thread that waits, as MariaDB's signal_hand */
static int g_preso;

static void *attesa(void *x)
{
    sigset_t s;
    int sig = 0;

    (void)x;
    sigemptyset(&s); sigaddset(&s, SIGTERM); sigaddset(&s, SIGHUP);
    if (sigwait(&s, &sig) == 0) g_preso = sig;
    return NULL;
}

static void *manda_dopo(void *x)
{
    dormi_ms(200);
    kill(getpid(), (int)(long)x);
    return NULL;
}

static volatile int g_gestito;
static void gestore(int sig) { g_gestito = sig; }

/* the child: blocks SIGTERM (or not), says it is ready, waits */
static int figlio(int blocca)
{
    sigset_t s;
    int sig = 0;

    sigemptyset(&s); sigaddset(&s, SIGTERM);
    if (blocca) {
        sigprocmask(SIG_BLOCK, &s, NULL);
        if (sigwait(&s, &sig) != 0) return 1;
        return sig == SIGTERM ? 42 : 2;
    }
    dormi_ms(5000);          /* SIGTERM with the default action ends it here */
    return 3;
}

static int lancia(const char *io, const char *modo)
{
    char *argv[3];
    argv[0] = (char *)io; argv[1] = (char *)modo; argv[2] = NULL;
    return spawn(io, argv);
}

int main(int argc, char **argv)
{
    sigset_t s, vecchia;
    siginfo_t info;
    struct timespec t;
    struct sigaction sa;
    pthread_t f, m;
    int sig = 0, r, stato = 0, pid;

    if (argc > 1 && strcmp(argv[1], "figlio-blocca") == 0) return figlio(1);
    if (argc > 1 && strcmp(argv[1], "figlio-libero") == 0) return figlio(0);

    /* 1. a pending signal of the set, taken at once */
    sigemptyset(&s); sigaddset(&s, SIGUSR1);
    sigprocmask(SIG_BLOCK, &s, &vecchia);
    kill(getpid(), SIGUSR1);
    r = sigwait(&s, &sig);
    prova("sigwait prende SIGUSR1 gia' in attesa", r == 0 && sig == SIGUSR1);

    /* 2. sigtimedwait: nothing comes, EAGAIN after the timeout */
    t.tv_sec = 0; t.tv_nsec = 300000000L;
    r = sigtimedwait(&s, &info, &t);
    prova("sigtimedwait senza segnale: -1 EAGAIN", r == -1 && errno == EAGAIN);

    /* 3. sigtimedwait gets one sent by another thread while it waits */
    pthread_create(&m, NULL, manda_dopo, (void *)(long)SIGUSR1);
    t.tv_sec = 5; t.tv_nsec = 0;
    r = sigtimedwait(&s, &info, &t);
    pthread_join(m, NULL);
    prova("sigtimedwait si sveglia col segnale", r == SIGUSR1 && info.si_signo == SIGUSR1);

    /* 4. the server case: SIGTERM blocked everywhere, one thread waits */
    sigemptyset(&s); sigaddset(&s, SIGTERM); sigaddset(&s, SIGHUP);
    sigprocmask(SIG_BLOCK, &s, NULL);
    pthread_create(&f, NULL, attesa, NULL);
    dormi_ms(200);
    kill(getpid(), SIGTERM);
    pthread_join(f, NULL);
    prova("SIGTERM bloccato va al filo in sigwait", g_preso == SIGTERM);

    /* 5. sigsuspend: the mask lets SIGUSR2 through, the handler runs, -1 EINTR */
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = gestore;
    sigaction(SIGUSR2, &sa, NULL);
    sigemptyset(&s); sigaddset(&s, SIGUSR2);
    sigprocmask(SIG_BLOCK, &s, NULL);
    pthread_create(&m, NULL, manda_dopo, (void *)(long)SIGUSR2);
    sigemptyset(&s);
    r = sigsuspend(&s);
    pthread_join(m, NULL);
    prova("sigsuspend: gestore eseguito, -1 EINTR", r == -1 && errno == EINTR && g_gestito == SIGUSR2);
    sigprocmask(SIG_SETMASK, NULL, &s);
    prova("sigsuspend rimette la maschera di prima", sigismember(&s, SIGUSR2) == 1);

    /* 6. from another process: blocked SIGTERM waits for sigwait */
    pid = lancia(argv[0], "figlio-blocca");
    dormi_ms(1000);
    kill(pid, SIGTERM);
    r = waitpid(pid, &stato, 0);
    prova("SIGTERM da fuori a chi lo blocca: lo prende sigwait", r == pid && WEXITSTATUS(stato) == 42);

    /* 7. the control: not blocked, the default action ends it */
    pid = lancia(argv[0], "figlio-libero");
    dormi_ms(1000);
    kill(pid, SIGTERM);
    r = waitpid(pid, &stato, 0);
    prova("SIGTERM da fuori a chi non lo blocca: termina", r == pid && WEXITSTATUS(stato) != 3);

    printf("SEGNALI fine %d\n", g_male);
    return g_male;
}
