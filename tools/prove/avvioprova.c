/* =============================================================================
 * tools/prove/avvioprova.c — install the boot on a mounted volume, and say it
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     avvioprova PUNTO      bootverify, then bootinstall on PUNTO ("/a")
 *
 * `install` does this too, after copying the whole system: here only the
 * boot, on a disk that tools/prova_rdboot.sh has already filled from outside.
 * One line each, "AVVIO verifica <r> ..." and "AVVIO installa <r> ...".
 * ============================================================================= */
#include <libc.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    BootInstallInfo i;
    int r;

    if (argc < 2) { printf("uso: avvioprova PUNTO\n"); return 2; }
    r = bootverify(argv[1], NULL, NULL, &i);
    printf("AVVIO verifica %d stage2 %u x%u kernel %u settori in %u\n",
           r, i.s2_lba, i.s2_cnt, i.k_cnt, i.k_next);
    if (r != 0) return 1;
    r = bootinstall(argv[1], &i);
    printf("AVVIO installa %d voce %u\n", r, i.voce);
    return r != 0;
}
