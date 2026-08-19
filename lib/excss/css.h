/* =============================================================================
 * lib/excss/css.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ExCss — dai fogli di stile allo stile di UN elemento
 *
 * ! I BUFFER SONO DI CHI CHIAMA, COME IN exhtml, e per la stessa ragione: la
 * libreria non tiene stato, quindi due programmi che leggono due documenti
 * insieme non si toccano e non c'e' niente da rendere rientrante. Il tetto
 * della memoria lo mette chi apre la pagina, che e' l'unico ad avere motivo di
 * sceglierlo.
 *
 * ! LO STILE SI CALCOLA A RICHIESTA, NON SI TIENE IN CACHE, ed e' una scelta
 * fatta GUARDANDO AVANTI. Il giorno che ci sara' un motore JavaScript, il
 * documento diventera' modificabile: un nodo cambia classe, un altro compare,
 * un terzo si prende un `style` nuovo. Uno stile calcolato una volta e
 * conservato accanto al nodo sarebbe, da quel giorno, un valore che invecchia
 * senza che nessuno se ne accorga — il difetto piu' difficile da vedere che ci
 * sia. Si ricalcola: costa un giro sulle regole, e le regole sono poche.
 *
 * ! L'EREDITARIETA' LA PASSA IL CHIAMANTE, e non e' pigrizia: `color` e i
 * `font-*` si ereditano dal padre, e chi impagina scende gia' nell'albero
 * ricorsivamente — ha in mano lo stile del padre nel momento esatto in cui
 * serve. Farlo risalire alla libreria vorrebbe dire ripercorrere la catena dei
 * padri per ogni elemento, cioe' pagare due volte lo stesso cammino.
 *
 * ! E I VALORI SONO STRUTTURATI, NON STRINGHE. Un `CssStile` e' fatto di
 * numeri e di codici, non di pezzi di testo da rileggere: quando `exjs`
 * scrivera' `elemento.style.color`, dovra' posare un valore in un campo, non
 * comporre del testo perche' qualcun altro lo rianalizzi.
 *
 * ! QUELLO CHE NON C'E', DICHIARATO: niente `@media`, niente `@import`, niente
 * pseudo-classi, niente selettori di attributo, niente unita' relative (`em`,
 * `%`, `rem`). Ci sono i selettori che si incontrano davvero — tipo, classe,
 * id, discendenza, elenco, `*` — e le proprieta' che l'impaginazione sa gia'
 * usare. Aggiungerne una e' una voce in una tabella.
 * ============================================================================= */
#ifndef CSS_H
#define CSS_H

#include "html.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------------
 * I valori
 *
 * ! «NON DICHIARATO» E' UN VALORE A SE', e serve davvero: senza, un `color`
 * non specificato sarebbe indistinguibile dal nero, e non si potrebbe piu'
 * sapere se ereditare dal padre o no. Zero non va bene — zero e' nero.
 * --------------------------------------------------------------------------- */
#define CSS_NIENTE      0xFFFFFFFFu     /* per i colori   */
#define CSS_MISURA_NO   (-32768)        /* per le misure  */
#define CSS_FORSE       0xFF            /* per i sì/no    */

/* display */
#define CSS_DISPLAY_EREDITA 0
#define CSS_DISPLAY_INLINE  1
#define CSS_DISPLAY_BLOCCO  2
#define CSS_DISPLAY_NIENTE  3

/* text-align */
#define CSS_ALL_EREDITA 0
#define CSS_ALL_SX      1
#define CSS_ALL_CENTRO  2
#define CSS_ALL_DX      3

/* -----------------------------------------------------------------------------
 * Lo stile calcolato di UN elemento
 * --------------------------------------------------------------------------- */
typedef struct {
    unsigned int  colore;       /* ARGB, o CSS_NIENTE            */
    unsigned int  sfondo;       /* ARGB, o CSS_NIENTE            */
    short         corpo;        /* px, o CSS_MISURA_NO           */
    unsigned char grassetto;    /* 0, 1, o CSS_FORSE             */
    unsigned char corsivo;      /* 0, 1, o CSS_FORSE             */
    unsigned char allineamento; /* CSS_ALL_*                     */
    unsigned char display;      /* CSS_DISPLAY_*                 */

    /* sopra, destra, sotto, sinistra — la stessa rotazione di CSS */
    short         margine[4];
} CssStile;

/* Mette uno stile a «niente dichiarato». */
void css_stile_vuoto(CssStile *s);

/* -----------------------------------------------------------------------------
 * L'origine di una dichiarazione, che e' meta' della cascata
 *
 * ! L'ORDINE DEI NUMERI E' L'ORDINE DELLA CASCATA, e va lasciato crescere in
 * fondo: chi arriva dopo con lo stesso peso vince. CSS_ORIGINE_JS non e' usata
 * da nessuno oggi ed e' dichiarata lo stesso, perche' e' il posto dove
 * finiranno le assegnazioni di un motore JavaScript — e il posto giusto e'
 * SOPRA lo `style=` scritto a mano, non accanto.
 * --------------------------------------------------------------------------- */
