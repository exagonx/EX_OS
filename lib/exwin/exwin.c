/* =============================================================================
 * lib/exwin/exwin.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ExWin — l'attuazione. Le decisioni e il perche' stanno in exwin.h.
 * ============================================================================= */

#include "libc.h"
#include "kbd_proto.h"
#include "win_proto.h"
#include "exwin.h"
#include "exlib.h"      /* per aprire eximg.so quando serve davvero */
#include "eximg.h"      /* solo il tipo e le firme: non ci si collega */

extern const unsigned char font8x16[256 * 16];

#define OGGETTI_MAX     64
#define TESTO_LEN       64

typedef struct {
    unsigned int usato;
    unsigned int classe;        /* CL_* */
    unsigned int id;            /* per i controlli */
    unsigned int win_id;        /* id lato server, solo per il primo livello */
    ExFinestra   padre;         /* 0 = primo livello */
    int          x, y, w, h;
    unsigned int stile;
    char         titolo[TESTO_LEN];
    ExProcedura  proc;

    /* Solo per il primo livello: i pixel */
    unsigned int *pix;
    unsigned int  passo_px;     /* pixel per riga */
    unsigned int  premuto;      /* il controllo e' giu' */
    ExFinestra    fuoco;        /* solo per il primo livello: chi ha i tasti */
    unsigned int  cursore;      /* posizione del cursore in una casella */
} Oggetto;

#define CL_FINESTRA     1
#define CL_PULSANTE     2
#define CL_ETICHETTA    3
#define CL_TESTO        4
#define CL_RIQUADRO     5
#define CL_SEPARATORE   6
#define CL_INTESTAZIONE 7
#define CL_TERMINALE    8
#define CL_LISTA        9
#define CL_AREA        10
#define CL_MENU        11

/* =============================================================================
 * IL TERMINALE — una shell dentro una finestra
 *
 * ! LA SHELL PARLA CON UNA PIPE, NON CON IL tty DELLA CONSOLE, ed e' tutto il
 * punto. Una shell che legge dal descrittore 0 della console si contende la
 * tastiera con chiunque altro stia su quella console — ed e' il difetto che ha
 * tenuto il server grafico senza tasti per una serata. Dietro una pipe quella
 * domanda non esiste: i tasti li da' il server alla finestra col fuoco, e da
 * li' vanno nella pipe di QUELLA shell.
 *
 * ! MA UNA PIPE NON E' UN tty, e va detto perche' si vede subito: non ha eco,
 * non ha Backspace, non ha Ctrl+C. Quelle cose le fa la line discipline, e
 * dietro una pipe non c'e' nessuna line discipline. Quindi le fa QUESTO
 * controllo: si scrive nella griglia mentre si batte, e alla shell si manda
 * solo la riga finita.
 *
 * ! IL Ctrl+C NON C'E', e non e' una dimenticanza da nascondere: mandare un
 * segnale attraverso una pipe non si puo', e finche' non c'e' un modo di
 * chiedere al kernel «interrompi quel processo» una shell in finestra non si
 * interrompe. Chi la usa deve saperlo.
 * ========================================================================== */
#define TERM_MAX        4

typedef struct {
    unsigned int usato;
    ExFinestra   ogg;
    int          fd_in;         /* noi scriviamo qui, la shell legge */
    int          fd_out;        /* la shell scrive qui, noi leggiamo */
    int          pid;
    unsigned int cols, righe;
    char        *griglia;
    unsigned int cx, cy;
    /* ! LA RIGA IN COSTRUZIONE NON STA PIU' QUI: sta nel pty, dove la tiene la
     * disciplina di linea. Erano i due campi che facevano di questo controllo
     * una line discipline scritta a mano — vedi term_tasto(). */
    /* 0 = testo normale, 1 = appena visto ESC, 2 = dentro una CSI */
    int          ansi;
    /* ! IL PROGRAMMA DENTRO PUO' USCIRE, e prima non lo diceva nessuno: la
     * finestra restava aperta con dentro una shell morta. `finito` lo segna,
     * `detto` fa si' che l'applicazione lo senta UNA volta sola — un
     * EXM_TERMFINITO a ogni giro del ciclo sarebbe un fiume. */
    int          finito;
    int          detto;
} Terminale;

static Terminale g_term[TERM_MAX];

/* =============================================================================
 * LA LISTA A SCORRIMENTO — CL_LISTA
 *
 * ! TRE APPLICAZIONI SE L'ERANO DISEGNATA A MANO prima che esistesse: l'elenco
 * del file manager, quello del dialogo Apri/Salva, e l'area dell'editor che e'
 * la stessa cosa con dentro del testo. Tre volte vuol dire che il pezzo
 * mancante e' nel toolkit, non nelle applicazioni.
 *
 * ! LO STATO STA IN UNA TABELLA A PARTE, come per il terminale, e per la
 * stessa ragione: `Oggetto` e' una struttura fissa uguale per tutte le classi,
 * e allargarla per la lista vorrebbe dire farla pagare anche a un separatore.
 * `lista_di()` la trova dall'oggetto, esattamente come `term_di()`.
 *
 * ! LE VOCI SONO UN BLOCCO SOLO, non una voce per malloc. free() non
 * restituisce niente: allocare e liberare a ogni cambio di directory
 * perderebbe memoria per sempre. Si alloca una volta alla creazione, e si
 * riusa.
 * ============================================================================= */
#define LISTA_MAX        4
#define LISTA_VOCI_MAX   512
#define LISTA_TESTO_MAX  64
#define LISTA_RIGA_H     16

typedef struct {
    unsigned int usato;
    ExFinestra   ogg;
    char        *voci;                  /* LISTA_VOCI_MAX * LISTA_TESTO_MAX */
    unsigned int n;                     /* quante ce ne sono */
    unsigned int sel;                   /* quale e' scelta */
    unsigned int primo;                 /* la prima visibile */
    unsigned int righe;                 /* quante ne stanno */
} Lista;

static Lista g_lista[LISTA_MAX];

/* =============================================================================
 * L'AREA DI TESTO MULTIRIGA — CL_AREA
 *
 * ! E' LA TERZA COSA CHE L'EDITOR SI DISEGNAVA DA SE'. Un'area di testo non e'
 * una lista con dentro delle righe: ha un cursore che si muove in due
 * direzioni, si scorre anche in orizzontale, e i tasti la CAMBIANO invece di
 * limitarsi a sceglierne una riga. Ma il grosso — quali righe si vedono, dove
 * sta il cursore, come si insegue — e' identico in ogni editor mai scritto, e
 * scriverlo in ogni applicazione e' scriverlo male tre volte.
 *
 * ! I LIMITI SONO UNA CONSEGUENZA, NON UNA PIGRIZIA. L'allocatore di EX-OS e'
 * a bump su sbrk e free() non restituisce niente: righe riallocate a ogni
 * tasto premuto perderebbero memoria per sempre. 512 righe da 200 colonne sono
 * un blocco solo, chiesto una volta alla creazione.
 * ============================================================================= */
#define AREA_MAX        2
#define AREA_RIGHE_MAX  512
#define AREA_COL_MAX    200
#define AREA_RIGA_H     16
#define AREA_CAR_W      8

typedef struct {
    unsigned int usato;
    ExFinestra   ogg;
    char        *testo;                 /* AREA_RIGHE_MAX * AREA_COL_MAX */
    unsigned int n;                     /* quante righe ci sono (>= 1) */
    unsigned int cx, cy;                /* il cursore */
    unsigned int top, left;             /* la prima riga e colonna visibili */
    unsigned int righe, cols;           /* quante ne stanno a video */
    int          modificato;
    /* ! LA SELEZIONE E' UN'ANCORA PIU' IL CURSORE, e non due estremi ordinati.
     * Tenendo «da qui a qui» ordinato si perde da quale parte si sta
     * allargando, e Shift+Sinistra dopo Shift+Destra restringerebbe dal lato
     * sbagliato. L'ordine si fa quando serve, in area_sel_ordina(). */
    int          sel;                   /* 1 = c'e' un'ancora posata */
    unsigned int ax, ay;                /* dove l'ancora e' stata posata */
} Area;

static Area g_area[AREA_MAX];

/* =============================================================================
 * GLI APPUNTI — una zona condivisa, non una variabile
 *
 * ! UNA VARIABILE QUI DENTRO SAREBBE STATA PER PROCESSO, e non e' un dettaglio:
 * i dati di una libreria condivisa sono una COPIA FRESCA per ogni programma che
 * la apre (vedi kernel/loader/lib.c). Copiare in un editor e incollare
 * nell'altro non avrebbe fatto niente — e siccome adesso l'editor si puo'
 * aprire due volte, sarebbe stata la prima cosa che qualcuno prova.
 *
 * ! LA ZONA SI CREA AL PRIMO USO E MUORE COL L'ULTIMO CHE LA TIENE APERTA, che
 * e' esattamente la semantica giusta: gli appunti valgono finche' c'e' almeno
 * un'applicazione grafica aperta. Non c'e' un shm_unlink e non serve.
 *
 * ! E NON SERVE SAPERE CHI L'HA CREATA: il kernel consegna pagine AZZERATE, e
 * una zona azzerata ha lunghezza zero — cioe' appunti vuoti. Chiedersi «l'ho
 * creata io o mi ci sono attaccato?» sarebbe una domanda in piu' con due
 * risposte da tenere d'accordo.
 * ============================================================================= */
#define APPUNTI_BYTE    4096

typedef struct {
    unsigned int n;                     /* quanti byte valgono */
    char         testo[APPUNTI_BYTE - 4];
} Appunti;

static Appunti *g_appunti = 0;

static Appunti *appunti(void)
{
    ShmZona z;

    if (g_appunti) return g_appunti;

    memset(&z, 0, sizeof(z));
    strcpy(z.nome, "exappunti");
    z.byte = APPUNTI_BYTE;
    z.flag = SHM_CREA;

    if (shm_apri(&z) != 0) return 0;    /* senza appunti si continua a vivere */
    g_appunti = (Appunti *)z.virt;
    if (g_appunti->n > sizeof(g_appunti->testo)) g_appunti->n = 0;
    return g_appunti;
}

/* =============================================================================
 * IL MENU A TENDINA — CL_MENU
 *
 * ! LA TENDINA SI DISEGNA DENTRO LA ZONA DI PIXEL DELLA FINESTRA, e non e' un
 * ripiego: e' la conseguenza di come e' fatto questo sistema. Una tendina che
 * possa uscire dal bordo dovrebbe essere una FINESTRA a se' — una zona di
 * memoria condivisa in piu' per ogni menu aperto, un giro di richieste al
 * server ogni volta che si preme «File», e la domanda «chi la chiude se il
 * programma muore mentre e' aperta?». Dentro la finestra, invece, non esiste
 * nessuna di queste domande: la tendina e' pixel come tutto il resto, e quando
 * il programma muore se ne va con la sua finestra.
 *
 * ! IL PREZZO E' DICHIARATO: una tendina piu' alta della finestra viene
 * TAGLIATA, e una piu' larga dello spazio a destra si sposta a sinistra invece
 * di uscire. Con finestre da 480 pixel in su e menu di dieci voci non capita;
 * il giorno che capitera' sara' perche' il menu e' diventato troppo lungo, e
 * la risposta giusta sara' accorciarlo.
 *
 * ! E LA SCELTA ARRIVA COME EXM_COMANDO, con lo stesso id di un pulsante. Non
 * e' pigrizia: premere «Salva» nella barra dei pulsanti e sceglierlo dal menu
 * File sono LA STESSA DECISIONE presa in due modi, e chi scrive
 * l'applicazione non deve imparare due meccanismi per sentirla. E' la stessa
 * regola dell'Invio su una lista.
 * ============================================================================= */
#define MENU_MAX         2      /* quante finestre possono avere un menu */
#define MENU_TITOLI_MAX  6
#define MENU_VOCI_MAX    12
#define MENU_TESTO_MAX   28
#define MENU_RIGA_H      16
#define MENU_BARRA_H     20

typedef struct {
    char         testo[MENU_TESTO_MAX];
    unsigned int id;                    /* 0 = separatore */
} MenuVoce;

typedef struct {
    char         nome[MENU_TESTO_MAX];
    int          x, w;                  /* dove sta nella barra */
    unsigned int n;
    MenuVoce     voce[MENU_VOCI_MAX];
} MenuTitolo;

typedef struct {
    unsigned int usato;
    ExFinestra   ogg;
    unsigned int n;
    MenuTitolo   titolo[MENU_TITOLI_MAX];
    int          aperto;                /* quale tendina, -1 = nessuna */
    int          sotto;                 /* quale voce evidenziata, -1 = nessuna */
} Menu;

static Menu g_menu[MENU_MAX];


static Oggetto g_ogg[OGGETTI_MAX];
static int     g_server = -1;
static int     g_uscita = 0;
static int     g_codice = 0;

/* -----------------------------------------------------------------------------
 * Servizio interno
 * --------------------------------------------------------------------------- */
static Oggetto *ogg(ExFinestra f)
{
    if (f == 0 || f > OGGETTI_MAX) return 0;
    if (!g_ogg[f - 1].usato) return 0;
    return &g_ogg[f - 1];
}

/* Le tre della lista stanno QUI e non accanto alla sua tabella: usano ogg()
 * e g_ogg, che sono dichiarati poco sopra. */
/* Le funzioni dell'area, accanto a quelle della lista e per la stessa ragione:
 * usano ogg() e g_ogg. */
static Area *area_di(const Oggetto *o)
{
    int i;
    ExFinestra h = (ExFinestra)(o - g_ogg + 1);

    for (i = 0; i < AREA_MAX; i++)
        if (g_area[i].usato && g_area[i].ogg == h) return &g_area[i];
    return 0;
}

static Area *area_da_h(ExFinestra f)
{
    Oggetto *o = ogg(f);
    return o ? area_di(o) : 0;
}

static char *area_riga(Area *A, unsigned int r)
{
    return &A->testo[r * AREA_COL_MAX];
}

static unsigned int area_lung(Area *A, unsigned int r)
{
    return (unsigned int)strlen(area_riga(A, r));
}

/* La vista insegue il cursore in TUTT'E DUE le direzioni: senza, le frecce
 * muoverebbero un cursore che non si vede — e chi guarda crede che il tasto
 * non funzioni. */
/* Definita fra le operazioni di selezione, ma serve anche al disegno: e' li'
 * che sta insieme a chi la usa per tagliare e copiare. */
static int area_sel_ordina(Area *A, unsigned int *y1, unsigned int *x1,
                           unsigned int *y2, unsigned int *x2);

static void area_segui(Area *A)
{
    if (A->cy < A->top)                A->top = A->cy;
    if (A->cy >= A->top + A->righe)    A->top = A->cy - A->righe + 1;
    if (A->cx < A->left)               A->left = A->cx;
    if (A->cx >= A->left + A->cols)    A->left = A->cx - A->cols + 1;
}

static Lista *lista_di(const Oggetto *o)
{
    int i;
    ExFinestra h = (ExFinestra)(o - g_ogg + 1);

    for (i = 0; i < LISTA_MAX; i++)
        if (g_lista[i].usato && g_lista[i].ogg == h) return &g_lista[i];
    return 0;
}

static Lista *lista_da_h(ExFinestra f)
{
    Oggetto *o = ogg(f);
    return o ? lista_di(o) : 0;
}

/* La vista insegue la scelta: senza, le frecce muoverebbero una riga che non
 * si vede, e chi guarda crede che il tasto non funzioni. */
static void lista_segui(Lista *L)
{
    if (L->righe == 0) return;
    if (L->sel < L->primo)                L->primo = L->sel;
    if (L->sel >= L->primo + L->righe)    L->primo = L->sel - L->righe + 1;
}

/* La finestra di primo livello a cui un oggetto appartiene: e' li' che
 * stanno i pixel, perche' i controlli non hanno una zona propria. */
