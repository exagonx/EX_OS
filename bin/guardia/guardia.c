/* =============================================================================
 * bin/guardia/guardia.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * guardia — the machine resets itself if nobody calls again in time
 *
 *     guardia SECONDI     sets the countdown, or sets it again (root)
 *     guardia 0           turns it off
 *
 * For remote tests: tools/guardia-pc.py runs it over telnet. The countdown is
 * in the kernel, not here: with the disk stopped no program can be loaded, and
 * that is exactly when the reset is needed. See guardia_controlla in
 * kernel/arch/x86/power.c.
 * ============================================================================= */
#include "libc.h"

EX_VERSIONE("guardia", "0.001");

int main(int argc, char **argv)
{
    int s, r;

    if (argc != 2) {
        printf("uso: guardia SECONDI    riavvia se non viene richiamato in tempo\n"
               "     guardia 0          spenta\n");
        return 2;
    }
    s = atoi(argv[1]);
    if (s < 0) { printf("guardia: secondi non validi\n"); return 2; }
    r = guardia_imposta((unsigned int)s);
    if (r < 0) { printf("guardia: rifiutata (%d)\n", r); return 1; }
    if (s == 0) printf("guardia: spenta\n");
    else        printf("guardia: attiva, %d secondi\n", s);
    return 0;
}
