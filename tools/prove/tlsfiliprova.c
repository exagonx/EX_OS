/* =============================================================================
 * tools/prove/tlsfiliprova.c - the TLS image of a new thread is its own (voce 8)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Built twice with different -DSEGNO (and a 40 KB __thread array, so the
 * kernel reads the image from the disk and sleeps). "tlsfiliprova" spawns
 * "tlsfiliprova-b" and both make 300 threads at the same time; every thread
 * checks that its __thread values are those of ITS program. Before the
 * fix the image buffer was one for everybody, and A's thread could start
 * with B's values. Prints "TLS SEGNO: N NO".
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <libc.h>

#ifndef SEGNO
#define SEGNO 0x41
#endif

static __thread unsigned char grande[40960] = { SEGNO, SEGNO, SEGNO, [40959] = SEGNO };
static __thread int segno = SEGNO;
static volatile int no = 0;

static void *filo(void *a)
{
    (void)a;
    if (segno != SEGNO || grande[0] != SEGNO || grande[40959] != SEGNO) no++;
    return NULL;
}

int main(int argc, char **argv)
{
    int i, figlio = -1, st = 0;

    if (argc > 1) {                  /* the other program */
        char *av[] = { argv[1], NULL };
        figlio = spawn(argv[1], av);
    }
    for (i = 0; i < 300; i++) {
        pthread_t t;

        if (pthread_create(&t, NULL, filo, NULL) == 0) pthread_join(t, NULL);
    }
    if (figlio > 0) waitpid(figlio, &st, 0);
    printf("TLS %02x: %d NO%s\n", SEGNO, no, (figlio > 0 && st != 0) ? " (e l'altro no)" : "");
    return no ? 1 : 0;
}
