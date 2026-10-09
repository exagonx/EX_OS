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
#define VERSIONE_APP "0.030"
/* ! GLI INDIRIZZI DI QUEL CHE LA PAGINA CARICA (fogli, script, immagini) si
 * risolvono in buffer da NAV_URL_MAX, non da EXHTTP_URL_MAX (600): il foglio
 * della pagina dei risultati di Wikipedia ne ha 636, e si perdeva. Quel che si
 * TIENE (l'indirizzo della pagina, la cronologia) resta da 600. */
#define NAV_URL_MAX 2048
/* The size the window is BORN with. Since 26 September 2026 it can be
 * resized (@EXBROWSER-1024): the size it has NOW is g_fin_w x g_fin_h. */
#define FIN_W       760
#define MENU_H      20
#define FIN_H       (520 + MENU_H)
#define FIN_W_MIN   480         /* below this the address bar has no room */
#define FIN_H_MIN   240
#define BARRA_H     30          /* la riga dell'indirizzo, sotto i menu */
#define BARRA_Y     MENU_H      /* dove comincia */
#define MARGINE     8
/* La casella «Cerca» nella barra, misurata dalla destra come i due pulsanti:
 * cosi' aggiungerne uno sposta solo l'indirizzo. 150 pixel sono una ventina
 * di caratteri — quanti ne ha una ricerca vera. */
#define CERCA_W     150
#define CERCA_X     (g_fin_w - MARGINE - 24 - 4 - 44 - 4 - CERCA_W)
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
#define ID_CERTI    13
#define ID_SCARICHI 14
#define ID_NUOVA    15      /* File > Nuova finestra */
#define ID_SCHEDA   16      /* File > Nuova scheda */
#define ID_SCHEDE   17      /* la barra delle schede */
#define ID_IMP_HOME    720
#define ID_IMP_ORA     721
#define ID_IMP_JS      722
#define ID_IMP_IMG     723
#define ID_IMP_CACHE   724
#define ID_IMP_MOTORE  727
#define ID_IMP_RICERCA 728
#define ID_IMP_URL     729     /* the «personale» engine's template */
#define ID_IMP_SALVA   725
#define ID_IMP_ANNULLA 726
/* ! I TETTI SONO DUE PER OGNI VETTORE (28 settembre 2026, sera). Questi sono
 * quelli di un IFRAME, cioe' i tetti di sempre: un iframe si alloca con
 * malloc, e su EX-OS sbrk da' le pagine SUBITO — ogni byte in piu' qui sarebbe
 * RAM vera per ogni iframe della pagina. Quelli della pagina principale, sotto
 * con _PR, sono vettori statici: stanno nel BSS, che il kernel carica solo
 * quando si tocca, e una pagina piccola non paga niente. Prima erano uguali,
 * e una voce di Wikipedia (Venezia) arrivava troncata a 1 MB. */
#define PAGINA_MAX  (1024u * 1024u)
#define NODI_MAX    24000
#define ARENA_MAX   (1024u * 1024u)
#define ATTR_MAX    16000
#define PEZZI_MAX   24000
#define PAGINA_MAX_PR  (3u * 1024u * 1024u)
#define NODI_MAX_PR    60000
#define ARENA_MAX_PR   (3u * 1024u * 1024u)
#define ATTR_MAX_PR    80000   /* Roma su Wikipedia: ~37000 (30 settembre 2026) */
#define PEZZI_MAX_PR   60000
#define LINK_MAX    2048
#define LINK_ARENA  (192u * 1024u)
/* ! THE PAGE'S LINKS HAVE THEIR OWN CEILING TOO (30 September 2026,
 * @NAV-WIKI). A Wikipedia article (Roma) has about 5250 links: with 2048 for
 * every view, from the 2049th on a link was drawn but not recorded, and a
 * click on it did nothing — "the links do not open", further down the page.
 * Static vectors in the BSS like the others: paid only as far as used. */
#define LINK_MAX_PR    8192
#define LINK_ARENA_PR  (1024u * 1024u)
#define STORIA_MAX  32
/* ! RAISED ON 24 SEPTEMBER 2026 (they were 2400 rules, 5000 declarations,
 * 160 KB, 4 sheets): amazon.com's home page links 8 sheets, two of 262 and
 * 564 KB, and every one of them was cut. Now that pieces sit in their own
 * vector and rules are indexed (lib/excss), the numbers can follow real
 * sites: about 2.5 MB per view, touched only as far as a page uses it. */
#define CSS_REGOLE_MAX  16000
#define CSS_PEZZI_MAX   40000
/* ! 60000 E NON 30000 DAL 28 SETTEMBRE 2026 (@NAV-BORDER): una scorciatoia si
 * espande nelle sue proprieta', e `border: 1px solid #ccc` da sola vale dodici
 * dichiarazioni. Si paga solo quel che una pagina usa davvero. */
#define CSS_DICH_MAX    60000
#define CSS_ARENA_MAX   (1024u * 1024u)
#define CSS_FOGLI_MAX   16      /* quanti <link rel=stylesheet> si seguono */
/* ! 1024 E NON 256 DAL 28 SETTEMBRE 2026 (@NAV-BORDER): un blocco con un
 * bordo CSS ne usa fino a cinque — lo sfondo e un rettangolo per lato — e una
 * pagina con una lista di riquadri finiva i posti a meta'. */
#define SFONDI_MAX  1024
#define SCORRI_W    16
#define SCORRI_MIN  24          /* il pollice non scende sotto: sparirebbe */
/* ! 64 E NON 24 DAL 28 SETTEMBRE 2026: con la voce intera di Wikipedia i 24
 * finivano e il titolo usciva col carattere normale (vedi font_per). */
