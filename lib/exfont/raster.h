/* =============================================================================
 * lib/exfont/raster.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Da contorni a pixel — il rasterizzatore
 *
 * ! L'ARITMETICA E' INTERA, IN 26.6, e non e' nostalgia. Il bersaglio
 * dichiarato di EX-OS e' un Pentium 133: la virgola mobile li' e' piu' lenta
 * degli interi, ma soprattutto ogni cambio di processo che tocca l'FPU costa
 * un salvataggio dello stato — e il disegno del testo e' proprio la cosa che
 * si fa di continuo. Un sessantaquattresimo di pixel e' una risoluzione
 * ridicolmente fine per glifi alti sedici: l'errore di arrotondamento e'
 * cento volte sotto la soglia di cio' che si vede.
 *
 * ! IL RIEMPIMENTO E' A CONTEGGIO NON NULLO, NON A PARITA'. E' la regola che
 * il TrueType richiede, e la differenza si vede in una «o»: il contorno
 * esterno gira in un verso e quello interno nell'altro, cosi' il buco resta
 * vuoto. Con la parita' verrebbe uguale per la «o», ma un glifo con tre
 * contorni annidati — succede negli accenti e nei simboli — verrebbe con i
 * pieni e i vuoti scambiati.
 *
 * ! LA COPERTURA SI CAMPIONA SEDICI VOLTE IN VERTICALE ED E' ESATTA IN
 * ORIZZONTALE. Sono due precisioni diverse apposta: in orizzontale la
 * copertura di un pixel attraversato da un bordo si CALCOLA, non si campiona,
 * perche' costa uguale; in verticale servirebbe risolvere un integrale, e
 * sedici campioni costano poco visto che ogni glifo si disegna UNA VOLTA e
 * poi sta in cache. Con meno campioni i bordi quasi orizzontali si vedono a
 * gradini.
 *
 * ! NON ALLOCA E NON INCLUDE LA libc, come ttf.c e per la stessa ragione: il
 * riquadro lo chiede a chi chiama, che e' anche l'unico a sapere dove metterlo
 * — dentro una cache, o in un buffer temporaneo.
 * ============================================================================= */
#ifndef RASTER_H
#define RASTER_H

#include "ttf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ! IL CORPO HA UN TETTO, e serve a rendere impossibile il traboccamento nei
 * conti di scala piu' che a limitare l'utente. Un carattere alto piu' di
 * duecentocinquanta pixel su uno schermo di seicento non e' testo, e' un
 * titolo disegnato una volta. */
#define RASTER_CORPO_MIN    4
#define RASTER_CORPO_MAX    256

typedef struct {
    int larghezza, altezza;     /* del riquadro di copertura, in pixel */
    int sinistra;               /* dalla penna al bordo sinistro del riquadro */
    int cima;                   /* dalla linea di base al bordo ALTO, in su */
    int avanzamento;            /* di quanto muovere la penna, in 26.6 */
} RasterMisure;

/* Quanto spazio serve per quel glifo a quel corpo, senza disegnarlo. Rende 1
 * se ha senso; 0 se il glifo non esiste o il corpo e' fuori dai limiti.
 *
 * ! UN GLIFO VUOTO RENDE 1 CON larghezza E altezza A ZERO, e non 0: lo spazio
 * e' un glifo perfettamente valido che non ha niente da disegnare, e ha un
 * avanzamento che conta. Confonderlo con un errore vorrebbe dire un testo
 * senza spazi fra le parole. */
int raster_misura(const TtfFont *f, unsigned int glifo, int corpo,
                  RasterMisure *m);

/* Disegna la copertura del glifo dentro `cop`, che deve essere grande almeno
 * m->larghezza * m->altezza byte, una riga dopo l'altra. Zero = niente, 255 =
 * pieno. `m` dev'essere quello reso da raster_misura() per lo STESSO glifo e
 * lo STESSO corpo.
 *
 * ! IL RIQUADRO SI AZZERA QUI DENTRO, tutto, prima di disegnare. Chi chiama
 * puo' riusare lo stesso buffer per glifi diversi senza pulirlo, che e'
 * esattamente cio' che fara' una cache. */
int raster_glifo(const TtfFont *f, unsigned int glifo, int corpo,
                 const RasterMisure *m, unsigned char *cop);

#ifdef __cplusplus
}
#endif

#endif /* RASTER_H */
