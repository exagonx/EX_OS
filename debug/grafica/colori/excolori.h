/* =============================================================================
 * debug/grafica/colori/excolori.h  ->  destinazione: lib/exwin/excolori.h
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * GLI SCHEMI DI COLORE — i colori del tema scelti dall'utente
 *
 * Un tema (ExTema, extema.h) ha una tavolozza di una cinquantina di colori:
 * troppi da far scegliere a una persona, e quasi tutti legati fra loro (la
 * luce del rilievo dipende dal fondo, il pulsante sotto il mouse dal
 * pulsante normale...). Qui l'utente sceglie i COLORI BASE — diciannove,
 * gli stessi elementi che un pannello "Aspetto" classico mostra — e
 * excol_deriva() ricava tutto il resto con regole fisse. Chi vuole di piu'
 * puo' forzare uno qualunque dei campi della tavolozza per nome
 * (i "ritocchi", righe che cominciano con '!' nel file).
 *
 * File (stesso formato per tutti):
 *     $HOME/.exwin/config/colori.cfg      lo schema in uso
 *     $HOME/.exwin/colori/<nome>.colori   gli schemi salvati dall'utente
 *     /exwin/lib/colori/<nome>.colori     gli schemi di sistema (predefiniti)
 *
 *     # commento
 *     schema   = Oceano            il nome che si vede nel pannello
 *     tema     = classico          la forma (extema_per_nome); oggi classico
 *     finestra = #D4D0C8           un colore base: #RRGGBB, RRGGBB, r,g,b
 *     ...
 *     sfumata  = si                le rifiniture (vedi ExColori)
 *     !campo_spento = #E0E0E0      un ritocco: un campo di ExTavolozza
 *
 * Righe sconosciute: ignorate (e contate, per il registro). Colori malformati:
 * resta il valore dello schema di partenza. Il file non puo' rompere il
 * toolkit: nel caso peggiore si torna al classico.
 * ============================================================================= */
#ifndef EXCOLORI_H
#define EXCOLORI_H

#include "extema.h"

/* gli elementi che l'utente sceglie, nell'ordine del pannello */
enum {
    EXC_SCRIVANIA = 0, EXC_SCRIVANIA2,
    EXC_FINESTRA, EXC_TESTO,
    EXC_CAMPO, EXC_CAMPO_TESTO,
    EXC_SELEZIONE, EXC_SELEZIONE_TESTO,
    EXC_BARRA, EXC_BARRA2, EXC_TITOLO,
    EXC_BARRA_INA, EXC_BARRA_INA2, EXC_TITOLO_INA,
    EXC_LUCE, EXC_OMBRA,
    EXC_SUGGERIMENTO, EXC_SUGGERIMENTO_TESTO,
    EXC_CHIUDI,
    EXC_N
};

#define EXC_NOME_MAX     40
#define EXC_RITOCCHI_MAX 16

typedef struct {
    char         schema[EXC_NOME_MAX];   /* "Classico", "Oceano", "Personalizzato" */
    char         tema[24];               /* "classico" */
    unsigned int c[EXC_N];               /* 0x00RRGGBB */
    /* le rifiniture: valgono per i temi a rilievo (classico, metallo) */
    int          sfumata;                /* barra del titolo sfumata          */
    int          grassetto;              /* titolo in grassetto              */
    int          ombra;                  /* px d'ombra sotto le finestre, 0..24 */
    int          raggio;                 /* angoli dei controlli, 0..8 (0 = vivi) */
    /* i ritocchi: campo della tavolozza per indice (excol_campo) e valore */
    int          n_ritocchi;
    int          rit_campo[EXC_RITOCCHI_MAX];
    unsigned int rit_valore[EXC_RITOCCHI_MAX];
} ExColori;

/* nomi degli elementi: per il file ("finestra") e per il pannello ("Finestre") */
const char *excol_chiave(int e);
const char *excol_etichetta(int e);
const char *excol_spiega(int e);

/* lo schema di un tema: i colori base letti dalla sua tavolozza */
void excol_da_tema(ExColori *s, const ExTema *m);

/* la tavolozza completa e la forma a partire dallo schema: in `out`, che
 * parte come copia di `base` (il tema della forma) */
void excol_applica(const ExColori *s, const ExTema *base, ExTema *out);

/* testo <-> schema. excol_leggi parte da cio' che c'e' in *s (di solito lo
 * schema del tema) e cambia solo cio' che il testo dice. Rende quante righe
 * non ha capito. */
int  excol_leggi(ExColori *s, const char *testo, int n);
int  excol_scrivi(const ExColori *s, char *out, int max);   /* byte scritti, -1 se non ci sta */

/* "#3A6EA5" <-> 0x3A6EA5; rende 1 se ha capito */
int  excol_colore(const char *s, int n, unsigned int *out);
void excol_esa(unsigned int c, char out[8]);

/* i campi della tavolozza per nome (per i ritocchi): indice o -1 */
int          excol_campo(const char *nome, int n);
const char  *excol_campo_nome(int i);
int          excol_campi(void);

/* quanto si leggono due colori uno sull'altro: rapporto di contrasto WCAG
 * x 100 (100 = nessun contrasto, 2100 = nero su bianco). Sotto 450 il
 * pannello avvisa. */
int  excol_contrasto(unsigned int a, unsigned int b);

#endif /* EXCOLORI_H */
