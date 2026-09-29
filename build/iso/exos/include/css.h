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
/* Il corpo del testo quando nessuno lo dice, in px: e' la base di `em` e `rem`
 * in cima alla pagina, e il navigatore lo usa per il testo senza stile. Una
 * costante sola perche' i due devono essere d'accordo (@NAV-UNITA). */
#define CSS_CORPO_PREDEFINITO 15
#define CSS_MISURA_NO   (-32768)        /* per le misure  */
/* `auto` nei margini (28 settembre 2026): chiede di prendersi lo spazio che
 * avanza, ed e' cosi' che si centra un blocco con una larghezza
 * (`margin: 0 auto`). Solo l'impaginatore sa quanto avanza. Chi non lo
 * distingue lo legga come zero: vedi css_margine(). */
#define CSS_MISURA_AUTO (-32767)
#define CSS_FORSE       0xFF            /* per i sì/no    */

/* display */
#define CSS_DISPLAY_EREDITA 0
#define CSS_DISPLAY_INLINE  1
#define CSS_DISPLAY_BLOCCO  2
#define CSS_DISPLAY_NIENTE  3
/* (28 settembre 2026) Un blocco che sta IN LINEA: si affianca ai fratelli
 * come una parola, con la larghezza sua. */
#define CSS_DISPLAY_INBLOCCO 4
/* Un contenitore flessibile: i figli si affiancano in una riga (flex-direction
 * row, il predefinito) o si impilano (column). inline-flex e' lo stesso. */
#define CSS_DISPLAY_FLEX     5

/* float (28 settembre 2026) */
#define CSS_GALLEGGIA_NO    0
#define CSS_GALLEGGIA_SX    1
#define CSS_GALLEGGIA_DX    2

/* position (28 settembre 2026) */
#define CSS_POS_STATICA     0
#define CSS_POS_RELATIVA    1
#define CSS_POS_ASSOLUTA    2
#define CSS_POS_FISSA       3
#define CSS_POS_APPICCICA   4   /* sticky: per noi e' statica */

/* width, max-width, min-width in larghezza_perc: un bit per campo quando il
 * valore e' una PERCENTUALE (in centesimi di punto: 5000 = 50%) invece che
 * pixel. ! LA % DI UNA LARGHEZZA E' DEL CONTENITORE, che excss non conosce:
 * la risolve l'impaginatore. */
#define CSS_LARG_PERC       0x01
#define CSS_LARG_MAX_PERC   0x02
#define CSS_LARG_MIN_PERC   0x04

/* clear: un bit per lato (28 settembre 2026, sera) */
#define CSS_PULISCI_SX      0x01
#define CSS_PULISCI_DX      0x02

/* justify-content e align-items */
#define CSS_GIU_INIZIO      0   /* flex-start, start, left, normal */
#define CSS_GIU_FINE        1   /* flex-end, end, right            */
#define CSS_GIU_CENTRO      2
#define CSS_GIU_TRA         3   /* space-between */
#define CSS_GIU_INTORNO     4   /* space-around  */
#define CSS_GIU_UGUALE      5   /* space-evenly  */
#define CSS_ALV_STIRA       0   /* stretch, normal: per noi e' in cima */
#define CSS_ALV_INIZIO      1
#define CSS_ALV_FINE        2
#define CSS_ALV_CENTRO      3
/* =============================================================================
 * font-family — e QUI SI SCEGLIE COSA SI PUO' DIRE
 *
 * ! LE FAMIGLIE SONO TRE PERCHE' TRE SONO I FILE CHE IL SISTEMA PORTA CON SE':
 * Liberation Serif, Sans e Mono. Un `font-family: Helvetica, Arial, sans-serif`
 * non si puo' onorare alla lettera — quei file non ci sono — ma la RICHIESTA si
 * capisce lo stesso: e' un carattere senza grazie. Ridurre l'elenco a una delle
 * tre facce che abbiamo e' molto piu' vicino a quel che la pagina voleva che
 * ignorarla e scrivere tutto in serif.
 *
 * ! E SI LEGGE IL PRIMO NOME CHE SI CONOSCE, non l'ultimo: e' l'ordine di
 * preferenza del CSS, e la generica in coda («sans-serif») e' l'ultima
 * spiaggia, non la prima scelta.
 * ========================================================================== */
