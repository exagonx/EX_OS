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
#include <stddef.h>
#include <stdint.h>

/* =============================================================================
 * I TIPI DEL BERSAGLIO, verificati in COMPILAZIONE
 *
 * Sono la quarta cosa che un bersaglio puo' sbagliare senza che nessuno se
 * ne accorga, e la piu' insidiosa: le larghezze sono giuste comunque, e il
 * programma gira. Si vede solo quando un sorgente di terzi dichiara size_t
 * accanto a <stddef.h> («conflicting types»), oppure quando ogni printf
 * di un int32_t diventa un avviso perche' il tipo e' `long int`.
 *
 * EX-OS condivide l'ABI con i386-linux e dichiara gli stessi tipi: vedi il
 * blocco sui tipi fondamentali in gcc/config/i386/exos.h. Se qualcuno
 * cambiasse quel file, questo programma smetterebbe di COMPILARE, che e'
 * il momento giusto per accorgersene.
 * ============================================================================= */
_Static_assert(sizeof(size_t) == 4,    "size_t deve essere a 32 bit");
_Static_assert(sizeof(ptrdiff_t) == 4, "ptrdiff_t deve essere a 32 bit");

/* Il tipo, non solo la larghezza: `unsigned int` e non `long unsigned int`.
 * _Generic e' l'unico modo di chiederlo al compilatore. */
_Static_assert(_Generic((size_t)0,    unsigned int: 1, default: 0),
               "size_t deve essere 'unsigned int' (vedi SIZE_TYPE in exos.h)");
_Static_assert(_Generic((ptrdiff_t)0, int: 1, default: 0),
               "ptrdiff_t deve essere 'int' (vedi PTRDIFF_TYPE in exos.h)");
_Static_assert(_Generic((int32_t)0,   int: 1, default: 0),
               "int32_t deve essere 'int' (vedi INT32_TYPE in exos.h)");
_Static_assert(_Generic((uint32_t)0,  unsigned int: 1, default: 0),
               "uint32_t deve essere 'unsigned int' (vedi UINT32_TYPE in exos.h)");
_Static_assert(sizeof(void *) == sizeof(intptr_t),
               "intptr_t deve poter contenere un puntatore");

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

    /* I tipi sono gia' stati verificati in compilazione dagli
     * _Static_assert in testa al file; qui si stampano perche' chi guarda
     * l'uscita veda con che bersaglio ha a che fare. */
    printf("  tipi         size_t %u byte, int32_t %u byte, puntatore %u byte\n",
           (unsigned)sizeof(size_t), (unsigned)sizeof(int32_t),
           (unsigned)sizeof(void *));

    printf("  esito        tutto a posto\n");
    return 0;
}
