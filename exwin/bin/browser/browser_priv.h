/* =============================================================================
 * exwin/bin/browser/browser_priv.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * La giuntura fra il navigatore e il suo impaginato
 *
 * ! QUESTO FILE E' L'ELENCO DEI LEGAMI, ed e' tutto il punto di averlo
 * scritto. Finche' impaginare stava dentro browser.c, quel che l'impaginato
 * chiedeva al resto del programma — e viceversa — non si vedeva da nessuna
 * parte: erano variabili globali che chiunque poteva toccare. Adesso si
 * contano: diciannove variabili condivise, undici funzioni chieste al
 * navigatore, tre offerte a lui.
 *
 * ! NON E' ANCORA UNA LIBRERIA, e non finge di esserlo. Una libreria vuole che
 * di qui non passino ne' i moduli, ne' le immagini, ne' gli script: qui
 * passano ancora tutti e tre. Ma il giorno che si vuole fare lib/exvista — per
 * il manuale dentro exide, per l'editor RTF — la domanda «cosa va tolto» ha
 * una risposta scritta invece che da cercare.
 *
 * ! I DUE FILE SONO ANCORA UN PROGRAMMA SOLO: le variabili condivise sono
 * definite in browser.c e dichiarate qui. Non sono un'interfaccia, sono una
 * confessione — ed e' meglio averla scritta in un posto che sparsa in
 * seimila righe.
 * ============================================================================= */
#ifndef BROWSER_PRIV_H
#define BROWSER_PRIV_H

#include "libc.h"
#include "exwin.h"
#include "exlib.h"
#include "eximg.h"
#include "exhttp.h"
#include "html.h"
#include "css.h"
#include "exjs.h"
#include "exdom.h"
#include "exdlg.h"
#include "exinfo.h"
#include "kbd_proto.h"
#include "biscotti.h"

/* ------------------------------------------------------------------ i tetti */
#define VERSIONE_APP "0.002"
#define FIN_W       760
#define MENU_H      20
#define FIN_H       (520 + MENU_H)
#define BARRA_H     30          /* la riga dell'indirizzo, sotto i menu */
#define BARRA_Y     MENU_H      /* dove comincia */
#define MARGINE     8
#define PERC_MAX    192
#define ID_URL      1
#define ID_VAI      2
#define ID_INDIETRO 3
#define ID_INFO     4
#define ID_APRI     5
#define ID_SALVA    6
#define ID_ESCI     7
#define ID_AIUTO    8
#define ID_DOC      9
#define ID_HOME     10
#define ID_IMPOST   11
#define ID_IMP_HOME    720
#define ID_IMP_ORA     721
#define ID_IMP_JS      722
#define ID_IMP_IMG     723
#define ID_IMP_CACHE   724
#define ID_IMP_MOTORE  727
#define ID_IMP_SALVA   725
#define ID_IMP_ANNULLA 726
#define PAGINA_MAX  (1024u * 1024u)
#define NODI_MAX    24000
#define ARENA_MAX   (1024u * 1024u)
#define ATTR_MAX    16000
#define PEZZI_MAX   24000
#define LINK_MAX    2048
#define LINK_ARENA  (192u * 1024u)
#define STORIA_MAX  32
#define CSS_REGOLE_MAX  2400
#define CSS_DICH_MAX    5000
#define CSS_ARENA_MAX   (160u * 1024u)
#define CSS_FOGLI_MAX   4       /* quanti <link rel=stylesheet> si seguono */
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
#define SFONDI_MAX  256
#define SCORRI_W    16
#define SCORRI_MIN  24          /* il pollice non scende sotto: sparirebbe */
#define FONT_MAX    24
#define CORPO_MIN   6
#define CORPO_MAX   72
#define FAM_SERIF   0
#define FAM_SANS    1
#define FAM_MONO    2

/* ------------------------------------------------------------------- i tipi */
typedef struct {
    int          x, y, w;
    unsigned int testo;         /* scostamento nell'arena del documento */
    ExFont       font;          /* il carattere, gia' scelto            */
    unsigned int colore;        /* ARGB, gia' deciso                    */
    short        h;             /* immagini e controlli: la loro altezza */
    short        link;          /* indice in g_link, -1 = niente         */
    short        img;           /* indice in g_imm, -1 = e' testo        */
    short        ctrl;          /* indice in g_ctrl, -1 = non e' un controllo */

    /* ! E IL NODO DA CUI VIENE, che prima non serviva a nessuno e adesso e'
     * l'unico modo di dire a uno script DOVE si e' cliccato. Sono quattro byte
     * per pezzo, novantasei chilobyte al tetto — e l'alternativa era ricavare
     * il nodo dalla posizione ripercorrendo l'albero a ogni clic, cioe' una
     * seconda impaginazione per sapere una cosa che la prima aveva in mano.
     *
     * ! PER IL TESTO CI SI METTE IL PADRE, non il nodo di testo. E' quel che
     * fa il browser vero: `event.target` di un clic su una parola e'
     * l'elemento che la contiene, e uno script che leggesse `target.tagName`
     * su un nodo di testo troverebbe `undefined`. */
    int          nodo;
} Pezzo;

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

typedef struct {
    int           x, y, w, h;
    unsigned int  colore;
    unsigned char bordo;    /* 0 = si riempie, >0 = contorno di tanti pixel */
} Sfondo;

/* ------------------------------------------- quel che i due file si dividono */
extern char g_arena[ARENA_MAX];
extern CssFoglio g_css;
extern Ctrl g_ctrl[CTRL_MAX];
extern int g_ctrl_fuoco;
extern int g_ctrl_n;
extern HtmlDoc g_doc;
extern ExFinestra g_f, g_url, g_stato;
extern ExFont g_font_testo, g_font_titolo;
extern Imm g_imm[IMM_MAX];
extern int g_js_acceso;
extern char g_link_arena[LINK_ARENA];
extern int g_link_n;
extern unsigned int g_link_off[LINK_MAX];
extern int g_marg_sx, g_marg_dx;
extern Modulo g_mod[MODULI_MAX];
extern int g_mod_n;
extern char g_opz[OPZ_MAX][CTRL_VAL_MAX];
extern int g_opz_n;
extern Pezzo g_pez[PEZZI_MAX];
extern int g_pez_n;
extern int g_scorri, g_altezza;

/* --------------------- quel che l'impaginato chiede al navigatore (11) */
int area_h(void);
int area_w(void);
int area_x(void);
int area_y(void);
void disegna_barra(void);
ExFont font_per(int neretto, int corsivo, int famiglia, int corpo);
int imm_indice(int nodo, const char *src);
void misura(const Imm *im, unsigned int nw, unsigned int nh,
                   unsigned int *pw, unsigned int *ph);
unsigned int numero(const char *s);
int riga_w(void);
int riga_x(void);

/* --------------------- e quel che il navigatore chiede all'impaginato (3) */
void impagina(void);
void disegna(void);
int uguale(const char *a, const char *b);

#endif /* BROWSER_PRIV_H */