static Menu *menu_di(const Oggetto *o)
{
    int j;

    if (!o || o->classe != CL_MENU) return 0;
    for (j = 0; j < MENU_MAX; j++)
        if (g_menu[j].usato && g_menu[j].ogg == (ExFinestra)(o - g_ogg + 1))
            return &g_menu[j];
    return 0;
}

static Oggetto *radice(ExFinestra f)
{
    Oggetto *o = ogg(f);

    while (o && o->padre) o = ogg(o->padre);
    return o;
}

/* La maniglia della finestra di primo livello che contiene un oggetto. */
static ExFinestra radice_h(ExFinestra f)
{
    Oggetto *o = ogg(f);

    while (o && o->padre) { f = o->padre; o = ogg(f); }
    return o ? f : 0;
}

static unsigned int classe_da_nome(const char *c)
{
    if (strcmp(c, "finestra")     == 0) return CL_FINESTRA;
    if (strcmp(c, "pulsante")     == 0) return CL_PULSANTE;
    if (strcmp(c, "etichetta")    == 0) return CL_ETICHETTA;
    if (strcmp(c, "testo")        == 0) return CL_TESTO;
    if (strcmp(c, "riquadro")     == 0) return CL_RIQUADRO;
    if (strcmp(c, "separatore")   == 0) return CL_SEPARATORE;
    if (strcmp(c, "intestazione") == 0) return CL_INTESTAZIONE;
    if (strcmp(c, "terminale")    == 0) return CL_TERMINALE;
    if (strcmp(c, "lista")        == 0) return CL_LISTA;
    if (strcmp(c, "areatesto")    == 0) return CL_AREA;
    if (strcmp(c, "menu")         == 0) return CL_MENU;
    return 0;
}

/* Lo spostamento di un oggetto rispetto all'AREA DEL CLIENT della sua
 * finestra: i controlli possono stare dentro un riquadro, e allora si sommano
 * gli spostamenti dei riquadri che li contengono.
 *
 * ! LA POSIZIONE DELLA FINESTRA NON ENTRA, e sommarla era un difetto solo con
 * DUE sintomi. La x,y di una finestra di primo livello e' dove sta SULLO
 * SCHERMO; i suoi figli sono relativi all'area del client, che comincia a
 * (0,0) dentro la zona di pixel condivisa. Sommandola:
 *
 *   - i controlli venivano DISEGNATI spostati di quanto la finestra distava
 *     dall'angolo dello schermo. Con la finestra a (80,60) sembrava quasi
 *     giusto — l'intestazione era larga e il taglio a destra si notava poco;
 *   - e il clic non trovava piu' niente, perche' il server manda coordinate
 *     GIA' relative al client e qui si cercava 80 pixel piu' in la'. Il
 *     sintomo era «un clic nell'area del client non da' il fuoco», che
 *     sembrava un problema di eventi e non di aritmetica.
 *
 * Percio' si sale finche' il padre ha a sua volta un padre: il primo livello
 * si ferma e non contribuisce. */
static void origine(Oggetto *o, int *ox, int *oy)
{
    int x = 0, y = 0;
    Oggetto *p = o->padre ? ogg(o->padre) : 0;

    while (p && p->padre) {
        x += p->x;
        y += p->y;
        p = ogg(p->padre);
    }
    *ox = x; *oy = y;
}

/* -----------------------------------------------------------------------------
 * Il fuoco
 *
 * ! IL SERVER SA QUALE FINESTRA, LA LIBRERIA QUALE CONTROLLO. Il server manda
 * i tasti alla finestra in cima e non sa che dentro ci siano dei controlli —
 * e non deve saperlo, o dovrebbe conoscere il toolkit. Chi sta dentro la
 * finestra lo decide chi la disegna, cioe' questo file.
 *
 * ! NON TUTTI I CONTROLLI LO ACCETTANO. Un'etichetta o un separatore col fuoco
 * sarebbe un buco nero: i tasti ci finirebbero dentro e non succederebbe
 * niente, e chi prova non capirebbe perche'. Tab li salta.
 * --------------------------------------------------------------------------- */
static int accetta_fuoco(const Oggetto *o)
{
    return o->classe == CL_TESTO || o->classe == CL_PULSANTE ||
           o->classe == CL_TERMINALE || o->classe == CL_LISTA ||
           o->classe == CL_AREA;
}

static void fuoco_metti(ExFinestra f, ExFinestra c)
{
    Oggetto *r = radice(f);
    Oggetto *o = ogg(c);

    if (!r || !o || !accetta_fuoco(o)) return;
    r->fuoco = c;
    o->cursore = (unsigned int)strlen(o->titolo);
}

/* =============================================================================
 * ex_fuoco — dare il fuoco a un controllo, dall'esterno
 *
 * ! FINO AL 17 AGOSTO 2026 NON SI POTEVA, e il fuoco andava al PRIMO controllo
 * creato che lo accettasse. Sembra un dettaglio finche' non morde: nel dialogo
 * «Apri» di ExDlg la casella del nome si creava prima del pulsante «Su» —
 * cioe' in un ordine che non e' quello in cui si legge — soltanto per
 * prendersi il fuoco. Senza quel trucco il dialogo sembrava sordo: tutto
 * quello che si batteva finiva in un pulsante, che i tasti non li usa.
 *
 * ! CHIEDERE IL FUOCO PER UN CONTROLLO CHE NON LO ACCETTA NON E' UN ERRORE, e'
 * un no: fuoco_metti() lascia le cose come stanno. Un'etichetta col fuoco
 * sarebbe una finestra che ignora la tastiera senza dirlo, ed e' peggio di una
 * chiamata che non fa niente.
 * ============================================================================= */
void ex_fuoco(ExFinestra f)
{
    Oggetto *o = ogg(f);

    if (!o) return;

    /* Si passa la RADICE come prima cosa: il fuoco e' un campo della finestra
     * di primo livello, non del controllo. Chi chiama ha in mano il controllo
     * e non deve sapere anche chi sia suo padre. */
    fuoco_metti(radice_h(f), f);
}

/* Il prossimo controllo che accetta il fuoco, in ordine di creazione. */
static void fuoco_avanti(ExFinestra f)
{
    Oggetto *r = radice(f);
    int i, partenza = -1, primo = -1;

    if (!r) return;

    for (i = 0; i < OGGETTI_MAX; i++) {
        if (!g_ogg[i].usato || g_ogg[i].padre == 0) continue;
        if (radice((ExFinestra)(i + 1)) != r) continue;
        if (!accetta_fuoco(&g_ogg[i])) continue;
        if (primo < 0) primo = i;
        if ((ExFinestra)(i + 1) == r->fuoco) { partenza = i; continue; }
        if (partenza >= 0) { fuoco_metti(f, (ExFinestra)(i + 1)); return; }
    }
    if (primo >= 0) fuoco_metti(f, (ExFinestra)(primo + 1));
}

/* -----------------------------------------------------------------------------
 * Disegnare
 * --------------------------------------------------------------------------- */
static void punto(Oggetto *r, int x, int y, unsigned int c)
{
    if (!r || !r->pix) return;
    if (x < 0 || y < 0 || x >= r->w || y >= r->h) return;
    r->pix[(unsigned int)y * r->passo_px + (unsigned int)x] = c;
}

void ex_riempi(ExFinestra f, int x, int y, int w, int h, unsigned int c)
{
    Oggetto *r = radice(f);
    int i, j;

    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            punto(r, x + i, y + j, c);
}

/* =============================================================================
 * ex_pixmap — posare un rettangolo di pixel gia' pronti
 *
 * ! SENZA QUESTA, UN'IMMAGINE SI DISEGNAVA UN PIXEL PER CHIAMATA. Il lettore
 * BMP faceva `ex_riempi(f, x+i, y+j, 1, 1, c)`: per un'immagine 800x600 sono
 * 480000 chiamate di funzione, ognuna con il suo controllo dei limiti — e
 * attraverso i ponti della libreria condivisa, ognuna anche un salto
 * indiretto. Qui i limiti si controllano una volta e si copia per righe.
 *
 * ! I PIXEL SONO ARGB A 32 BIT, come dappertutto nel toolkit: chi decodifica
 * un formato produce quello, e non deve sapere com'e' fatto lo schermo. La
 * conversione a 16 o 24 bit la fa il server, in un posto solo.
 *
 * `passo` e' quanti pixel c'e' fra l'inizio di una riga e l'inizio della
 * successiva: serve a posare un RITAGLIO di un'immagine piu' grande senza
 * doverla ricopiare.
 * ============================================================================= */
void ex_pixmap(ExFinestra f, int x, int y, int w, int h,
               const unsigned int *px, unsigned int passo)
{
    Oggetto *r = radice(f);
    int j, i;

    if (!r || !r->pix || !px || w <= 0 || h <= 0) return;
    if (passo == 0) passo = (unsigned int)w;

    /* Il ritaglio si fa QUI e non nel ciclo: un confronto per pixel su
     * mezzo milione di pixel e' mezzo milione di confronti. */
    for (j = 0; j < h; j++) {
        int ry = y + j;
        const unsigned int *src;
        unsigned int *dst;

        if (ry < 0 || ry >= r->h) continue;

        src = px + (unsigned int)j * passo;
        dst = r->pix + (unsigned int)ry * r->passo_px;

        for (i = 0; i < w; i++) {
            int rx = x + i;
            if (rx < 0 || rx >= r->w) continue;
            dst[rx] = src[i];
        }
    }
}

void ex_riquadro_disegna(ExFinestra f, int x, int y, int w, int h, unsigned int c)
{
    ex_riempi(f, x, y, w, 1, c);
    ex_riempi(f, x, y + h - 1, w, 1, c);
    ex_riempi(f, x, y, 1, h, c);
    ex_riempi(f, x + w - 1, y, 1, h, c);
}

/* =============================================================================
 * IL RILIEVO — il perche' sta in exwin.h, qui c'e' solo l'aritmetica
 *
 * ! GLI ANGOLI APPARTENGONO ALLA LUCE, e non e' un dettaglio da lasciare al
 * caso: le due righe si incontrano in due angoli, e chi dei due ci arriva per
 * ultimo decide come si vede lo spigolo. Disegnando prima l'ombra e poi la
 * luce, l'angolo in alto a destra e quello in basso a sinistra restano chiari
 * — che e' come si vede uno spigolo illuminato da sopra a sinistra. Al
 * contrario si ottiene un rettangolo che sembra ritagliato male.
 * ============================================================================= */
static void bordo3d(ExFinestra f, int x, int y, int w, int h,
                    unsigned int sopra, unsigned int sotto)
{
    if (w <= 0 || h <= 0) return;

    ex_riempi(f, x, y + h - 1, w, 1, sotto);            /* il fondo   */
    ex_riempi(f, x + w - 1, y, 1, h, sotto);            /* la destra  */
    ex_riempi(f, x, y, w, 1, sopra);                    /* la cima    */
    ex_riempi(f, x, y, 1, h, sopra);                    /* la sinistra*/
}

void ex_rilievo(ExFinestra f, int x, int y, int w, int h)
{
    bordo3d(f, x, y, w, h, EX_LUCE, EX_OMBRA);
}

void ex_incavo(ExFinestra f, int x, int y, int w, int h)
{
    bordo3d(f, x, y, w, h, EX_OMBRA, EX_LUCE);
}

void ex_scrivi(ExFinestra f, int x, int y, const char *s, unsigned int c)
{
    Oggetto *r = radice(f);
    unsigned int i;

    if (!s) return;

    for (i = 0; s[i]; i++) {
        const unsigned char *g = &font8x16[(unsigned char)s[i] * 16];
        int rr, b;

        for (rr = 0; rr < 16; rr++)
            for (b = 0; b < 8; b++)
                if (g[rr] & (0x80 >> b))
                    punto(r, x + (int)i * 8 + b, y + rr, c);
    }
}

void ex_aggiorna(ExFinestra f)
{
    Oggetto   *r = radice(f);
    WinRegione w;

    if (!r || g_server < 0) return;

    w.id = r->win_id;
    w.x = 0; w.y = 0;
    w.larghezza = (unsigned int)r->w;
    w.altezza   = (unsigned int)r->h;
    (void)ipc_send((unsigned int)g_server, WIN_MSG_AGGIORNA, &w, sizeof(w));
}

/* -----------------------------------------------------------------------------
 * I controlli, disegnati dalla libreria
 *
 * ! SONO DISEGNATI QUI, NON DAL SERVER, ed e' la conseguenza di avere una
 * zona di pixel per FINESTRA e non per controllo. Un pulsante e' un rettangolo
 * con un bordo e una scritta dentro la zona del suo padre: dargli una zona
 * condivisa sua vorrebbe dire una zona per etichetta, cioe' decine di zone per
 * una finestra qualunque.
 * --------------------------------------------------------------------------- */
static Terminale *term_di(const Oggetto *o)
{
    int i;
    ExFinestra h = (ExFinestra)(o - g_ogg + 1);

    for (i = 0; i < TERM_MAX; i++)
        if (g_term[i].usato && g_term[i].ogg == h) return &g_term[i];
    return 0;
}

/* Fa scorrere la griglia di una riga. Il terminale piu' semplice che esista
 * fa questo e basta: niente storia, niente selezione. */
static void term_scorri(Terminale *t)
{
    unsigned int i, n = t->cols * (t->righe - 1);

    for (i = 0; i < n; i++) t->griglia[i] = t->griglia[i + t->cols];
    for (i = n; i < t->cols * t->righe; i++) t->griglia[i] = ' ';
    if (t->cy > 0) t->cy--;
}

/* =============================================================================
 * CAMBIARE MISURA A UN TERMINALE
 *
 * ! IL TESTO SI PORTA DIETRO DAL FONDO, NON DALLA CIMA, ed e' l'unica scelta
 * che non fa sparire quello che si stava guardando. La riga che interessa e'
 * sempre l'ultima scritta — il prompt, la risposta all'ultimo comando —, e
 * allineando le griglie in alto quella finirebbe fuori dal bordo inferiore
 * appena la finestra si stringe. E' quello che fa xterm, e per lo stesso
 * motivo.
 *
 * ! E LA MISURA NUOVA SI DICE AL pty, o l'avra' cambiata solo la finestra. Un
 * programma a schermo pieno la chiede al pty con PTY_CTL_LEGGI_MISURA: senza
 * questa riga continuerebbe a disegnare 80x25 dentro un'area piu' grande, che
 * e' esattamente il difetto che si sta togliendo.
 * ============================================================================= */
static void term_misura(Terminale *t, unsigned int cols, unsigned int righe)
{
    char *nuova;
    unsigned int nr, nc, sy, dy, r, c;

    if (!t->usato || cols == 0 || righe == 0) return;
    if (cols == t->cols && righe == t->righe) return;

    nuova = (char *)malloc(cols * righe);
    if (!nuova) return;                 /* niente memoria: si resta com'era */
    memset(nuova, ' ', cols * righe);

    nr = (righe < t->righe) ? righe : t->righe;
    nc = (cols  < t->cols)  ? cols  : t->cols;
    sy = t->righe - nr;                 /* le ULTIME righe della vecchia */
    dy = righe - nr;                    /* in fondo alla nuova */

    for (r = 0; r < nr; r++)
        for (c = 0; c < nc; c++)
            nuova[(dy + r) * cols + c] = t->griglia[(sy + r) * t->cols + c];

    free(t->griglia);
    t->griglia = nuova;

    t->cy = (t->cy >= sy) ? (t->cy - sy) + dy : dy;
    if (t->cy >= righe) t->cy = righe - 1;
    if (t->cx >= cols)  t->cx = cols - 1;

    t->cols  = cols;
    t->righe = righe;

    pty_ctl(t->fd_in, PTY_CTL_MISURA, (righe << 16) | cols);
}

