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
#include "exfont.h"     /* il lettore dei font bitmap: compilato qui dentro */
#include "exfont_ttf.h" /* il TrueType: sta in exfont.so, si apre a richiesta */

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

    /* La sveglia periodica: vedi ex_sveglia(). Zero = nessuna. */
    unsigned int  sveglia_ms;
    unsigned int  sveglia_quando;    /* uptime_ms della prossima */
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
#define LISTA_CAR_W       8    /* il passo del carattere: vedi ex_scrivi */

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

/* ! IL CONTROLLO CHE HA PRESO IL BOTTONE GIU' SE LO TIENE FINO AL SU, e non si
 * ricerca a ogni movimento. Cercandolo sotto il puntatore, chi trascina la
 * selezione oltre il bordo dell'area la vedrebbe passare al controllo accanto
 * — e con essa il cursore. Il trascinamento appartiene a chi l'ha cominciato,
 * che e' la stessa regola che il server applica alle finestre. */
static ExFinestra g_trascinato = 0;

/* ! IL PULSANTE TENUTO GIU', e serve perche' il comando parte al RILASCIO.
 * Fra la pressione e il rilascio il puntatore puo' uscire dal pulsante, e in
 * quel caso il comando non deve partire: senza ricordarsi QUALE fosse, al
 * rilascio non ci sarebbe modo di sapere se si sta alzando il dito dallo
 * stesso pulsante su cui lo si era posato. Vedi WIN_EV_MOUSE_SU. */
static ExFinestra g_premuto = 0;

/* ! LA RIGA SCELTA QUANDO IL DITO E' SCESO, per sapere al rilascio se il
 * trascinamento l'ha cambiata. Senza, chi sceglie una voce trascinando invece
 * che cliccando non lo direbbe a nessuno: la lista si vedrebbe cambiare la
 * barra blu e l'applicazione resterebbe ferma su quella di prima. */
static unsigned int g_tras_sel = 0;

/* =============================================================================
 * IL DOPPIO CLIC — riconosciuto qui, non dal server
 *
 * ! DUE CLIC SONO UN DOPPIO CLIC SOLO PER CHI LI INTERPRETA. Il server manda
 * pressioni e rilasci: quanto vicini debbano essere per «contare come uno» e'
 * una convenzione dell'interfaccia, non un fatto dell'hardware. Metterla nel
 * server vorrebbe dire un messaggio in piu' per ogni clic in una mailbox
 * profonda quattro, e una soglia unica imposta a tutti; qui costa due
 * variabili e un confronto, e ogni programma ne ha una copia sua, perche' i
 * dati di una libreria condivisa sono per processo (vedi il commento sugli
 * appunti piu' su).
 *
 * ! MA L'OROLOGIO NON E' QUELLO DI QUI, ED E' LA PARTE CHE COSTA. La prima
 * versione chiamava uptime_ms() al momento in cui l'evento veniva letto, e non
 * funzionava mai proprio dove serviva: al primo clic su una directory il file
 * manager LEGGE la directory — da un CD sono anche tre decimi di secondo — e
 * quel lavoro finiva dentro l'intervallo misurato. Due clic svelti risultavano
 * lenti, e dai pixel si vedeva solo «il doppio clic non fa niente». L'ora la
 * scrive il server dentro l'evento, nell'istante in cui il clic e' avvenuto:
 * vedi WinEvento in win_proto.h. Qui resta la SOGLIA, che e' una convenzione
 * dell'interfaccia e sta bene dov'e'.
 *
 * La risoluzione vera di quell'ora e' il tick del PIT, 10 ms: dentro una
 * soglia di 400 e' un errore del due e mezzo per cento, irrilevante rispetto
 * alla mano di chi clicca.
 *
 * ! LA DISTANZA CONTA QUANTO IL TEMPO. Due clic rapidi in due punti lontani
 * sono due decisioni diverse: chi sceglie una voce e subito dopo un'altra non
 * ha chiesto di aprire niente. Quattro pixel e' quanto si muove un mouse
 * mentre si preme di nuovo senza volerlo.
 * ============================================================================= */
#define DOPPIO_MS       400
#define DOPPIO_PIX      4

static unsigned int g_clic_ms = 0;
static int          g_clic_x = 0, g_clic_y = 0;
static ExFinestra   g_clic_ogg = 0;

static int doppio_clic(ExFinestra c, int x, int y, unsigned int ora)
{
    int          dx  = x - g_clic_x;
    int          dy  = y - g_clic_y;
    int          si;

    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;

    si = (g_clic_ogg == c) && (ora - g_clic_ms < DOPPIO_MS) &&
         (dx <= DOPPIO_PIX) && (dy <= DOPPIO_PIX);

    /* ! DOPO UN DOPPIO SI RICOMINCIA DA CAPO, e non e' pignoleria: senza
     * azzerare, un terzo clic vicino diventerebbe un secondo doppio clic e un
     * quarto un terzo — battere velocemente su una directory la aprirebbe piu'
     * volte di quante se ne sono chieste. */
    if (si) { g_clic_ogg = 0; g_clic_ms = 0; }
    else    { g_clic_ogg = c; g_clic_ms = ora; g_clic_x = x; g_clic_y = y; }

    return si;
}

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
/* Definita piu' in basso, dov'e' il resto del dialogo col server. */
static int server_trova(void);

unsigned int ex_appunti_metti(const char *testo, unsigned int n)
{
    Appunti     *ap = appunti();
    unsigned int i;

    if (!ap || !testo) return 0;
    if (n > sizeof(ap->testo) - 1) n = sizeof(ap->testo) - 1;

    for (i = 0; i < n; i++) ap->testo[i] = testo[i];
    ap->testo[n] = '\0';
    ap->n = n;
    return n;
}

unsigned int ex_appunti_prendi(char *out, unsigned int max)
{
    Appunti     *ap = appunti();
    unsigned int i, n;

    if (!out || max == 0) return 0;
    out[0] = '\0';
    if (!ap || ap->n == 0) return 0;

    n = ap->n;
    if (n > max - 1) n = max - 1;
    for (i = 0; i < n; i++) out[i] = ap->testo[i];
    out[n] = '\0';
    return n;
}

void ex_spegni_scrivania(void)
{
    if (!server_trova()) return;
    ipc_send((unsigned int)g_server, WIN_MSG_SPEGNI, 0, 0);
}

void ex_fuoco_via(ExFinestra f)
{
    Oggetto *r = radice(f);

    if (!r) return;
    r->fuoco = 0;
}

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

/* =============================================================================
 * ! FONDERE VUOL DIRE LEGGERE IL PIXEL CHE C'E' GIA', e fino a oggi il disegno
 * di questo toolkit non ha mai letto niente: scriveva e basta. E' il cambio
 * che l'antialiasing porta con se' e che vale la pena dire, perche' ha una
 * conseguenza — cio' che sta sotto una lettera deve essere gia' disegnato
 * quando la lettera arriva. Per un'etichetta su un pannello grigio e' sempre
 * vero; per chi disegnasse il testo prima dello sfondo, non piu'.
 *
 * ! LA DIVISIONE PER 255 NON SI FA, e non e' pignoleria da ottimizzatore:
 * questa funzione gira per ogni pixel di ogni lettera di ogni ridisegno, e su
 * un Pentium 133 una divisione costa quaranta cicli. L'identita' usata qui —
 * sommare al numero il suo ottavo di spostamento e poi spostare — rende
 * esattamente v/255 per ogni v che puo' uscire da questo prodotto, con due
 * addizioni e due spostamenti.
 * ============================================================================= */
static unsigned int fondi_canale(unsigned int davanti, unsigned int dietro,
                                 unsigned int a)
{
    unsigned int v = davanti * a + dietro * (255u - a) + 128u;

    return (v + (v >> 8)) >> 8;
}

