/* =============================================================================
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     readlink FILE
 *
 * Where a link points. The only ones are /proc/.../exe, cwd and fd/N.
 * ============================================================================= */
#include "libc.h"

EX_VERSIONE("readlink", "0.001");

int main(int argc, char **argv)
{
    char    b[512];
    ssize_t n;

    if (argc != 2) { printf("uso: readlink file\n"); return 2; }
    n = readlink(argv[1], b, sizeof(b) - 1);
    if (n < 0) return 1;
    b[n] = 0;
    printf("%s\n", b);
    return 0;
}
