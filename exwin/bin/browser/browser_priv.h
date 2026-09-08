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
 * parte: erano variabili globali che chiunque poteva toccare. Averle contate
 * e' cio' che ha permesso di tagliarne.
 *
 * ! I TRE LEGAMI CHE CONTAVANO SONO TAGLIATI, dall'8 settembre 2026. Erano i
 * moduli, le immagini e gli script, e la loro fine sta in browser_vista.h: due
 * domande e un «si ricomincia», invece di sette tag scritti dentro
 * l'impaginazione. Da qui non passano piu' ne' g_ctrl, ne' g_imm, ne' g_mod,
 * ne' g_opz, ne' g_js_acceso — quelli adesso sono affari fra browser.c e
 * browser_estranei.c, e l'impaginato non li nomina.
 *
 * ! QUEL CHE RESTA E' UNA LISTA DI PARAMETRI, non di decisioni: quattordici
 * variabili condivise — g_doc, g_css, g_pez, il font del testo, la finestra, i
 * margini, lo scorrimento — nove funzioni chieste al navigatore e tre offerte
 * a lui. Erano ventisei, undici e tre. Nessuna delle quattordici obbliga
 * l'impaginato a sapere che cosa sia una casella di testo: sono le cose che a
 * una lib/exvista si passerebbero, non quelle che le impedirebbero di esistere.
 *
 * ! E TRE DICHIARAZIONI SONO SPARITE SENZA TRASLOCARE — g_url, g_stato,
 * g_font_titolo: le tocca solo browser.c, che le definisce. Stavano qui per
 * abitudine, e un elenco dei legami che elenca cose che legami non sono fa il
 * danno che l'elenco doveva evitare.
 *
 * ! E I FILE SONO ANCORA UN PROGRAMMA SOLO: le variabili condivise sono
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
/* La casella «Cerca» nella barra, misurata dalla destra come i due pulsanti:
 * cosi' aggiungerne uno sposta solo l'indirizzo. 150 pixel sono una ventina
 * di caratteri — quanti ne ha una ricerca vera. */
#define CERCA_W     150
#define CERCA_X     (FIN_W - MARGINE - 24 - 4 - 44 - 4 - CERCA_W)
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
#define ID_CERCA    12
#define ID_IMP_HOME    720
#define ID_IMP_ORA     721
#define ID_IMP_JS      722
#define ID_IMP_IMG     723
#define ID_IMP_CACHE   724
#define ID_IMP_MOTORE  727
#define ID_IMP_RICERCA 728
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

    /* ! IL RIFERIMENTO DEL CLIENTE, -1 = questo pezzo e' testo. Erano due
     * campi, `img` e `ctrl`, e il tipo del pezzo si leggeva da quale dei due
     * fosse >= 0: cioe' l'impaginato sapeva che al mondo esistono le immagini
     * e i controlli. Adesso e' un intero opaco — chi l'ha messo lo sa leggere,
     * e sono due byte in meno per pezzo, quarantotto chilobyte al tetto. */
    short        rif;

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
    int           x, y, w, h;
    unsigned int  colore;
    unsigned char bordo;    /* 0 = si riempie, >0 = contorno di tanti pixel */
} Sfondo;

/* ------------------------------------------ quel che i tre file si dividono */
extern char g_arena[ARENA_MAX];
extern CssFoglio g_css;
extern HtmlDoc g_doc;
extern ExFinestra g_f;
extern ExFont g_font_testo;
extern char g_link_arena[LINK_ARENA];
extern int g_link_n;
extern unsigned int g_link_off[LINK_MAX];
extern int g_marg_sx, g_marg_dx;
extern Pezzo g_pez[PEZZI_MAX];
extern int g_pez_n;
extern int g_scorri, g_altezza;

/* --------------------- quel che l'impaginato chiede al navigatore (9)
 *
 * ! ERANO UNDICI: se ne sono andate imm_indice e misura, che erano le due
 * chiamate con cui l'impaginato prendeva in mano un'immagine. Adesso stanno in
 * browser_estranei.h, con tutto il resto delle immagini. */
int area_h(void);
int area_w(void);
int area_x(void);
int area_y(void);
void disegna_barra(void);
ExFont font_per(int neretto, int corsivo, int famiglia, int corpo);
unsigned int numero(const char *s);
int riga_w(void);
int riga_x(void);

/* --------------------- e quel che il navigatore chiede all'impaginato (3) */
void impagina(void);
void disegna(void);
int uguale(const char *a, const char *b);

#endif /* BROWSER_PRIV_H */
