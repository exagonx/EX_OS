/* =============================================================================
 * lib/exwin/exwin_esporta.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Cio' che exwin.so mette a disposizione — l'unico file da toccare per
 * aggiungere una funzione alla libreria
 *
 * ! LE VOCI SI AGGIUNGONO IN FONDO PER ABITUDINE, NON PER OBBLIGO. La
 * risoluzione e' per nome (vedi lib/include/exlib.h): l'ordine di questo
 * elenco non e' parte dell'ABI e riordinarlo non rompe niente. Lo si tiene
 * ordinato per comodita' di chi legge, non perche' serva.
 *
 * ! QUELLO CHE ROMPE E' TOGLIERE UN NOME. Un'applicazione gia' compilata lo
 * cerchera' comunque, non lo trovera', e si fermera' dicendolo. E' il patto di
 * una DLL: si aggiunge quanto si vuole, non si toglie.
 *
 * ! I DUE ELENCHI DEVONO AVERE LO STESSO ORDINE, ed e' l'unica cosa che si
 * puo' sbagliare qui dentro. L'asserzione in fondo controlla che siano lunghi
 * uguale — che non e' la stessa cosa, ma prende il caso in cui se ne aggiunge
 * uno solo dei due, che e' come si sbaglia davvero.
 * ============================================================================= */

#include "exlib.h"
#include "exwin.h"

/* ! exwin.so USA LA LIBC CONDIVISA, quindi i suoi ponti vanno riempiti prima
 * che una qualunque delle sue funzioni venga chiamata. Una libreria non ha un
 * _start in cui farlo: lo fa exlib_apri() di chi la apre, cercando proprio
 * questo nome. Vedi lib/exlib/exlib.c. */
void __libc_ponti_avvia(void);

static const char *const g_nomi[] = {
    /* creare e distruggere */
    "ex_crea",
    "ex_distruggi",
    "ex_titolo",
    "ex_sposta",
    "ex_misura",
    "ex_mostra",
    "ex_fuoco",

    /* il testo di un controllo */
    "ex_testo_metti",
    "ex_testo_prendi",

    /* il ciclo dei messaggi */
    "ex_prendi_msg",
    "ex_smista",
    "ex_esci",
    "ex_procedura_base",

    /* disegnare */
    "ex_riempi",
    "ex_riquadro_disegna",
    "ex_scrivi",
    "ex_aggiorna",
    "ex_pixmap",
    "ex_immagine",

    /* la lista a scorrimento */
    "ex_lista_svuota",
    "ex_lista_aggiungi",
    "ex_lista_quante",
    "ex_lista_scelta",
    "ex_lista_scegli",
    "ex_lista_testo",

    /* l'area di testo multiriga */
    "ex_area_svuota",
    "ex_area_aggiungi",
    "ex_area_righe",
    "ex_area_riga",
    "ex_area_modificato",
    "ex_area_pulita",
    "ex_area_cursore",

    /* lo schermo */
    "ex_area_seleziona_tutto",
    "ex_area_copia",
    "ex_area_taglia",
    "ex_area_incolla",
    "ex_area_cancella",
    "ex_menu",
    "ex_menu_voce",
    "ex_rilievo",
    "ex_incavo",
    "ex_schermo",

    /* I font. In fondo, come vuole la regola: si aggiunge, non si riordina. */
    "ex_font_apri",
    "ex_font_chiudi",
    "ex_font_altezza",
    "ex_font_base",
    "ex_larghezza_testo",
    "ex_scrivi_con",
    "ex_sveglia",

    /* Aggiunte il 25 agosto 2026: trovare un carattere per famiglia. */
    "ex_font_trova",
    "ex_font_nome",

    /* L'avvio della libreria: lo chiama chi la apre, non l'applicazione. */
    "__lib_avvio"
};

static void *const g_indirizzi[] = {
    (void *)ex_crea,
    (void *)ex_distruggi,
    (void *)ex_titolo,
    (void *)ex_sposta,
    (void *)ex_misura,
    (void *)ex_mostra,
    (void *)ex_fuoco,

    (void *)ex_testo_metti,
    (void *)ex_testo_prendi,

    (void *)ex_prendi_msg,
    (void *)ex_smista,
    (void *)ex_esci,
    (void *)ex_procedura_base,

    (void *)ex_riempi,
    (void *)ex_riquadro_disegna,
    (void *)ex_scrivi,
    (void *)ex_aggiorna,
    (void *)ex_pixmap,
    (void *)ex_immagine,

    (void *)ex_lista_svuota,
    (void *)ex_lista_aggiungi,
    (void *)ex_lista_quante,
    (void *)ex_lista_scelta,
    (void *)ex_lista_scegli,
    (void *)ex_lista_testo,

    (void *)ex_area_svuota,
    (void *)ex_area_aggiungi,
    (void *)ex_area_righe,
    (void *)ex_area_riga,
    (void *)ex_area_modificato,
    (void *)ex_area_pulita,
    (void *)ex_area_cursore,

    (void *)ex_area_seleziona_tutto,
    (void *)ex_area_copia,
    (void *)ex_area_taglia,
    (void *)ex_area_incolla,
    (void *)ex_area_cancella,
    (void *)ex_menu,
    (void *)ex_menu_voce,
    (void *)ex_rilievo,
    (void *)ex_incavo,
    (void *)ex_schermo,

    (void *)ex_font_apri,
    (void *)ex_font_chiudi,
    (void *)ex_font_altezza,
    (void *)ex_font_base,
    (void *)ex_larghezza_testo,
    (void *)ex_scrivi_con,
    (void *)ex_sveglia,

    (void *)ex_font_trova,
    (void *)ex_font_nome,

    (void *)__libc_ponti_avvia
};

/* Se qualcuno aggiunge un nome e dimentica l'indirizzo (o viceversa), la
 * compilazione si ferma qui invece di esportare un puntatore preso a caso. */
typedef char exwin_esporta_elenchi_pari[
    (sizeof(g_nomi) / sizeof(g_nomi[0]) ==
     sizeof(g_indirizzi) / sizeof(g_indirizzi[0])) ? 1 : -1];

EXLIB_TESTA(exwin_tabella, g_nomi, g_indirizzi);
