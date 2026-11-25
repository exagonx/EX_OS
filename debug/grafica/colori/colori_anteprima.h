/* debug/grafica/colori/colori_anteprima.h -> exwin/bin/pm/colori_anteprima.h
 * SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef COLORI_ANTEPRIMA_H
#define COLORI_ANTEPRIMA_H

#include "extema.h"

#define COLZONE_MAX 48

typedef struct {
    int n;
    struct { int x, y, w, h, el; } z[COLZONE_MAX];
} ColZone;

/* la miniatura col tema m; registra in z dove sta ogni elemento EXC_* */
void colori_anteprima(ExTela *t, const ExTema *m, int x, int y, int w, int h,
                      int scelto, ColZone *z);
/* le sedici tinte rapide (8 x 2, caselle 22x18 a passo 26x22); zone 100..115 */
void colori_rapidi(ExTela *t, int x, int y, ColZone *z);
extern const unsigned int g_colori_rapidi[16];
/* l'elemento sotto (x, y), o -1 */
int  colori_zona_a(const ColZone *z, int x, int y);

#endif
