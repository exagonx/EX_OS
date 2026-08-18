/* =============================================================================
 * lib/exfont/exfont.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il lettore del formato EXFN. La descrizione del formato sta in exfont.h, e
 * non si ripete qui: due copie di una descrizione divergono.
 *
 * ! OGNI CAMPO SI CONTROLLA PRIMA DI USARLO. Un file di font e' un file come
 * un altro — puo' essere troncato, corrotto, o scelto da chi sta dall'altra
 * parte di una connessione — e ogni numero che ci si legge dentro finisce in
 * un conto che poi indicizza memoria. Un'altezza di zero fa una divisione per
 * zero, un `quanti` enorme fa una moltiplicazione che TRABOCCA e rende una
 * misura piccola: il controllo «i dati ci stanno» passerebbe, e la lettura
 * andrebbe a spasso. Per questo il conto si fa in 64 bit.
 * ============================================================================= */

#include "libc.h"
#include "exfont.h"

/* I limiti non sono gusto: sono cio' che rende impossibile il traboccamento
 * qui sotto. Con questi tetti, quanti * altezza * passo sta largamente in 32
 * bit, e il controllo in 64 bit e' una cintura in piu' invece dell'unica. */
#define ALTEZZA_MAX     256
#define LARGHEZZA_MAX   256
#define GLIFI_MAX       65536

static unsigned int leggi16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

int exfont_apri(const unsigned char *dati, unsigned int n, ExFontDati *f)
{
    unsigned int  versione, bandiere, altezza, base, primo, quanti, larg_max;
    unsigned int  passo, tab, inizio;
    unsigned long long byte_glifi, servono;

    if (!f) return 0;
    memset(f, 0, sizeof(*f));

    if (!dati || n < 20) return 0;
    if (dati[0] != 'E' || dati[1] != 'X' || dati[2] != 'F' || dati[3] != 'N')
        return 0;

    versione = leggi16(dati + 4);
    if (versione != EXFONT_VERSIONE) return 0;

    bandiere = leggi16(dati + 6);
    altezza  = leggi16(dati + 8);
    base     = leggi16(dati + 10);
    primo    = leggi16(dati + 12);
    quanti   = leggi16(dati + 14);
    larg_max = leggi16(dati + 16);

    if (altezza == 0 || altezza > ALTEZZA_MAX)     return 0;
    if (larg_max == 0 || larg_max > LARGHEZZA_MAX) return 0;
    if (quanti == 0 || quanti > GLIFI_MAX)         return 0;
    if (primo + quanti > GLIFI_MAX)                return 0;

    /* ! LA BASE DENTRO L'ALTEZZA, e non oltre. Chi allinea due font sulla
     * stessa riga somma y + base: una base piu' alta dell'interlinea farebbe
     * disegnare la riga sopra quella di prima, e sembrerebbe un difetto del
     * disegno invece che di un file. */
    if (base > altezza) return 0;

    passo = (larg_max + 7u) / 8u;

    /* La tabella delle larghezze, e poi il riempimento a multiplo di quattro. */
    tab    = 20u + quanti;
    inizio = (tab + 3u) & ~3u;

    /* ! IL CONTO IN 64 BIT E' IL CONTROLLO VERO. In 32 bit
     * quanti * altezza * passo con i valori massimi tocca i 4 miliardi, e un
     * traboccamento renderebbe un numero PICCOLO: «i dati ci stanno» sarebbe
     * vero per un file che non li ha. */
    byte_glifi = (unsigned long long)quanti *
                 (unsigned long long)altezza *
                 (unsigned long long)passo;
    servono    = (unsigned long long)inizio + byte_glifi;

    if (servono > (unsigned long long)n) return 0;

    f->altezza    = altezza;
    f->base       = base;
    f->primo      = primo;
    f->quanti     = quanti;
    f->larg_max   = larg_max;
    f->passo      = passo;
    f->fisso      = (bandiere & EXFONT_FISSO) ? 1u : 0u;
    f->larghezze  = dati + 20;
    f->glifi      = dati + inizio;
    return 1;
}

/* L'indice del glifo, o -1 se il font non ha quel codice. Un posto solo per il
 * conto: e' lo stesso per la larghezza e per i pixel, e due copie divergono. */
static int indice(const ExFontDati *f, unsigned char c)
{
    unsigned int k = (unsigned int)c;

    if (!f || f->quanti == 0) return -1;
    if (k < f->primo || k >= f->primo + f->quanti) return -1;
    return (int)(k - f->primo);
}

unsigned int exfont_larghezza_car(const ExFontDati *f, unsigned char c)
{
    int i;

    if (!f) return 0;
    if (f->fisso) return f->larg_max;

    i = indice(f, c);

    /* ! UN CODICE CHE IL FONT NON HA AVANZA COME UNO SPAZIO. Rendere zero
     * farebbe sovrapporre le lettere seguenti a quelle di prima, e chi guarda
     * penserebbe a un difetto del disegno invece che a un font incompleto. */
    if (i < 0) {
        int sp = indice(f, ' ');
        return (sp < 0) ? f->larg_max : (unsigned int)f->larghezze[sp];
    }

    /* ! UNA LARGHEZZA OLTRE IL MASSIMO DICHIARATO SI TRONCA. Il passo dei dati
     * e' calcolato su larg_max: un glifo che si dichiarasse piu' largo
     * verrebbe disegnato leggendo i byte del glifo successivo. */
    return (f->larghezze[i] > f->larg_max) ? f->larg_max
                                           : (unsigned int)f->larghezze[i];
}

unsigned int exfont_larghezza(const ExFontDati *f, const char *s)
{
    unsigned int w = 0;

    if (!f || !s) return 0;
    while (*s) w += exfont_larghezza_car(f, (unsigned char)*s++);
    return w;
}

const unsigned char *exfont_glifo(const ExFontDati *f, unsigned char c)
{
    int i = indice(f, c);

    if (i < 0) return 0;
    return f->glifi + (unsigned int)i * f->altezza * f->passo;
}