#define CSS_ORIGINE_SISTEMA 0   /* i valori predefiniti del browser */
#define CSS_ORIGINE_FOGLIO  1   /* <style> e <link rel=stylesheet>  */
#define CSS_ORIGINE_INLINE  2   /* l'attributo style=               */
#define CSS_ORIGINE_JS      3   /* riservata: vedi sopra            */

/* -----------------------------------------------------------------------------
 * Il foglio analizzato
 *
 * Le strutture sono esposte perche' il chiamante ne alloca i vettori, come per
 * HtmlDoc. Chi le legge dovrebbe passare da css_calcola().
 * --------------------------------------------------------------------------- */
#define CSS_SEL_PEZZI_MAX   4       /* «div ul li a» sono quattro pezzi */

typedef struct {
    unsigned int tipo;      /* scostamento nell'arena, 0 = «*»      */
    unsigned int classe;    /* scostamento, 0 = nessuna             */
    unsigned int id;        /* scostamento, 0 = nessuno             */
} CssPezzo;

typedef struct {
    CssPezzo     pezzo[CSS_SEL_PEZZI_MAX];
    unsigned char n_pezzi;      /* l'ultimo e' l'elemento stesso    */
    unsigned char origine;
    unsigned int  peso;         /* specificita': id*10000+cl*100+tp */
    unsigned int  ordine;       /* per rompere la parita'           */
    int           prima_dich;   /* indice della prima dichiarazione */
} CssRegola;

/* Le proprieta' riconosciute. ! AGGIUNGERNE UNA E' UNA VOCE QUI, una riga nella
 * tabella dei nomi in css.c e una riga in css_posa(): tre punti, tutti vicini,
 * e il compilatore trova il terzo se ne dimentichi uno (lo switch e' completo).
 * L'ordine non conta; CSS_P_N deve restare in fondo. */
#define CSS_P_COLORE        0
#define CSS_P_SFONDO        1
#define CSS_P_PESO          2   /* font-weight  */
#define CSS_P_STILE         3   /* font-style   */
#define CSS_P_CORPO         4   /* font-size    */
#define CSS_P_ALLINEA       5   /* text-align   */
#define CSS_P_DISPLAY       6
#define CSS_P_MARG_SOPRA    7
#define CSS_P_MARG_DX       8
#define CSS_P_MARG_SOTTO    9
#define CSS_P_MARG_SX       10
#define CSS_P_N             11

typedef struct {
    unsigned short proprieta;   /* CSS_P_*                          */
    int            prossima;    /* -1 = fine                        */
    unsigned int   numero;      /* colore, misura o codice          */
} CssDich;

typedef struct {
    CssRegola   *regole;
    unsigned int regole_max, regole_n;
    CssDich     *dich;
    unsigned int dich_max, dich_n;
    char        *arena;
    unsigned int arena_max, arena_n;
    unsigned int ordine;        /* cresce a ogni regola letta       */

    /* ! CHE SIA FINITO LO SPAZIO SI DICE, come in exhtml: un foglio applicato
     * a meta' produce una pagina che sembra sbagliata e non lo dice. */
    int          troncato;
} CssFoglio;

/* -----------------------------------------------------------------------------
 * Le funzioni
 * --------------------------------------------------------------------------- */

/* Prepara il foglio sui buffer di chi chiama. Azzera tutto: lo stesso foglio si
 * riusa per una pagina nuova senza rifare i buffer. */
void css_prepara(CssFoglio *f,
                 CssRegola *regole, unsigned int regole_max,
                 CssDich *dich, unsigned int dich_max,
                 char *arena, unsigned int arena_max);

/* Aggiunge le regole di un foglio. Si puo' chiamare piu' volte — un <style>,
 * poi un altro, poi un file esterno — e le regole si accumulano nell'ordine in
 * cui arrivano, che e' anche l'ordine della cascata a parita' di peso.
 * Rende il numero di regole aggiunte. */
unsigned int css_analizza(CssFoglio *f, const char *testo, unsigned int n,
                          unsigned char origine);

/* Legge un blocco di sole dichiarazioni — «color:red;font-weight:bold» — cioe'
 * il contenuto di un attributo `style`, e lo posa direttamente su `s`. */
void css_stile_inline(const char *testo, unsigned int n, CssStile *s);

/* Lo stile di `nodo`: parte da `ereditato`, applica cio' che si eredita, poi la
 * cascata delle regole che corrispondono, poi l'attributo `style`.
 *
 * `ereditato` puo' essere 0 per la radice. */
void css_calcola(const CssFoglio *f, const HtmlDoc *d, int nodo,
                 const CssStile *ereditato, CssStile *out);

#ifdef __cplusplus
}
#endif

#endif /* CSS_H */
