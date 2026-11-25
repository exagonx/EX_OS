/* =============================================================================
 * debug/grafica/anim_07_respiro.c  ->  lib/exwin/anim/anim_respiro.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * ANIMAZIONE 07 — RESPIRO (il pulsante predefinito che pulsa)
 *
 * Un alone del colore `fuoco` attorno al pulsante predefinito di un dialogo,
 * che cresce e cala lentamente: dice "Invio preme questo" senza leggere. E'
 * CICLICA: t va da 0 a 1000 in un periodo (consigliato 1800 ms) e poi
 * ricomincia. Si disegna PRIMA del pulsante (l'alone sta sotto i bordi).
 * Intensita' = (1 - cos(360 t)) / 2, fra 30 e 140 di alfa; ampiezza fra 2 e
 * 7 px.
 *
 * ! UN'ANIMAZIONE CICLICA COSTA SEMPRE: 30 fotogrammi al secondo finche' il
 * dialogo e' aperto. Il toolkit la ferma quando la finestra non e' in primo
 * piano (EX_WE_FOCUSED spento) e quando c'e' il salvaschermo; con
 * `animazioni = ridotte` in tema.cfg resta un alone fisso.
 *
 * Anteprima: anim_07_respiro.png, anim_07_respiro_fotogrammi.png
 * ============================================================================= */
#include "extanim.h"

void exa_respiro(ExTela *d, const ExTema *m, int x, int y, int w, int h, int t)
{
    int f = (1000 - ext_coseno(t * 360 / 1000)) / 2;      /* 0..1000..0 */
    int s = 2 + 5 * f / 1000;
    int a = 30 + 110 * f / 1000;

    ext_ombra(d, x, y, w, h, m->f.raggio, s + 4, a, m->c.fuoco, 0);
}
