/* =============================================================================
 * tools/iso/prova-make/conta.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * ============================================================================= */

#include "prova.h"

/* Il secondo membro dell'archivio. Vedi il commento in prova.h: con un
 * membro solo, `ranlib` non avrebbe niente da indicizzare e la sua parte
 * di prova non proverebbe nulla. */
int conta(const char *s, char c)
{
    int n = 0;

    while (*s) if (*s++ == c) n++;
    return n;
}
