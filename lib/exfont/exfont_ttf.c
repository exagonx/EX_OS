/* =============================================================================
 * lib/exfont/exfont_ttf.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * L'istanza di un font TrueType, e la sua cache dei glifi.
 *
 * ! LA CACHE NON E' UN'OTTIMIZZAZIONE, E' LA CONDIZIONE PERCHE' SI POSSA
 * DISEGNARE. Rasterizzare un contorno vuol dire appiattire le curve, ordinare
 * gli incroci e campionare sedici volte ogni riga: per una schermata di testo
 * sono decine di migliaia di volte quel lavoro, a ogni ridisegno. Il bersaglio
 * dichiarato e' un Pentium 133. Senza cache non e' lento: e' inutilizzabile.
 *
 * ! ED E' UNA TABELLA INDICIZZATA DAL CODICE, NON UNA CACHE VERA. Niente
 * chiavi, niente sfratto, niente politica da sbagliare: EX-OS dichiara Latin-1
 * (vedi <wchar.h> nella libc), quindi i caratteri che contano sono
 * duecentocinquantasei e ci stanno tutti. Un codice oltre 255 si rasterizza al
 * momento in un riquadro di servizio — piu' lento, ma non e' il caso comune di
 * questo sistema, e il giorno che lo diventasse la risposta sarebbe una
 * seconda tabella, non una politica di sfratto.
 *
 * ! IL COSTO SI SA DIRE, e per una cache e' la proprieta' che conta di piu'.
 * A 16 pixel un glifo medio occupa una decina di byte per riga per sedici
 * righe: duecentocinquantasei glifi stanno in una quarantina di kilobyte. A 48
 * pixel sono circa quattrocento. Nessuno dei due e' una sorpresa.
 * ============================================================================= */

#include "libc.h"
#include "exfont_ttf.h"
#include "ttf.h"
#include "raster.h"

#define CACHE_N     256     /* Latin-1: vedi sopra */

typedef struct {
    unsigned char *cop;         /* 0 = mai chiesto, oppure niente da disegnare */
    int            chiesto;     /* 1 = gia' provato, anche se e' venuto vuoto */
    RasterMisure   m;
} Glifo;

typedef struct {
    TtfFont f;
    int     corpo;
    int     altezza, base;
    Glifo   cache[CACHE_N];

    /* Il riquadro di servizio per i codici oltre la tabella. Uno solo per
     * istanza: chi disegna usa il puntatore subito e poi passa al carattere
     * dopo, quindi non serve che due glifi fuori tabella vivano insieme. */
    unsigned char *servizio;
    unsigned int   servizio_byte;
    RasterMisure   servizio_m;
} Istanza;

ExTtf exttf_apri(const unsigned char *dati, unsigned int n, int corpo)
{
    Istanza *s;

    if (corpo < RASTER_CORPO_MIN || corpo > RASTER_CORPO_MAX) return 0;

    s = (Istanza *)malloc(sizeof(Istanza));
    if (!s) return 0;
    memset(s, 0, sizeof(*s));

    if (!ttf_apri(dati, n, &s->f)) { free(s); return 0; }
    s->corpo = corpo;

    /* ! L'INTERLINEA VIENE DAL FONT, non dal corpo. Ascesa piu' discesa piu'
     * il divario che il disegnatore ha voluto: su un corpo 16 di Liberation
     * fanno 18, non 16. Usare il corpo darebbe righe che si toccano, ed e'
     * l'errore che si vede subito su un paragrafo e mai su una parola.
     *
     * La discesa e' NEGATIVA nel font, quindi si sottrae. */
    {
        long scala = (long)corpo;
        long em    = (long)s->f.unita_em;

        s->base    = (int)(((long)s->f.ascesa * scala + em / 2) / em);
        s->altezza = (int)(((long)(s->f.ascesa - s->f.discesa + s->f.interlinea)
                            * scala + em / 2) / em);
        if (s->altezza < 1) s->altezza = 1;
    }

    return (ExTtf)s;
}

void exttf_chiudi(ExTtf f)
{
    Istanza *s = (Istanza *)f;
    int      i;

    if (!s) return;

    for (i = 0; i < CACHE_N; i++)
        if (s->cache[i].cop) free(s->cache[i].cop);

    if (s->servizio) free(s->servizio);
    free(s);
}

