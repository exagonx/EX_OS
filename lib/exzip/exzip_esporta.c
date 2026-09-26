/* =============================================================================
 * lib/exzip/exzip_esporta.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * What exzip.so offers. See lib/exwin/exwin_esporta.c: the rules are the same,
 * and there are two of them - entries are appended, and never taken away.
 * ============================================================================= */

#include "exlib.h"
#include "exzip.h"

/* See exwin_esporta.c: it is the hook that fills the bridges to the libc. */
void __libc_ponti_avvia(void);

static const char *const g_nomi[] = {
    "ex_zip_apri",
    "ex_zip_quante",
    "ex_zip_voce",
    "ex_zip_estrai",
    "ex_zip_crea",
    "ex_zip_aggiungi",
    "ex_zip_finisci",
    "ex_zip_chiudi",
    "ex_zip_errore",
    "ex_zip_aggiungi_albero",     /* 23 settembre 2026 */
    "ex_zip_riapri",              /* 26 settembre 2026 */
    "ex_zip_livello",             /* 26 settembre 2026 */
    "__lib_avvio"
};

static void *const g_indirizzi[] = {
    (void *)ex_zip_apri,
    (void *)ex_zip_quante,
    (void *)ex_zip_voce,
    (void *)ex_zip_estrai,
    (void *)ex_zip_crea,
    (void *)ex_zip_aggiungi,
    (void *)ex_zip_finisci,
    (void *)ex_zip_chiudi,
    (void *)ex_zip_errore,
    (void *)ex_zip_aggiungi_albero,
    (void *)ex_zip_riapri,
    (void *)ex_zip_livello,
    (void *)__libc_ponti_avvia
};

typedef char exzip_esporta_elenchi_pari[
    (sizeof(g_nomi) / sizeof(g_nomi[0]) ==
     sizeof(g_indirizzi) / sizeof(g_indirizzi[0])) ? 1 : -1];

EXLIB_TESTA(exzip_tabella, g_nomi, g_indirizzi);