#define CSS_FAM_EREDITA 0
#define CSS_FAM_SERIF   1
#define CSS_FAM_SANS    2
#define CSS_FAM_FISSO   3   /* monospace */

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
    unsigned char famiglia;     /* CSS_FAM_*                     */

    /* sopra, destra, sotto, sinistra — la stessa rotazione di CSS */
    short         margine[4];
    /* visibility: 1 visible, 0 hidden/collapse, CSS_FORSE non detta. Si
     * eredita. Il browser tratta «hidden» come assente: sui siti veri e'
     * quasi sempre un menu a comparsa in posizione assoluta, il cui posto
     * non conta (Vector 2022 nasconde cosi' i suoi menu). */
    unsigned char visibile;

    /* I bordi (@NAV-BORDER, 28 settembre 2026), sopra-destra-sotto-sinistra.
     * ! UN BORDO C'E' SOLO SE HA UNO STILE: e' la regola del CSS, e non una
     * scelta nostra — `border: 1px #ccc` senza «solid» non si vede. Lo
     * spessore da solo non basta. Tutti gli stili visibili (solid, dashed,
     * dotted, double...) si disegnano pieni: il tratteggio non c'e'.
     *   bordo       spessore in px, o CSS_MISURA_NO (= medium, 3 px)
     *   bordo_stile 1 visibile, 0 none/hidden, CSS_FORSE non detto (= none)
     *   bordo_col   ARGB, o CSS_NIENTE = il colore del testo (currentcolor)
     * Il padding e' lo spazio fra il bordo e il contenuto, in px. Nessuno dei
     * due si eredita. */
    short         bordo[4];
    unsigned char bordo_stile[4];
    unsigned int  bordo_col[4];
    short         imbottitura[4];

    /* LA DISPOSIZIONE (28 settembre 2026, @EXBROWSER-HTML5). Nessuno di
     * questi si eredita.
     *   larghezza, _max, _min   px, o centesimi di % (vedi larghezza_perc),
     *                           o CSS_MISURA_NO (auto / none)
     *   scatola_bordo           box-sizing: 1 border-box, 0 content-box
     *   galleggia               CSS_GALLEGGIA_*
     *   posizione               CSS_POS_*
     *   pos[4]                  top right bottom left, px o CSS_MISURA_NO
     *   flex_colonna            flex-direction: 1 column, 0 row
     *   flex_a_capo             flex-wrap: 1 wrap, 0 nowrap
     *   flex_cresce             flex-grow, in centesimi (0 = non cresce) */
    short         larghezza, larghezza_max, larghezza_min;
    unsigned char larghezza_perc;
    unsigned char scatola_bordo;
    unsigned char galleggia;
    unsigned char posizione;
    short         pos[4];
    unsigned char flex_colonna;
    unsigned char flex_a_capo;
    unsigned short flex_cresce;
    /*   pulisci                 clear: CSS_PULISCI_*
     *   giustifica              justify-content: CSS_GIU_*
     *   allinea_voci            align-items: CSS_ALV_*
     *   spazio_riga, spazio_col row-gap e column-gap in px, o CSS_MISURA_NO */
    unsigned char pulisci;
    unsigned char giustifica;
    unsigned char allinea_voci;
    short         spazio_riga, spazio_col;
} CssStile;

/* Il margine di un lato in pixel per chi non sa cosa farsene di `auto` e di
 * «non detto»: tutti e due valgono `se_no`. ! IN LINEA NELL'HEADER, e non
 * nella libreria: excss.so arriva ai programmi da uno stub con la tabella dei
 * nomi, e tre righe non valgono un nome in piu' nella tabella. */