static void term_carattere(Terminale *t, char c)
{
    /* =========================================================================
     * ! LE SEQUENZE ANSI SI INGOIANO INTERE, NON UN CARATTERE PER VOLTA.
     *
     * Il commento qui sotto diceva che i caratteri di controllo si buttano, ed
     * era vero e insufficiente: di ESC [ 9 2 m l'unico carattere sotto 0x20 e'
     * ESC. Il resto sono lettere e cifre normali, che finivano dritte nella
     * griglia — la prima riga di ogni terminale in finestra era
     *
     *     [92mex-os[97m:[96m/[97m>
     *
     * cioe' il prompt colorato della shell, letto alla lettera. Sembrava che
     * la shell mandasse spazzatura, e invece mandava esattamente quello che
     * manda a qualunque terminale: eravamo noi a non saperlo leggere.
     *
     * ! SI INGOIA, NON SI INTERPRETA. Fingere di avere i colori vorrebbe dire
     * un attributo per cella e un secondo posto in cui decidere come si
     * disegna il testo. Buttare la sequenza da' un prompt in bianco e nero —
     * che e' esattamente quello che questa griglia sa fare.
     * ========================================================================= */
    if (t->ansi) {
        /* Una sequenza CSI finisce al primo byte fra '@' e '~': fino a li' ci
         * sono solo cifre, ';' e '?'. */
        if (t->ansi == 1) {
            /* Appena dopo ESC: '[' apre una CSI, qualunque altra cosa e' una
             * sequenza breve che finisce subito. */
            t->ansi = (c == '[') ? 2 : 0;
            return;
        }
        if ((unsigned char)c >= '@' && (unsigned char)c <= '~') t->ansi = 0;
        return;
    }
    if (c == 0x1B) { t->ansi = 1; return; }

    if (c == '\n') { t->cx = 0; t->cy++; }
    else if (c == '\r') { t->cx = 0; }
    else if (c == '\b') { if (t->cx > 0) t->cx--; t->griglia[t->cy * t->cols + t->cx] = ' '; }
    else if (c == '\t') { t->cx = (t->cx + 8) & ~7u; }
    else if ((unsigned char)c >= 0x20) {
        t->griglia[t->cy * t->cols + t->cx] = c;
        t->cx++;
    } else {
        /* ! I CARATTERI DI CONTROLLO SI BUTTANO, non si disegnano. Una shell
         * che manda una sequenza ANSI per colorare il prompt riempirebbe la
         * griglia di simboli senza senso: qui non c'e' un interprete ANSI, e
         * fingere di averlo e' peggio che non averlo. */
        return;
    }

    if (t->cx >= t->cols) { t->cx = 0; t->cy++; }
    while (t->cy >= t->righe) term_scorri(t);
}

/* Legge cio' che la shell ha scritto, se c'e'. Rende 1 se qualcosa e'
 * cambiato e la finestra va ridisegnata. */
static int term_leggi(Terminale *t)
{
    struct pollfd v[1 + TERM_MAX];
    char buf[256];
    int  n, i, cambiato = 0;

    if (!t->usato || t->fd_out < 0) return 0;

    /* ! SI GUARDA CON poll PRIMA DI LEGGERE. Una read su una pipe vuota
     * BLOCCA, e un toolkit che si blocca dentro il proprio ciclo di eventi
     * smette di rispondere a tutto il resto — mouse compreso. */
    for (;;) {
        v[0].fd = t->fd_out;
        v[0].events = POLLIN;
        v[0].revents = 0;
        if (poll(v, 1, 0) <= 0) break;
        if (!(v[0].revents & POLLIN)) break;

        n = (int)read(t->fd_out, buf, sizeof(buf));

        /* ! read che rende 0 E' LA FINE DEI DATI, cioe' il programma dentro il
         * terminale se n'e' andato. Fino al 17 agosto 2026 qui si usciva dal
         * ciclo e basta: la finestra restava aperta con dentro una shell
         * morta, e chi ci batteva dentro non capiva perche' non rispondesse
         * piu' niente. Adesso lo si segna, e il ciclo dei messaggi lo dice
         * all'applicazione UNA volta sola. */
        if (n == 0) { t->finito = 1; break; }
        if (n < 0) break;

        for (i = 0; i < n; i++) term_carattere(t, buf[i]);
        cambiato = 1;
    }
    return cambiato;
}

/* Un tasto dentro il terminale: eco nella griglia, e alla shell si manda solo
 * la riga finita. Rende 1 se il tasto e' stato consumato. */
/* ! IL TASTO SI MANDA E BASTA, E QUESTA FUNZIONE E' DIVENTATA TRE RIGHE.
 * Prima teneva una riga sua (t->riga), la correggeva col Backspace, ne faceva
 * l'eco a mano e la spediva tutta insieme all'Invio: era una disciplina di
 * linea scritta dentro un'applicazione grafica, e per questo la shell lanciata
 * a mano su una pipe non ne aveva nessuna. Adesso quel lavoro sta nel pty, che
 * lo fa per chiunque — il terminale in finestra come un futuro telnet.
 *
 * ! E L'ECO NON SI DISEGNA PIU' QUI: torna dal master insieme all'uscita del
 * programma, e la disegna il ciclo che legge. Farlo in tutt'e due i posti
 * darebbe ogni lettera due volte. */
static int term_tasto(Terminale *t, unsigned int c)
{
    unsigned char b;

    if (c > 0xFF) return 0;         /* frecce e compagnia: non ancora */

    b = (unsigned char)(c == '\r' ? '\n' : c);
    write(t->fd_in, &b, 1);
    return 1;
}

/* =============================================================================
 * IL MENU — geometria, disegno, mouse e tasti
 *
 * ! LE MISURE SI RICALCOLANO, NON SI RICORDANO. Larghezza di un titolo e
 * larghezza di una tendina discendono dal testo: tenerle in un campo vorrebbe
 * dire ricordarsi di aggiornarle ogni volta che si aggiunge una voce, e
 * dimenticarsene una volta da' un menu che si disegna sopra il successivo.
 * Sono moltiplicazioni per otto: costano meno del difetto.
 * ============================================================================= */
static Menu *menu_della_finestra(ExFinestra f)
{
    Oggetto *r = radice(f);
    int i;

    if (!r) return 0;
    for (i = 0; i < OGGETTI_MAX; i++)
        if (g_ogg[i].usato && g_ogg[i].classe == CL_MENU &&
            radice_h((ExFinestra)(i + 1)) == radice_h(f))
            return menu_di(&g_ogg[i]);
    return 0;
}

static void menu_geometria(Menu *M)
{
    unsigned int i;
    int x = 4;

    for (i = 0; i < M->n; i++) {
        M->titolo[i].x = x;
        M->titolo[i].w = (int)strlen(M->titolo[i].nome) * 8 + 16;
        x += M->titolo[i].w;
    }
}

/* La larghezza di una tendina: la voce piu' lunga, con la scorciatoia contata
 * a destra e tre spazi in mezzo perche' non si tocchino. */
static int menu_tendina_w(const MenuTitolo *T)
{
    unsigned int i;
    int max = 8;

    for (i = 0; i < T->n; i++) {
        const char *t   = T->voce[i].testo;
        const char *tab = strchr(t, '\t');
        int l = tab ? (int)(tab - t) + 3 + (int)strlen(tab + 1)
                    : (int)strlen(t);

        if (l > max) max = l;
    }
    return max * 8 + 20;
}

static int menu_tendina_h(const MenuTitolo *T)
{
    return (int)T->n * MENU_RIGA_H + 6;
}

/* La tendina aperta, in coordinate dell'area del client. Rende 0 se non ce
 * n'e' nessuna. */
static int menu_tendina_dove(Menu *M, int *tx, int *ty, int *tw, int *th)
{
    Oggetto *o = ogg(M->ogg);
    Oggetto *r;
    int ox, oy;

    if (!o || M->aperto < 0 || M->aperto >= (int)M->n) return 0;

    origine(o, &ox, &oy);
    *tw = menu_tendina_w(&M->titolo[M->aperto]);
    *th = menu_tendina_h(&M->titolo[M->aperto]);
    *tx = ox + o->x + M->titolo[M->aperto].x;
    *ty = oy + o->y + o->h;

    /* ! SI SPOSTA A SINISTRA INVECE DI USCIRE. Una tendina tagliata a meta'
     * nasconde proprio le voci piu' lunghe, che sono quelle con la
     * scorciatoia scritta a destra. */
    r = radice(M->ogg);
    if (r && *tx + *tw > r->w) *tx = r->w - *tw;
    if (*tx < 0) *tx = 0;
    return 1;
}

/* ! SI DISEGNA DOPO TUTTI GLI ALTRI, e non insieme alla barra: una tendina
 * aperta COPRE i controlli sotto di se', e i figli si disegnano in ordine di
 * creazione — il menu si crea per primo, quindi finirebbe sotto. */
static void menu_sopra(ExFinestra f)
{
    Menu *M = menu_della_finestra(f);
    MenuTitolo *T;
    int tx, ty, tw, th;
    unsigned int i;

    if (!M || !menu_tendina_dove(M, &tx, &ty, &tw, &th)) return;

    T = &M->titolo[M->aperto];

    ex_riempi(f, tx, ty, tw, th, EX_GRIGIO);
    ex_rilievo(f, tx, ty, tw, th);

    for (i = 0; i < T->n; i++) {
        int ry = ty + 3 + (int)i * MENU_RIGA_H;
        const char *t   = T->voce[i].testo;
        const char *tab = strchr(t, '\t');
        char sinistra[MENU_TESTO_MAX];
        unsigned int colore = EX_NERO;

        /* Un separatore e' un solco, non una voce: non si sceglie e non si
         * evidenzia. */
        if (T->voce[i].id == 0) {
            ex_riempi(f, tx + 4, ry + MENU_RIGA_H / 2 - 1, tw - 8, 1, EX_OMBRA);
            ex_riempi(f, tx + 4, ry + MENU_RIGA_H / 2,     tw - 8, 1, EX_LUCE);
            continue;
        }

        if ((int)i == M->sotto) {
            ex_riempi(f, tx + 2, ry - 1, tw - 4, MENU_RIGA_H, EX_BLU);
            colore = EX_BIANCO;
        }

        if (tab) {
            unsigned int l = (unsigned int)(tab - t);

            if (l >= sizeof(sinistra)) l = sizeof(sinistra) - 1;
            memcpy(sinistra, t, l);
            sinistra[l] = '\0';
            ex_scrivi(f, tx + 8, ry, sinistra, colore);
            ex_scrivi(f, tx + tw - 8 - (int)strlen(tab + 1) * 8, ry,
                      tab + 1, colore);
        } else {
            ex_scrivi(f, tx + 8, ry, t, colore);
        }
    }
}

/* Rende 1 se il clic era roba del menu — e allora NON deve arrivare ai
 * controlli sotto. `cmd` esce diverso da zero solo se si e' scelta una voce. */
static int menu_clic(ExFinestra f, int x, int y, unsigned int *cmd)
{
    Menu *M = menu_della_finestra(f);
    Oggetto *o;
    int ox, oy, bx, by;
    unsigned int i;

    *cmd = 0;
    if (!M) return 0;

    o = ogg(M->ogg);
    if (!o) return 0;
    origine(o, &ox, &oy);
    bx = ox + o->x;
    by = oy + o->y;

    /* 1. dentro la barra: si apre, o si richiude quella gia' aperta */
    if (y >= by && y < by + o->h && x >= bx && x < bx + o->w) {
        for (i = 0; i < M->n; i++)
            if (x >= bx + M->titolo[i].x &&
                x <  bx + M->titolo[i].x + M->titolo[i].w) {
                M->aperto = (M->aperto == (int)i) ? -1 : (int)i;
                M->sotto  = -1;
                return 1;
            }
        M->aperto = -1;             /* uno spazio vuoto della barra chiude */
        return 1;
    }

    /* 2. dentro la tendina aperta: si sceglie */
    {
        int tx, ty, tw, th;

        if (menu_tendina_dove(M, &tx, &ty, &tw, &th) &&
            x >= tx && x < tx + tw && y >= ty && y < ty + th) {
            int r = (y - ty - 3) / MENU_RIGA_H;
            MenuTitolo *T = &M->titolo[M->aperto];

            if (r >= 0 && r < (int)T->n && T->voce[r].id != 0)
                *cmd = T->voce[r].id;
            M->aperto = -1;
            M->sotto  = -1;
            return 1;
        }
    }

    /* 3. altrove, con una tendina aperta: si chiude e IL CLIC SI MANGIA.
     * ! CHIUDERE E LASCIAR PASSARE SAREBBE PEGGIO DI TUTT'E DUE: chi clicca
     * fuori da un menu vuole chiuderlo, non premere quello che c'e' sotto —
     * e sotto ci puo' essere un pulsante che cancella qualcosa. */
    if (M->aperto >= 0) {
        M->aperto = -1;
        M->sotto  = -1;
        return 1;
    }
    return 0;
}

/* La prima voce scegliibile a partire da `da`, andando in direzione `passo`.
 * Serve a saltare i separatori, che non si possono evidenziare. */
static int menu_voce_vicina(const MenuTitolo *T, int da, int passo)
{
    int i, n = (int)T->n;

    for (i = 0; i < n; i++) {
        if (da < 0) da = n - 1;
        if (da >= n) da = 0;
        if (T->voce[da].id != 0) return da;
        da += passo;
    }
    return -1;
}

static int menu_tasto(ExFinestra f, unsigned int k, unsigned int *cmd)
{
    Menu *M = menu_della_finestra(f);
    unsigned int c = k & KBD_KEY_MASK;

    *cmd = 0;
    if (!M || M->n == 0) return 0;

    /* ! F10 APRE, ED E' L'UNICO TASTO CHE FUNZIONA A MENU CHIUSO. Tutto il
     * resto — frecce, Invio, Esc — appartiene ai controlli finche' un menu non
     * e' aperto: un menu che si prendesse le frecce sempre renderebbe muta una
     * lista, che e' esattamente il difetto del pulsante che si prendeva il
     * fuoco. */
    if (M->aperto < 0) {
        if (c != KBD_K_F(10)) return 0;
        M->aperto = 0;
        M->sotto  = menu_voce_vicina(&M->titolo[0], 0, 1);
        return 1;
    }

    switch (c) {
    case 27:                            /* Esc: lo stesso di ExDlg */
        M->aperto = -1; M->sotto = -1;
        return 1;

    case KBD_K_LEFT:
        M->aperto = (M->aperto == 0) ? (int)M->n - 1 : M->aperto - 1;
        M->sotto  = menu_voce_vicina(&M->titolo[M->aperto], 0, 1);
        return 1;

    case KBD_K_RIGHT:
        M->aperto = (M->aperto + 1 >= (int)M->n) ? 0 : M->aperto + 1;
        M->sotto  = menu_voce_vicina(&M->titolo[M->aperto], 0, 1);
        return 1;

    case KBD_K_UP:
        M->sotto = menu_voce_vicina(&M->titolo[M->aperto], M->sotto - 1, -1);
        return 1;

    case KBD_K_DOWN:
        M->sotto = menu_voce_vicina(&M->titolo[M->aperto], M->sotto + 1, 1);
        return 1;

    case '\n':
    case '\r':
        if (M->sotto >= 0 && M->sotto < (int)M->titolo[M->aperto].n)
            *cmd = M->titolo[M->aperto].voce[M->sotto].id;
        M->aperto = -1; M->sotto = -1;
        return 1;

    default:
        break;
    }

    /* ! QUALUNQUE ALTRO TASTO CHIUDE, invece di essere ignorato. Un menu
     * aperto che mangia le lettere in silenzio da' un editor in cui si batte e
     * non compare niente, e non c'e' modo di capire perche'. */
    M->aperto = -1;
    M->sotto  = -1;
    return 1;
}

