/* =============================================================================
 * tools/prove/forkfilo.c - fork from a thread, then exec (voce 118)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     forkfilo PROGRAM [ARG...]
 *
 * What Firefox's file picker does: a second thread calls posix_spawn (fork,
 * dup2 of a pipe on stdout, execve). Three rounds, each printing
 * "FF <round> rc <posix_spawn> esce <status> letto <bytes>":
 *   1 posix_spawn from the main thread
 *   2 posix_spawn from a second thread
 *   3 fork + execve by hand from a second thread
 * The child's stdout comes back through the pipe and is printed after "FF-OUT".
 * ============================================================================= */
#include <pthread.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
static char **g_argv;

static void lancia(int giro)
{
    int  tubo[2], rc, stato = -1;
    long letti = 0;
    char buf[256];
    pid_t pid = -1;
    ssize_t r;

    if (pipe(tubo) != 0) { printf("FF %d pipe fallita\n", giro); return; }
    if (giro == 3) {
        pid = fork();
        if (pid == 0) {
            dup2(tubo[1], 1);
            close(tubo[0]);
            execve(g_argv[0], g_argv, environ);
            _exit(126);
        }
        rc = pid < 0 ? -1 : 0;
    } else {
        posix_spawn_file_actions_t a;
        posix_spawn_file_actions_init(&a);
        posix_spawn_file_actions_adddup2(&a, tubo[1], 1);
        posix_spawn_file_actions_addclose(&a, tubo[0]);
        rc = posix_spawn(&pid, g_argv[0], &a, NULL, g_argv, environ);
        posix_spawn_file_actions_destroy(&a);
    }
    close(tubo[1]);
    if (rc == 0) {
        while ((r = read(tubo[0], buf, sizeof(buf) - 1)) > 0) {
            buf[r] = '\0';
            letti += r;
            printf("FF-OUT %s", buf);
        }
        waitpid(pid, &stato, 0);
    }
    close(tubo[0]);
    printf("FF %d rc %d esce %d letto %ld\n", giro, rc,
           (rc == 0 && WIFEXITED(stato)) ? WEXITSTATUS(stato) : -1, letti);
    fflush(stdout);
}

static void *filo(void *arg)
{
    lancia((int)(long)arg);
    return NULL;
}

int main(int argc, char **argv)
{
    pthread_t t;

    if (argc < 2) { printf("uso: forkfilo PROGRAMMA [ARG...]\n"); return 2; }
    g_argv = argv + 1;
    setvbuf(stdout, NULL, _IONBF, 0);
    lancia(1);
    pthread_create(&t, NULL, filo, (void *)2L);
    pthread_join(t, NULL);
    pthread_create(&t, NULL, filo, (void *)3L);
    pthread_join(t, NULL);
    printf("FF fine\n");
    return 0;
}