static inline int css_margine(const CssStile *s, int lato, int se_no)
{
    int m;

    if (!s || lato < 0 || lato > 3) return se_no;
    m = s->margine[lato];
    return (m == CSS_MISURA_NO || m == CSS_MISURA_AUTO) ? se_no : m;
}

/* Mette uno stile a «niente dichiarato». */
void css_stile_vuoto(CssStile *s);

/* La larghezza della finestra in pixel, contro cui si valutano le @media
 * (min-width, max-width): va detta PRIMA di css_analizza, e di nuovo quando
 * la finestra cambia misura (e allora i fogli vanno riletti). 800 finche'
 * nessuno la dice. Aggiunta il 28 settembre 2026. */
void css_media_larghezza(int px);

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
/* ! A SELECTOR IS A CHAIN OF COMPOUNDS, and each compound is everything a
 * selector can say about ONE element: its type, its id, any number of
 * classes and attribute tests, pseudo-classes, and how it hangs on the
 * compound at its left (descendant, `>`, `+`, `~`). Until 24 September 2026
 * a piece knew one type, ONE class and one id, joined only by spaces: `.a.b`,
 * `ul > li`, `[hidden]`, `li:first-child` threw the whole rule away — which is
 * why sites showed what their style sheets hide. The strings live in the
 * sheet's arena; the layouts of the lists are written in css.c. */
#define CSS_SEL_PEZZI_MAX   8       /* «div ul li a» are four; real sheets write six */

typedef struct {
    unsigned int   tipo;    /* arena offset, 0 = «*»                            */
    unsigned int   id;      /* arena offset, 0 = none                          */
    unsigned int   classi;  /* arena: names each ended by '\0', then an empty one */
    unsigned int   attr;    /* arena: the attribute tests (see css.c), 0 = none  */
    unsigned int   nega;    /* arena: the simple selector inside :not(), 0 = none */
    unsigned short pseudo;  /* CSS_PS_*                                          */
    short          nth_a, nth_b;   /* :nth-*(an+b)                               */
    unsigned char  comb;    /* on the compound at its LEFT: ' ' '>' '+' '~'; 0 first */
} CssPezzo;

/* The pseudo-classes that can match in a document that nobody is hovering or
 * focusing. The -of-type flag applies to every structural one in the piece. */
#define CSS_PS_PRIMO      0x0001    /* :first-child                 */
#define CSS_PS_ULTIMO     0x0002    /* :last-child                  */
#define CSS_PS_UNICO      0x0004    /* :only-child                  */
#define CSS_PS_NTH        0x0008    /* :nth-child(an+b)             */
#define CSS_PS_NTH_ULT    0x0010    /* :nth-last-child(an+b)        */
#define CSS_PS_DI_TIPO    0x0020    /* ...-of-type                  */
#define CSS_PS_VUOTO      0x0040    /* :empty                       */
#define CSS_PS_RADICE     0x0080    /* :root                        */
#define CSS_PS_LINK       0x0100    /* :link, :any-link             */
#define CSS_PS_ACCESO     0x0200    /* :checked                     */
#define CSS_PS_SPENTO     0x0400    /* :disabled                    */
#define CSS_PS_ABILITATO  0x0800    /* :enabled                     */

/* ! THE PIECES LIVE IN THE SHEET'S VECTOR (f->pezzi), not in the rule: most
 * rules have one or two, and eight inside every rule would have made 16000
 * rules weigh 4 MB. A rule says where its first piece is, and how many. */