static void disegna_oggetto(Oggetto *o)
{
    int ox, oy, x, y;

    if (!o->usato || !(o->stile & EX_VISIBILE)) return;

    origine(o, &ox, &oy);
    x = ox + o->x;
    y = oy + o->y;

    switch (o->classe) {
    case CL_PULSANTE:
        ex_riempi(o->padre, x, y, o->w, o->h,
                  o->premuto ? EX_GRIGIO_SC : EX_GRIGIO);
        ex_riquadro_disegna(o->padre, x, y, o->w, o->h, EX_NERO);
        ex_scrivi(o->padre, x + (o->w - (int)strlen(o->titolo) * 8) / 2,
                  y + (o->h - 16) / 2, o->titolo, EX_NERO);
        break;

    case CL_ETICHETTA:
        ex_scrivi(o->padre, x, y, o->titolo, EX_NERO);
        break;

    case CL_TESTO: {
        Oggetto *r = radice(o->padre);
        int col = (int)strlen(o->titolo);

        ex_riempi(o->padre, x, y, o->w, o->h, EX_BIANCO);
        /* ! IL BORDO DICE CHI HA I TASTI. Senza, chi guarda non sa dove
         * andra' a finire quello che batte — e Tab sembra non fare niente. */
        ex_riquadro_disegna(o->padre, x, y, o->w, o->h,
                            (r && r->fuoco == (ExFinestra)(o - g_ogg + 1))
                            ? EX_BLU : EX_GRIGIO_SC);
        ex_scrivi(o->padre, x + 3, y + (o->h - 16) / 2, o->titolo, EX_NERO);

        if (r && r->fuoco == (ExFinestra)(o - g_ogg + 1))
            ex_riempi(o->padre, x + 3 + col * 8, y + 3, 1, o->h - 6, EX_NERO);
        break;
    }

    /* Un riquadro e' un solco, non un rettangolo: una riga che rientra e una
     * che sporge subito dopo, come una cucitura nel pannello. */
    case CL_RIQUADRO:
        ex_incavo(o->padre, x, y + 8, o->w, o->h - 8);
        ex_rilievo(o->padre, x + 1, y + 9, o->w - 2, o->h - 10);
        if (o->titolo[0]) {
            ex_riempi(o->padre, x + 6, y + 8,
                      (int)strlen(o->titolo) * 8 + 6, 2, EX_GRIGIO);
            ex_scrivi(o->padre, x + 9, y, o->titolo, EX_NERO);
        }
        break;

    case CL_SEPARATORE:
        ex_riempi(o->padre, x, y, o->w, 1, EX_OMBRA);
        ex_riempi(o->padre, x, y + 1, o->w, 1, EX_LUCE);
        break;

    case CL_AREA: {
        Area *A = area_di(o);
        Oggetto *r = radice(o->padre);
        unsigned int k;
        int col_fuoco;

        ex_riempi(o->padre, x, y, o->w, o->h, EX_BIANCO);
        col_fuoco = (r && r->fuoco == (ExFinestra)(o - g_ogg + 1));
        ex_incavo(o->padre, x, y, o->w, o->h);
        if (col_fuoco) ex_riquadro_disegna(o->padre, x + 1, y + 1,
                                           o->w - 2, o->h - 2, EX_BLU);
        if (!A) break;

        {
            unsigned int sy1 = 0, sx1 = 0, sy2 = 0, sx2 = 0;
            int c_e_sel = area_sel_ordina(A, &sy1, &sx1, &sy2, &sx2);

            for (k = 0; k < A->righe && A->top + k < A->n; k++) {
                unsigned int riga = A->top + k;
                const char  *src = area_riga(A, riga);
                unsigned int l   = (unsigned int)strlen(src);
                char         vis[AREA_COL_MAX];
                unsigned int c;
                int          ry = y + 2 + (int)k * AREA_RIGA_H;
                int          da = -1, a = -1;   /* la parte scelta, in colonne */

                for (c = 0; c < A->cols && A->left + c < l && c + 1 < sizeof(vis); c++) {
                    char ch = src[A->left + c];
                    /* ! IL TAB SI MOSTRA COME UNO SPAZIO E RESTA UN TAB NEL
                     * TESTO. Espanderlo vorrebbe dire che una colonna sullo
                     * schermo non e' piu' un carattere nel testo, e allora
                     * cursore, clic e lunghezza della riga direbbero tre cose
                     * diverse. */
                    vis[c] = (ch == '\t') ? ' ' : ch;
                }
                vis[c] = '\0';

                /* ! LA SELEZIONE E' UN FONDO, e le righe di mezzo si tingono
                 * FINO AL BORDO — anche dove non c'e' testo. E' cosi' che si
                 * vede che il fine-riga fa parte di cio' che si e' preso: una
                 * selezione che si fermasse all'ultima lettera farebbe credere
                 * che il ritorno a capo resti fuori, e incollando salterebbe
                 * fuori lo stesso. */
                if (c_e_sel && riga >= sy1 && riga <= sy2) {
                    unsigned int d = (riga == sy1) ? sx1 : 0;
                    unsigned int e_ = (riga == sy2) ? sx2 : A->left + A->cols;

                    if (d < A->left) d = A->left;
                    if (e_ > A->left + A->cols) e_ = A->left + A->cols;
                    if (e_ > d) {
                        da = (int)(d - A->left);
                        a  = (int)(e_ - A->left);
                        ex_riempi(o->padre, x + 2 + da * AREA_CAR_W, ry - 1,
                                  (a - da) * AREA_CAR_W, AREA_RIGA_H, EX_BLU);
                    }
                }

                if (!vis[0]) continue;

                if (da < 0) {
                    ex_scrivi(o->padre, x + 2, ry, vis, EX_NERO);
                } else {
                    /* Tre pezzi: prima, dentro, dopo. Scriverli tutti in nero
                     * e poi ripassare quello dentro in bianco lascerebbe il
                     * nero SOTTO, perche' ex_scrivi disegna solo i pixel del
                     * carattere e non il fondo. */
                    char pezzo[AREA_COL_MAX];
                    int  lung = (int)strlen(vis);
                    int  fine = (a > lung) ? lung : a;

                    if (da > 0) {
                        int q = (da > lung) ? lung : da;
                        memcpy(pezzo, vis, (unsigned int)q); pezzo[q] = '\0';
                        ex_scrivi(o->padre, x + 2, ry, pezzo, EX_NERO);
                    }
                    if (fine > da) {
                        memcpy(pezzo, vis + da, (unsigned int)(fine - da));
                        pezzo[fine - da] = '\0';
                        ex_scrivi(o->padre, x + 2 + da * AREA_CAR_W, ry,
                                  pezzo, EX_BIANCO);
                    }
                    if (lung > a) {
                        strcpy(pezzo, vis + a);
                        ex_scrivi(o->padre, x + 2 + a * AREA_CAR_W, ry,
                                  pezzo, EX_NERO);
                    }
                }
            }
        }

        /* Il cursore: un blocco pieno col carattere ridisegnato sopra in
         * bianco. Una barretta di un pixel su un font 8x16 si perde. */
        if (col_fuoco &&
            A->cy >= A->top && A->cy < A->top + A->righe &&
            A->cx >= A->left && A->cx < A->left + A->cols) {
            int cx = x + 2 + (int)(A->cx - A->left) * AREA_CAR_W;
            int cy = y + 2 + (int)(A->cy - A->top) * AREA_RIGA_H;
            char sotto[2];

            ex_riempi(o->padre, cx, cy, AREA_CAR_W, AREA_RIGA_H, EX_BLU);
            sotto[0] = area_riga(A, A->cy)[A->cx];
            sotto[1] = '\0';
            if (sotto[0] == '\t') sotto[0] = ' ';
            if (sotto[0]) ex_scrivi(o->padre, cx, cy, sotto, EX_BIANCO);
        }
        break;
    }

    case CL_LISTA: {
        Lista *L = lista_di(o);
        Oggetto *r = radice(o->padre);
        unsigned int k;
        int col_fuoco;

        ex_riempi(o->padre, x, y, o->w, o->h, EX_BIANCO);

        /* ! IL BORDO DICE CHI HA I TASTI, come per la casella di testo: senza,
         * chi guarda non sa se le frecce muoveranno questa lista o un'altra. */
        col_fuoco = (r && r->fuoco == (ExFinestra)(o - g_ogg + 1));
        ex_incavo(o->padre, x, y, o->w, o->h);
        if (col_fuoco) ex_riquadro_disegna(o->padre, x + 1, y + 1,
                                           o->w - 2, o->h - 2, EX_BLU);
        if (!L) break;

        for (k = 0; k < L->righe && L->primo + k < L->n; k++) {
            unsigned int v = L->primo + k;
            int ry = y + 2 + (int)k * LISTA_RIGA_H;

            /* ! LA SCELTA E' UN FONDO, NON UN COLORE DEL TESTO. Su voci di
             * lunghezza diversa un testo colorato non dice dove finisce la
             * riga scelta; un fondo si'. */
            if (v == L->sel)
                ex_riempi(o->padre, x + 2, ry - 1, o->w - 4, LISTA_RIGA_H,
                          col_fuoco ? EX_BLU : EX_GRIGIO_SC);

            ex_scrivi(o->padre, x + 4, ry, &L->voci[v * LISTA_TESTO_MAX],
                      (v == L->sel) ? EX_BIANCO : EX_NERO);
        }
        break;
    }

    case CL_TERMINALE: {
        Terminale *t = term_di(o);
        unsigned int r, k;
        char linea[128];

        ex_riempi(o->padre, x, y, o->w, o->h, EX_NERO);
        ex_incavo(o->padre, x, y, o->w, o->h);
        if (!t) break;

        for (r = 0; r < t->righe; r++) {
            for (k = 0; k < t->cols && k + 1 < sizeof(linea); k++)
                linea[k] = t->griglia[r * t->cols + k];
            linea[k] = '\0';
            ex_scrivi(o->padre, x, y + (int)r * 16, linea, EX_BIANCO);
        }

        /* Il cursore: un blocchetto, non un trattino. Su un fondo nero un
         * trattino sottile a volte non si vede, e un terminale in cui non si
         * sa dove si sta scrivendo non e' un terminale. */
        ex_riempi(o->padre, x + (int)t->cx * 8, y + (int)t->cy * 16 + 14,
                  8, 2, EX_BIANCO);
        break;
    }

    case CL_INTESTAZIONE:
        ex_riempi(o->padre, x, y, o->w, o->h, EX_BLU);
        ex_rilievo(o->padre, x, y, o->w, o->h);
        ex_scrivi(o->padre, x + 6, y + (o->h - 16) / 2, o->titolo, EX_BIANCO);
        break;

    /* La barra dei menu. La TENDINA no: quella la disegna menu_sopra(), dopo
     * tutti gli altri controlli — vedi il commento li'. */
    case CL_MENU: {
        Menu *M = menu_di(o);
        unsigned int i;

        ex_riempi(o->padre, x, y, o->w, o->h, EX_GRIGIO);
        ex_rilievo(o->padre, x, y, o->w, o->h);
        if (!M) break;

        menu_geometria(M);
        for (i = 0; i < M->n; i++) {
            int tx = x + M->titolo[i].x;
            unsigned int col = EX_NERO;

            if (M->aperto == (int)i) {
                ex_riempi(o->padre, tx, y + 2, M->titolo[i].w, o->h - 4, EX_BLU);
                col = EX_BIANCO;
            }
            ex_scrivi(o->padre, tx + 8, y + (o->h - 16) / 2,
                      M->titolo[i].nome, col);
        }
        break;
    }

    default:
        break;
    }
}

static void disegna_figli(ExFinestra padre)
{
    int i;

    /* ! IN ORDINE DI CREAZIONE, e non e' indifferente: chi si sovrappone a
     * qualcosa creato prima ci finisce sopra, che e' l'unico ordine che chi
     * scrive l'applicazione puo' prevedere leggendo il proprio codice. */
    for (i = 0; i < OGGETTI_MAX; i++)
        if (g_ogg[i].usato && g_ogg[i].padre == padre) {
            disegna_oggetto(&g_ogg[i]);
            disegna_figli((ExFinestra)(i + 1));
        }

    /* ! LA TENDINA PER ULTIMA, E SOLO AL PRIMO LIVELLO. Deve coprire i
     * controlli, e i controlli si disegnano in ordine di creazione: il menu si
     * crea per primo — e' in cima alla finestra — quindi disegnandola insieme
     * alla barra finirebbe SOTTO tutto il resto. Il controllo sulla classe
     * serve perche' questa funzione e' ricorsiva: senza, ogni riquadro
     * ridisegnerebbe la tendina un'altra volta. */
    {
        Oggetto *p = ogg(padre);

        if (p && p->classe == CL_FINESTRA) menu_sopra(padre);
    }
}

/* -----------------------------------------------------------------------------
 * Creare
 * --------------------------------------------------------------------------- */
static int server_trova(void)
{
    unsigned int attesa;

    if (g_server >= 0) return 1;

    /* Si aspetta il server invece di pretenderlo pronto, come fanno i driver
     * USB col servizio PCI: un'applicazione avviata insieme al server non
     * deve fallire per una corsa persa di qualche millisecondo. */
    for (attesa = 0; attesa < 30; attesa++) {
        char nome[32];

        /* ! LO STESSO NOME CHE USA IL SERVER PER REGISTRARSI, calcolato dalla
         * stessa funzione: se le due parti lo componessero ognuna per conto
         * suo, un giorno divergerebbero e il client cercherebbe un servizio
         * che nessuno ha registrato. */
        win_nome_servizio(nome, sizeof(nome));
        g_server = ipc_lookup(nome);
        if (g_server >= 0) return 1;
        usleep(100000);
    }
    return 0;
}

