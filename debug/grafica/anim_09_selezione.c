/* =============================================================================
 * debug/grafica/anim_09_selezione.c  ->  lib/exwin/anim/anim_selezione.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * ANIMAZIONE 09 — SELEZIONE CHE SCORRE (liste, menu, linguette)
 *
 * Quando la scelta passa da una riga all'altra (frecce, clic) l'evidenziatore
 * non salta: scivola da y_da a y_a con la curva d'uscita e, a meta' corsa,
 * si allunga del 30% nella direzione del moto (come una goccia), poi torna
 * della sua misura. Il testo delle righe si ridisegna sopra a ogni
 * fotogramma nei colori normali; quello della riga d'arrivo prende il colore
 * accento_testo da t >= 600.
 * Durata consigliata: 120 ms (si deve poter tenere premuta la freccia: un
 * tasto nuovo durante l'animazione la fa ripartire dalla posizione ATTUALE,
 * non da quella di partenza).
 *
 * Lo stesso codice serve alle linguette con la sottolineatura (si passano x
 * e w invece di y e h: vedi la nota in implementazione_ANIM001.txt).
 *
 * Anteprima: anim_09_selezione.png, anim_09_selezione_fotogrammi.png
 * ============================================================================= */
#include "extanim.h"

void exa_selezione(ExTela *d, const ExTema *m, int x, int w,
                   int y_da, int y_a, int h, int t)
{
    int p = exa_esce(t);
    int y = exa_lerp(y_da, y_a, p);
    int allunga = h * 3 / 10 * ext_seno(t * 180 / 1000) / 1000;   /* 0 -> 30% -> 0 */
    int r = m->f.raggio > 4 ? 4 : m->f.raggio;
    unsigned int a = m->c.accento;
    unsigned int b = m->f.sfumato ? ext_schiarisci(a, -25) : a;

    if (y_a >= y_da) ext_tondo_pieno(d, x, y - allunga, w, h + allunga, r, a, b);
    else             ext_tondo_pieno(d, x, y, w, h + allunga, r, a, b);
}
