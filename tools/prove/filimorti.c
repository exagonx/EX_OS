/* =============================================================================
 * tools/prove/filimorti.c - threads killed while they read (analisi voce 6)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     filimorti DIR [volte]      (default 200)
 *
 * Writes DIR/grosso (4 MB), then launches itself `volte` times as a child
 * with 8 threads reading that file in a loop; the child's main thread exits
 * after 0-49 ms, while the others are inside read(). After each child the
 * parent opens and reads the file: if a dead thread kept the file or disk
 * lock, that open never comes back and the test hangs (the harness times
 * out). Prints "filimorti: N su N, i file si aprono ancora".
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

static char g_file[256];

static void *leggi(void *x)
{
    static char buf[64 * 1024];     /* shared on purpose: contents don't matter */
    (void)x;
    for (;;) {
        int fd = open(g_file, O_RDONLY);

        if (fd < 0) continue;
        while (read(fd, buf, sizeof(buf)) > 0) { }
        close(fd);
    }
    return NULL;
}

static int figlio(void)
{
    pthread_t t;
    int i;

    for (i = 0; i < 8; i++) pthread_create(&t, NULL, leggi, NULL);
    usleep((unsigned)(getpid() * 7919u % 50u) * 1000u);
    exit(0);                        /* the leader goes: the 8 die where they are */
}

int main(int argc, char **argv)
{
    int volte = argc > 2 ? atoi(argv[2]) : 200, i, fatti = 0;
    char buf[4096];

    if (argc < 2) { printf("uso: filimorti DIR [volte]\n"); return 2; }
    snprintf(g_file, sizeof(g_file), "%s/grosso", argv[1]);
    if (argc > 2 && strcmp(argv[2], "-figlio") == 0) return figlio();

    {
        int fd = open(g_file, O_WRONLY | O_CREAT | O_TRUNC, 0644), k;

        if (fd < 0) { printf("filimorti: non creo %s\n", g_file); return 2; }
        memset(buf, 'x', sizeof(buf));
        for (k = 0; k < 1024; k++) write(fd, buf, sizeof(buf));
        close(fd);
    }
    for (i = 0; i < volte; i++) {
        char *av[] = { argv[0], argv[1], "-figlio", NULL };
        int pid = spawn(argv[0], av), st, fd;

        if (pid < 0) { printf("filimorti: spawn %d fallita\n", i); break; }
        waitpid(pid, &st, 0);
        fd = open(g_file, O_RDONLY);
        if (fd < 0 || read(fd, buf, 16) != 16) { printf("filimorti: al giro %d il file non si legge\n", i); break; }
        close(fd);
        fatti++;
        if (i % 50 == 49) { printf("filimorti: %d giri\n", i + 1); fflush(stdout); }
    }
    printf("filimorti: %d su %d, i file si aprono ancora\n", fatti, volte);
    return fatti == volte ? 0 : 1;
}