ExFinestra ex_crea(const char *classe, const char *titolo, unsigned int stile,
                   int x, int y, int w, int h,
                   ExFinestra padre, unsigned int id, ExProcedura proc)
{
    unsigned int cl = classe_da_nome(classe);
    int i;
    Oggetto *o;

    if (cl == 0) return 0;

    for (i = 0; i < OGGETTI_MAX; i++) if (!g_ogg[i].usato) break;
    if (i == OGGETTI_MAX) return 0;

    o = &g_ogg[i];
    memset(o, 0, sizeof(*o));
    o->usato  = 1;
    o->classe = cl;
    o->id     = id;
    o->padre  = padre;
    o->x = x; o->y = y; o->w = w; o->h = h;
    o->stile  = stile | EX_VISIBILE;
    o->proc   = proc;
    if (titolo) {
        strncpy(o->titolo, titolo, TESTO_LEN - 1);
        o->titolo[TESTO_LEN - 1] = '\0';
    }

    /* ! IL PRIMO CONTROLLO CHE LO ACCETTA PRENDE IL FUOCO, e senza non lo
     * prende nessuno: una finestra appena aperta ignorerebbe la tastiera
     * finche' non ci si clicca dentro. Chi apre una finestra con una casella
     * si aspetta di poterci scrivere subito. */
    if (cl != CL_FINESTRA && padre) {
        Oggetto *r = radice(padre);
        if (r && r->fuoco == 0 && accetta_fuoco(o))
            r->fuoco = (ExFinestra)(i + 1);
    }

    if (cl == CL_AREA) {
        Area *A = 0;
        int j;

        for (j = 0; j < AREA_MAX; j++) if (!g_area[j].usato) { A = &g_area[j]; break; }
        if (!A) { o->usato = 0; return 0; }

        memset(A, 0, sizeof(*A));
        A->ogg   = (ExFinestra)(i + 1);
        A->righe = (unsigned int)(h / AREA_RIGA_H);
        A->cols  = (unsigned int)((w - 4) / AREA_CAR_W);
        if (A->righe == 0 || A->cols == 0) { o->usato = 0; return 0; }

        A->testo = (char *)malloc(AREA_RIGHE_MAX * AREA_COL_MAX);
        if (!A->testo) { o->usato = 0; return 0; }

        memset(A->testo, 0, AREA_RIGHE_MAX * AREA_COL_MAX);
        A->n = 1;                       /* un'area vuota ha una riga vuota */
        A->usato = 1;
        return (ExFinestra)(i + 1);
    }

    if (cl == CL_MENU) {
        Menu *M = 0;
        int j;

        for (j = 0; j < MENU_MAX; j++) if (!g_menu[j].usato) { M = &g_menu[j]; break; }
        if (!M) { o->usato = 0; return 0; }

        memset(M, 0, sizeof(*M));
        M->ogg    = (ExFinestra)(i + 1);
        M->aperto = -1;
        M->sotto  = -1;
        M->usato  = 1;
        return (ExFinestra)(i + 1);
    }

    if (cl == CL_LISTA) {
        Lista *L = 0;
        int j;

        for (j = 0; j < LISTA_MAX; j++) if (!g_lista[j].usato) { L = &g_lista[j]; break; }
        if (!L) { o->usato = 0; return 0; }

        memset(L, 0, sizeof(*L));
        L->ogg   = (ExFinestra)(i + 1);
        L->righe = (unsigned int)(h / LISTA_RIGA_H);

        /* Una lista alta meno di una riga non e' una lista: meglio dire di no
         * qui che disegnare zero righe e lasciare chi guarda a chiedersi
         * perche' l'elenco e' vuoto. */
        if (L->righe == 0) { o->usato = 0; return 0; }

        L->voci = (char *)malloc(LISTA_VOCI_MAX * LISTA_TESTO_MAX);
        if (!L->voci) { o->usato = 0; return 0; }

        L->usato = 1;
        return (ExFinestra)(i + 1);
    }

    if (cl == CL_TERMINALE) {
        Terminale *t = 0;
        int j, pin[2], pout[2];
        SpawnRedir red[3];
        char *sh_argv[2];

        for (j = 0; j < TERM_MAX; j++) if (!g_term[j].usato) { t = &g_term[j]; break; }
        if (!t) { o->usato = 0; return 0; }

        memset(t, 0, sizeof(*t));
        t->ogg   = (ExFinestra)(i + 1);
        t->cols  = (unsigned int)(w / 8);
        t->righe = (unsigned int)(h / 16);
        if (t->cols == 0 || t->righe == 0) { o->usato = 0; return 0; }

        t->griglia = (char *)malloc(t->cols * t->righe);
        if (!t->griglia) { o->usato = 0; return 0; }
        memset(t->griglia, ' ', t->cols * t->righe);

        /* ! DUE PIPE SONO DIVENTATE UN PTY, ED E' TUTTA LA DIFFERENZA FRA UN
         * TUBO E UNA SESSIONE. Fino al 18 agosto 2026 qui c'erano `pipe(pin)`
         * e `pipe(pout)`, e con loro tutto quello che una pipe non sa fare:
         * l'eco lo doveva rifare il terminale a mano, il Backspace era un byte
         * come un altro che la shell si ritrovava dentro la riga, e Ctrl+C non
         * poteva esistere — un segnale attraverso una pipe non si manda.
         * Adesso in mezzo c'e' la disciplina di linea, e quelle tre cose le fa
         * il kernel. */
        if (pty_apri(pin) != 0) {
            free(t->griglia); o->usato = 0; return 0;
        }
        pout[0] = pin[0];               /* si legge e si scrive lo stesso capo */

        /* Il figlio: le tre standard sono lo SLAVE, che per lui e' un
         * terminale come un altro. */
        red[0].fd = 0; red[0].flags = 0; red[0].percorso = 0; red[0].fd_padre = pin[1];
        red[1].fd = 1; red[1].flags = 0; red[1].percorso = 0; red[1].fd_padre = pin[1];
        red[2].fd = 2; red[2].flags = 0; red[2].percorso = 0; red[2].fd_padre = pin[1];

        sh_argv[0] = (char *)(titolo && titolo[0] ? titolo : "/bin/sh");
        sh_argv[1] = 0;

        /* La misura la sa il terminale, e va detta PRIMA che il programma
         * parta: chi disegna a schermo pieno la chiede all'avvio. */
        pty_ctl(pin[0], PTY_CTL_MISURA,
                ((unsigned int)t->righe << 16) | (unsigned int)t->cols);

        t->pid = spawn_ex(sh_argv[0], sh_argv, 0, red, 3);

        /* ! SI CHIUDE LO SLAVE, per la stessa ragione delle pipe: finche' lo
         * teniamo aperto noi, il pty conta un capo vivo dalla parte della
         * shell e la fine dei dati non arriva mai. */
        close(pin[1]);

        if (t->pid < 0) { close(pin[0]); free(t->griglia);
                          o->usato = 0; return 0; }

        /* ! NESSUNO E' IN PRIMO PIANO ALL'INIZIO, ED E' VOLUTO. Dichiarare qui
         * la shell sembra naturale e si e' rivelato sbagliato alla prima
         * prova: un Ctrl+C battuto al prompt la ammazzava, e la finestra
         * restava con l'eco che funzionava e nessuno dall'altra parte. Il
         * primo piano lo dichiara la shell quando lancia qualcosa — vedi
         * sh_setfg() in bin/sh/shell.c. */

        t->fd_in  = pin[0];
        t->fd_out = pout[0];
        t->usato  = 1;

        /* Il titolo era il percorso della shell: nella griglia non ci va. */
        o->titolo[0] = '\0';
        return (ExFinestra)(i + 1);
    }

    if (cl != CL_FINESTRA) return (ExFinestra)(i + 1);

    /* Primo livello: si chiede al server. */
    {
        WinCrea       c;
        WinCreata     r;
        IpcMessage    meta;
        unsigned char buf[IPC_MSG_MAX_DATA];
        ShmZona       z;
        int giri;

        if (!server_trova()) { o->usato = 0; return 0; }

        memset(&c, 0, sizeof(c));
        c.x = (unsigned int)x; c.y = (unsigned int)y;
        c.larghezza = (unsigned int)w; c.altezza = (unsigned int)h;
        c.stile = WIN_ST_VISIBILE
                | ((stile & EX_TITOLO) ? WIN_ST_TITOLO : 0)
                | ((stile & EX_BORDO)  ? WIN_ST_BORDO  : 0)
                | ((stile & EX_CHIUDI) ? WIN_ST_CHIUDI : 0)
                | ((stile & EX_SFONDO) ? WIN_ST_SFONDO : 0)
                | ((stile & EX_SOPRA)  ? WIN_ST_SOPRA  : 0)
                | ((stile & EX_MODALE) ? WIN_ST_MODALE : 0)
                | ((stile & EX_RIDIM)  ? WIN_ST_RIDIM  : 0);
        strncpy(c.titolo, o->titolo, WIN_TITOLO_LEN - 1);

        if (ipc_send((unsigned int)g_server, WIN_MSG_CREA, &c, sizeof(c)) < 0) {
            o->usato = 0; return 0;
        }

        /* ! SI MANDA UNA VOLTA SOLA E SI ASPETTA A LUNGO, non il contrario.
         * Ripetere la richiesta sarebbe la cosa istintiva e sbagliata: se la
         * prima risposta e' solo IN RITARDO, il secondo invio fa creare al
         * server una SECONDA finestra di cui nessuno sa niente — che resta
         * sullo schermo e non si chiude piu'.
         *
         * ! E SI CONTINUA AD ASPETTARE ANCHE SE SCADE, invece di arrendersi.
         * Qui c'era `break`, e bastava che il server fosse occupato a
         * comporre perche' un'applicazione avviata in SFONDO si arrendesse
         * dicendo «il server non risponde» — mentre la stessa in primo piano
         * funzionava, perche' li' la shell e' ferma e lascia la CPU al
         * server. E' il genere di difetto che la prova comoda non vede. */
        r.id = 0;
        for (giri = 0; giri < 30 && r.id == 0; giri++) {
            if (ipc_recv_timeout(&meta, buf, sizeof(buf), 500) < 0) continue;
            if (meta.tipo == WIN_MSG_CREATA && meta.len >= sizeof(r))
                memcpy(&r, buf, sizeof(r));
        }
        if (r.id == 0) { o->usato = 0; return 0; }

        memset(&z, 0, sizeof(z));
        win_nome_zona(z.nome, r.id, r.giro);
        z.byte = r.byte;
        z.flag = 0;                 /* la crea il server, noi ci attacchiamo */
        if (shm_apri(&z) != 0) { o->usato = 0; return 0; }

        o->win_id   = r.id;
        o->w        = (int)r.larghezza;
        o->h        = (int)r.altezza;
        o->pix      = (unsigned int *)z.virt;
        o->passo_px = r.larghezza;
    }

    return (ExFinestra)(i + 1);
}

void ex_distruggi(ExFinestra f)
{
    Oggetto *o = ogg(f);
    int i;

    if (!o) return;

    for (i = 0; i < OGGETTI_MAX; i++)
        if (g_ogg[i].usato && g_ogg[i].padre == f)
            ex_distruggi((ExFinestra)(i + 1));

    if (o->classe == CL_FINESTRA && g_server >= 0) {
        WinRegione w;
        memset(&w, 0, sizeof(w));
        w.id = o->win_id;
        (void)ipc_send((unsigned int)g_server, WIN_MSG_DISTRUGGI, &w, sizeof(w));
        if (o->pix) shm_chiudi((void *)o->pix);
    }
    o->usato = 0;
}

void ex_titolo(ExFinestra f, const char *s)
{
    Oggetto *o = ogg(f);

    if (!o || !s) return;
    strncpy(o->titolo, s, TESTO_LEN - 1);
    o->titolo[TESTO_LEN - 1] = '\0';

    if (o->classe == CL_FINESTRA && g_server >= 0) {
        WinTitolo t;
        memset(&t, 0, sizeof(t));
        t.id = o->win_id;
        strncpy(t.titolo, o->titolo, WIN_TITOLO_LEN - 1);
        (void)ipc_send((unsigned int)g_server, WIN_MSG_TITOLO, &t, sizeof(t));
    }
}

void ex_sposta(ExFinestra f, int x, int y)
{
    Oggetto *o = ogg(f);

    if (!o) return;
    o->x = x; o->y = y;

    if (o->classe == CL_FINESTRA && g_server >= 0) {
        WinRegione w;
        w.id = o->win_id;
        w.x = (unsigned int)x; w.y = (unsigned int)y;
        w.larghezza = (unsigned int)o->w; w.altezza = (unsigned int)o->h;
        (void)ipc_send((unsigned int)g_server, WIN_MSG_SPOSTA, &w, sizeof(w));
    }
}

/* =============================================================================
 * ex_misura — cambiare misura
 *
 * ! DUE COSE DIVERSE SOTTO UN NOME SOLO, e va detto invece che nascosto. Su un
 * CONTROLLO e' un fatto compiuto: i pixel sono quelli del padre, e cambiargli
 * misura e' cambiare due numeri e la geometria di dentro. Su una FINESTRA DI
 * PRIMO LIVELLO e' una RICHIESTA: la zona di pixel la crea il server, e la
 * misura vera torna indietro con EXM_MISURA — che puo' anche essere piu'
 * piccola di quella chiesta, se non ci stava nello schermo.
 *
 * ! E QUI NON SI TOCCA o->w, apposta. Segnarsi la misura chiesta come se fosse
 * quella concessa vorrebbe dire un toolkit che disegna in un'area piu' grande
 * della zona di pixel che ha davvero — cioe' scritture oltre la fine di una
 * memoria condivisa. La misura la scrive un posto solo: rimappa(), quando la
 * zona nuova e' aperta per davvero.
 * ============================================================================= */
void ex_misura(ExFinestra f, int w, int h)
{
    Oggetto *o = ogg(f);

    if (!o || w <= 0 || h <= 0) return;

    if (o->classe == CL_FINESTRA) {
        WinRegione r;

        if (g_server < 0) return;

        /* ! SI MANDA LA MISURA E BASTA, e non si riusa WIN_MSG_SPOSTA: la
         * posizione vera la sa solo il server — se l'utente ha trascinato la
         * finestra per la barra, qui dentro c'e' ancora quella di partenza — e
         * mandarla vorrebbe dire una finestra che, ridimensionandosi, torna da
         * sola dove e' nata. */
        r.id = o->win_id;
        r.x = 0; r.y = 0;
        r.larghezza = (unsigned int)w; r.altezza = (unsigned int)h;
        (void)ipc_send((unsigned int)g_server, WIN_MSG_MISURA, &r, sizeof(r));
        return;
    }

    o->w = w;
    o->h = h;

    /* La geometria di dentro, per i controlli che ne hanno una. */
    if (o->classe == CL_TERMINALE) {
        Terminale *t = term_di(o);
        if (t) term_misura(t, (unsigned int)(w / 8), (unsigned int)(h / 16));
    } else if (o->classe == CL_LISTA) {
        Lista *L = lista_di(o);
        if (L) {
            L->righe = (unsigned int)(h / LISTA_RIGA_H);
            if (L->righe == 0) L->righe = 1;
            lista_segui(L);
        }
    } else if (o->classe == CL_AREA) {
        Area *A = area_di(o);
        if (A) {
            A->righe = (unsigned int)(h / AREA_RIGA_H);
            A->cols  = (unsigned int)((w - 4) / AREA_CAR_W);
            if (A->righe == 0) A->righe = 1;
            if (A->cols  == 0) A->cols  = 1;
            area_segui(A);
        }
    }
}

void ex_mostra(ExFinestra f, int visibile)
{
    Oggetto *o = ogg(f);
    if (!o) return;
    if (visibile) o->stile |= EX_VISIBILE;
    else          o->stile &= ~(unsigned int)EX_VISIBILE;
}

void ex_testo_metti(ExFinestra f, const char *s) { ex_titolo(f, s); }

const char *ex_testo_prendi(ExFinestra f)
{
    Oggetto *o = ogg(f);
    return o ? o->titolo : "";
}

/* -----------------------------------------------------------------------------
 * Il ciclo dei messaggi
 * --------------------------------------------------------------------------- */
static ExFinestra da_win_id(unsigned int win_id)
{
    int i;
    for (i = 0; i < OGGETTI_MAX; i++)
        if (g_ogg[i].usato && g_ogg[i].classe == CL_FINESTRA &&
            g_ogg[i].win_id == win_id) return (ExFinestra)(i + 1);
    return 0;
}

/* Il controllo sotto un punto dell'area del client. */
/* =============================================================================
 * rimappa — prendere la zona di pixel nuova dopo un ridimensionamento
 *
 * ! SI APRE LA NUOVA PRIMA DI CHIUDERE LA VECCHIA, e non e' un dettaglio di
 * stile. Chiudendo per prima, un'apertura fallita lascerebbe la finestra senza
 * pixel — che non vuol dire lenta o brutta, vuol dire un puntatore nullo dentro
 * ogni disegno. Cosi' invece, quando fallisce, resta tutto com'era: la finestra
 * e' congelata ma viva, e il server ripete la notizia cinque volte al secondo.
 *
 * ! LE DUE ZONE CONVIVONO PER UN ISTANTE, ed e' proprio per questo che il nome
 * porta dentro il giro: due zone con lo stesso nome sarebbero la stessa zona.
 *
 * Rende 1 se da qui in poi si puo' disegnare nella misura nuova.
 * ============================================================================= */
