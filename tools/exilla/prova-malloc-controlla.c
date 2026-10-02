/* tools/exilla/prova-malloc-controlla.c — il controllo dello heap di libc.a
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *   AMBIENTE=EXOS_MALLOC_CONTROLLA=1 tools/exilla/esegui-in-exos.sh <binario>
 *
 * Alloca, scrive bene, controlla malloc_usable_size, poi sfora di tre byte e
 * libera: con il controllo acceso la libc deve stampare [MALLOC] zona rossa
 * e abortire PRIMA della riga "non visto". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

int main(void)
{
    char *a = malloc(10), *b;
    int   i;

    for (i = 0; i < 200; i++) free(realloc(malloc(i + 1), i * 3 + 1));
    memset(a, 'x', 10);
    printf("usable_size(10) = %u\n", (unsigned)malloc_usable_size(a));
    b = malloc(37);
    memcpy(b, "abcdefghijklmnopqrstuvwxyz0123456789!!!!", 40);   /* 3 di troppo */
    printf("sforato, ora free\n");
    fflush(stdout);
    free(b);
    printf("non visto: il controllo non ha visto lo sforamento\n");
    free(a);
    return 0;
}