typedef struct {
    unsigned int  pezzo_primo;  /* index in f->pezzi                */
    unsigned char n_pezzi;      /* l'ultimo e' l'elemento stesso    */
    unsigned char origine;
    unsigned int  peso;         /* specificita': id*10000+cl*100+tp */
    unsigned int  ordine;       /* per rompere la parita'           */
    int           prima_dich;   /* indice della prima dichiarazione */
    int           seguente;     /* the next rule in its bucket, -1  */
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
#define CSS_P_FAMIGLIA      11  /* font-family  */
#define CSS_P_VISIBILE      12  /* visibility (28 settembre 2026) */
/* @NAV-BORDER (28 settembre 2026): i bordi e il padding, lato per lato, nella
 * stessa rotazione dei margini — sopra, destra, sotto, sinistra. Il codice del
 * lato e' la base piu' 0..3. */
#define CSS_P_BORDO_LARG    13  /* border-*-width: 13..16 */
#define CSS_P_BORDO_STILE   17  /* border-*-style: 17..20 */
#define CSS_P_BORDO_COL     21  /* border-*-color: 21..24 */
#define CSS_P_IMBOTTITURA   25  /* padding-*:      25..28 */
/* LA DISPOSIZIONE (28 settembre 2026) */
#define CSS_P_LARG          29  /* width      */
#define CSS_P_LARG_MAX      30  /* max-width  */
#define CSS_P_LARG_MIN      31  /* min-width  */
#define CSS_P_SCATOLA       32  /* box-sizing */
#define CSS_P_GALLEGGIA     33  /* float      */
#define CSS_P_POSIZIONE     34  /* position   */
#define CSS_P_POS           35  /* top right bottom left: 35..38 */
#define CSS_P_FLEX_DIR      39  /* flex-direction */
#define CSS_P_FLEX_CAPO     40  /* flex-wrap  */
#define CSS_P_FLEX_CRESCE   41  /* flex-grow  */
#define CSS_P_PULISCI       42  /* clear      */
#define CSS_P_GIUSTIFICA    43  /* justify-content */
#define CSS_P_ALLINEA_VOCI  44  /* align-items */
#define CSS_P_SPAZIO_RIGA   45  /* row-gap    */
#define CSS_P_SPAZIO_COL    46  /* column-gap */
#define CSS_P_N             47

typedef struct {
    unsigned short proprieta;   /* CSS_P_*                          */
    int            prossima;    /* -1 = fine                        */
    unsigned int   numero;      /* colore, misura o codice          */
} CssDich;

/* ! THE RULE INDEX, as Gecko and Chromium both have one (24 September 2026):
 * every rule goes into ONE bucket, by the rightmost compound — its id, else
 * its first class, else its type, else the universal list. An element looks
 * only in the buckets of its id, its classes, its type, and the universal
 * one; before, every element was compared with every rule, and a real site
 * (15000 rules, 2000 elements) meant thirty million tests per layout. */
#define CSS_SECCHI  512

typedef struct {
    CssRegola   *regole;
    unsigned int regole_max, regole_n;
    CssPezzo    *pezzi;
    unsigned int pezzi_max, pezzi_n;
    int          testa[CSS_SECCHI], coda[CSS_SECCHI];
    int          uni_testa, uni_coda;
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
                 CssPezzo *pezzi, unsigned int pezzi_max,
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
/* Legge un colore CSS — «#rgb», «#rrggbb», o un nome fra quelli noti — e lo
 * rende in ARGB. Rende 1 se ci e' riuscito.
 *
 * ! SERVE AI SUGGERIMENTI DI PRESENTAZIONE dell'HTML vecchio: `bgcolor`,
 * `text`, `link` sono attributi, non stile, e il browser li deve leggere con
 * LO STESSO parser dei fogli di stile — o due colori scritti allo stesso modo
 * verrebbero due colori diversi. */
int css_colore(const char *v, unsigned int n, unsigned int *out);

void css_calcola(const CssFoglio *f, const HtmlDoc *d, int nodo,
                 const CssStile *ereditato, CssStile *out);

#ifdef __cplusplus
}
#endif

#endif /* CSS_H */