static int rimappa(ExFinestra f, const WinCreata *r)
{
    Oggetto      *o = ogg(f);
    ShmZona       z;
    unsigned int *vecchio;

    if (!o || o->classe != CL_FINESTRA) return 0;

    memset(&z, 0, sizeof(z));
    win_nome_zona(z.nome, r->id, r->giro);
    z.byte = r->byte;
    z.flag = 0;                     /* la crea il server, noi ci attacchiamo */

    if (shm_apri(&z) != 0) {
        /* ! -EEXIST VUOL DIRE «CE L'HO GIA'», cioe' che questa e' una
         * ripetizione e la ricevuta e' andata persa. Si risponde ridisegnando,
         * che e' anche il modo con cui la ricevuta si manda. */
        return (o->pix && (int)r->larghezza == o->w &&
                          (int)r->altezza   == o->h);
    }

    vecchio     = o->pix;
    o->pix      = (unsigned int *)z.virt;
    o->w        = (int)r->larghezza;
    o->h        = (int)r->altezza;
    o->passo_px = r->passo / 4;

    if (vecchio) shm_chiudi((void *)vecchio);

    /* ! LA BARRA DEI MENU LA ALLARGA IL TOOLKIT, NON L'APPLICAZIONE. E' l'unico
     * controllo la cui misura non e' una scelta di chi scrive il programma: sta
     * in cima e va da un bordo all'altro, sempre. Lasciarlo fare a ognuno
     * vorrebbe dire che chi se ne dimentica ha una barra corta con lo sfondo
     * grigio accanto, e sembra un difetto del toolkit — perche' lo sarebbe. */
    {
        int j;

        for (j = 0; j < OGGETTI_MAX; j++)
            if (g_ogg[j].usato && g_ogg[j].classe == CL_MENU &&
                g_ogg[j].padre == f)
                g_ogg[j].w = o->w;
    }
    return 1;
}

static ExFinestra controllo_in(ExFinestra padre, int x, int y)
{
    int i;

    for (i = OGGETTI_MAX - 1; i >= 0; i--) {
        Oggetto *o = &g_ogg[i];
        int ox, oy;

        if (!o->usato || o->padre == 0 || !(o->stile & EX_VISIBILE)) continue;
        if (radice((ExFinestra)(i + 1)) != ogg(padre)) continue;
        if (o->classe == CL_RIQUADRO || o->classe == CL_SEPARATORE ||
            o->classe == CL_ETICHETTA || o->classe == CL_INTESTAZIONE) continue;

        origine(o, &ox, &oy);
        if (x >= ox + o->x && x < ox + o->x + o->w &&
            y >= oy + o->y && y < oy + o->y + o->h)
            return (ExFinestra)(i + 1);
    }
    return 0;
}

/* -----------------------------------------------------------------------------
 * Le modifiche al testo dentro l'area
 * --------------------------------------------------------------------------- */
static void area_inserisci(Area *A, char c)
{
    char        *r = area_riga(A, A->cy);
    unsigned int l = area_lung(A, A->cy), i;

    if (l >= AREA_COL_MAX - 1) return;      /* riga piena: si ignora */

    for (i = l + 1; i > A->cx; i--) r[i] = r[i - 1];
    r[A->cx] = c;
    A->cx++;
    A->modificato = 1;
}

static void area_spezza(Area *A)
{
    unsigned int i;

    if (A->n >= AREA_RIGHE_MAX) return;

    for (i = A->n; i > A->cy + 1; i--)
        strcpy(area_riga(A, i), area_riga(A, i - 1));

    strcpy(area_riga(A, A->cy + 1), area_riga(A, A->cy) + A->cx);
    area_riga(A, A->cy)[A->cx] = '\0';

    A->n++;
    A->cy++;
    A->cx = 0;
    A->modificato = 1;
}

/* Toglie il carattere PRIMA del cursore; a inizio riga unisce con quella
 * sopra, che e' quello che ci si aspetta da un Backspace. */
static void area_indietro(Area *A)
{
    unsigned int i;

    if (A->cx > 0) {
        char        *r = area_riga(A, A->cy);
        unsigned int l = area_lung(A, A->cy);

        for (i = A->cx - 1; i < l; i++) r[i] = r[i + 1];
        A->cx--;
        A->modificato = 1;
        return;
    }
    if (A->cy == 0) return;

    {
        unsigned int sopra = area_lung(A, A->cy - 1);

        if (sopra + area_lung(A, A->cy) >= AREA_COL_MAX - 1) return;

        strcat(area_riga(A, A->cy - 1), area_riga(A, A->cy));
        for (i = A->cy; i + 1 < A->n; i++)
            strcpy(area_riga(A, i), area_riga(A, i + 1));
        A->n--;
        A->cy--;
        A->cx = sopra;
        A->modificato = 1;
    }
}

/* Toglie il carattere SOTTO il cursore: e' il Backspace della riga dopo. */
static void area_avanti(Area *A)
{
    if (A->cx < area_lung(A, A->cy)) {
        char        *r = area_riga(A, A->cy);
        unsigned int i, l = area_lung(A, A->cy);

        for (i = A->cx; i < l; i++) r[i] = r[i + 1];
        A->modificato = 1;
        return;
    }
    if (A->cy + 1 >= A->n) return;

    A->cy++;
    A->cx = 0;
    area_indietro(A);
}

/* =============================================================================
 * LA SELEZIONE — l'ancora, il testo in mezzo, e cosa se ne fa
 *
 * ! LE COORDINATE SI ORDINANO QUANDO SERVONO, NON QUANDO SI POSANO. Un'ancora
 * puo' stare dopo il cursore (si e' selezionato all'indietro) e va benissimo:
 * ordinarle subito farebbe perdere da che parte si sta allargando.
 * ============================================================================= */
static int area_sel_ordina(Area *A, unsigned int *y1, unsigned int *x1,
                           unsigned int *y2, unsigned int *x2)
{
    if (!A->sel) return 0;
    if (A->ay == A->cy && A->ax == A->cx) return 0;     /* vuota */

    if (A->ay < A->cy || (A->ay == A->cy && A->ax < A->cx)) {
        *y1 = A->ay; *x1 = A->ax; *y2 = A->cy; *x2 = A->cx;
    } else {
        *y1 = A->cy; *x1 = A->cx; *y2 = A->ay; *x2 = A->ax;
    }
    return 1;
}

/* Rende quanti byte ha messo negli appunti, 0 se non c'era niente. */
static unsigned int area_sel_copia(Area *A)
{
    Appunti     *ap = appunti();
    unsigned int y1, x1, y2, x2, y, n = 0;

    if (!ap || !area_sel_ordina(A, &y1, &x1, &y2, &x2)) return 0;

    for (y = y1; y <= y2 && y < A->n; y++) {
        const char  *r = area_riga(A, y);
        unsigned int l = area_lung(A, y);
        unsigned int da = (y == y1) ? x1 : 0;
        unsigned int a  = (y == y2) ? x2 : l;

        if (da > l) da = l;
        if (a  > l) a  = l;

        while (da < a && n < sizeof(ap->testo) - 1) ap->testo[n++] = r[da++];
        if (y < y2 && n < sizeof(ap->testo) - 1) ap->testo[n++] = '\n';
    }

    ap->testo[n] = '\0';
    ap->n = n;
    return n;
}

static void area_sel_via(Area *A)
{
    unsigned int y1, x1, y2, x2, i;

    if (!area_sel_ordina(A, &y1, &x1, &y2, &x2)) return;

    /* ! SI CANCELLA DALLA FINE VERSO L'INIZIO USANDO IL Backspace CHE C'E'
     * GIA'. Riscrivere la cancellazione di un intervallo vorrebbe dire una
     * seconda attuazione dell'unione fra due righe — e le due divergerebbero
     * al primo caso limite (una riga piena, l'ultima riga, il testo vuoto). */
    A->cy = y2; A->cx = x2;
    A->sel = 0;

    while (A->cy > y1 || A->cx > x1) {
        unsigned int py = A->cy, px = A->cx;

        area_indietro(A);
        if (A->cy == py && A->cx == px) break;   /* non si e' mosso: basta */
    }

    /* Le righe possono essere sparite sotto i piedi al cursore. */
    if (A->cy >= A->n) A->cy = A->n ? A->n - 1 : 0;
    if (A->cx > area_lung(A, A->cy)) A->cx = area_lung(A, A->cy);
    (void)i;
}

static void area_incolla(Area *A)
{
    Appunti     *ap = appunti();
    unsigned int i;

    if (!ap || ap->n == 0) return;

    area_sel_via(A);
    for (i = 0; i < ap->n && i < sizeof(ap->testo); i++) {
        if (ap->testo[i] == '\n') area_spezza(A);
        else                      area_inserisci(A, ap->testo[i]);
    }
    area_segui(A);
}

/* ! L'ANCORA SI POSA QUANDO SI COMINCIA A TENERE Shift, E SI TOGLIE QUANDO SI
 * MUOVE SENZA. E' l'unica regola che non stupisce: una freccia normale
 * cancella la selezione — come dappertutto — e una col Shift la allarga da
 * dove si era. */
static void area_ancora(Area *A, int con_shift)
{
    if (!con_shift) { A->sel = 0; return; }
    if (!A->sel) { A->sel = 1; A->ax = A->cx; A->ay = A->cy; }
}

static int area_tasto(Area *A, unsigned int k)
{
    unsigned int passo = A->righe ? A->righe : 1;
    unsigned int c = k & KBD_KEY_MASK;
    int muove = (c == KBD_K_UP   || c == KBD_K_DOWN || c == KBD_K_LEFT ||
                 c == KBD_K_RIGHT || c == KBD_K_HOME || c == KBD_K_END  ||
                 c == KBD_K_PGUP || c == KBD_K_PGDN);

    /* ! CHI SCRIVE SU UNA SELEZIONE LA SOSTITUISCE, e non ci scrive dentro:
     * battere una lettera con del testo scelto lo cancella e mette la lettera
     * al suo posto. E' quello che fa qualunque editor, ed e' anche l'unica
     * cosa che non lascia il testo in uno stato che nessuno ha chiesto. */
    if (A->sel && !muove &&
        (c == '\b' || c == KBD_K_DEL || c == '\n' || c == '\r' ||
         c == '\t' || (c >= 0x20 && c < 0x7F))) {
        area_sel_via(A);
        if (c == '\b' || c == KBD_K_DEL) { area_segui(A); return 1; }
    }

    if (muove) area_ancora(A, (k & KBD_MOD_SHIFT) != 0);
    else       A->sel = 0;

    switch (c) {
    case KBD_K_UP:    if (A->cy > 0) A->cy--;                          break;
    case KBD_K_DOWN:  if (A->cy + 1 < A->n) A->cy++;                   break;
    case KBD_K_LEFT:
        if (A->cx > 0) A->cx--;
        else if (A->cy > 0) { A->cy--; A->cx = area_lung(A, A->cy); }
        break;
    case KBD_K_RIGHT:
        if (A->cx < area_lung(A, A->cy)) A->cx++;
        else if (A->cy + 1 < A->n) { A->cy++; A->cx = 0; }
        break;
    case KBD_K_HOME:  A->cx = 0;                                       break;
    case KBD_K_END:   A->cx = area_lung(A, A->cy);                     break;
    case KBD_K_PGUP:  A->cy = (A->cy > passo) ? A->cy - passo : 0;      break;
    case KBD_K_PGDN:
        A->cy += passo;
        if (A->cy >= A->n) A->cy = A->n - 1;
        break;
    case KBD_K_DEL:   area_avanti(A);                                  break;
    case '\b':        area_indietro(A);                                break;
    case '\n':
    case '\r':        area_spezza(A);                                  break;
    default:
        /* ! SOLO I CARATTERI STAMPABILI E IL TAB. I tasti speciali stanno da
         * 0x100 in su apposta per non poterli confondere con un carattere;
         * infilarne uno nel testo darebbe un file con dentro un valore che
         * nessun altro programma sa leggere. */
        if (c == '\t' || (c >= 0x20 && c < 0x7F)) area_inserisci(A, (char)c);
        else return 0;
        break;
    }

    /* Il cursore non puo' stare oltre la fine della riga in cui e' finito. */
    if (A->cx > area_lung(A, A->cy)) A->cx = area_lung(A, A->cy);
    area_segui(A);
    return 1;
}

/* Rende 1 se il tasto e' stato consumato da un controllo.
 *
 * ! LE COMBINAZIONI CON Ctrl NON SI MANGIANO. Un Ctrl+Q dentro una casella
 * deve arrivare all'applicazione, o non ci sarebbe modo di dare una scorciatoia
 * a un programma che ha una casella col fuoco. Il servizio 'kbd' tiene i
 * modificatori in un campo a parte apposta. */
static int tasto_al_fuoco(ExFinestra f, unsigned int k)
{
    Oggetto *r = radice(f);
    Oggetto *o;
    unsigned int c = k & KBD_KEY_MASK;
    unsigned int n;

    if (!r) return 0;

    if (c == '\t') { fuoco_avanti(f); return 1; }

    o = ogg(r->fuoco);

    /* ! IL TERMINALE VUOLE ANCHE I Ctrl, E VA GUARDATO PRIMA DEL FILTRO. Per
     * ogni altro controllo un Ctrl+lettera e' una scorciatoia
     * dell'applicazione e non deve essere mangiata; per un terminale e' un
     * CARATTERE — Ctrl+C e' il byte 3, Ctrl+D il 4 — e senza questa eccezione
     * Ctrl+C non arriverebbe mai al pty, cioe' tutto il lavoro sulla
     * disciplina di linea resterebbe irraggiungibile da qui. */
    if (o && o->classe == CL_TERMINALE) {
        Terminale *t = term_di(o);

        if (!t) return 0;
        if (k & KBD_MOD_CTRL) {
            unsigned int l = c | 0x20;      /* Ctrl+C e Ctrl+c sono lo stesso */

            if (l >= 'a' && l <= 'z') return term_tasto(t, l - 'a' + 1);
            return 0;                       /* Ctrl+altro: non e' roba nostra */
        }
        return term_tasto(t, c);
    }

    if (k & KBD_MOD_CTRL) return 0;

    if (!o) return 0;

    /* ! LA LISTA CONSUMA SOLO CIO' CHE SA USARE. Frecce, PgSu/PgGiu, Home/End
     * e Invio sono suoi; una lettera no — e lasciarla passare e' quello che
     * permette a un'applicazione di dare una scorciatoia mentre la lista ha il
     * fuoco. Un controllo che mangia tutto e' un controllo che si prende
     * l'applicazione. */
    if (o->classe == CL_AREA) {
        Area *A = area_di(o);
        /* ! ALL'AREA SI PASSA IL TASTO INTERO, MODIFICATORI COMPRESI, e non
         * il solo carattere: senza il bit dello Shift non c'e' modo di
         * distinguere una freccia che sposta il cursore da una che allarga la
         * selezione. E' l'unico controllo a cui serva. */
        return A ? area_tasto(A, k) : 0;
    }

    if (o->classe == CL_LISTA) {
        Lista *L = lista_di(o);
        unsigned int passo;

        if (!L || L->n == 0) return 0;
        passo = L->righe ? L->righe : 1;

        switch (c) {
        case KBD_K_DOWN:  if (L->sel + 1 < L->n) L->sel++;                break;
        case KBD_K_UP:    if (L->sel > 0) L->sel--;                       break;
        case KBD_K_HOME:  L->sel = 0;                                     break;
        case KBD_K_END:   L->sel = L->n - 1;                              break;
        case KBD_K_PGUP:  L->sel = (L->sel > passo) ? L->sel - passo : 0;  break;
        case KBD_K_PGDN:
            L->sel += passo;
            if (L->sel >= L->n) L->sel = L->n - 1;
            break;

        /* ! INVIO NON MUOVE NIENTE: DICE CHE SI E' SCELTO. Arriva
         * all'applicazione come EXM_COMANDO con l'id della lista, cioe' con lo
         * stesso meccanismo di un pulsante premuto — e chi lo riceve non deve
         * imparare un secondo modo di sentire le cose. */
        case '\n':
        case '\r': {
            Oggetto *r = radice(o->padre);
            lista_segui(L);
            /* L'ultimo argomento e' l'1 che EX_DA_INVIO legge: e' l'unico
             * posto del toolkit che manda un comando venuto da un tasto. */
            if (r && r->proc)
                r->proc(radice_h((ExFinestra)(o - g_ogg + 1)),
                        EXM_COMANDO, o->id, 1);
            return 1;
        }

        default:
            return 0;
        }

        lista_segui(L);
        return 1;
    }

    if (o->classe != CL_TESTO) return 0;

    n = (unsigned int)strlen(o->titolo);

    if (c == '\b') {                        /* Backspace */
        if (n > 0) o->titolo[n - 1] = '\0';
        o->cursore = (unsigned int)strlen(o->titolo);
        return 1;
    }
    if (c == '\n' || c == '\r') return 0;  /* Invio va all'applicazione */

    /* ! SOLO I CARATTERI STAMPABILI. I tasti speciali stanno da 0x100 in su
     * apposta per non poterli mai confondere con un carattere: infilare una
     * freccia dentro il testo darebbe una stringa con dentro un valore che
     * non si stampa. */
    if (c < 0x20 || c > 0x7E) return 0;

    if (n + 1 < TESTO_LEN) {
        o->titolo[n] = (char)c;
        o->titolo[n + 1] = '\0';
        o->cursore = n + 1;
    }
    return 1;
}

