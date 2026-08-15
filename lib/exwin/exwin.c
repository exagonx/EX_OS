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
} Terminale;

static Terminale g_term[TERM_MAX];

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
           o->classe == CL_TERMINALE;
}

static void fuoco_metti(ExFinestra f, ExFinestra c)
{
    Oggetto *r = radice(f);
    Oggetto *o = ogg(c);

    if (!r || !o || !accetta_fuoco(o)) return;
    r->fuoco = c;
    o->cursore = (unsigned int)strlen(o->titolo);
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
        if (n <= 0) break;

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
        g_server = ipc_lookup(WIN_SERVIZIO);
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

                for (j = 0; j < TERM_MAX; j++)
                    if (g_term[j].usato && term_leggi(&g_term[j])) {
                        cambiato = 1;
                        ex_procedura_base(radice_h(g_term[j].ogg), EXM_DISEGNA, 0, 0);
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
