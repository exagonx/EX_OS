/* =============================================================================
 * lib/excss/excss_esporta.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Cio' che excss.so mette a disposizione.
 *
 * ! E' LA PRIMA LIBRERIA DI EX-OS CHE NE USA UN'ALTRA: chiama html_nome e
 * html_attr, cioe' passa dallo stub di exhtml esattamente come farebbe un
 * programma. Non e' una complicazione, e' la cosa giusta — uno stile si applica
 * a un albero, e l'albero ha gia' un proprietario. L'alternativa era collegarsi
 * dentro una seconda copia di html.c, che e' il difetto che le librerie
 * condivise esistono per togliere.
 *
 * ! CHI APRE excss APRE ANCHE exhtml, e va saputo perche' il tetto del kernel
 * sulle librerie caricate insieme e' un numero finito (LIB_MAX in
 * kernel/loader/lib.c, dodici). Il browser ne tiene sei: libc, exwin, exfont,
 * exhttp, eximg, exhtml — e con questa sette.
 * ============================================================================= */

#include "exlib.h"
#include "css.h"

/* Il gancio che riempie i ponti verso la libc. */
void __libc_ponti_avvia(void);

static const char *const g_nomi[] = {
    "css_prepara",
    "css_analizza",
    "css_calcola",
    "css_stile_inline",
    "css_stile_vuoto",

    /* Aggiunta il 25 agosto 2026: i colori degli attributi HTML. */
    "css_colore",

    "__lib_avvio"
};

static void *const g_indirizzi[] = {
    (void *)css_prepara,
    (void *)css_analizza,
    (void *)css_calcola,
    (void *)css_stile_inline,
    (void *)css_stile_vuoto,

    (void *)css_colore,

    (void *)__libc_ponti_avvia
};

typedef char excss_esporta_elenchi_pari[
    (sizeof(g_nomi) / sizeof(g_nomi[0]) ==
     sizeof(g_indirizzi) / sizeof(g_indirizzi[0])) ? 1 : -1];

EXLIB_TESTA(excss_tabella, g_nomi, g_indirizzi);
