/* =============================================================================
 * lib/exhtml/exhtml_esporta.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Cio' che exhtml.so mette a disposizione.
 *
 * ! QUI IL CRITERIO DEI «DUE UTENTI» E' STATO SCAVALCATO DI PROPOSITO, e va
 * detto invece che nascosto. La regola di questo sistema e' che una libreria
 * condivisa conviene quando due programmi la usano, e fino a ieri il lettore di
 * HTML ne aveva uno solo: il browser. La si fa condivisa lo stesso perche' un
 * albero HTML non serve solo a impaginare una pagina — serve a chiunque debba
 * leggere del marcatore, e tenerlo dentro un eseguibile vuol dire che il
 * secondo utente non nasce perche' e' scomodo, non perche' non serve.
 *
 * ! E LA FORMA DELL'API E' GIA' QUELLA GIUSTA PER UNA .so: html_prepara()
 * riceve i buffer da chi chiama, quindi la libreria NON TIENE STATO. Due
 * programmi che analizzano due documenti nello stesso momento non si toccano,
 * e non c'e' niente da rendere rientrante — era gia' cosi' quando era
 * collegata dentro.
 *
 * ! SI ESPORTANO ANCHE I TRE LETTORI, e non sono un di piu': html_nome,
 * html_testo e html_attr sono l'unico modo di guardare dentro un nodo senza
 * conoscere l'arena. Chi li riscrivesse nell'applicazione dipenderebbe dalla
 * disposizione interna di HtmlDoc, che e' esattamente cio' che una libreria
 * deve poter cambiare.
 * ============================================================================= */

#include "exlib.h"
#include "html.h"

/* Il gancio che riempie i ponti verso la libc. */
void __libc_ponti_avvia(void);

static const char *const g_nomi[] = {
    "html_prepara",
    "html_analizza",
    "html_nome",
    "html_testo",
    "html_attr",

    /* ! E LE MUTAZIONI, dal 26 agosto 2026. Servono a exdom, che e' un'altra
     * libreria: senza esportarle, il ponte col motore JavaScript non potrebbe
     * toccare il documento — e uno script che legge la pagina e non la cambia
     * non serve a niente. */
    "html_crea_elemento",
    "html_crea_testo",
    "html_aggiungi",
    "html_inserisci_prima",
    "html_togli",
    "html_attr_metti",
    "html_attr_togli",
    "html_testo_metti",
    "html_versione",

    "__lib_avvio"
};

static void *const g_indirizzi[] = {
    (void *)html_prepara,
    (void *)html_analizza,
    (void *)html_nome,
    (void *)html_testo,
    (void *)html_attr,

    (void *)html_crea_elemento,
    (void *)html_crea_testo,
    (void *)html_aggiungi,
    (void *)html_inserisci_prima,
    (void *)html_togli,
    (void *)html_attr_metti,
    (void *)html_attr_togli,
    (void *)html_testo_metti,
    (void *)html_versione,

    (void *)__libc_ponti_avvia
};

typedef char exhtml_esporta_elenchi_pari[
    (sizeof(g_nomi) / sizeof(g_nomi[0]) ==
     sizeof(g_indirizzi) / sizeof(g_indirizzi[0])) ? 1 : -1];

EXLIB_TESTA(exhtml_tabella, g_nomi, g_indirizzi);
