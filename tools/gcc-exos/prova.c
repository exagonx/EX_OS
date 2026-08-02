/* =============================================================================
 * tools/gcc-exos/prova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Il programma che dimostra che il cross-compilatore i386-exos funziona.
 *
 * Non prova il compilatore: prova il BERSAGLIO. Si compila cosi',
 * senza una sola opzione:
 *
 *     i386-exos-gcc tools/gcc-exos/prova.c -o build/bin/provagcc
 *
 * Niente -m32, niente -ffreestanding, niente -nostdlib, niente script di
 * link, niente crt0 nominato a mano. Se il binario che ne esce parte
 * dentro EX-OS e stampa, allora tutte queste cose le ha messe
 * gcc/config/i386/exos.h — che e' esattamente cio' che quel file esiste
 * per fare.
 *
 * Il contenuto tocca le tre cose che un bersaglio puo' sbagliare senza
 * che la compilazione se ne accorga:
 *
 *   1. l'AVVIO — se crt0.o non fosse linkato, o argv non fosse passato,
 *      il programma non arriverebbe a main o non vedrebbe i propri
 *      argomenti;
 *   2. la LIBC — printf e malloc vengono da libc.a, che il bersaglio
 *      aggiunge da solo con -lc;
 *   3. il CODICE GENERATO — una piccola aritmetica e una chiamata
 *      indiretta, perche' un'ABI sbagliata si vede solo eseguendo.
 * ============================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int somma(int a, int b) { return a + b; }
static int prodotto(int a, int b) { return a * b; }

int main(int argc, char **argv)
{
    int (*op[2])(int, int) = { somma, prodotto };
    char *buf;
    int   i;

    printf("provagcc — compilato con i386-exos-gcc\n");

#ifdef __exos__
    printf("  __exos__     definito dal bersaglio\n");
#else
    printf("  __exos__     ASSENTE: non e' stato usato il bersaglio giusto\n");
#endif

    printf("  argc         %d\n", argc);
    for (i = 0; i < argc && i < 4; i++) {
        printf("  argv[%d]      %s\n", i, argv[i]);
    }

    printf("  chiamata indiretta: 6+7=%d  6*7=%d\n", op[0](6, 7), op[1](6, 7));

    buf = (char *)malloc(64);
    if (buf == NULL) {
        printf("  malloc       FALLITA\n");
        return 1;
    }
    strcpy(buf, "libc.a linkata dal bersaglio");
    printf("  malloc       %s\n", buf);
    free(buf);

    printf("  esito        tutto a posto\n");
    return 0;
}
