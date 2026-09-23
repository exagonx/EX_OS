/* =============================================================================
 * exwin/bin/browser/browser_estranei.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il navigatore visto dall'impaginato: quel che lui non sa misurare
 *
 * ! IL RIFERIMENTO E' UN NUMERO SOLO PERCHE' L'IMPAGINATO NE PORTA UNO SOLO.
 * Prima ogni pezzo aveva due campi, `img` e `ctrl`, e il tipo del pezzo si
 * leggeva da quale dei due era >= 0: era l'impaginato a sapere che al mondo
 * esistono le immagini e i controlli. Adesso porta un intero opaco, e la
 * distinzione sta QUI — dove le due cose sono di casa.
 *
 * ! LE FASCE SI SOMMANO, NON SI MESCOLANO: le immagini stanno sotto IMM_MAX,
 * i controlli sopra. Cambiare uno dei due tetti sposta la fascia dell'altro e
 * non rompe niente, perche' nessuno scrive quei numeri a mano; ma la somma
 * deve stare in uno `short`, che e' quanto il pezzo tiene da parte.
 * ============================================================================= */
#ifndef BROWSER_ESTRANEI_H
#define BROWSER_ESTRANEI_H

#include "browser_priv.h"
#include "browser_vista.h"

#define EST_IMM(k)     (k)
#define EST_CTRL(k)    (IMM_MAX + (k))
#define EST_CORN(k)    (IMM_MAX + CTRL_MAX + (k))     /* an iframe: 24 Sept 2026 */
#define EST_E_IMM(r)   ((r) >= 0 && (r) < IMM_MAX)
#define EST_E_CTRL(r)  ((r) >= IMM_MAX && (r) < IMM_MAX + CTRL_MAX)
#define EST_E_CORN(r)  ((r) >= IMM_MAX + CTRL_MAX)
#define EST_CHI(r)     ((r) >= IMM_MAX + CTRL_MAX ? (r) - IMM_MAX - CTRL_MAX : \
                        (r) >= IMM_MAX ? (r) - IMM_MAX : (r))

/* =============================================================================
 * ! QUESTA META' DEL FILE VIENE DA browser_priv.h, e ci e' venuta l'8 settembre
 * 2026 insieme al taglio. Finche' g_ctrl, g_imm e g_mod erano dichiarati nella
 * giuntura dell'impaginato, la frase «di qui non passano piu' i moduli e le
 * immagini» era vera nel codice e falsa nell'intestazione — e un'intestazione
 * che dice piu' di quel che il file mantiene e' peggio di nessuna.
 *
 * ! CHI LI USA LI INCLUDE: browser.c e browser_estranei.c. L'impaginato no, e
 * adesso non compilerebbe nemmeno se ci provasse.
 * ============================================================================= */
#define IMM_MAX      64
#define IMM_BYTE_MAX (128u * 1024u)     /* il file di UNA immagine  */
#define IMM_PX_MIN   (256u * 1024u)
#define IMM_PX_MAX   (2048u * 1024u)
#define CTRL_MAX        192
#define CTRL_VAL_MAX    256
#define CTRL_TESTO      0       /* input di testo, password, ricerca... */
#define CTRL_PULSANTE   1       /* button, submit, reset, image         */
#define CTRL_SPUNTA     2       /* checkbox                             */
#define CTRL_RADIO      3       /* radio                                */
#define CTRL_SCELTA     4       /* select                               */
#define CTRL_AREA       5       /* textarea                             */
#define CTRL_NASCOSTO   6       /* input type=hidden: si manda, non si vede */
#define CTRL_NOME_MAX   40
#define OPZ_MAX     128
#define MODULI_MAX      16
#define AZIONE_MAX      EXHTTP_URL_MAX
typedef struct {
    unsigned char tipo;
    unsigned char segreto;      /* password: si mostrano asterischi */
    unsigned char acceso;       /* spunta e radio */
    short         modulo;       /* indice del <form> che lo contiene, -1 */
    short         opz_primo;    /* scelta: la prima opzione in g_opz, -1 */
    short         opz_n;        /* quante ne ha */
    short         opz_ora;      /* quale e' scelta adesso */
    int           nodo;         /* il nodo che l'ha generato, -1 se libero */
    short         cur;          /* dove si sta scrivendo, dentro `valore` */
    /* ! L'ANCORA DELLA SELEZIONE, -1 quando non c'e' niente di scelto. La
     * selezione e' il tratto fra `sel` e `cur`, in un verso o nell'altro: chi
     * la usa ordina i due estremi. Tenere l'ancora invece di «inizio e fine»
     * e' cio' che fa muovere l'estremo giusto quando si allarga con Shift. */
    short         sel;
    char          nome[CTRL_NOME_MAX];   /* l'attributo `name` */
    char          valore[CTRL_VAL_MAX];
} Ctrl;