#define FONT_MAX    64
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

/* =============================================================================
 * ONE DOCUMENT'S LAYOUT STATE — the page, or an iframe's page
 *
 * ! THESE WERE GLOBALS UNTIL 24 SEPTEMBER 2026, and their names still are:
 * each is a macro to the field of the CURRENT view (g_vi), so no function
 * changed. An iframe has its own view; exbrowser.c switches between them
 * with vista_usa(). Cut by tools/locali/vista_taglio.py.
 * ============================================================================= */
#define GEN_MAX     (64u * 1024u)   /* the text the layout makes up: bullets, numbers */

typedef struct {
    /* i vettori grandi e i loro tetti: vedi PAGINA_MAX_PR */
    HtmlNodo      *nodi;
    HtmlAttr      *attr;
    char          *arena;
    unsigned int   nodi_max, attr_max, arena_max, pez_max;
    HtmlDoc        doc;
    Pezzo         *pez;
    int            pez_n;
    char          *link_arena;
    unsigned int  *link_off;
    /* 1 = il collegamento si apre in una finestra nuova (target="_blank" o
     * un nome di finestra; @NAV-FINESTRA, 29 settembre 2026) */
    unsigned char *link_nuova;
    unsigned int   link_max, link_arena_max;   /* LINK_MAX(_PR), LINK_ARENA(_PR) */
    int            link_n;
    CssRegola      css_reg[CSS_REGOLE_MAX];
    CssPezzo       css_pezzi[CSS_PEZZI_MAX];
    CssDich        css_dich[CSS_DICH_MAX];
    char           css_arena[CSS_ARENA_MAX];
    CssFoglio      css;
    Sfondo         sfondi[SFONDI_MAX];
    int            sfondi_n;
    char           gen[GEN_MAX];
    unsigned int   gen_n;
    int            scorri;
    int            altezza;
    int            tab_link;
    int            tab_rif;
    /* ! AN IFRAME'S VIEW HAS ITS OWN AREA: the rectangle it is drawn in (and
     * laid out in, with y from 0). cornice = 0 is the page: area_x() and the
     * others return the window's constants, as they always did. */
    int            cornice;
    int            ax, ay, aw, ah;
    /* ...and where its DRAWING is cut: the part of the rectangle that is on
     * the screen, which moves when the page scrolls. The layout keeps ax..ah
     * (y from 0); cornice_disegna() sets these and g_scorri just before
     * drawing or hit-testing. For the page they are not used. */
    int            tx, ty, tw, th;
} VistaImp;

extern VistaImp *g_vi;
#define g_nodi             (g_vi->nodi)
#define g_attr             (g_vi->attr)
#define g_arena            (g_vi->arena)
#define g_doc              (g_vi->doc)
#define g_pez              (g_vi->pez)
#define g_pez_n            (g_vi->pez_n)
#define g_nodi_max         (g_vi->nodi_max)
#define g_attr_max         (g_vi->attr_max)
#define g_arena_max        (g_vi->arena_max)
#define g_pez_max          (g_vi->pez_max)
#define g_link_arena       (g_vi->link_arena)
#define g_link_off         (g_vi->link_off)
#define g_link_nuova       (g_vi->link_nuova)
#define g_link_n           (g_vi->link_n)
#define g_link_max         (g_vi->link_max)
#define g_link_arena_max   (g_vi->link_arena_max)
#define g_css_reg          (g_vi->css_reg)
#define g_css_pezzi        (g_vi->css_pezzi)
#define g_css_dich         (g_vi->css_dich)
#define g_css_arena        (g_vi->css_arena)
#define g_css              (g_vi->css)
#define g_sfondi           (g_vi->sfondi)
#define g_sfondi_n         (g_vi->sfondi_n)
#define g_gen              (g_vi->gen)
#define g_gen_n            (g_vi->gen_n)
#define g_scorri           (g_vi->scorri)
#define g_altezza          (g_vi->altezza)
#define g_tab_link         (g_vi->tab_link)
#define g_tab_rif          (g_vi->tab_rif)

extern ExWindow g_f;
extern int g_fin_w, g_fin_h;
extern ExFont g_font_testo;
extern int g_marg_sx, g_marg_dx;

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
/* il titolo della scheda scelta segue la pagina (exbrowser.c, @NAV-SCHEDE) */
void schede_segui_titolo(void);
/* The document's own part of the drawing, inside area_*(): what an iframe
 * draws in its rectangle. disegna() is this plus the window around it. */
void disegna_contenuto(void);
/* In exbrowser.c: redraw the whole window from the page's view, whatever view
 * is current. disegna() calls it when the current view is an iframe's. */
void disegna_tutto(void);
int uguale(const char *a, const char *b);

/* ------------------------------------------------ browser_preludio.c */
void preludio_esegui(ExJsCtx *js, int dentro_w, int dentro_h);


/* =============================================================================
 * THE SELECTED TEXT OF THE PAGE (@NAV-SELEZIONE, 30 September 2026)
 *
 * Pieces from g_sel_da to g_sel_a (indices in g_pez, -1 = none), chosen by
 * dragging over the page in exbrowser.c and drawn white on blue by disegna.
 * A piece is a word: the selection goes word by word, and the copied text
 * gets a space between words on a line and a newline between lines.
 * pezzo_parola copies the word of piece i into out (at most max-1 bytes).
 * ============================================================================= */
extern int g_sel_da, g_sel_a;
int pezzo_parola(int i, char *out, int max);

#endif /* BROWSER_PRIV_H */