static void punto_fuso(Oggetto *r, int x, int y, unsigned int c, unsigned int a)
{
    unsigned int i, d;

    if (!r || !r->pix) return;
    if (x < 0 || y < 0 || x >= r->w || y >= r->h) return;
    if (a == 0) return;

    i = (unsigned int)y * r->passo_px + (unsigned int)x;

    /* Copertura piena: si scrive e basta, come si e' sempre fatto. E' il caso
     * piu' frequente dentro una lettera, e non deve pagare la fusione. */
    if (a >= 255) { r->pix[i] = c; return; }

    d = r->pix[i];
    r->pix[i] =
        (fondi_canale((c >> 16) & 0xFFu, (d >> 16) & 0xFFu, a) << 16) |
        (fondi_canale((c >>  8) & 0xFFu, (d >>  8) & 0xFFu, a) <<  8) |
         fondi_canale( c        & 0xFFu,  d        & 0xFFu, a);
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

/* =============================================================================
 * I FONT
 *
 * ! IL FONT DI SISTEMA E' DESCRITTO DALLA STESSA STRUTTURA DI QUELLI CHE SI
 * CARICANO, e non e' un vezzo: e' cio' che tiene UNO SOLO il ciclo che accende
 * i pixel. Con due strade — una per l'8x16 compilato dentro e una per i file —
 * la seconda nascerebbe copiando la prima, e da quel momento un difetto andrebbe
 * corretto due volte. Qui l'8x16 e' semplicemente un font a larghezza fissa 8,
 * alto 16, che sta in memoria invece che su disco.
 *
 * ! LA LINEA DI BASE E' 14, ED E' MISURATA DAI GLIFI, non scelta. A, H, X
 * finiscono alla riga 13; la coda di Q scende alla 14 e le discendenti di g, p,
 * q, y arrivano alla 15. Serve a chi deve allineare due font diversi sulla
 * stessa riga di testo — cioe' al browser, il giorno che ne usera' due.
 *
 * ! LE LARGHEZZE SONO ZERO PERCHE' IL FONT E' FISSO, e chi legge questa riga
 * deve poterlo verificare senza fidarsi: exfont_larghezza_car() con `fisso`
 * rende larg_max e non tocca mai la tabella. Un font non fisso con le larghezze
 * a zero sarebbe invece un disegno tutto sovrapposto nell'angolo.
 * ============================================================================= */
/* ! OTTO NON BASTAVANO, E IL MODO IN CUI SI SCOPRIVA ERA IL PEGGIORE. Il nono
 * `ex_font_apri` rendeva 0 — che E' il font di sistema, non un errore — quindi
 * il programma continuava e disegnava con un carattere diverso. `fontprova` su
 * una directory con dodici facce ne apriva otto e dichiarava «NON aperto» le
 * altre quattro: sembrava che quattro FILE fossero guasti, e invece era la
 * tabella che era finita.
 *
 * ! MA IL NUMERO NON E' IL PUNTO: OGNI VOCE TENEVA UNA COPIA DEL FILE. Un
 * TrueType di Liberation pesa fra i 280 e i 410 kilobyte, e il browser apre la
 * STESSA faccia a cinque o sei corpi diversi — cinque copie degli stessi
 * quattrocento kilobyte, su una macchina che ne ha trentadue milioni. Alzare
 * il limite e basta avrebbe trasformato un difetto visibile in un esaurimento
 * di memoria, che si vede molto piu' tardi e molto peggio.
 *
 * Adesso i BYTE del file stanno in una riserva a parte, con un contatore: la
 * stessa faccia a sei corpi e' un file solo in memoria e sei voci qui. E le
 * voci possono essere molte, perche' una voce da sola non pesa niente. */
#define FONT_MAX        48

/* ! E DODICI FILE ERANO «LE NOSTRE DODICI FACCE», cioe' un CONTO e non un
 * confine: lo stesso errore dell'otto qui sopra, rifatto un piano piu' in la'.
 * In /exwin/font i file sono TREDICI — le dodici Liberation piu' DejaVuSans,
 * che sta li' come riserva per i caratteri che le altre non hanno.
 *
 * ! E IL TREDICESIMO E' IL PEGGIORE CHE POTESSE TOCCARE. I file si aprono
 * nell'ordine in cui la directory li rende, cioe' alfabetico, e ultimo di
 * quell'ordine viene LiberationSerif-Regular.ttf: il carattere con cui il
 * browser scrive il testo normale. `fontprova` lo dichiarava «NON CARICATO» in
 * rosso — sembrava un file guasto, o una copia sbagliata dall'installatore —
 * mentre gli altri dodici si aprivano benissimo. Non era il file: era questa
 * tabella, finita.
 *
 * ! VENTIQUATTRO NON E' «TREDICI PIU' UN MARGINE». Una voce qui pesa
 * centoquaranta byte e NON tiene piu' i byte del font — quelli stanno nella
 * riserva condivisa, contati — quindi il tetto puo' stare largo senza costare
 * niente. Chi copia un .ttf dentro /exwin/font, che fontprova.c dichiara come
 * cosa da fare, non deve tornare qui a contare. */
#define FILE_MAX        24

typedef struct {
    int            usato;
    int            file;        /* indice in g_file, -1 = nessuno */
    int            corpo;       /* a che misura e' aperto: serve al ripiego */
    ExFontDati     f;           /* se e' un EXFN */
    ExTtf          ttf;         /* se e' un TrueType: sta in exfont.so */
} FontAperto;

static FontAperto g_font[FONT_MAX];

/* I byte dei file, condivisi fra tutti i corpi della stessa faccia. */
typedef struct {
    char           percorso[128];
    unsigned char *dati;
    int            n;
    int            quanti;      /* quante voci di g_font lo usano */
} FontFile;

static FontFile g_file[FILE_MAX];

static const ExFontDati g_sistema = {
    16,     /* altezza   */
    14,     /* base      */
    0,      /* primo     */
    256,    /* quanti    */
    8,      /* larg_max  */
    1,      /* passo     */
    1,      /* fisso     */
    0,      /* larghezze: non si guarda, il font e' fisso */
    font8x16
};

/* ! UN MANICO CHE NON VALE PIU' RENDE IL FONT DI SISTEMA, non zero. Chi scrive
 * un'etichetta con un font che nel frattempo e' stato chiuso vedra' un
 * carattere diverso da quello che voleva; se rendesse zero non vedrebbe NIENTE,
 * e una finestra vuota non dice da dove cominciare a cercare. */
static const ExFontDati *font_di(ExFont h)
{
    if (h == 0 || h > FONT_MAX) return &g_sistema;
    if (!g_font[h - 1].usato)   return &g_sistema;
    return &g_font[h - 1].f;
}

/* =============================================================================
 * IL TRUETYPE STA IN exfont.so, E SI APRE AL PRIMO FONT CHE LO CHIEDE
 *
 * ! STESSO PATTO DI eximg.so, per la stessa ragione. Il contenitore TrueType,
 * l'appiattimento delle curve, il rasterizzatore e la cache sono «qualcosa che
 * costa»: una barra delle applicazioni o un orologio non ne aprono uno mai, e
 * non devono pagarlo. Il lettore dei font BITMAP invece sta qui dentro, perche'
 * quello lo usano tutti — vedi il commento in lib/exfont/exfont.h.
 *
 * ! E SE exfont.so NON C'E', NON SI MUORE: il font non si apre, ex_font_apri()
 * rende 0, e zero e' il font di sistema. Il testo esce con un carattere diverso
 * da quello voluto e il programma continua. Farlo morire vorrebbe dire che
 * installare mezza libreria grafica spegne applicazioni che funzionerebbero.
 * ============================================================================= */
static struct {
    int    cercata;
    ExTtf  (*apri)(const unsigned char *, unsigned int, int);
    void   (*chiudi)(ExTtf);
    int    (*altezza)(ExTtf);
    int    (*base)(ExTtf);
    int    (*larghezza_car)(ExTtf, unsigned int);
    const unsigned char *(*glifo)(ExTtf, unsigned int, int *, int *, int *, int *);
    int    (*ha_glifo)(ExTtf, unsigned int);
} T;

static int exfont_pronta(void)
{
    static const char *const dove[] = {
        "/exwin/lib/exfont.so",
        "/cdrom/exwin/lib/exfont.so"
    };
    const ExLibTesta *t;

    if (!T.cercata) {
        T.cercata = 1;      /* prima del tentativo: se manca, non si ricerca */
        t = exlib_apri_fra(dove, (int)(sizeof dove / sizeof dove[0]));
        if (t) {
            T.apri = (ExTtf (*)(const unsigned char *, unsigned int, int))
                     exlib_simbolo(t, "exttf_apri");
            T.chiudi = (void (*)(ExTtf))exlib_simbolo(t, "exttf_chiudi");
            T.altezza = (int (*)(ExTtf))exlib_simbolo(t, "exttf_altezza");
            T.base = (int (*)(ExTtf))exlib_simbolo(t, "exttf_base");
            T.larghezza_car = (int (*)(ExTtf, unsigned int))
                              exlib_simbolo(t, "exttf_larghezza_car");
            T.glifo = (const unsigned char *(*)(ExTtf, unsigned int,
                                                int *, int *, int *, int *))
                      exlib_simbolo(t, "exttf_glifo");

            /* ! QUESTO NON SI PRETENDE, ed e' voluto: una exfont.so piu'
             * vecchia di questo toolkit non ce l'ha, e senza di lui il
             * ripiego non si fa — ma tutto il resto continua a funzionare
             * esattamente come prima. Metterlo fra gli obbligatori qui sotto
             * avrebbe reso illeggibile ogni testo su un sistema misto. */
            T.ha_glifo = (int (*)(ExTtf, unsigned int))
                         exlib_simbolo(t, "exttf_ha_glifo");
        }
    }

    /* Uno solo senza gli altri vuol dire una exfont.so piu' vecchia di questo
     * toolkit: si rinuncia invece di chiamare un indirizzo nullo. */
    return T.apri && T.chiudi && T.altezza && T.base &&
           T.larghezza_car && T.glifo;
}

/* ! IL FORMATO SI RICONOSCE DAI PRIMI BYTE, NON DAL NOME. Un file di font
 * arriva anche dalla rete, e li' il nome lo sceglie chi sta dall'altra parte.
 * 0x00010000 e' TrueType, 'true' e' la variante di Apple. */
static int e_truetype(const unsigned char *d, int n)
{
    if (n < 4) return 0;
    if (d[0] == 0 && d[1] == 1 && d[2] == 0 && d[3] == 0) return 1;
    return d[0] == 't' && d[1] == 'r' && d[2] == 'u' && d[3] == 'e';
}

/* Legge un file intero in memoria fresca. Rende il buffer e scrive quanti byte
 * sono, oppure 0. */
static unsigned char *file_intero(const char *percorso, int *quanti)
{
    int            fd, n, k;
    long           misura;
    unsigned char *d;

    *quanti = 0;

    fd = open(percorso, O_RDONLY);
    if (fd < 0) return 0;

    /* ! LA MISURA SI CHIEDE AL FILE, NON SI INDOVINA. Qui c'era un tetto di
     * 256 KB copiato da ex_immagine(), e i font Liberation ne pesano da 280 a
     * 420: si leggevano TRONCATI, e non se ne apriva nemmeno uno. Il difetto
     * non si e' visto come «buffer piccolo» ma come «nessun font si carica»,
     * ed e' stato il controllo di troncamento dentro ttf_apri() a fermarli —
     * cioe' la cosa giusta e' successa per la ragione giusta, ma il tetto
     * restava sbagliato.
     *
     * ! IL TETTO C'E' ANCORA, ED E' PIU' ALTO PERCHE' NON SI CARICA UN FILE
     * QUALUNQUE IN MEMORIA. Un font di sedici megabyte non e' un font: e' un
     * modo di far finire la memoria a chi lo apre. */
    misura = lseek(fd, 0, SEEK_END);
    if (misura <= 0 || misura > 8L * 1024L * 1024L) { close(fd); return 0; }
    (void)lseek(fd, 0, SEEK_SET);

    d = (unsigned char *)malloc((unsigned int)misura);
    if (!d) { close(fd); return 0; }

    n = 0;
    while (n < (int)misura &&
           (k = (int)read(fd, d + n, (unsigned int)((int)misura - n))) > 0)
        n += k;
    close(fd);

    /* Letto meno di quanto il file dichiara: meglio niente che un font a meta'. */
    if (n != (int)misura) { free(d); return 0; }

    *quanti = n;
    return d;
}

/* =============================================================================
 * ! UN FONT SI CERCA IN DUE POSTI, COME TUTTO IL RESTO DI /exwin, e questa
 * riga mancava. Su un sistema installato l'albero sta in /exwin; avviando dal
 * FLOPPY col CD dentro, la radice e' il floppy e /exwin sta sotto /cdrom. E'
 * la stessa regola che seguono gli stub del toolkit, eximg, exfont.so e
 * l'elenco delle applicazioni del program manager.
 *
 * ! IL DIFETTO E' PASSATO PERCHE' LE PROVE ERANO TUTTE CON EXOS_NO_FLOPPY=1,
 * dove la radice E' il CD e quindi /exwin/font esiste davvero. Avviando in
 * modo normale non se ne apriva NESSUNO. E' il genere di cosa che una prova
 * fatta sempre nella stessa configurazione non puo' trovare.
 *
 * ! E STA QUI, NON DENTRO LE APPLICAZIONI. Ogni programma che apre un font
 * avrebbe dovuto conoscere questa faccenda e ricopiarla; chi se ne dimentica
 * scrive un programma che funziona sul suo banco e non sul CD di qualcun
 * altro. Un posto solo, e le applicazioni scrivono il percorso naturale.
 * ============================================================================= */
/* Rende l'indice del file in riserva, caricandolo se serve, o -1. Chi lo
 * ottiene ha gia' un riferimento contato. */
static int file_font(const char *percorso)
{
    int            i, slot = -1, n;
    unsigned char *d;

    for (i = 0; i < FILE_MAX; i++) {
        if (g_file[i].dati && strcmp(g_file[i].percorso, percorso) == 0) {
            g_file[i].quanti++;
            return i;
        }
        if (!g_file[i].dati && slot < 0) slot = i;
    }
    if (slot < 0) return -1;

    d = file_intero(percorso, &n);

    /* ! IL RIPIEGO SU /cdrom STA QUI E NON NELLE APPLICAZIONI. Avviando dal CD
     * la radice E' il CD e «/exwin/font/...» si apre da solo; ma un programma
     * del CD lanciato mentre gira un sistema installato deve trovare i propri
     * dati sotto /cdrom, e chi scrive il programma non deve saperlo. */
    if (!d && percorso[0] == '/') {
        char alt[160];

        strcpy(alt, "/cdrom");
        strncat(alt, percorso, sizeof(alt) - 8);
        d = file_intero(alt, &n);
    }

    /* =========================================================================
     * ! E IL SECONDO RIPIEGO E' IL NOME TUTTO MINUSCOLO, che e' come i font si
     * sono trovati sui dischi installati fino ad agosto 2026.
     *
     * L'installatore abbassava OGNI nome che copiava — regola nata sul floppy,
     * dove FAT in 8.3 rende «KERNEL.BIN» e il caso vero non esiste piu'. Dal CD
     * pero' i nomi vengono da Joliet, che il caso lo conserva: sul disco
     * finivano dodici «liberationserif-regular.ttf» mentre chiunque li apre li
     * chiede con le maiuscole. Su ext2, dove il caso conta, non si apriva
     * NIENTE — e la pagina usciva tutta col font di sistema.
     *
     * ! L'INSTALLATORE E' STATO CORRETTO, MA I DISCHI GIA' INSTALLATI RESTANO
     * COM'ERANO. Questo ripiego e' il ponte: costa una `open` in piu' solo
     * quando la prima e' gia' fallita, e vale anche per chi si copia un .ttf a
     * mano passando da una chiavetta FAT.
     * ===================================================================== */
    if (!d) {
        char alt[160];
        int  i, taglio = 0;

        for (i = 0; percorso[i] && i < (int)sizeof(alt) - 1; i++) {
            alt[i] = percorso[i];
            if (percorso[i] == '/') taglio = i + 1;
        }
        alt[i] = '\0';

        for (i = taglio; alt[i]; i++)
            if (alt[i] >= 'A' && alt[i] <= 'Z')
                alt[i] = (char)(alt[i] - 'A' + 'a');

        if (strcmp(alt, percorso) != 0) d = file_intero(alt, &n);
    }

    if (!d || n <= 0) return -1;

    strncpy(g_file[slot].percorso, percorso, sizeof(g_file[slot].percorso) - 1);
    g_file[slot].percorso[sizeof(g_file[slot].percorso) - 1] = '\0';
    g_file[slot].dati   = d;
    g_file[slot].n      = n;
    g_file[slot].quanti = 1;
    return slot;
}

static void file_font_lascia(int i)
{
    if (i < 0 || i >= FILE_MAX || !g_file[i].dati) return;
    if (--g_file[i].quanti > 0) return;

    free(g_file[i].dati);
    g_file[i].dati = 0;
    g_file[i].n    = 0;
    g_file[i].percorso[0] = '\0';
}

ExFont ex_font_apri(const char *percorso, int corpo)
{
    int slot, fi;

    if (!percorso) return 0;

    for (slot = 0; slot < FONT_MAX && g_font[slot].usato; slot++) { }
    if (slot >= FONT_MAX) return 0;

    fi = file_font(percorso);
    if (fi < 0) return 0;

    if (e_truetype(g_file[fi].dati, g_file[fi].n)) {
        if (!exfont_pronta()) { file_font_lascia(fi); return 0; }

        /* Un corpo non chiesto vuol dire «quello di sistema», che e' 16: un
         * font scalabile una misura deve pur averla. */
        g_font[slot].ttf = T.apri(g_file[fi].dati, (unsigned int)g_file[fi].n,
                                  corpo > 0 ? corpo : 16);
        if (!g_font[slot].ttf) { file_font_lascia(fi); return 0; }
    } else if (!exfont_apri(g_file[fi].dati, (unsigned int)g_file[fi].n,
                            &g_font[slot].f)) {
        file_font_lascia(fi);
        return 0;
    }

    g_font[slot].file  = fi;
    g_font[slot].corpo = corpo > 0 ? corpo : 16;
    g_font[slot].usato = 1;
    return (ExFont)(slot + 1);
}

/* =============================================================================
 * ex_font_trova / ex_font_nome
 * ========================================================================== */

/* Le dodici facce che EX-OS porta con se'. L'ordine e' famiglia * 4 + stile,
 * con lo stile = grassetto + corsivo * 2: cosi' l'indice e' un conto e non una
 * ricerca. */
static const char *const FACCE[12] = {
    "/exwin/font/LiberationSerif-Regular.ttf",
    "/exwin/font/LiberationSerif-Bold.ttf",
    "/exwin/font/LiberationSerif-Italic.ttf",
    "/exwin/font/LiberationSerif-BoldItalic.ttf",
    "/exwin/font/LiberationSans-Regular.ttf",
    "/exwin/font/LiberationSans-Bold.ttf",
    "/exwin/font/LiberationSans-Italic.ttf",
    "/exwin/font/LiberationSans-BoldItalic.ttf",
    "/exwin/font/LiberationMono-Regular.ttf",
    "/exwin/font/LiberationMono-Bold.ttf",
    "/exwin/font/LiberationMono-Italic.ttf",
    "/exwin/font/LiberationMono-BoldItalic.ttf"
};

#define TROVA_MAX  24

static struct {
    unsigned char usato, fam, stile;
    short         corpo;
    ExFont        f;
} g_trovati[TROVA_MAX];

static int indice_faccia(int famiglia, int grassetto, int corsivo)
{
    int fam = famiglia;
    int st  = (grassetto ? 1 : 0) + (corsivo ? 2 : 0);

    if (fam < 0 || fam > 2) fam = EX_FAM_SANS;
    return fam * 4 + st;
}

const char *ex_font_nome(int famiglia, int grassetto, int corsivo)
{
    return FACCE[indice_faccia(famiglia, grassetto, corsivo)];
}

ExFont ex_font_trova(int famiglia, int corpo, int grassetto, int corsivo)
{
    int    fam = (famiglia < 0 || famiglia > 2) ? EX_FAM_SANS : famiglia;
    int    st  = (grassetto ? 1 : 0) + (corsivo ? 2 : 0);
    int    i, slot = -1;
    ExFont f;

    if (corpo <= 0) corpo = 16;

    for (i = 0; i < TROVA_MAX; i++) {
        if (g_trovati[i].usato && g_trovati[i].fam == (unsigned char)fam &&
            g_trovati[i].stile == (unsigned char)st &&
            g_trovati[i].corpo == (short)corpo)
            return g_trovati[i].f;
        if (!g_trovati[i].usato && slot < 0) slot = i;
    }

    /* ! I RIPIEGHI IN ORDINE, e ognuno e' una rinuncia dichiarata: la faccia
     * chiesta, la normale della stessa famiglia, il sans normale. L'ultimo
     * ripiego e' 0, che E' il font di sistema — non un errore. */
    f = ex_font_apri(FACCE[fam * 4 + st], corpo);
    if (!f && st != 0) f = ex_font_apri(FACCE[fam * 4], corpo);
    if (!f && fam != EX_FAM_SANS) f = ex_font_apri(FACCE[EX_FAM_SANS * 4], corpo);

    if (slot >= 0) {
        g_trovati[slot].usato = 1;
        g_trovati[slot].fam   = (unsigned char)fam;
        g_trovati[slot].stile = (unsigned char)st;
        g_trovati[slot].corpo = (short)corpo;
        g_trovati[slot].f     = f;
    }
    return f;
}

void ex_font_chiudi(ExFont h)
{
    if (h == 0 || h > FONT_MAX) return;
    if (!g_font[h - 1].usato) return;

    /* ! IL FONT SI CHIUDE PRIMA DI LIBERARE I SUOI BYTE, e l'ordine conta: la
     * cache dentro exfont.so tiene puntatori DENTRO questo buffer (vedi
     * exfont_ttf.h, «non si copia»). Liberarlo prima lascerebbe la libreria a
     * leggere memoria restituita, e il guasto comparirebbe alla prossima
     * allocazione di qualcun altro. */
    if (g_font[h - 1].ttf && T.chiudi) T.chiudi(g_font[h - 1].ttf);

    /* ! I BYTE SE NE VANNO SOLO QUANDO NON LI USA PIU' NESSUNO. Erano di
     * questa voce e basta; adesso la stessa faccia a corpi diversi li
     * condivide, e liberarli qui toglierebbe il file da sotto agli altri
     * corpi — che continuerebbero a disegnare da memoria restituita. */
    file_font_lascia(g_font[h - 1].file);
    memset(&g_font[h - 1], 0, sizeof(g_font[h - 1]));
    g_font[h - 1].file = -1;
}

/* Il TrueType di quel manico, o 0 se e' un bitmap. Un posto solo per la
 * domanda: i quattro rami qui sotto la fanno tutti. */
static ExTtf ttf_di(ExFont h)
{
    if (h == 0 || h > FONT_MAX) return 0;
    if (!g_font[h - 1].usato) return 0;
    return g_font[h - 1].ttf;
}

/* =============================================================================
 * IL FONT DI RIPIEGO
 *
 * ! LIBERATION COPRE DUEMILATRECENTO CODICI, DEJAVU QUASI SEIMILA, e la
 * differenza si vede sulla colonna delle lingue di Wikipedia: col solo
 * Liberation l'arabo esce come una fila di collegamenti VUOTI — sottolineati e
 * senza lettere — e chi guarda pensa che il collegamento sia rotto, non che
 * manchi un carattere.
 *
 * ! MA NON SI SOSTITUISCE IL FONT: SI RIPIEGA CARATTERE PER CARATTERE. Sono
 * due cose diverse. Sostituirlo vorrebbe dire cambiare l'aspetto di OGNI
 * pagina — le forme, le larghezze, l'impaginazione — per via di una manciata
 * di caratteri che quasi nessuna pagina usa. Ripiegare lascia il testo com'e' e
 * riempie solo i buchi, che e' quel che fa ogni sistema con un fontconfig.
 *
 * ! E RESTA FUORI IL CINESE, IL GIAPPONESE, IL COREANO, il devanagari e il
 * thai: DejaVu non li ha, e chi li ha (Noto CJK) pesa venti megabyte su un CD
 * che ne pesa dieci. E' un limite DICHIARATO, non una dimenticanza.
 * ========================================================================== */
#define RIPIEGO_FILE  "/exwin/font/DejaVuSans.ttf"
#define RIPIEGO_MAX   8

static struct { int corpo; ExFont f; } g_ripiego[RIPIEGO_MAX];
static int g_ripiego_n = 0;
static int g_ripiego_no = 0;    /* il file non c'e': non si richiede piu' */

/* Il ripiego a quel corpo, aperto la prima volta che serve davvero.
 *
 * ! NON SI APRE ALL'AVVIO, e non e' pigrizia: sono settecento kilobyte, e la
 * stragrande maggioranza delle pagine non ha un solo carattere che li chieda.
 * Chi non ne ha bisogno non li paga. */
static ExTtf ripiego_ttf(int corpo)
{
    int i;

    if (g_ripiego_no || corpo <= 0) return 0;

    for (i = 0; i < g_ripiego_n; i++)
        if (g_ripiego[i].corpo == corpo) return ttf_di(g_ripiego[i].f);

    if (g_ripiego_n >= RIPIEGO_MAX) return 0;

    {
        ExFont f = ex_font_apri(RIPIEGO_FILE, corpo);

        /* ex_font_apri rende 0 anche per «il file non c'e'»: si smette di
         * chiedere, o ogni parola tornerebbe a cercarlo sul disco. */
        if (!f) { if (g_ripiego_n == 0) g_ripiego_no = 1; return 0; }

        g_ripiego[g_ripiego_n].corpo = corpo;
        g_ripiego[g_ripiego_n].f     = f;
        g_ripiego_n++;
        return ttf_di(f);
    }
}

/* Quale font disegna DAVVERO questo carattere: il suo, o il ripiego.
 *
 * ! LA MISURA E IL DISEGNO CHIAMANO QUESTA STESSA FUNZIONE. Se si separassero
 * — uno che ripiega e l'altro no — le larghezze non tornerebbero e il testo si
 * sovrapporrebbe, con l'aria di un difetto d'impaginazione. E' lo stesso
 * motivo per cui il decodificatore UTF-8 sta in un posto solo. */
static ExTtf ttf_per_codice(ExFont h, ExTtf suo, unsigned int ch)
{
    ExTtf r;

    if (!suo || !T.ha_glifo) return suo;
    if (T.ha_glifo(suo, ch)) return suo;

    /* Lo spazio e i caratteri di servizio non si ripiegano: non hanno un
     * disegno da nessuna parte, e cercarlo aprirebbe il ripiego per niente. */
    if (ch < 0x80) return suo;

    r = ripiego_ttf(h && h <= FONT_MAX ? g_font[h - 1].corpo : 0);
    if (r && r != suo && T.ha_glifo(r, ch)) return r;
    return suo;
}

int ex_font_altezza(ExFont h)
{
    ExTtf t = ttf_di(h);

    if (t) return T.altezza(t);
    return (int)font_di(h)->altezza;
}

int ex_font_base(ExFont h)
{
    ExTtf t = ttf_di(h);

    if (t) return T.base(t);
    return (int)font_di(h)->base;
}

/* =============================================================================
 * UTF-8 — perche' senza, una pagina web moderna e' illeggibile
 *
 * ! IL PROBLEMA NON E' TEORICO, SI VEDE. Il browser su una pagina vera
 * mostrava «RISC-V â» dove c'era un trattino lungo: il testo era UTF-8, il
 * disegno prendeva un BYTE per volta, e i tre byte del trattino diventavano
 * tre glifi presi da dove capita. Tutto il web di oggi e' UTF-8.
 *
 * ! MA LE STRINGHE DI EX-OS NON LO SONO, e questa e' la parte delicata: la
 * tastiera emette CP437 — la `a` accentata e' il byte 0x85 — e un editor che
 * mostra un file scritto qui dentro non deve peggiorare. Percio' la regola e'
 * INDULGENTE, come quella di un browser vero:
 *
 *     sequenza UTF-8 valida  ->  si decodifica
 *     byte che non ne fa parte  ->  vale per se stesso (CP437/Latin-1)
 *
 * Un byte CP437 isolato non forma quasi mai una sequenza valida, quindi le due
 * cose convivono senza che nessuno debba dichiarare la codifica.
 *
 * ! E LA MISURA DEVE DECODIFICARE COME IL DISEGNO. Se ex_larghezza_testo conta
 * i byte e ex_scrivi_con conta i caratteri, l'impaginazione del browser e il
 * disegno non sono piu' d'accordo: il testo si sovrappone o lascia buchi, e il
 * difetto sembra dell'impaginazione. Le due funzioni usano lo stesso
 * decodificatore, ed e' il motivo per cui sta qui in mezzo.
 * ============================================================================= */
static unsigned int prossimo_codice(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    unsigned int         c = s[0], n = 0, i, v;

    if (c < 0x80) { *p = (const char *)(s + 1); return c; }

    if      ((c & 0xE0) == 0xC0) { n = 1; v = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { n = 2; v = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { n = 3; v = c & 0x07; }
    else                         { *p = (const char *)(s + 1); return c; }

    for (i = 1; i <= n; i++) {
        if ((s[i] & 0xC0) != 0x80) {        /* troncata o non e' UTF-8 */
            *p = (const char *)(s + 1);
            return c;                        /* il byte vale per se' */
        }
        v = (v << 6) | (unsigned int)(s[i] & 0x3F);
    }
    *p = (const char *)(s + n + 1);
    return v;
}

/* Da codice Unicode al byte che il font 8x16 disegna (CP437).
 *
 * ! IL FONT DI SISTEMA HA 256 GLIFI E NON DI PIU', quindi qui si SCEGLIE cosa
 * perdere. Le lettere accentate ci sono davvero; i segni tipografici del web —
 * trattini lunghi, virgolette curve, puntini di sospensione — non ci sono, e
 * si sostituiscono con il loro parente ASCII. Meglio un trattino corto al posto
 * di uno lungo che tre glifi a caso: e' la stessa scelta che il progetto ha
 * gia' fatto rendendo ASCII le proprie stringhe. */
static unsigned char codice_a_cp437(unsigned int u)
{
    static const struct { unsigned short u; unsigned char b; } T437[] = {
        {0x00E0,0x85},{0x00E8,0x8A},{0x00E9,0x82},{0x00EC,0x8D},{0x00F2,0x95},
        {0x00F9,0x97},{0x00E1,0xA0},{0x00ED,0xA1},{0x00F3,0xA2},{0x00FA,0xA3},
        {0x00E2,0x83},{0x00EA,0x88},{0x00EE,0x8C},{0x00F4,0x93},{0x00FB,0x96},
        {0x00E4,0x84},{0x00EB,0x89},{0x00EF,0x8B},{0x00F6,0x94},{0x00FC,0x81},
        {0x00E7,0x87},{0x00F1,0xA4},{0x00C7,0x80},{0x00D1,0xA5},{0x00C4,0x8E},
        {0x00D6,0x99},{0x00DC,0x9A},{0x00C9,0x90},{0x00DF,0xE1},{0x00B0,0xF8},
        {0x00A3,0x9C},{0x00A5,0x9D},{0x00A7,0x15},{0x00BF,0xA8},{0x00A1,0xAD},
        {0x00AB,0xAE},{0x00BB,0xAF},{0x00BD,0xAB},{0x00BC,0xAC},{0x00B5,0xE6},
        /* i segni del web che il font non ha: il parente ASCII piu' vicino */
        {0x2013,'-'}, {0x2014,'-'}, {0x2018,'\''},{0x2019,'\''},{0x201C,'"'},
        {0x201D,'"'}, {0x2026,'.'}, {0x2022,'*'}, {0x2192,'>'}, {0x00A0,' '},
        {0x00AD,'-'}, {0x2212,'-'}, {0x00D7,'x'}, {0x00F7,'/'}
    };
    unsigned int i;

    if (u < 0x80) return (unsigned char)u;
    for (i = 0; i < sizeof(T437) / sizeof(T437[0]); i++)
        if (T437[i].u == u) return T437[i].b;

    /* ! UN CODICE CHE NON SI SA DISEGNARE DIVENTA '?', NON SPARISCE. Un buco
     * silenzioso fa sembrare la pagina scritta male; un punto interrogativo
     * dice che li' c'era qualcosa che non sappiamo mostrare. */
    return (u < 0x100) ? (unsigned char)u : (unsigned char)'?';
}

int ex_larghezza_testo(ExFont h, const char *s)
{
    ExTtf t = ttf_di(h);
    int   w = 0;

    if (!s) return 0;

    if (!t) {
        const ExFontDati *fo = font_di(h);

        while (*s) w += (int)exfont_larghezza_car(fo, codice_a_cp437(prossimo_codice(&s)));
        return w;
    }

    while (*s) {
        unsigned int ch = prossimo_codice(&s);

        w += T.larghezza_car(ttf_per_codice(h, t, ch), ch);
    }
    return w;
}

/* Quanto e' larga una stringa nel font di sistema. Dentro il toolkit si scrive
 * spesso, e `ex_larghezza_testo(0, s)` ripetuto trenta volte direbbe meno di
 * questa. */
static int larg(const char *s)
{
    return (int)exfont_larghezza(&g_sistema, s);
}

void ex_scrivi_con(ExFinestra f, ExFont h, int x, int y,
                   const char *s, unsigned int c)
{
    const ExFontDati *fo;
    Oggetto          *r  = radice(f);
    ExTtf             t  = ttf_di(h);

    if (!s) return;

    /* ! LA y RESTA LA CIMA DELLA RIGA, ANCHE COL TRUETYPE, e non diventa la
     * linea di base. Chi ha scritto ex_scrivi(f, x, y, ...) per un'etichetta
     * ha in mente il bordo alto del testo: cambiare significato al parametro
     * a seconda del font vorrebbe dire che lo stesso codice mette la scritta
     * in due posti diversi. La linea di base si ricava, e la ricava questa. */
    if (t) {
        int base = T.base(t);

        while (*s) {
            unsigned int         ch = prossimo_codice(&s);
            ExTtf                q  = ttf_per_codice(h, t, ch);
            int                  gw, gh, sx, sy, px, py;
            const unsigned char *cop;
            int                  b2;

            cop = T.glifo(q, ch, &gw, &gh, &sx, &sy);

            /* ! LA BASE E' QUELLA DEL FONT CHE DISEGNA, non del suo. Due facce
             * diverse hanno due linee di base diverse, e allinearle alla cima
             * farebbe ballare le lettere ripiegate mezza riga piu' su. */
            b2 = (q == t) ? base : T.base(q);

            if (cop && gw > 0 && gh > 0) {
                for (py = 0; py < gh; py++)
                    for (px = 0; px < gw; px++)
                        punto_fuso(r, x + sx + px, y + b2 - sy + py, c,
                                   cop[py * gw + px]);
            }
            x += T.larghezza_car(q, ch);
        }
        return;
    }

    fo = font_di(h);

    while (*s) {
        unsigned char        ch = codice_a_cp437(prossimo_codice(&s));
        const unsigned char *g  = exfont_glifo(fo, ch);
        unsigned int         w  = exfont_larghezza_car(fo, ch);
        unsigned int         rr, b;

        /* ! UN CODICE CHE IL FONT NON HA AVANZA E BASTA. Saltarlo senza
         * avanzare stringerebbe la riga proprio dove manca qualcosa, e chi
         * guarda vedrebbe un testo storto invece di un buco. */
        if (g) {
            for (rr = 0; rr < fo->altezza; rr++)
                for (b = 0; b < w; b++)
                    if (g[rr * fo->passo + (b >> 3)] & (0x80u >> (b & 7)))
                        punto(r, x + (int)b, y + (int)rr, c);
        }
        x += (int)w;
    }
}

void ex_scrivi(ExFinestra f, int x, int y, const char *s, unsigned int c)
{
    ex_scrivi_con(f, 0, x, y, s, c);
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
        M->titolo[i].w = larg(M->titolo[i].nome) + 16;
        x += M->titolo[i].w;
    }
}

/* La larghezza di una tendina: la voce piu' lunga, con la scorciatoia contata
 * a destra e tre spazi in mezzo perche' non si tocchino. */
static int menu_tendina_w(const MenuTitolo *T)
{
    unsigned int i;
    int max = 8;

    /* ! SI MISURA IN PIXEL, NON IN CARATTERI, e prima si contavano i
     * caratteri. Con un font a larghezza fissa le due cose coincidono; con uno
     * proporzionale no, e una tendina larga «il numero di lettere per otto»
     * taglierebbe proprio le voci con le lettere larghe. Il conto dei tre
     * spazi fra la voce e la scorciatoia resta un conto di caratteri, perche'
     * tre spazi sono tre spazi in qualunque font. */
    for (i = 0; i < T->n; i++) {
        const char *t   = T->voce[i].testo;
        const char *tab = strchr(t, '\t');
        int l;

        if (tab) {
            char sinistra[64];
            unsigned int q = (unsigned int)(tab - t);

            if (q >= sizeof(sinistra)) q = sizeof(sinistra) - 1;
            memcpy(sinistra, t, q);
            sinistra[q] = '\0';
            l = larg(sinistra) + 3 * larg(" ") + larg(tab + 1);
        } else {
            l = larg(t);
        }

        if (l > max) max = l;
    }
    return max + 20;
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
            ex_scrivi(f, tx + tw - 8 - larg(tab + 1), ry,
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
    /* =====================================================================
     * ! IL PULSANTE PREMUTO SI SCAMBIA L'OMBRA, non si tinge di grigio scuro.
     *
     * Prima l'unica differenza fra su e giu' era il colore del riempimento, e
     * la si notava solo confrontando due fotografie. Un pulsante sporge
     * perche' ha la LUCE sopra e a sinistra e l'OMBRA sotto e a destra:
     * scambiando i due bordi lo stesso disegno sprofonda, ed e' cio' che
     * l'occhio legge come «l'ho premuto» senza doverci pensare. Le due
     * funzioni c'erano gia' — ex_rilievo e ex_incavo, che chiamano lo stesso
     * bordo3d con i colori invertiti — e non le usava nessuno qui.
     *
     * ! E LA SCRITTA SI SPOSTA DI UN PIXEL IN GIU' E A DESTRA. Sembra un
     * dettaglio ed e' meta' dell'effetto: senza, il rilievo cambia ma cio' che
     * c'e' scritto sopra resta inchiodato, e il pulsante sembra un riquadro
     * che cambia colore invece di una cosa che si muove.
     *
     * ! IL BORDO NERO RESTA FUORI, e il rilievo sta dentro di lui. E' l'ordine
     * che tiene i pulsanti riconoscibili accanto agli altri controlli, che il
     * riquadro nero ce l'hanno anche loro.
     * ===================================================================== */
    case CL_PULSANTE: {
        int dx = o->premuto ? 1 : 0;

        ex_riempi(o->padre, x, y, o->w, o->h, EX_GRIGIO);
        ex_riquadro_disegna(o->padre, x, y, o->w, o->h, EX_NERO);

        if (o->premuto) ex_incavo(o->padre,  x + 1, y + 1, o->w - 2, o->h - 2);
        else            ex_rilievo(o->padre, x + 1, y + 1, o->w - 2, o->h - 2);

        ex_scrivi(o->padre, x + dx + (o->w - larg(o->titolo)) / 2,
                  y + dx + (o->h - 16) / 2, o->titolo, EX_NERO);
        break;
    }

    case CL_ETICHETTA:
        ex_scrivi(o->padre, x, y, o->titolo, EX_NERO);
        break;

    case CL_TESTO: {
        Oggetto *r = radice(o->padre);
        /* ! IL CURSORE STA DOPO IL TESTO SCRITTO, e la sua x e' la LARGHEZZA
         * di quel testo — non il numero di lettere per otto. Con un font
         * proporzionale la seconda si allontanerebbe dalla prima di piu' a
         * ogni lettera battuta. */
        int col = larg(o->titolo);

        ex_riempi(o->padre, x, y, o->w, o->h, EX_BIANCO);
        /* ! IL BORDO DICE CHI HA I TASTI. Senza, chi guarda non sa dove
         * andra' a finire quello che batte — e Tab sembra non fare niente. */
        ex_riquadro_disegna(o->padre, x, y, o->w, o->h,
                            (r && r->fuoco == (ExFinestra)(o - g_ogg + 1))
                            ? EX_BLU : EX_GRIGIO_SC);
        ex_scrivi(o->padre, x + 3, y + (o->h - 16) / 2, o->titolo, EX_NERO);

        if (r && r->fuoco == (ExFinestra)(o - g_ogg + 1))
            ex_riempi(o->padre, x + 3 + col, y + 3, 1, o->h - 6, EX_NERO);
        break;
    }

    /* Un riquadro e' un solco, non un rettangolo: una riga che rientra e una
     * che sporge subito dopo, come una cucitura nel pannello. */
    case CL_RIQUADRO:
        ex_incavo(o->padre, x, y + 8, o->w, o->h - 8);
        ex_rilievo(o->padre, x + 1, y + 9, o->w - 2, o->h - 10);
        if (o->titolo[0]) {
            ex_riempi(o->padre, x + 6, y + 8,
                      larg(o->titolo) + 6, 2, EX_GRIGIO);
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

        /* ! DOVE E' FINITA LO DICE IL SERVER, e con EX_AUTO e' l'unico modo di
         * saperlo: qui dentro x e y valgono ancora il -1 che si e' chiesto. */
        o->x = (int)r.x;
        o->y = (int)r.y;
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

    /* ! ANCHE LA POSIZIONE ARRIVA QUI DENTRO, e va presa. Il server la mette
     * nella ricevuta della misura perche' ridimensionare dall'angolo puo'
     * spostare la finestra — se non ci stava piu' nello schermo. Buttandola
     * via, la finestra saprebbe la misura nuova e la posizione vecchia, che e'
     * lo stesso ricordo sbagliato che WIN_MSG_POSTA esiste per correggere. */
    o->x = (int)r->x;
    o->y = (int)r->y;

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

/* =============================================================================
 * PUNTARE — dove cade il puntatore dentro un'area o una lista
 *
 * ! UNA FUNZIONE SOLA PER IL CLIC E PER IL TRASCINAMENTO. Erano lo stesso
 * calcolo scritto due volte, e la seconda copia sarebbe nata gia' sbagliata:
 * quella del clic aveva dovuto imparare, il 18 agosto, che origine() rende la
 * posizione del PADRE e che la propria va sommata. Una copia nuova non lo
 * saprebbe, e il difetto tornerebbe identico — clic su una riga, scelta di
 * un'altra.
 * ============================================================================= */
static void area_punta(Oggetto *co, int x, int y)
{
    Area *A = area_di(co);
    int   ox, oy;
    unsigned int r, c;

    if (!A) return;

    origine(co, &ox, &oy);
    ox += co->x;
    oy += co->y;

    /* Sopra la prima riga si punta la prima: trascinando all'insu' il cursore
     * deve fermarsi in cima, non saltare in fondo per un numero negativo che
     * diventa enorme da senza segno. */
    r = ((int)y - oy - 2 < 0) ? A->top
                              : A->top + (unsigned int)(((int)y - oy - 2) / AREA_RIGA_H);
    c = ((int)x - ox - 2 < 0) ? A->left
                              : A->left + (unsigned int)(((int)x - ox - 2) / AREA_CAR_W);

    if (r >= A->n) r = A->n - 1;
    A->cy = r;
    A->cx = (c > area_lung(A, r)) ? area_lung(A, r) : c;
    area_segui(A);
}

static void lista_punta(Oggetto *co, int y)
{
    Lista *L = lista_di(co);
    int    ox, oy;
    unsigned int r;

    if (!L || L->n == 0) return;

    origine(co, &ox, &oy);
    oy += co->y;
    (void)ox;

    r = ((int)y - oy - 2 < 0) ? L->primo
                              : L->primo + (unsigned int)(((int)y - oy - 2) / LISTA_RIGA_H);
    if (r < L->n) { L->sel = r; lista_segui(L); }
}

/* In quale colonna della riga e' caduto il clic. Il testo comincia a +4: e'
 * dove lo scrive il disegno della lista, e i due posti vanno d'accordo o il
 * segno «+» dell'albero si preme un carattere piu' in la'. */
static int lista_colonna(Oggetto *co, int x)
{
    int ox, oy, c;

    origine(co, &ox, &oy);
    ox += co->x;

    c = (x - ox - 4) / LISTA_CAR_W;
    return (c < 0) ? 0 : c;
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
            /* L'ultimo argomento e' il bit che EX_APRIRE legge. I bit della
             * colonna restano a zero, ed e' cio' che fa dire -1 a EX_COL: da
             * tastiera una colonna non c'e'. E' l'unico posto del toolkit che
             * manda un comando venuto da un tasto. */
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

/* ! DUE PORTE SULLO STESSO CICLO, e il corpo e' uno solo. `ex_prendi_msg`
 * dorme finche' non succede qualcosa; `ex_msg_ora` guarda e torna. La seconda
 * e' nata perche' un programma che sta ASPETTANDO ALTRO — una risposta dalla
 * rete, per dirne una — deve poter restare vivo senza rinunciare a quel che
 * sta facendo: se dormisse qui dentro, l'attesa diventerebbe due.
 *
 * ! E IL CORPO NON SI DUPLICA. Le due funzioni fanno le stesse otto cose —
 * sveglie, terminali, mailbox, misure, tasti, clic — e due copie sarebbero
 * due copie da tenere d'accordo per sempre. Cambia una riga: quanto si aspetta
 * dentro poll(), e se al giro dopo si riprova o si torna a mani vuote. */
/* =============================================================================
 * ! CHE COSA E' NOSTRO, E CHE COSA E' DI QUALCUN ALTRO
 *
 * La mailbox e' una sola per processo e i consumatori sono piu' d'uno: qui
 * arrivano gli eventi del server a finestre, e nello stesso posto arrivano le
 * risposte dello stack IP a chi sta scaricando una pagina. `ipc_scegli` prende
 * il nostro e LASCIA DOV'E' quel che non lo e' — non lo si tiene in mano,
 * quindi non lo si puo' perdere.
 *
 * ! CHI DORME PUO' BUTTARE, CHI NON DORME NO, e la differenza non e' un
 * dettaglio di comodo. `ex_prendi_msg` dorme solo quando l'applicazione non sta
 * aspettando nient'altro: se siamo li', nessun'altra attesa e' aperta e una
 * risposta rimasta indietro non serve piu' a nessuno — buttarla tiene pulito lo
 * scaffale. `ex_msg_ora` invece si chiama PROPRIO MENTRE si aspetta altro, e
 * li' quella stessa risposta e' di qualcuno: buttarla vuol dire che chi
 * l'aspetta aspetta per sempre. E' il guasto che ha fatto smettere di aprirsi
 * le pagine https quando la stretta di mano ha cominciato a leggere a pezzi.
 *
 * ! E ADESSO ANCHE CHI NON DORME PUO' ANDARE AVANTI. Prima un messaggio non
 * nostro si RIMETTEVA sullo scaffale e si tornava a mani vuote — ma lo scaffale
 * si serve prima della coda del kernel, quindi al giro dopo si ritrovava
 * davanti lo stesso messaggio, e la pompa della finestra restava ferma li'
 * sopra per tutto il tempo che lo stack aveva una risposta da parte. Un ALTRUI
 * si SALTA: quel che sta dietro si vede.
 * ========================================================================== */
static int filtro_finestra(const IpcMessage *m, void *dato)
{
    int bloccante = *(const int *)dato;

    if (m->tipo == WIN_MSG_EVENTO || m->tipo == WIN_MSG_MISURATA ||
        m->tipo == WIN_MSG_POSTA)
        return IPC_MIO;

    return bloccante ? IPC_BUTTA : IPC_ALTRUI;
}

static int prendi_msg(ExMsg *m, int bloccante)
{
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    struct pollfd v[1 + TERM_MAX];
    int           giri = 0;

    for (;;) {
        WinEvento e;
        ExFinestra f;

        if (g_uscita) return 0;

        /* ! CHI NON DORME FA UN GIRO SOLO. Tutti i «continue» qui sotto
         * vogliono dire «non c'era niente, riprova»: per chi non blocca,
         * riprovare vuol dire girare a vuoto, e la risposta giusta e' «adesso
         * non c'e' niente». */
        if (!bloccante && giri++ > 0) return 0;

        /* ! LE SVEGLIE SI GUARDANO PRIMA DI DORMIRE, non dopo: guardarle dopo
         * vorrebbe dire che la prima scade sempre con 200 ms di ritardo anche
         * quando era gia' scaduta entrando. */
        {
            unsigned int ora = uptime_ms();
            int          j;

            for (j = 0; j < OGGETTI_MAX; j++) {
                Oggetto *o = &g_ogg[j];

                if (!o->usato || o->classe != CL_FINESTRA) continue;
                if (o->sveglia_ms == 0) continue;

                /* ! LA DIFFERENZA SI GUARDA CON LA SOTTRAZIONE, non con «>=».
                 * uptime_ms torna a zero dopo quarantanove giorni: con un
                 * confronto diretto, quel giorno la sveglia smetterebbe di
                 * scattare per sempre. Con la sottrazione senza segno il giro
                 * si chiude da se'. */
                if ((int)(ora - o->sveglia_quando) >= 0) {
                    o->sveglia_quando = ora + o->sveglia_ms;
                    m->finestra = (ExFinestra)(j + 1);
                    m->msg      = EXM_TEMPO;
                    m->wp       = 0;
                    m->lp       = (long)ora;
                    return 1;
                }
            }
        }

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
            /* ! poll() GUARDA LA CODA DEL KERNEL, NON LO SCAFFALE. Un messaggio
             * lasciato da parte mentre si scaricava una pagina non la fa
             * scattare: senza questo controllo resterebbe li' fino al prossimo
             * messaggio che arriva davvero, e su uno schermo fermo vuol dire
             * per sempre. */
            int gia = (int)ipc_pronto();

            v[nv].fd = FD_IPC; v[nv].events = POLLIN; v[nv].revents = 0; nv++;

            for (j = 0; j < TERM_MAX && nv < (int)(sizeof(v)/sizeof(v[0])); j++)
                if (g_term[j].usato && g_term[j].fd_out >= 0) {
                    v[nv].fd = g_term[j].fd_out;
                    v[nv].events = POLLIN;
                    v[nv].revents = 0;
                    nv++;
                }

            if (poll(v, (unsigned int)nv, (bloccante && !gia) ? 200 : 0) <= 0
                && !gia) continue;

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
                if (!(v[0].revents & POLLIN) && !gia) {
                    if (cambiato) continue;
                    continue;
                }
            }
        }

        /* ! IPC_SUBITO E NON UNA SCADENZA CORTA: qui si arriva solo dopo che
         * poll() (o lo scaffale) ha detto che c'e' qualcosa, e la scadenza piu'
         * breve che il kernel sappia rappresentare e' un tick intero — dieci
         * millisecondi che la pompa dei messaggi, chiamata otto volte per giro
         * mentre si scarica, non puo' pagare. */
        if (ipc_scegli(filtro_finestra, &bloccante, &meta, buf, sizeof(buf),
                       IPC_SUBITO) < 0) continue;

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

        /* ! LA POSIZIONE NUOVA SI PRENDE E NON SI DICE A NESSUNO: e' il
         * toolkit che si tiene aggiornato, non una notizia per
         * l'applicazione. Chi ha trascinato la finestra l'ha gia' vista
         * muoversi; a chi scrive il programma serve solo che ex_sposta() e
         * ex_misura() non partano da un ricordo sbagliato. */
        if (meta.tipo == WIN_MSG_POSTA && meta.len >= sizeof(WinRegione)) {
            WinRegione r;
            Oggetto   *o;

            memcpy(&r, buf, sizeof(r));
            o = ogg(da_win_id(r.id));
            if (o) { o->x = (int)r.x; o->y = (int)r.y; }
            continue;
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
                /* =========================================================
                 * ! CONSUMATO DA UNA CASELLA — MA L'APPLICAZIONE VA AVVISATA
                 * LO STESSO, e qui c'era scritto il contrario.
                 *
                 * `ex_procedura_base(EXM_DISEGNA)` rifa' lo SFONDO della
                 * finestra e i controlli del toolkit. Per una finestra fatta
                 * di soli controlli e' tutto, e non svegliare l'applicazione
                 * a ogni lettera era un risparmio giusto. Ma una finestra che
                 * disegna ANCHE del suo — il browser dipinge la pagina, e
                 * l'editor di testo che verra' dipingera' il testo — si vedeva
                 * cancellare quel disegno a ogni tasto battuto in una casella:
                 * bastava scrivere nella barra dell'indirizzo per far sparire
                 * la pagina, e restava sparita finche' qualcos'altro non
                 * provocava un ridisegno.
                 *
                 * ! IL MESSAGGIO IN PIU' COSTA UN NIENTE, ed e' il confronto
                 * che conta: un messaggio per ogni TASTO BATTUTO — non per
                 * ogni pixel di mouse mosso, che e' il caso per cui quella
                 * regola era nata. Chi non disegna niente di suo non fa
                 * niente e paga solo il giro del ciclo.
                 * ========================================================= */
                ex_procedura_base(f, EXM_DISEGNA, 0, 0);
                m->msg = EXM_DISEGNA;
                m->wp  = 0;
                m->lp  = 0;
                return 1;
            }
            m->msg = EXM_TASTO;
            m->wp  = e.tasto;
            return 1;
        /* ! IL TRASCINAMENTO VA AL CONTROLLO CHE HA PRESO IL BOTTONE, non a
         * quello sotto il puntatore, e non sveglia l'applicazione: allargare
         * una selezione e' lavoro del controllo, e un messaggio per ogni
         * pixel percorso sarebbe un fiume per chi non lo guarda. */
        case WIN_EV_MOUSE_MOSSO: {
            Oggetto *co = ogg(g_trascinato);

            /* ! IL PULSANTE SI ALZA SE IL DITO SCIVOLA VIA, e si riabbassa se
             * torna. Senza, «scivolare fuori per annullare» resterebbe una
             * cosa vera che non si vede: il pulsante continuerebbe a sembrare
             * premuto mentre ormai non farebbe piu' niente, e chi guarda
             * crederebbe di aver comandato. Il disegno deve dire in ogni
             * istante cosa succedera' alzando il dito adesso. */
            {
                Oggetto *pr = ogg(g_premuto);

                if (pr && pr->usato && pr->classe == CL_PULSANTE) {
                    unsigned int giu =
                        (controllo_in(f, (int)e.x, (int)e.y) == g_premuto);

                    if (giu != pr->premuto) {
                        pr->premuto = giu;
                        ex_procedura_base(f, EXM_DISEGNA, 0, 0);
                    }
                }
            }

            if (co && co->classe == CL_AREA) {
                area_punta(co, (int)e.x, (int)e.y);
                ex_procedura_base(f, EXM_DISEGNA, 0, 0);
                continue;
            }
            if (co && co->classe == CL_LISTA) {
                lista_punta(co, (int)e.y);
                ex_procedura_base(f, EXM_DISEGNA, 0, 0);
                continue;
            }
            m->msg = EXM_MOUSE_MOSSO;
            return 1;
        }

        case WIN_EV_MOUSE_SU: {
            /* =================================================================
             * ! QUI PARTE IL COMANDO, ed e' il rilascio a farlo partire.
             *
             * Il perche' sta accanto a CL_PULSANTE in WIN_EV_MOUSE_GIU. Qui
             * c'e' la seconda meta': il comando parte SOLO se il dito si alza
             * ancora sopra il pulsante su cui si era posato. Scivolare via
             * prima di alzarlo annulla, e non serve spiegarlo a nessuno —
             * chiunque abbia usato un mouse lo sa gia' senza saperlo.
             *
             * ! I PULSANTI SI ALZANO TUTTI, non solo quello sotto il
             * puntatore: chi preme e poi trascina fuori lascerebbe altrimenti
             * un pulsante schiacciato per sempre.
             * ============================================================= */
            unsigned int cmd_id = 0;
            {
                Oggetto *pr = ogg(g_premuto);
                int j, cambiato = 0;

                if (pr && pr->usato && pr->classe == CL_PULSANTE &&
                    controllo_in(f, (int)e.x, (int)e.y) == g_premuto)
                    cmd_id = pr->id;

                g_premuto = 0;

                for (j = 0; j < OGGETTI_MAX; j++)
                    if (g_ogg[j].usato && g_ogg[j].premuto) {
                        g_ogg[j].premuto = 0;
                        cambiato = 1;
                    }
                if (cambiato) ex_procedura_base(f, EXM_DISEGNA, 0, 0);
            }

            /* ! IL PULSANTE SI SERVE PRIMA DELLA LISTA TRASCINATA. Sono due
             * cose che non possono essere vere insieme — o si e' posato il
             * dito su un pulsante o dentro una lista — ma l'ordine va scelto
             * lo stesso, o domani lo sceglie il caso. */
            if (cmd_id) {
                g_trascinato = 0;
                m->msg = EXM_COMANDO;
                m->wp  = cmd_id;
                m->lp  = 0;
                return 1;
            }

            /* ! SCEGLIERE TRASCINANDO E' SCEGLIERE, e va detto all'applicazione
             * come lo si dice per un clic — ma UNA VOLTA SOLA, al rilascio, e
             * solo se la riga e' davvero cambiata. Dirlo a ogni riga
             * attraversata vorrebbe dire, in un file manager, rileggere una
             * directory per ogni voce sfiorata dal puntatore. */
            {
                Oggetto *co = ogg(g_trascinato);
                Lista   *L  = co ? lista_di(co) : 0;

                g_trascinato = 0;
                if (L && L->sel != g_tras_sel) {
                    m->msg = EXM_COMANDO;
                    m->wp  = co->id;
                    m->lp  = 0;         /* ne' aperto ne' su una colonna */
                    return 1;
                }
            }

            m->msg = EXM_MOUSE_SU;
            return 1;
        }
        case WIN_EV_MOUSE_GIU: {
            /* ! IL CLIC SU UN PULSANTE DIVENTA EXM_COMANDO, e il messaggio
             * grezzo non arriva all'applicazione. E' cio' che distingue un
             * toolkit da un pannello di pixel: chi scrive l'applicazione
             * guarda l'id del pulsante, non le coordinate. */
            ExFinestra c;
            Oggetto *co;
            int      doppio;

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

            /* Si chiede SEMPRE, anche quando la risposta non serve: e' la
             * chiamata stessa che si segna il clic per la volta dopo. */
            doppio = doppio_clic(c, (int)e.x, (int)e.y, e.tempo);

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

            /* =============================================================
             * ! PREMERE NON E' ANCORA COMANDARE.
             *
             * Fino a oggi EXM_COMANDO partiva qui, alla pressione. E' il
             * comportamento che si scrive per primo perche' e' il piu' corto,
             * ed e' anche quello che toglie a chi usa il programma l'unica
             * possibilita' di RIPENSARCI: un pulsante premuto per sbaglio era
             * gia' un pulsante eseguito. Su «Esci» o «Spegni» la differenza
             * non e' estetica.
             *
             * Ovunque — dai Macintosh in poi — un pulsante si arma premendo e
             * spara alzando il dito, e scivolare via prima di alzarlo annulla.
             * Qui si fa lo stesso: adesso si segna soltanto chi e' giu', il
             * disegno lo mostra sprofondato, e il comando parte da
             * WIN_EV_MOUSE_SU se il puntatore e' ancora sopra.
             * ============================================================= */
            if (co && co->classe == CL_PULSANTE) {
                co->premuto = 1;
                g_premuto   = c;
                ex_procedura_base(f, EXM_DISEGNA, 0, 0);
                continue;
            }

            /* ! UN CLIC SU UNA LISTA SCEGLIE LA RIGA, e l'applicazione lo
             * riceve come EXM_COMANDO con l'id della lista — lo stesso
             * messaggio dell'Invio, perche' sono la stessa decisione presa in
             * due modi. Chi vuole distinguere «ho scelto» da «ho aperto»
             * guarda EX_APRIRE(lp): il doppio clic lo accende come l'Invio. */
            if (co && co->classe == CL_AREA) {
                Area *A = area_di(co);

                area_punta(co, (int)e.x, (int)e.y);

                /* ! IL CLIC POSA L'ANCORA E TOGLIE LA SELEZIONE DI PRIMA, e il
                 * trascinamento la allarga da li'. E' l'unica sequenza che non
                 * stupisce: chi clicca ha smesso di essere interessato a cio'
                 * che aveva scelto prima. */
                if (A) { A->sel = 1; A->ax = A->cx; A->ay = A->cy; }

                g_trascinato = c;
                ex_procedura_base(f, EXM_DISEGNA, 0, 0);
                continue;
            }

            if (co && co->classe == CL_LISTA) {
                Lista *L;

                lista_punta(co, (int)e.y);
                g_trascinato = c;
                L = lista_di(co);
                g_tras_sel = L ? L->sel : 0;
                ex_procedura_base(f, EXM_DISEGNA, 0, 0);
                m->msg = EXM_COMANDO;
                m->wp  = co->id;
                /* La colonna sta nei bit alti aumentata di uno, il «aprire»
                 * nel bit zero: vedi EX_COL e EX_APRIRE in exwin.h. */
                m->lp  = (long)(((lista_colonna(co, (int)e.x) + 1) << 8) |
                                (doppio ? 1 : 0));
                return 1;
            }

            /* ! IL DOPPIO CLIC CHE NESSUN CONTROLLO HA INTERPRETATO ARRIVA
             * ALL'APPLICAZIONE COM'E'. Sullo sfondo della scrivania, su
             * un'immagine, su qualunque cosa un programma disegni da se': li'
             * il toolkit non sa cosa voglia dire, e chi lo sa e' chi ha
             * disegnato. */
            m->msg = doppio ? EXM_DOPPIOCLIC : EXM_MOUSE_GIU;
            return 1;
        }
        default:
            m->msg = EXM_DISEGNA;
            return 1;
        }
    }
}

int ex_prendi_msg(ExMsg *m) { return prendi_msg(m, 1); }

/* ! RENDE 0 ANCHE QUANDO L'APPLICAZIONE STA USCENDO, come l'altra: chi la usa
 * dentro un'attesa deve accorgersi che non ha piu' senso aspettare. */
int ex_msg_ora(ExMsg *m) { return prendi_msg(m, 0); }

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
        /* Lo sfondo dell'area del client, e poi i controlli sopra. */
        ex_riempi(f, 0, 0, o->w, o->h, EX_GRIGIO);
        disegna_figli(f);
        ex_aggiorna(f);
        return 0;

    /* ! SI RIDIPINGE SOLO SU EXM_DISEGNA, E NON SU QUALUNQUE MESSAGGIO.
     *
     * Qui `default:` cadeva dentro il disegno, cioe' OGNI messaggio che
     * un'applicazione non gestisce riempiva la sua finestra di grigio. Si
     * vedeva sull'orologio: un clic sulla barra arriva come EXM_MOUSE_GIU,
     * l'orologio non lo gestisce, e l'ora spariva — per tornare al cambio di
     * minuto, che e' l'unico momento in cui quel programma ridisegna.
     * Segnalato provandolo, non trovato leggendo.
     *
     * ! ED ERA ANCHE LAVORO CONTINUO PER NIENTE: ogni movimento del mouse
     * sopra una finestra che non ascolta il mouse ne ripitturava il fondo e
     * chiedeva al server di ricomporre lo schermo. Su una macchina che
     * ricompone tutto a ogni aggiornamento, e' il tipo di costo che non si
     * vede in un profilo perche' e' sparso dappertutto.
     *
     * Chi deve ridisegnarsi riceve EXM_DISEGNA dal server — e' il server a
     * saperlo, quando una finestra viene scoperta — quindi non si perde niente.
     */
    default:
        return 0;
    }
}

void ex_esci(int codice) { g_uscita = 1; g_codice = codice; }

/* =============================================================================
 * LA SVEGLIA PERIODICA
 *
 * ! SENZA QUESTA UN'APPLICAZIONE NON PUO' FARE NIENTE DA SOLA. ex_prendi_msg()
 * dorme finche' non arriva un evento: un orologio, un'animazione, una barra di
 * avanzamento non hanno nessuno che li svegli — si aggiornerebbero solo quando
 * l'utente muove il mouse, che e' esattamente il contrario di cio' che
 * servirebbe.
 *
 * ! E NON COSTA UN GIRO IN PIU'. Il poll() del ciclo dei messaggi ha gia' una
 * scadenza di 200 ms: il processo si sveglia cinque volte al secondo comunque,
 * per guardare le pipe dei terminali. Qui si guarda anche l'orologio mentre si
 * e' svegli, e non si aggiunge nessuna attesa.
 *
 * ! LA RISOLUZIONE E' QUELLA DEL poll, cioe' 200 ms, e va detto invece di
 * lasciarlo scoprire: chiedere 50 ms non da' 50 ms, da' 200. Per un orologio
 * al secondo e' irrilevante — il ritardo massimo e' un quinto di secondo — e
 * per un'animazione fluida servirebbe un'altra cosa, non questa.
 *
 * ! IL MESSAGGIO ARRIVA ALLA FINESTRA, NON AL PROGRAMMA, cosi' un programma
 * con due finestre puo' averne una che si aggiorna e una ferma senza
 * distinguere niente a mano.
 * ============================================================================= */
void ex_sveglia(ExFinestra f, unsigned int ms)
{
    Oggetto *o = ogg(f);

    if (!o) return;

    o->sveglia_ms = ms;
    o->sveglia_quando = ms ? uptime_ms() + ms : 0;
}

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