int exttf_altezza(ExTtf f) { return f ? ((Istanza *)f)->altezza : 0; }
int exttf_base(ExTtf f)    { return f ? ((Istanza *)f)->base    : 0; }

int exttf_larghezza_car(ExTtf f, unsigned int codice)
{
    Istanza     *s = (Istanza *)f;
    unsigned int g;
    RasterMisure m;

    if (!s) return 0;

    /* Se il glifo e' gia' in cache la misura c'e' gia': non si rifa'. */
    if (codice < CACHE_N && s->cache[codice].chiesto)
        return (s->cache[codice].m.avanzamento + 32) >> 6;

    g = ttf_glifo_di(&s->f, codice);
    if (!raster_misura(&s->f, g, s->corpo, &m)) return 0;
    return (m.avanzamento + 32) >> 6;
}

int exttf_ha_glifo(ExTtf f, unsigned int codice)
{
    Istanza *s = (Istanza *)f;

    if (!s) return 0;
    return ttf_glifo_di(&s->f, codice) != 0;
}

/* Rasterizza il glifo di `codice` dentro l'elemento di cache `c`. */
static void prepara(Istanza *s, unsigned int codice, Glifo *c)
{
    unsigned int g = ttf_glifo_di(&s->f, codice);
    long         byte;

    c->chiesto = 1;

    if (!raster_misura(&s->f, g, s->corpo, &c->m)) {
        memset(&c->m, 0, sizeof(c->m));
        return;
    }
    if (c->m.larghezza <= 0 || c->m.altezza <= 0) return;   /* lo spazio */

    byte = (long)c->m.larghezza * (long)c->m.altezza;
    c->cop = (unsigned char *)malloc((unsigned int)byte);

    /* ! SENZA MEMORIA SI RESTA SENZA QUEL GLIFO, non si muore. Un programma
     * che sta finendo la memoria ha problemi piu' grossi di una lettera che
     * manca, e farlo morire dentro il disegno del testo gli toglierebbe anche
     * il modo di dirlo. */
    if (!c->cop) return;

    if (!raster_glifo(&s->f, g, s->corpo, &c->m, c->cop)) {
        free(c->cop);
        c->cop = 0;
    }
}

const unsigned char *exttf_glifo(ExTtf f, unsigned int codice,
                                 int *larghezza, int *altezza,
                                 int *sx, int *sy)
{
    Istanza *s = (Istanza *)f;
    Glifo   *c;

    if (larghezza) *larghezza = 0;
    if (altezza)   *altezza   = 0;
    if (sx)        *sx        = 0;
    if (sy)        *sy        = 0;
    if (!s) return 0;

    if (codice < CACHE_N) {
        c = &s->cache[codice];
        if (!c->chiesto) prepara(s, codice, c);
    } else {
        /* Fuori tabella: nel riquadro di servizio, che cresce se serve. */
        unsigned int g = ttf_glifo_di(&s->f, codice);
        long         byte;

        if (!raster_misura(&s->f, g, s->corpo, &s->servizio_m)) return 0;
        if (s->servizio_m.larghezza <= 0 || s->servizio_m.altezza <= 0) {
            if (larghezza) *larghezza = 0;
            return 0;
        }

        byte = (long)s->servizio_m.larghezza * (long)s->servizio_m.altezza;
        if ((unsigned int)byte > s->servizio_byte) {
            unsigned char *nuovo = (unsigned char *)malloc((unsigned int)byte);

            if (!nuovo) return 0;
            if (s->servizio) free(s->servizio);
            s->servizio      = nuovo;
            s->servizio_byte = (unsigned int)byte;
        }

        if (!raster_glifo(&s->f, g, s->corpo, &s->servizio_m, s->servizio))
            return 0;

        if (larghezza) *larghezza = s->servizio_m.larghezza;
        if (altezza)   *altezza   = s->servizio_m.altezza;
        if (sx)        *sx        = s->servizio_m.sinistra;
        if (sy)        *sy        = s->servizio_m.cima;
        return s->servizio;
    }

    if (larghezza) *larghezza = c->m.larghezza;
    if (altezza)   *altezza   = c->m.altezza;
    if (sx)        *sx        = c->m.sinistra;
    if (sy)        *sy        = c->m.cima;
    return c->cop;
}
