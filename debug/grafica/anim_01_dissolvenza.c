/* =============================================================================
 * debug/grafica/anim_01_dissolvenza.c  ->  lib/exwin/anim/anim_dissolvenza.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * ANIMAZIONE 01 — DISSOLVENZA (apertura e chiusura di una finestra)
 *
 * La finestra compare dal trasparente al pieno (apre = 1) o sparisce
 * (apre = 0). Curva d'uscita: i primi fotogrammi fanno quasi tutto il
 * lavoro, e la finestra e' gia' leggibile a meta' del tempo.
 * Durata consigliata: 160 ms in apertura, 120 ms in chiusura.
 * Costo: una fusione per pixel della finestra per fotogramma.
 *
 * Dove: wserver, compositore. Al posto della copia della zona della
 * finestra (oggi un memcpy per riga) si chiama questa con la zona come
 * sorgente finche' l'animazione e' in corso.
 *
 * Anteprima: anim_01_dissolvenza.png (PNG animato) e
 *            anim_01_dissolvenza_fotogrammi.png
 * ============================================================================= */
#include "extanim.h"

void exa_dissolvenza(ExTela *d, int x, int y, const unsigned int *src,
                     int sw, int sh, int passo, int t, int apre)
{
    int a = exa_esce(t);

    if (!apre) a = 1000 - a;
    exa_posa(d, x, y, src, sw, sh, passo, a * 255 / 1000);
}