typedef struct {
    /* ! IL NODO <form> DA CUI VIENE, ed e' quel che rende inutile ricordare
     * «il modulo aperto» durante l'impaginazione: un controllo trova il suo
     * modulo salendo l'albero, e qui si riconosce quello gia' registrato. */
    int  nodo;
    char azione[AZIONE_MAX];
    int  post;                  /* 1 = method="post" */
} Modulo;

typedef struct {
    int           nodo;             /* il nodo <img> dentro g_doc              */
    unsigned int  dich_w, dich_h;   /* width= e height=, 0 se non ci sono      */
    unsigned int  w, h;             /* la misura con cui si disegna            */
    unsigned int  ris_w, ris_h;     /* il posto riservato prima che arrivasse  */
    unsigned int *px;               /* ARGB, nostri: free() li restituisce     */
    unsigned char stato;            /* 0 da prendere, 1 presa, 2 rinunciata    */
    char          src[EXHTTP_URL_MAX];
} Imm;
extern int g_js_acceso;

/* =============================================================================
 * AN IFRAME, as the layout sees it: a rectangle with a document inside
 *
 * ! THE MODEL IS GECKO'S nsSubDocumentFrame (24 September 2026). The layout
 * registers the <iframe> by node, like a control, and reserves its rectangle
 * (width x height, 300 x 150 when missing, as in Firefox). Loading happens
 * OUTSIDE the layout, in exbrowser.c (cornici_carica): the iframe's document
 * has its own view — tree, pieces, controls, engine — and draws itself into
 * the rectangle through cornice_disegna().
 *
 * ! THREE AT MOST, and only in the page: a view is 4.7 MB, allocated once and
 * reused (free() gives nothing back on EX-OS). An iframe inside an iframe, or
 * a fourth one, is an empty rectangle.
 * ============================================================================= */
#define CORNICI_MAX  3

typedef struct {
    int  nodo;                  /* the <iframe> in the parent's tree        */
    int  w, h;                  /* its rectangle                            */
    int  lx;                    /* the x it was laid out at                 */
    int  stato;                 /* 0 to load, 1 loaded, 2 given up         */
    char src[EXHTTP_URL_MAX];   /* as written in the attribute              */
} Cornice;

/* In exbrowser.c: draw iframe k of the current (parent) view in its rectangle. */
void cornice_disegna(int k, int x, int y, int w, int h);

/* One document's controls and images: see VistaImp in browser_priv.h. */
typedef struct {
    Ctrl           ctrl[CTRL_MAX];
    int            ctrl_n;
    int            ctrl_fuoco;
    char           opz[OPZ_MAX][CTRL_VAL_MAX];
    int            opz_n;
    Modulo         mod[MODULI_MAX];
    int            mod_n;
    Imm            imm[IMM_MAX];
    int            imm_n;
    unsigned int   imm_px_negato;
    /* the iframes of this document: see Cornice */
    Cornice        corn[CORNICI_MAX];
    int            corn_n;
} VistaEst;

extern VistaEst *g_ve;
#define g_ctrl             (g_ve->ctrl)
#define g_ctrl_n           (g_ve->ctrl_n)
#define g_ctrl_fuoco       (g_ve->ctrl_fuoco)
#define g_opz              (g_ve->opz)
#define g_opz_n            (g_ve->opz_n)
#define g_mod              (g_ve->mod)
#define g_mod_n            (g_ve->mod_n)
#define g_imm              (g_ve->imm)
#define g_imm_n            (g_ve->imm_n)
#define g_imm_px_negato    (g_ve->imm_px_negato)
int imm_indice(int nodo, const char *src);
void misura(const Imm *im, unsigned int nw, unsigned int nh,
                   unsigned int *pw, unsigned int *ph);

/* Il cliente da passare a vista_cliente(). */
extern const VistaCliente g_estranei;

#endif /* BROWSER_ESTRANEI_H */
