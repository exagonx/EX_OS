/* debug/grafica/colori/pm_colori.h -> exwin/bin/pm/pm_colori.h
 * SPDX-License-Identifier: GPL-2.0-or-later
 * La scheda Colori di Impostazioni: cio' che pm.c chiama. */
#ifndef PM_COLORI_H
#define PM_COLORI_H

#include "exwin.h"

/* crea i controlli nella finestra f, registrandoli nella scheda con metti();
 * iw, ih = misura dell'area client della finestra Impostazioni */
void colori_crea(ExWindow f, int scheda, void (*metti)(int, ExWindow), int iw, int ih);
void colori_visibile(int si);                  /* la scheda e' stata mostrata/nascosta */
int  colori_comando(unsigned int wp, long lp); /* 1 = era nostro */
int  colori_mouse(int x, int y);               /* 1 = era nostro */
void colori_disegna(void);                     /* dopo ex_default_proc(EXM_PAINT) */
void colori_chiudi(void);                      /* la finestra si chiude */

#endif
