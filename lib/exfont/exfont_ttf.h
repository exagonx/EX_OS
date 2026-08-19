/* =============================================================================
 * lib/exfont/exfont_ttf.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Cio' che exfont.so mette a disposizione del toolkit
 *
 * ! QUESTA VOLTA LA LIBRERIA A PARTE E' GIUSTIFICATA, e vale la pena dire in
 * cosa e' diversa dal lettore EXFN che sta dentro exwin.so. Quello sono
 * centocinquanta righe che ogni programma grafico usa: metterlo fuori avrebbe
 * fatto aprire a tutti una libreria in piu'. Questo e' il contenitore
 * TrueType, l'appiattimento delle curve, un rasterizzatore e una cache — cioe'
 * esattamente «qualcosa che COSTA», che era la condizione scritta in
 * exfont.h il giorno che quel file e' nato. Un orologio o una barra delle
 * applicazioni non aprono un TrueType mai, e non devono pagarlo.
 *
 * ! UN'ISTANZA E' UN FONT A UN CORPO, non un font. La rasterizzazione dipende
 * dalla misura: tenere un oggetto solo per tutti i corpi vorrebbe dire una
 * cache con due chiavi, e la seconda chiave non serve a nessuno — chi scrive
 * un titolo e un testo apre due istanze e le tiene, che e' piu' semplice da
 * spiegare e da liberare.
 *
 * ! I PIXEL CHE ESCONO SONO COPERTURA, NON COLORE: un byte per pixel, 0 =
 * niente, 255 = pieno. Il colore lo mette chi disegna, che e' l'unico a sapere
 * su cosa sta scrivendo — e la fusione col fondo e' roba di chi possiede i
 * pixel della finestra, non di un lettore di font.
 * ============================================================================= */
#ifndef EXFONT_TTF_H
#define EXFONT_TTF_H

#ifdef __cplusplus
extern "C" {
#endif

/* Opaco: chi lo usa non deve vedere ne' la cache ne' il contenitore. */
typedef void *ExTtf;

/* Apre un TrueType a un corpo dato. `dati` deve restare vivo finche' il font
 * e' aperto — non si copia, per la stessa ragione di exfont_apri().
 * Rende 0 se il file non e' un TrueType leggibile o il corpo e' fuori scala. */
ExTtf exttf_apri(const unsigned char *dati, unsigned int n, int corpo);

/* Libera l'istanza e tutti i glifi che aveva in cache. */
void  exttf_chiudi(ExTtf f);

/* L'interlinea consigliata dal font, e la distanza dalla cima alla linea di
 * base, in pixel. Sono le due cose che servono per andare a capo e per
 * allineare font diversi sulla stessa riga. */
int   exttf_altezza(ExTtf f);
int   exttf_base(ExTtf f);

/* Di quanto avanzare dopo quel carattere, in pixel interi.
 *
 * ! ARROTONDA, E LA SOMMA SI FA SUI PIXEL INTERI. Sommare gli avanzamenti in
 * 26.6 e arrotondare alla fine darebbe righe piu' fedeli, ma vorrebbe dire che
 * la larghezza di una parola dipende da DOVE comincia — e allora la misura di
 * un testo non e' piu' una funzione del testo. Con questo sistema, chi misura
 * e chi disegna ottengono lo stesso numero, sempre. */
int   exttf_larghezza_car(ExTtf f, unsigned int codice);

/* Il glifo, dalla cache o rasterizzato al momento. Rende il puntatore alla
 * copertura (larghezza * altezza byte) o 0 se il glifo non ha niente da
 * disegnare — lo spazio, per esempio, che NON e' un errore.
 *
 * `sx` e' quanto spostarsi a destra dalla penna, `sy` quanto SALIRE dalla
 * linea di base per arrivare al bordo alto del riquadro.
 *
 * ! IL PUNTATORE APPARTIENE ALLA CACHE e vale finche' il font e' aperto. Chi
 * chiama non lo libera: liberarlo sarebbe liberare la cache di tutti. */
const unsigned char *exttf_glifo(ExTtf f, unsigned int codice,
                                 int *larghezza, int *altezza,
                                 int *sx, int *sy);

#ifdef __cplusplus
}
#endif

#endif /* EXFONT_TTF_H */