int ex_prendi_msg(ExMsg *m)
{
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    struct pollfd v[1 + TERM_MAX];

    for (;;) {
        WinEvento e;
        ExFinestra f;

        if (g_uscita) return 0;

        /* ! SI DORME DAVVERO, E SU TUTTE LE SORGENTI INSIEME. poll() mette il
         * processo in BLOCKED finche' non succede qualcosa; e le sorgenti sono
         * due specie diverse — la mailbox IPC da cui arrivano i tasti e i
         * clic, e le pipe da cui arriva cio' che le shell hanno scritto.
         *
         * ! E' ESATTAMENTE IL CASO PER CUI SYS_POLL E' NATA: descrittori e
         * mailbox nella stessa attesa. Guardarne una sola vorrebbe dire o una
         * finestra che non risponde al mouse mentre la shell lavora, o
         * l'uscita della shell che compare solo quando si preme un tasto. */
        {
            int nv = 0, j;

            v[nv].fd = FD_IPC; v[nv].events = POLLIN; v[nv].revents = 0; nv++;

            for (j = 0; j < TERM_MAX && nv < (int)(sizeof(v)/sizeof(v[0])); j++)
                if (g_term[j].usato && g_term[j].fd_out >= 0) {
                    v[nv].fd = g_term[j].fd_out;
                    v[nv].events = POLLIN;
                    v[nv].revents = 0;
                    nv++;
                }

            if (poll(v, (unsigned int)nv, 200) <= 0) continue;

            /* Cio' che le shell hanno scritto si raccoglie prima dei
             * messaggi: se qualcosa e' cambiato, la finestra si ridisegna. */
            {
                int cambiato = 0;

                for (j = 0; j < TERM_MAX; j++) {
                    if (!g_term[j].usato) continue;

                    if (term_leggi(&g_term[j])) {
                        cambiato = 1;
                        ex_procedura_base(radice_h(g_term[j].ogg), EXM_DISEGNA, 0, 0);
                    }

                    /* ! E SE IL PROGRAMMA DENTRO E' USCITO, SI DICE — una
                     * volta sola. Si consegna come un messaggio normale, cosi'
                     * l'applicazione decide lei: chiudere la finestra, dirlo,
                     * o riaprire un'altra shell. */
                    if (g_term[j].finito && !g_term[j].detto) {
                        g_term[j].detto = 1;
                        m->finestra = radice_h(g_term[j].ogg);
                        m->msg      = EXM_TERMFINITO;
                        m->wp       = 0;
                        m->lp       = 0;
                        return 1;
                    }
                }
                if (!(v[0].revents & POLLIN)) { if (cambiato) continue; continue; }
            }
        }

        if (ipc_recv_timeout(&meta, buf, sizeof(buf), 0) < 0) continue;

        /* ! LA ZONA NUOVA SI PRENDE PRIMA DI SVEGLIARE L'APPLICAZIONE, e non
         * dopo: consegnando EXM_MISURA con i pixel ancora vecchi, la prima
         * cosa che l'applicazione fa — ridisegnarsi nella misura nuova —
         * scriverebbe oltre la fine della zona vecchia. */
        if (meta.tipo == WIN_MSG_MISURATA && meta.len >= sizeof(WinCreata)) {
            WinCreata r;

            memcpy(&r, buf, sizeof(r));
            f = da_win_id(r.id);
            if (f == 0 || !rimappa(f, &r)) continue;

            m->finestra = f;
            m->msg      = EXM_MISURA;
            m->wp       = 0;
            m->lp       = (long)((r.larghezza & 0xFFFF) |
                                 ((r.altezza & 0xFFFF) << 16));
            return 1;
        }

        if (meta.tipo != WIN_MSG_EVENTO || meta.len < sizeof(e)) continue;

        memcpy(&e, buf, sizeof(e));
        f = da_win_id(e.id);
        if (f == 0) continue;

        m->finestra = f;
        m->wp = 0;
        m->lp = (long)((e.x & 0xFFFF) | ((e.y & 0xFFFF) << 16));

        switch (e.tipo) {
        case WIN_EV_CHIUDI:     m->msg = EXM_CHIUDI;    return 1;
        case WIN_EV_TASTO:
            /* ! IL TASTO ARRIVA GIA' TRADOTTO dal servizio 'kbd': carattere
             * nei bit bassi, tasti speciali da 0x100, modificatori in alto.
             * Qui non si tocca nessuna mappa di tastiera — quella e' di chi
             * la tastiera la possiede, e cambia a caldo con `keymap`. */
            /* ! IL MENU GUARDA I TASTI PRIMA DEL CONTROLLO COL FUOCO, e non
             * dopo: con una tendina aperta le frecce muovono la tendina, non
             * la lista sotto. Ma a menu chiuso lascia passare tutto tranne
             * F10 — vedi menu_tasto(). */
            {
                unsigned int cmd = 0;

                if (menu_tasto(f, e.tasto, &cmd)) {
                    ex_procedura_base(f, EXM_DISEGNA, 0, 0);
                    if (cmd == 0) continue;
                    m->msg = EXM_COMANDO;
                    m->wp  = cmd;
                    return 1;
                }
            }

            if (tasto_al_fuoco(f, e.tasto)) {
                /* Consumato da una casella: si ridisegna e si aspetta ancora,
                 * invece di svegliare l'applicazione per ogni lettera. */
                ex_procedura_base(f, EXM_DISEGNA, 0, 0);
                continue;
            }
            m->msg = EXM_TASTO;
            m->wp  = e.tasto;
            return 1;
        case WIN_EV_MOUSE_SU:
            /* ! IL PULSANTE TORNA SU QUANDO IL DITO SI ALZA, e non prima: il
             * comando parte gia' alla pressione, ma se il rilievo tornasse
             * subito non si vedrebbe MAI premuto — e un pulsante che non si
             * muove sembra un pulsante che non ha sentito. Si alzano tutti,
             * non solo quello sotto il puntatore: chi preme e poi trascina
             * fuori lascerebbe un pulsante schiacciato per sempre. */
            {
                int j, cambiato = 0;

                for (j = 0; j < OGGETTI_MAX; j++)
                    if (g_ogg[j].usato && g_ogg[j].premuto) {
                        g_ogg[j].premuto = 0;
                        cambiato = 1;
                    }
                if (cambiato) ex_procedura_base(f, EXM_DISEGNA, 0, 0);
            }
            m->msg = EXM_MOUSE_SU;
            return 1;
        case WIN_EV_MOUSE_GIU: {
            /* ! IL CLIC SU UN PULSANTE DIVENTA EXM_COMANDO, e il messaggio
             * grezzo non arriva all'applicazione. E' cio' che distingue un
             * toolkit da un pannello di pixel: chi scrive l'applicazione
             * guarda l'id del pulsante, non le coordinate. */
            ExFinestra c;
            Oggetto *co;

            /* ! IL MENU GUARDA IL CLIC PRIMA DI TUTTI, perche' una tendina
             * aperta COPRE i controlli: un clic dentro la tendina cade sopra
             * un pulsante che si vede solo a menu chiuso, e senza questo
             * controllo sarebbe quel pulsante a riceverlo. */
            {
                unsigned int cmd = 0;

                if (menu_clic(f, (int)e.x, (int)e.y, &cmd)) {
                    ex_procedura_base(f, EXM_DISEGNA, 0, 0);
                    if (cmd == 0) continue;
                    m->msg = EXM_COMANDO;
                    m->wp  = cmd;
                    return 1;
                }
            }

            c  = controllo_in(f, (int)e.x, (int)e.y);
            co = ogg(c);

            /* ! UN PULSANTE NON SI PRENDE LA TASTIERA, e la differenza si vede
             * usando il file manager: premuto «Su» col mouse, le frecce non
             * muovevano piu' la selezione: il fuoco era passato al pulsante,
             * che i tasti non li usa — non consuma nemmeno l'Invio (vedi
             * tasto_al_fuoco) — e da li' non tornava indietro da solo.
             *
             * ! IL FUOCO VA A CHI SE NE FA QUALCOSA: una casella, una lista,
             * un'area, un terminale. Un pulsante lo si raggiunge con Tab, che
             * e' il modo in cui lo si chiede apposta invece di ottenerlo per
             * caso premendolo. */
            if (co && co->classe != CL_PULSANTE) fuoco_metti(f, c);

            if (co && co->classe == CL_PULSANTE) {
                co->premuto = 1;
                ex_procedura_base(f, EXM_DISEGNA, 0, 0);
                m->msg = EXM_COMANDO;
                m->wp  = co->id;
                return 1;
            }

            /* ! UN CLIC SU UNA LISTA SCEGLIE LA RIGA, e l'applicazione lo
             * riceve come EXM_COMANDO con l'id della lista — lo stesso
             * messaggio dell'Invio, perche' sono la stessa decisione presa in
             * due modi. Chi vuole distinguere «ho scelto» da «ho aperto»
             * guarda se il comando arriva due volte di fila; oggi il server
             * non manda il doppio clic. */
            if (co && co->classe == CL_AREA) {
                Area *A = area_di(co);
                int ox, oy;

                if (A) {
                    unsigned int r, c;

                    /* ! origine() RENDE LA POSIZIONE DEL PADRE, NON LA PROPRIA,
                     * e chi la usa aggiunge la sua: e' cosi' che la usano il
                     * disegno e la ricerca del controllo sotto il puntatore.
                     * Qui mancava, e il clic veniva diviso come se l'area
                     * cominciasse in cima alla finestra — nell'editor
                     * significava battere su una riga e vederne scelta un'altra
                     * piu' su, di tante quante ne stanno nello spazio sopra
                     * l'area. */
                    origine(co, &ox, &oy);
                    ox += co->x;
                    oy += co->y;

                    r = A->top  + (unsigned int)(((int)e.y - oy - 2) / AREA_RIGA_H);
                    c = A->left + (unsigned int)(((int)e.x - ox - 2) / AREA_CAR_W);
                    if (r >= A->n) r = A->n - 1;
                    A->cy = r;
                    A->cx = (c > area_lung(A, r)) ? area_lung(A, r) : c;
                    area_segui(A);
                }
                ex_procedura_base(f, EXM_DISEGNA, 0, 0);
                continue;
            }

            if (co && co->classe == CL_LISTA) {
                Lista *L = lista_di(co);
                int ox, oy;

                if (L) {
                    unsigned int r;

                    /* Stesso difetto dell'area, e stesso rimedio: senza la
                     * propria posizione, un clic sul nome di una cartella ne
                     * sceglieva una piu' in basso di quante righe stanno fra la
                     * cima della finestra e la lista. */
                    origine(co, &ox, &oy);
                    ox += co->x;
                    oy += co->y;
                    (void)ox;

                    r = L->primo + (unsigned int)(((int)e.y - oy - 2) / LISTA_RIGA_H);
                    if (r < L->n) { L->sel = r; lista_segui(L); }
                }
                ex_procedura_base(f, EXM_DISEGNA, 0, 0);
                m->msg = EXM_COMANDO;
                m->wp  = co->id;
                m->lp  = 0;             /* dal clic: vedi EX_DA_INVIO */
                return 1;
            }
            m->msg = EXM_MOUSE_GIU;
            return 1;
        }
        default:
            m->msg = EXM_DISEGNA;
            return 1;
        }
    }
}

void ex_smista(const ExMsg *m)
{
    Oggetto *o = ogg(m->finestra);

    if (!o) return;

    if (o->proc) {
        if (o->proc(m->finestra, m->msg, m->wp, m->lp) == 0) {
            /* La procedura ha gestito il messaggio: si ridisegna comunque,
             * perche' quasi sempre l'ha gestito cambiando qualcosa.
             *
             * ! E IL RIDISEGNO PASSA DALLA SUA PROCEDURA, NON DALLA BASE.
             * Fino al 18 agosto 2026 qui c'era ex_procedura_base(), che
             * riempie la finestra di grigio e ridisegna i controlli: una
             * finestra che disegna i propri pixel — la scrivania, con il suo
             * colore e la sua immagine — se li vedeva cancellare al primo
             * messaggio gestito, e non aveva NESSUN modo di difendersi,
             * perche' il ridisegno non le veniva nemmeno chiesto. Il sintomo
             * era «un clic sullo sfondo lo fa diventare grigio».
             *
             * Chi non gestisce EXM_DISEGNA ricade sulla base da se', e per
             * quelle finestre non cambia niente. */
            if (m->msg != EXM_DISEGNA)
                o->proc(m->finestra, EXM_DISEGNA, 0, 0);
            return;
        }
    }
    ex_procedura_base(m->finestra, m->msg, m->wp, m->lp);
}

long ex_procedura_base(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    Oggetto *o = ogg(f);

    (void)wp; (void)lp;
    if (!o) return 0;

    switch (msg) {
    case EXM_CHIUDI:
        ex_esci(0);
        return 0;

    case EXM_DISEGNA:
    default:
        /* Lo sfondo dell'area del client, e poi i controlli sopra. */
        ex_riempi(f, 0, 0, o->w, o->h, EX_GRIGIO);
        disegna_figli(f);
        ex_aggiorna(f);
        return 0;
    }
}

void ex_esci(int codice) { g_uscita = 1; g_codice = codice; }

/* =============================================================================
 * L'API DELLA LISTA
 *
 * ! LE VOCI SI AGGIUNGONO, NON SI PASSA UN VETTORE. Un vettore vorrebbe dire
 * che chi chiama lo tiene vivo finche' la lista esiste — e nessuno ricorda di
 * farlo. Qui il testo si COPIA dentro la lista al momento: il chiamante puo'
 * usare un buffer sullo stack e dimenticarsene.
 * ============================================================================= */
void ex_lista_svuota(ExFinestra f)
{
    Lista *L = lista_da_h(f);
    if (!L) return;
    L->n = L->sel = L->primo = 0;
}

/* Rende 1 se la voce c'e' entrata, 0 se la lista e' piena. */
int ex_lista_aggiungi(ExFinestra f, const char *testo)
{
    Lista *L = lista_da_h(f);
    char *dst;

    if (!L || !testo) return 0;

    /* ! PIENA SI DICE, non si sovrascrive l'ultima. Un elenco che smette in
     * silenzio di crescere fa credere che la directory abbia meno file di
     * quanti ne ha. */
    if (L->n >= LISTA_VOCI_MAX) return 0;

    dst = &L->voci[L->n * LISTA_TESTO_MAX];
    strncpy(dst, testo, LISTA_TESTO_MAX - 1);
    dst[LISTA_TESTO_MAX - 1] = '\0';
    L->n++;
    return 1;
}

