/* =============================================================================
 * lib/exdlg/exdlg_esporta.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Cio' che exdlg.so mette a disposizione. Vedi lib/exwin/exwin_esporta.c: le
 * regole sono le stesse, e sono due — si aggiunge in fondo, non si toglie mai.
 * ============================================================================= */

#include "exlib.h"
#include "exdlg.h"

/* Vedi exwin_esporta.c: e' il gancio che riempie i ponti verso la libc. */
void __libc_ponti_avvia(void);

static const char *const g_nomi[] = {
    "ex_dlg_apri",
    "ex_dlg_salva",
    "ex_dlg_avviso",
    "ex_dlg_conferma",
    "__lib_avvio"
};

static void *const g_indirizzi[] = {
    (void *)ex_dlg_apri,
    (void *)ex_dlg_salva,
    (void *)ex_dlg_avviso,
    (void *)ex_dlg_conferma,
    (void *)__libc_ponti_avvia
};

typedef char exdlg_esporta_elenchi_pari[
    (sizeof(g_nomi) / sizeof(g_nomi[0]) ==
     sizeof(g_indirizzi) / sizeof(g_indirizzi[0])) ? 1 : -1];

EXLIB_TESTA(exdlg_tabella, g_nomi, g_indirizzi);
