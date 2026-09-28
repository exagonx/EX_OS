/* =============================================================================
 * lib/eximg/eximg_esporta.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Cio' che eximg.so mette a disposizione. Vedi lib/exwin/exwin_esporta.c: le
 * regole sono le stesse, e sono due — si aggiunge in fondo, non si toglie mai.
 *
 * ! DUE NOMI SOLI (PIU' I TRE DELLE ANIMAZIONI), E NON E' POCO: E' TUTTA LA LIBRERIA. Un formato in piu' non
 * aggiunge un nome qui — si aggiunge alla tabella dentro eximg.c, e chi la usa
 * non cambia una riga. Se ogni formato avesse la sua funzione esportata, ogni
 * formato nuovo sarebbe una modifica in tre file e in ogni applicazione.
 * ============================================================================= */

#include "exlib.h"
#include "eximg.h"

/* Il gancio che riempie i ponti verso la libc: senza, malloc dentro la
 * libreria salterebbe a un indirizzo mai riempito. */
void __libc_ponti_avvia(void);

static const char *const g_nomi[] = {
    "eximg_carica",
    "eximg_libera",
    "__lib_avvio",
    /* @NAV-GIF, 28 settembre 2026: in fondo, come vuole la regola */
    "eximg_anima_apri",
    "eximg_anima_passo",
    "eximg_anima_chiudi"
};

static void *const g_indirizzi[] = {
    (void *)eximg_carica,
    (void *)eximg_libera,
    (void *)__libc_ponti_avvia,
    (void *)eximg_anima_apri,
    (void *)eximg_anima_passo,
    (void *)eximg_anima_chiudi
};

typedef char eximg_esporta_elenchi_pari[
    (sizeof(g_nomi) / sizeof(g_nomi[0]) ==
     sizeof(g_indirizzi) / sizeof(g_indirizzi[0])) ? 1 : -1];

EXLIB_TESTA(eximg_tabella, g_nomi, g_indirizzi);