unsigned int ex_lista_quante(ExFinestra f)
{
    Lista *L = lista_da_h(f);
    return L ? L->n : 0;
}

/* L'indice della voce scelta. Con la lista vuota rende 0, e ex_lista_quante()
 * dice che non c'e' niente: chi legge deve guardare prima quella. */
unsigned int ex_lista_scelta(ExFinestra f)
{
    Lista *L = lista_da_h(f);
    return L ? L->sel : 0;
}

void ex_lista_scegli(ExFinestra f, unsigned int i)
{
    Lista *L = lista_da_h(f);
    if (!L || i >= L->n) return;
    L->sel = i;
    lista_segui(L);
}

/* Il testo di una voce. Rende "" e non 0 per un indice fuori posto: un
 * puntatore nullo dentro una printf e' un fault, una stringa vuota e' una riga
 * vuota — e la differenza si vede subito. */
const char *ex_lista_testo(ExFinestra f, unsigned int i)
{
    Lista *L = lista_da_h(f);

    if (!L || i >= L->n) return "";
    return &L->voci[i * LISTA_TESTO_MAX];
}

/* =============================================================================
 * IL MENU — l'API. Il perche' sta accanto a #define MENU_MAX.
 * ============================================================================= */
ExFinestra ex_menu(ExFinestra finestra)
{
    Oggetto *r = radice(finestra);
    Menu    *M;

    if (!r || r->classe != CL_FINESTRA) return 0;

    /* ! UNA SOLA BARRA PER FINESTRA, e chiederla due volte rende quella che
     * c'e' gia' invece di crearne un'altra: e' quello che serve a chi la
     * chiede in due punti del proprio codice per aggiungere voci, e due barre
     * sovrapposte sarebbero un difetto muto. */
    M = menu_della_finestra(finestra);
    if (M) return M->ogg;

    return ex_crea("menu", "", EX_FIGLIO, 0, 0, r->w, MENU_BARRA_H,
                   (ExFinestra)(r - g_ogg + 1), 0, 0);
}

int ex_menu_voce(ExFinestra menu, const char *titolo, const char *voce,
                 unsigned int id)
{
    Menu       *M = menu_di(ogg(menu));
    MenuTitolo *T = 0;
    unsigned int i;

    if (!M || !titolo || !titolo[0]) return 0;

    for (i = 0; i < M->n; i++)
        if (strcmp(M->titolo[i].nome, titolo) == 0) { T = &M->titolo[i]; break; }

    if (!T) {
        if (M->n >= MENU_TITOLI_MAX) return 0;
        T = &M->titolo[M->n++];
        memset(T, 0, sizeof(*T));
        strncpy(T->nome, titolo, MENU_TESTO_MAX - 1);
        T->nome[MENU_TESTO_MAX - 1] = '\0';
        menu_geometria(M);
    }

    /* Senza voce si e' solo creato il titolo: e' come si mette in barra un
     * menu che si riempira' dopo. */
    if (!voce || !voce[0]) return 1;

    if (T->n >= MENU_VOCI_MAX) return 0;

    /* ! UN SEPARATORE E' UNA VOCE CON id ZERO, e si scrive "-". Zero non e' un
     * id valido comunque — EXM_COMANDO con wp a zero non direbbe niente a
     * nessuno — quindi non c'e' un secondo modo di dirlo da tenere in piedi. */
    if (voce[0] == '-' && voce[1] == '\0') {
        T->voce[T->n].testo[0] = '\0';
        T->voce[T->n].id = 0;
        T->n++;
        return 1;
    }

    if (id == 0) return 0;              /* una voce senza id non si sceglie */

    strncpy(T->voce[T->n].testo, voce, MENU_TESTO_MAX - 1);
    T->voce[T->n].testo[MENU_TESTO_MAX - 1] = '\0';
    T->voce[T->n].id = id;
    T->n++;
    return 1;
}


/* =============================================================================
 * L'API DELL'AREA DI TESTO
 *
 * ! IL TESTO SI CARICA E SI RILEGGE UNA RIGA PER VOLTA, e non c'e' una
 * funzione che prenda tutto il buffer: darebbe a chi chiama un puntatore
 * dentro la libreria, cioe' un modo di scriverci sopra senza che il controllo
 * se ne accorga — e allora il cursore e il numero di righe direbbero una cosa
 * e il testo un'altra.
 * ============================================================================= */
void ex_area_svuota(ExFinestra f)
{
    Area *A = area_da_h(f);
    if (!A) return;
    memset(A->testo, 0, AREA_RIGHE_MAX * AREA_COL_MAX);
    A->n = 1;
    A->cx = A->cy = A->top = A->left = 0;
    A->modificato = 0;
}

/* Aggiunge una riga in fondo. Rende 1, o 0 se l'area e' piena — e chi carica
 * un file DEVE guardarlo: caricare mezzo file e salvarlo cancellerebbe il
 * resto senza averlo mai mostrato. */
int ex_area_aggiungi(ExFinestra f, const char *riga)
{
    Area *A = area_da_h(f);

    if (!A || !riga) return 0;

    /* La prima riga di un'area vuota c'e' gia' ed e' vuota: si riempie quella
     * invece di aggiungerne una seconda. */
    if (A->n == 1 && area_riga(A, 0)[0] == '\0') {
        strncpy(area_riga(A, 0), riga, AREA_COL_MAX - 1);
        area_riga(A, 0)[AREA_COL_MAX - 1] = '\0';
        return 1;
    }

    if (A->n >= AREA_RIGHE_MAX) return 0;

    strncpy(area_riga(A, A->n), riga, AREA_COL_MAX - 1);
    area_riga(A, A->n)[AREA_COL_MAX - 1] = '\0';
    A->n++;
    return 1;
}

unsigned int ex_area_righe(ExFinestra f)
{
    Area *A = area_da_h(f);
    return A ? A->n : 0;
}

const char *ex_area_riga(ExFinestra f, unsigned int i)
{
    Area *A = area_da_h(f);

    if (!A || i >= A->n) return "";
    return area_riga(A, i);
}

/* 1 se il testo e' cambiato dall'ultimo ex_area_pulita(). Serve a chiedere
 * «vuoi salvare?» solo quando ha senso chiederlo. */
int ex_area_modificato(ExFinestra f)
{
    Area *A = area_da_h(f);
    return A ? A->modificato : 0;
}

void ex_area_pulita(ExFinestra f)
{
    Area *A = area_da_h(f);
    if (A) A->modificato = 0;
}

/* Dove sta il cursore, contando da 1: e' quello che si scrive in una riga di
 * stato, e farlo contare da 0 e' l'errore che fa ogni editor scritto in
 * fretta. */
/* =============================================================================
 * SELEZIONE E APPUNTI — quello che il menu «Modifica» chiama
 *
 * ! GLI APPUNTI SONO DI TUTTA LA SCRIVANIA, non dell'applicazione: stanno in
 * una zona di memoria condivisa (vedi appunti()). Copiare in un editor e
 * incollare in un altro funziona — ed e' la prima cosa che qualcuno prova ora
 * che l'editor si puo' aprire due volte.
 *
 * ! E VIVONO FINCHE' C'E' UN'APPLICAZIONE GRAFICA APERTA. Chiuse tutte, la
 * zona muore con l'ultima: e' la semantica della memoria condivisa di EX-OS,
 * non una scelta di qui, e va detta invece che scoperta.
 * ============================================================================= */
void ex_area_seleziona_tutto(ExFinestra f)
{
    Area *A = area_da_h(f);

    if (!A) return;
    A->sel = 1;
    A->ay = 0; A->ax = 0;
    A->cy = A->n ? A->n - 1 : 0;
    A->cx = area_lung(A, A->cy);
    area_segui(A);
}

int ex_area_copia(ExFinestra f)
{
    Area *A = area_da_h(f);
    return A ? (int)area_sel_copia(A) : 0;
}

int ex_area_taglia(ExFinestra f)
{
    Area        *A = area_da_h(f);
    unsigned int n;

    if (!A) return 0;
    n = area_sel_copia(A);
    if (n) area_sel_via(A);
    area_segui(A);
    return (int)n;
}

int ex_area_cancella(ExFinestra f)
{
    Area        *A = area_da_h(f);
    unsigned int y1, x1, y2, x2;

    if (!A || !area_sel_ordina(A, &y1, &x1, &y2, &x2)) return 0;
    area_sel_via(A);
    area_segui(A);
    return 1;
}

int ex_area_incolla(ExFinestra f)
{
    Area    *A  = area_da_h(f);
    Appunti *ap = appunti();

    if (!A || !ap || ap->n == 0) return 0;
    area_incolla(A);
    return (int)ap->n;
}

void ex_area_cursore(ExFinestra f, unsigned int *riga, unsigned int *col)
{
    Area *A = area_da_h(f);

    if (riga) *riga = A ? A->cy + 1 : 0;
    if (col)  *col  = A ? A->cx + 1 : 0;
}

void ex_schermo(unsigned int *larghezza, unsigned int *altezza)
{
    VideoInfo v;

    if (video_info(&v) != 0) { v.larghezza = 0; v.altezza = 0; }
    if (larghezza) *larghezza = v.larghezza;
    if (altezza)   *altezza   = v.altezza;
}

/* -----------------------------------------------------------------------------
 * Le immagini
 *
 * ! IL FORMATO SI RICONOSCE DAI PRIMI BYTE, NON DALL'ESTENSIONE. Un file
 * chiamato .jpg puo' essere un PNG, e succede: fidarsi del nome vuol dire
 * passare i byte al lettore sbagliato, che li interpreta e disegna
 * spazzatura invece di dire «non e' mio».
 *
 * ! LA TABELLA E' IL PUNTO. Aggiungere JPG, PNG o ICO vuol dire scrivere un
 * lettore e aggiungere una riga qui: non si tocca ne' questa funzione, ne'
 * exwin.h, ne' — soprattutto — il server.
 * --------------------------------------------------------------------------- */
typedef int (*ExLettore)(ExFinestra f, const unsigned char *d, unsigned int n,
                         int x, int y);

/* BMP non compresso a 24 o 32 bit: e' il formato piu' semplice che esista, e
 * serve a far nascere la struttura contro qualcosa che si puo' verificare. */
static int leggi_bmp(ExFinestra f, const unsigned char *d, unsigned int n,
                     int x, int y)
{
    unsigned int off, larg, alt, bit, riga, i, j;

    if (n < 54 || d[0] != 'B' || d[1] != 'M') return 0;

    off  = (unsigned int)d[10] | ((unsigned int)d[11] << 8) |
           ((unsigned int)d[12] << 16) | ((unsigned int)d[13] << 24);
    larg = (unsigned int)d[18] | ((unsigned int)d[19] << 8) |
           ((unsigned int)d[20] << 16) | ((unsigned int)d[21] << 24);
    alt  = (unsigned int)d[22] | ((unsigned int)d[23] << 8) |
           ((unsigned int)d[24] << 16) | ((unsigned int)d[25] << 24);
    bit  = (unsigned int)d[28] | ((unsigned int)d[29] << 8);

    if (bit != 24 && bit != 32) return 0;
    if (larg == 0 || alt == 0 || larg > 4096 || alt > 4096) return 0;

    /* ! LE RIGHE SONO ALLINEATE A QUATTRO BYTE, e saltarlo da' un'immagine
     * che si inclina progressivamente verso il basso — un difetto che si
     * riconosce a colpo d'occhio, ma solo se si sa che esiste. */
    riga = ((larg * (bit / 8)) + 3u) & ~3u;
    if (off + riga * alt > n) return 0;

    for (j = 0; j < alt; j++) {
        /* ! E LE RIGHE STANNO SOTTOSOPRA: un BMP parte dal fondo. */
        const unsigned char *r = d + off + (alt - 1 - j) * riga;

        for (i = 0; i < larg; i++) {
            const unsigned char *p = r + i * (bit / 8);
            unsigned int c = ((unsigned int)p[2] << 16) |
                             ((unsigned int)p[1] << 8) | p[0];
            ex_riempi(f, x + (int)i, y + (int)j, 1, 1, c);
        }
    }
    return 1;
}

/* I formati che costano — PNG oggi, JPG e ICO domani — stanno in eximg.so, e
 * questa e' la riga che li porta dentro.
 *
 * ! SI APRE ALLA PRIMA IMMAGINE CHE NESSUNO HA RICONOSCIUTO, non all'avvio. Un
 * pannello, un orologio, una barra delle applicazioni non disegnano un PNG
 * mai: se il toolkit si collegasse alla libreria, la pagherebbero anche loro
 * — ed e' esattamente la ragione per cui eximg e' una libreria a parte invece
 * che due funzioni in piu' qui dentro. Aperta una volta, l'indirizzo resta.
 *
 * ! E SE eximg.so NON C'E', NON SI MUORE: si rende 0, cioe' «questo formato
 * non lo so leggere». Gli stub di exwin e exdlg gridano e muoiono, e li' e'
 * giusto — un programma che chiama ex_finestra() senza toolkit non puo' fare
 * niente. Qui invece il programma sa disegnare, sa leggere i BMP, e gli manca
 * un formato: farlo morire vorrebbe dire che installare mezza libreria grafica
 * spegne applicazioni che funzionerebbero.
 *
 * ! IL PERCORSO E' DOPPIO PER LA STESSA RAGIONE DEGLI STUB: su un sistema
 * installato le librerie stanno in /exwin/lib, avviando dal CD sotto /cdrom. */
static int leggi_eximg(ExFinestra f, const unsigned char *d, unsigned int n,
                       int x, int y)
{
    static const char *const dove[] = {
        "/exwin/lib/eximg.so",
        "/cdrom/exwin/lib/eximg.so"
    };
    static int (*carica)(const unsigned char *, unsigned int, EximgBitmap *);
    static void (*libera)(EximgBitmap *);
    static int cercata = 0;

    EximgBitmap bm;

    if (!cercata) {
        const ExLibTesta *t;

        cercata = 1;    /* ! PRIMA DEL TENTATIVO: se la libreria non c'e', non
                         * si torna a cercarla a ogni immagine. */
        t = exlib_apri_fra(dove, (int)(sizeof dove / sizeof dove[0]));
        if (t) {
            carica = (int (*)(const unsigned char *, unsigned int,
                              EximgBitmap *))exlib_simbolo(t, "eximg_carica");
            libera = (void (*)(EximgBitmap *))exlib_simbolo(t, "eximg_libera");
        }
    }

    /* Uno dei due senza l'altro vuol dire una eximg.so piu' vecchia di questo
     * toolkit: si rinuncia al formato invece di chiamare un indirizzo nullo. */
    if (!carica || !libera) return 0;

    if (!carica(d, n, &bm)) return 0;

    ex_pixmap(f, x, y, (int)bm.larghezza, (int)bm.altezza, bm.px, bm.larghezza);
    libera(&bm);
    return 1;
}

/* ! L'ORDINE CONTA: PRIMA CIO' CHE SI SA FARE IN CASA. leggi_eximg e' l'ultimo
 * perche' aprire una libreria costa, e non ha senso pagarlo per un BMP. */
static const ExLettore g_lettori[] = { leggi_bmp, leggi_eximg, 0 };

int ex_immagine(ExFinestra f, const char *percorso, int x, int y)
{
    int fd, n, k;
    unsigned char *d;
    unsigned int cap = 256u * 1024u;

    fd = open(percorso, O_RDONLY);
    if (fd < 0) return 0;

    d = (unsigned char *)malloc(cap);
    if (!d) { close(fd); return 0; }

    n = (int)read(fd, d, cap);
    close(fd);

    if (n <= 0) { free(d); return 0; }

    for (k = 0; g_lettori[k]; k++)
        if (g_lettori[k](f, d, (unsigned int)n, x, y)) { free(d); return 1; }

    free(d);
    return 0;
}
