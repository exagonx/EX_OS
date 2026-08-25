/* =============================================================================
 * lib/exfont/exfont_esporta.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Cio' che exfont.so mette a disposizione. Le regole sono quelle di
 * lib/exwin/exwin_esporta.c, e sono due: si aggiunge in fondo, non si toglie
 * mai.
 *
 * ! SETTE NOMI, E NESSUNO DI LORO DISEGNA SULLO SCHERMO. Chi guarda questo
 * elenco capisce la divisione senza leggere altro: si apre un font a un corpo,
 * si chiedono le due linee che servono per andare a capo, quanto avanza un
 * carattere, e dove stanno i suoi byte di copertura. I pixel li accende chi ha
 * la finestra, perche' e' l'unico a sapere su cosa sta scrivendo.
 *
 * ! IL CONTENITORE E IL RASTERIZZATORE NON SI ESPORTANO, apposta. Sono dentro
 * — ttf.c e raster.c — e chi li vuole provare li compila sull'host con un
 * `cc`, che e' come sono stati verificati. Esportarli vorrebbe dire prometterne
 * la forma per sempre a chi non ne ha bisogno.
 * ============================================================================= */

#include "exlib.h"
#include "exfont_ttf.h"

/* Il gancio che riempie i ponti verso la libc: senza, malloc dentro la
 * libreria salterebbe a un indirizzo mai riempito. */
void __libc_ponti_avvia(void);

static const char *const g_nomi[] = {
    "exttf_apri",
    "exttf_chiudi",
    "exttf_altezza",
    "exttf_base",
    "exttf_larghezza_car",
    "exttf_glifo",
    "exttf_ha_glifo",
    "__lib_avvio"
};

static void *const g_indirizzi[] = {
    (void *)exttf_apri,
    (void *)exttf_chiudi,
    (void *)exttf_altezza,
    (void *)exttf_base,
    (void *)exttf_larghezza_car,
    (void *)exttf_glifo,
    (void *)exttf_ha_glifo,
    (void *)__libc_ponti_avvia
};

typedef char exfont_esporta_elenchi_pari[
    (sizeof(g_nomi) / sizeof(g_nomi[0]) ==
     sizeof(g_indirizzi) / sizeof(g_indirizzi[0])) ? 1 : -1];

EXLIB_TESTA(exfont_tabella, g_nomi, g_indirizzi);
