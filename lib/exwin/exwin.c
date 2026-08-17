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
#define TERM_RIGA_MAX   128

typedef struct {
    unsigned int usato;
    ExFinestra   ogg;
    int          fd_in;         /* noi scriviamo qui, la shell legge */
    int          fd_out;        /* la shell scrive qui, noi leggiamo */
    int          pid;
    unsigned int cols, righe;
    char        *griglia;
    unsigned int cx, cy;
    char         riga[TERM_RIGA_MAX];
    unsigned int riga_len;
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
} Area;

static Area g_area[AREA_MAX];


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
static int term_tasto(Terminale *t, unsigned int c)
{
    if (c == '\n' || c == '\r') {
        t->riga[t->riga_len] = '\n';
        write(t->fd_in, t->riga, t->riga_len + 1);
        t->riga_len = 0;
        term_carattere(t, '\n');
        return 1;
    }
    if (c == '\b') {
        if (t->riga_len > 0) { t->riga_len--; term_carattere(t, '\b'); }
        return 1;
    }
    if (c < 0x20 || c > 0x7E) return 0;

    if (t->riga_len + 2 < TERM_RIGA_MAX) {
        t->riga[t->riga_len++] = (char)c;
        term_carattere(t, (char)c);
    }
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

    case CL_RIQUADRO:
        ex_riquadro_disegna(o->padre, x, y + 8, o->w, o->h - 8, EX_GRIGIO_SC);
        if (o->titolo[0]) {
            ex_riempi(o->padre, x + 6, y + 8,
                      (int)strlen(o->titolo) * 8 + 6, 1, EX_GRIGIO);
            ex_scrivi(o->padre, x + 9, y, o->titolo, EX_NERO);
        }
        break;

    case CL_SEPARATORE:
        ex_riempi(o->padre, x, y, o->w, 1, EX_GRIGIO_SC);
        ex_riempi(o->padre, x, y + 1, o->w, 1, EX_BIANCO);
        break;

    case CL_AREA: {
        Area *A = area_di(o);
        Oggetto *r = radice(o->padre);
        unsigned int k;
        int col_fuoco;

        ex_riempi(o->padre, x, y, o->w, o->h, EX_BIANCO);
        col_fuoco = (r && r->fuoco == (ExFinestra)(o - g_ogg + 1));
        ex_riquadro_disegna(o->padre, x, y, o->w, o->h,
                            col_fuoco ? EX_BLU : EX_GRIGIO_SC);
        if (!A) break;

        for (k = 0; k < A->righe && A->top + k < A->n; k++) {
            const char  *src = area_riga(A, A->top + k);
            unsigned int l   = (unsigned int)strlen(src);
            char         vis[AREA_COL_MAX];
            unsigned int c;
            int          ry = y + 2 + (int)k * AREA_RIGA_H;

            for (c = 0; c < A->cols && A->left + c < l && c + 1 < sizeof(vis); c++) {
                char ch = src[A->left + c];
                /* ! IL TAB SI MOSTRA COME UNO SPAZIO E RESTA UN TAB NEL TESTO.
                 * Espanderlo vorrebbe dire che una colonna sullo schermo non
                 * e' piu' un carattere nel testo, e allora cursore, clic e
                 * lunghezza della riga direbbero tre cose diverse. */
                vis[c] = (ch == '\t') ? ' ' : ch;
            }
            vis[c] = '\0';
            if (vis[0]) ex_scrivi(o->padre, x + 2, ry, vis, EX_NERO);
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
        ex_riquadro_disegna(o->padre, x, y, o->w, o->h,
                            col_fuoco ? EX_BLU : EX_GRIGIO_SC);
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
        ex_scrivi(o->padre, x + 6, y + (o->h - 16) / 2, o->titolo, EX_BIANCO);
        break;

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

        if (pipe(pin) != 0 || pipe(pout) != 0) {
            free(t->griglia); o->usato = 0; return 0;
        }

        /* Il figlio: stdin dalla nostra pipe, stdout e stderr nella sua. */
        red[0].fd = 0; red[0].flags = 0; red[0].percorso = 0; red[0].fd_padre = pin[0];
        red[1].fd = 1; red[1].flags = 0; red[1].percorso = 0; red[1].fd_padre = pout[1];
        red[2].fd = 2; red[2].flags = 0; red[2].percorso = 0; red[2].fd_padre = pout[1];

        sh_argv[0] = (char *)(titolo && titolo[0] ? titolo : "/bin/sh");
        sh_argv[1] = 0;

        t->pid = spawn_ex(sh_argv[0], sh_argv, 0, red, 3);

        /* ! SI CHIUDONO LE ESTREMITA' PASSATE AL FIGLIO, e non e' pulizia:
         * finche' le teniamo aperte la pipe conta uno scrittore vivo — noi —
         * e chi legge non vedra' mai la fine dei dati. E' l'errore classico
         * con le pipe, ed e' scritto in libc.h accanto a SpawnRedir. */
        close(pin[0]);
        close(pout[1]);

        if (t->pid < 0) { close(pin[1]); close(pout[0]); free(t->griglia);
                          o->usato = 0; return 0; }

        t->fd_in  = pin[1];
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
                | ((stile & EX_SOPRA)  ? WIN_ST_SOPRA  : 0);
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
        win_nome_zona(z.nome, r.id);
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

static int area_tasto(Area *A, unsigned int c)
{
    unsigned int passo = A->righe ? A->righe : 1;

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

    if (k & KBD_MOD_CTRL) return 0;

    o = ogg(r->fuoco);
    if (!o) return 0;

    if (o->classe == CL_TERMINALE) {
        Terminale *t = term_di(o);
        return t ? term_tasto(t, c) : 0;
    }

    /* ! LA LISTA CONSUMA SOLO CIO' CHE SA USARE. Frecce, PgSu/PgGiu, Home/End
     * e Invio sono suoi; una lettera no — e lasciarla passare e' quello che
     * permette a un'applicazione di dare una scorciatoia mentre la lista ha il
     * fuoco. Un controllo che mangia tutto e' un controllo che si prende
     * l'applicazione. */
    if (o->classe == CL_AREA) {
        Area *A = area_di(o);
        return A ? area_tasto(A, c) : 0;
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
            if (r && r->proc)
                r->proc(radice_h((ExFinestra)(o - g_ogg + 1)),
                        EXM_COMANDO, o->id, 0);
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
            if (tasto_al_fuoco(f, e.tasto)) {
                /* Consumato da una casella: si ridisegna e si aspetta ancora,
                 * invece di svegliare l'applicazione per ogni lettera. */
                ex_procedura_base(f, EXM_DISEGNA, 0, 0);
                continue;
            }
            m->msg = EXM_TASTO;
            m->wp  = e.tasto;
            return 1;
        case WIN_EV_MOUSE_SU:   m->msg = EXM_MOUSE_SU;  return 1;
        case WIN_EV_MOUSE_GIU: {
            /* ! IL CLIC SU UN PULSANTE DIVENTA EXM_COMANDO, e il messaggio
             * grezzo non arriva all'applicazione. E' cio' che distingue un
             * toolkit da un pannello di pixel: chi scrive l'applicazione
             * guarda l'id del pulsante, non le coordinate. */
            ExFinestra c = controllo_in(f, (int)e.x, (int)e.y);
            Oggetto *co = ogg(c);

            if (co) fuoco_metti(f, c);      /* il clic da' i tasti */

            if (co && co->classe == CL_PULSANTE) {
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
                    origine(co, &ox, &oy);
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
                    origine(co, &ox, &oy);
                    r = L->primo + (unsigned int)(((int)e.y - oy - 2) / LISTA_RIGA_H);
                    if (r < L->n) { L->sel = r; lista_segui(L); }
                }
                ex_procedura_base(f, EXM_DISEGNA, 0, 0);
                m->msg = EXM_COMANDO;
                m->wp  = co->id;
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
             * perche' quasi sempre l'ha gestito cambiando qualcosa. */
            ex_procedura_base(m->finestra, EXM_DISEGNA, 0, 0);
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

static const ExLettore g_lettori[] = { leggi_bmp, 0 };

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
