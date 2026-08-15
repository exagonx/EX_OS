/* =============================================================================
 * drivers/wserver/wserver.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il server a finestre — gradino 1 di DIREZIONE.md
 *
 *     /dev/wserver.drv          compone le finestre, muove il puntatore
 *     /dev/wserver.drv -v       dice tutto quello che fa
 *
 * ! GIRA IN RING 3, ED E' IL PUNTO. La direttiva 2 di DIREZIONE.md dice che
 * la grafica sta in spazio utente: quando questo processo muore, muore lui.
 * Kernel, scheduler, console seriale e tastiera restano vivi, e lo schermo si
 * rimette con /bin/testo — che si digita alla cieca ed e' la rete di
 * sicurezza costruita apposta prima di scrivere questo file.
 *
 * ! SI CHIAMA .drv PER UNA RAGIONE SOLA: mappare il framebuffer. mmio_map()
 * e' riservata agli eseguibili caricati da un file *.drv, e il framebuffer e'
 * una finestra di memoria fisica come i registri di una scheda. Non guida
 * nessuna periferica e non registra un servizio di driver: il nome e' il
 * lasciapassare, non una descrizione.
 *
 * -----------------------------------------------------------------------------
 * ! NON DISEGNA IL CONTENUTO DELLE FINESTRE, LO COMPONE
 *
 * Ogni finestra e' una zona di memoria condivisa che il CLIENT riempie di
 * pixel. Qui dentro si mette il bordo, la barra del titolo, si impilano nel
 * giusto ordine e si copia nel framebuffer. Le tre ragioni stanno in testa a
 * win_proto.h, e la piu' importante e' che un client che sbaglia a disegnare
 * rovina la propria finestra e non lo schermo.
 *
 * ! ED E' ANCHE PERCHE' I FORMATI DI IMMAGINE NON SONO QUI. Un
 * decodificatore JPG o PNG e' migliaia di righe che leggono dati venuti da
 * fuori: nel server sarebbe un difetto di tutte le applicazioni insieme. Sta
 * nella libreria del client, che disegna nella propria zona — e il server non
 * sa nemmeno che esistano le immagini.
 * ============================================================================= */

#include "libc.h"
#include "kbd_proto.h"
#include "win_proto.h"

#define FINESTRE_MAX    16
#define BARRA_H         18
#define BORDO           1

/* I colori, in ARGB. Sono pochi e stanno qui: una scrivania che cambia
 * aspetto non deve voler dire cercarli sparsi nel file. */
#define C_SFONDO        0x00204060
#define C_BARRA_ATT     0x00305A8A
#define C_BARRA_INA     0x00606060
#define C_BORDO         0x00101010
#define C_TITOLO        0x00FFFFFF
#define C_CHIUDI        0x00C04040
#define C_CLIENT        0x00C0C0C0

extern const unsigned char font8x16[256 * 16];

typedef struct {
    unsigned int usata;
    unsigned int id;
    unsigned int pid;
    unsigned int x, y, w, h;        /* l'area del CLIENT, senza cornice */
    unsigned int stile;
    char         titolo[WIN_TITOLO_LEN];
    unsigned int zona_virt;         /* i pixel del client, ARGB */
    unsigned int zona_byte;
} Finestra;

static Finestra g_fin[FINESTRE_MAX];
static unsigned int g_ordine[FINESTRE_MAX];     /* dal fondo alla cima */
static unsigned int g_n_ordine = 0;
static unsigned int g_prossimo_id = 1;
static unsigned int g_verboso = 0;

/* Lo schermo */
static unsigned char *g_fb = 0;
static unsigned int g_fb_passo = 0, g_fb_w = 0, g_fb_h = 0, g_fb_bit = 0;

/* Il puntatore */
static int g_px = 0, g_py = 0;
static unsigned int g_bottoni = 0, g_bottoni_prec = 0;

/* Il trascinamento in corso */
static int g_trascino = -1;             /* indice, -1 = nessuno */
static int g_tr_dx = 0, g_tr_dy = 0;

/* ! SI RICOMPONE SOLO QUANDO QUALCOSA E' CAMBIATO, e non e' un'ottimizzazione
 * prematura: e' una CORREZIONE. Ricomponendo a ogni giro, 480000 pixel per
 * fotogramma tenevano il processo occupato tanto a lungo che una richiesta di
 * un client restava in mailbox per secondi — e il client si arrendeva. Il
 * sintomo era «il server a finestre non risponde» da un'applicazione avviata
 * in sfondo, mentre la stessa in primo piano funzionava: perche' li' la shell
 * era ferma e lasciava tutta la CPU al server.
 *
 * ! ED E' IL GENERE DI DIFETTO CHE LA PROVA COMODA NON VEDE. Provando a mano,
 * in primo piano, andava. */
static unsigned int g_sporco = 1;

/* -----------------------------------------------------------------------------
 * Lo schermo, un pixel per volta
 *
 * ! LA CONVERSIONE STA QUI E SOLO QUI. E' l'unico posto del sistema che sa se
 * lo schermo e' a 16, 24 o 32 bit: i client disegnano sempre in ARGB a 32 e
 * non lo sanno. Un toolkit che dovesse conoscere il formato dello schermo
 * avrebbe sei strade da provare invece di una.
 * --------------------------------------------------------------------------- */
static void px(unsigned int x, unsigned int y, unsigned int c)
{
    unsigned char *p;

    if (x >= g_fb_w || y >= g_fb_h) return;

    p = g_fb + y * g_fb_passo + x * (g_fb_bit >> 3);

    if (g_fb_bit == 32) {
        *(unsigned int *)p = c;
    } else if (g_fb_bit == 24) {
        p[0] = (unsigned char)(c);
        p[1] = (unsigned char)(c >> 8);
        p[2] = (unsigned char)(c >> 16);
    } else {    /* 16 bit, 5-6-5 */
        unsigned int v = (((c >> 16) & 0xF8) << 8) |
                         (((c >> 8)  & 0xFC) << 3) |
                         (( c        & 0xF8) >> 3);
        *(unsigned short *)p = (unsigned short)v;
    }
}

/* ! LA STRADA VELOCE A 32 BIT NON E' UN LUSSO: riempire lo sfondo di
 * 800x600 con una chiamata di funzione per pixel sono 480000 chiamate a
 * fotogramma, ed e' quanto bastava a far scadere le richieste dei client.
 * Qui si scrive una riga per volta, e la strada generale resta per gli altri
 * formati. */
static void riempi(unsigned int x, unsigned int y, unsigned int w,
                   unsigned int h, unsigned int c)
{
    unsigned int i, j;

    if (g_fb_bit == 32) {
        if (x >= g_fb_w || y >= g_fb_h) return;
        if (x + w > g_fb_w) w = g_fb_w - x;
        if (y + h > g_fb_h) h = g_fb_h - y;

        for (j = 0; j < h; j++) {
            unsigned int *p = (unsigned int *)(g_fb + (y + j) * g_fb_passo + x * 4);
            for (i = 0; i < w; i++) p[i] = c;
        }
        return;
    }

    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            px(x + i, y + j, c);
}

static void scrivi(unsigned int x, unsigned int y, const char *s, unsigned int c)
{
    unsigned int i;

    for (i = 0; s[i]; i++) {
        const unsigned char *g = &font8x16[(unsigned char)s[i] * 16];
        unsigned int r, b;

        for (r = 0; r < 16; r++)
            for (b = 0; b < 8; b++)
                if (g[r] & (0x80 >> b)) px(x + i * 8 + b, y + r, c);
    }
}

/* -----------------------------------------------------------------------------
 * Comporre
 *
 * ! SI RIDISEGNA TUTTO, E PER ADESSO VA BENE. Un compositore serio ridisegna
 * solo cio' che e' cambiato, e ci arrivera': ma la lista delle regioni sporche
 * e' la struttura piu' facile da sbagliare di un server grafico, e sbagliarla
 * si vede come pezzi di finestra che restano sullo schermo dopo che la
 * finestra se n'e' andata. Prima che sia giusto, poi che sia veloce.
 * --------------------------------------------------------------------------- */
static void cornice(const Finestra *f, unsigned int attiva)
{
    unsigned int bx = f->x - BORDO;
    unsigned int by = f->y - BORDO - ((f->stile & WIN_ST_TITOLO) ? BARRA_H : 0);
    unsigned int bw = f->w + BORDO * 2;
    unsigned int bh = f->h + BORDO * 2 + ((f->stile & WIN_ST_TITOLO) ? BARRA_H : 0);

    if (f->stile & WIN_ST_BORDO) {
        riempi(bx, by, bw, BORDO, C_BORDO);
        riempi(bx, by + bh - BORDO, bw, BORDO, C_BORDO);
        riempi(bx, by, BORDO, bh, C_BORDO);
        riempi(bx + bw - BORDO, by, BORDO, bh, C_BORDO);
    }

    if (f->stile & WIN_ST_TITOLO) {
        riempi(f->x, by + BORDO, f->w, BARRA_H,
               attiva ? C_BARRA_ATT : C_BARRA_INA);
        scrivi(f->x + 4, by + BORDO + 1, f->titolo, C_TITOLO);

        if (f->stile & WIN_ST_CHIUDI)
            riempi(f->x + f->w - BARRA_H + 2, by + BORDO + 2,
                   BARRA_H - 6, BARRA_H - 6, C_CHIUDI);
    }
}

static void componi(void)
{
    unsigned int k, i, j;

    riempi(0, 0, g_fb_w, g_fb_h, C_SFONDO);

    for (k = 0; k < g_n_ordine; k++) {
        Finestra *f = &g_fin[g_ordine[k]];
        const unsigned int *src;

        if (!f->usata || !(f->stile & WIN_ST_VISIBILE)) continue;

        cornice(f, (k + 1 == g_n_ordine));

        /* ! I PIXEL SI LEGGONO DALLA ZONA DEL CLIENT SENZA FIDARSI DELLA
         * MISURA CHE IL CLIENT CREDE DI AVERE: il ciclo va sui numeri che
         * abbiamo noi. La zona e' condivisa, quindi il client puo' averci
         * scritto qualunque cosa — ma non puo' cambiarne la dimensione. */
        src = (const unsigned int *)f->zona_virt;
        if (!src) continue;

        if (g_fb_bit == 32 && f->x < g_fb_w && f->y < g_fb_h) {
            unsigned int ww = (f->x + f->w > g_fb_w) ? g_fb_w - f->x : f->w;
            unsigned int hh = (f->y + f->h > g_fb_h) ? g_fb_h - f->y : f->h;

            for (j = 0; j < hh; j++) {
                unsigned int *d = (unsigned int *)(g_fb + (f->y + j) * g_fb_passo
                                                   + f->x * 4);
                const unsigned int *sr = src + j * f->w;
                for (i = 0; i < ww; i++) d[i] = sr[i];
            }
        } else {
            for (j = 0; j < f->h; j++)
                for (i = 0; i < f->w; i++)
                    px(f->x + i, f->y + j, src[j * f->w + i]);
        }
    }

    /* Il puntatore, una freccia semplice disegnata a mano. */
    {
        int a, b;
        for (a = 0; a < 12; a++)
            for (b = 0; b <= a && b < 8; b++)
                if (a < 10 || b < 4)
                    px((unsigned int)(g_px + b), (unsigned int)(g_py + a),
                       (b == 0 || b == a || a == 11) ? 0x00000000 : 0x00FFFFFF);
    }
}

/* -----------------------------------------------------------------------------
 * Le finestre
 * --------------------------------------------------------------------------- */
static int trova_id(unsigned int id)
{
    int i;
    for (i = 0; i < FINESTRE_MAX; i++)
        if (g_fin[i].usata && g_fin[i].id == id) return i;
    return -1;
}

static void in_cima(int idx)
{
    unsigned int k, j = 0;

    for (k = 0; k < g_n_ordine; k++)
        if (g_ordine[k] != (unsigned int)idx) g_ordine[j++] = g_ordine[k];

    /* ! LO SFONDO NON SALE MAI. E' cio' che lo rende uno sfondo invece di una
     * finestra come le altre, ed e' anche il motivo per cui un'immagine di
     * scrivania non ha bisogno di un meccanismo suo. */
    if (g_fin[idx].stile & WIN_ST_SFONDO) {
        for (k = j; k > 0; k--) g_ordine[k] = g_ordine[k - 1];
        g_ordine[0] = (unsigned int)idx;
    } else {
        g_ordine[j] = (unsigned int)idx;
    }
    g_n_ordine = j + 1;

    /* ! E QUELLE «SOPRA» RESTANO SOPRA, anche a chi e' appena salito. Senza,
     * la barra delle applicazioni finirebbe coperta dalla prima finestra che
     * si porta davanti — e con una finestra a schermo intero non ci sarebbe
     * piu' modo di arrivare al menu. */
    {
        unsigned int q, alto = 0;
        unsigned int tmp[FINESTRE_MAX];

        for (q = 0; q < g_n_ordine; q++)
            if (!(g_fin[g_ordine[q]].stile & WIN_ST_SOPRA)) tmp[alto++] = g_ordine[q];
        for (q = 0; q < g_n_ordine; q++)
            if (g_fin[g_ordine[q]].stile & WIN_ST_SOPRA) tmp[alto++] = g_ordine[q];
        for (q = 0; q < g_n_ordine; q++) g_ordine[q] = tmp[q];
    }
}

/* Chi c'e' sotto il puntatore, dalla cima al fondo. -1 = nessuno.
 * `dove`: 0 = area del client, 1 = barra del titolo, 2 = pulsante chiudi. */
static int sotto(int x, int y, unsigned int *dove)
{
    int k;

    for (k = (int)g_n_ordine - 1; k >= 0; k--) {
        Finestra *f = &g_fin[g_ordine[k]];
        int by, bh;

        if (!f->usata || !(f->stile & WIN_ST_VISIBILE)) continue;

        if (x >= (int)f->x && x < (int)(f->x + f->w) &&
            y >= (int)f->y && y < (int)(f->y + f->h)) {
            *dove = 0;
            return (int)g_ordine[k];
        }

        if (!(f->stile & WIN_ST_TITOLO)) continue;

        by = (int)f->y - BARRA_H;
        bh = BARRA_H;
        if (x >= (int)f->x && x < (int)(f->x + f->w) && y >= by && y < by + bh) {
            *dove = 1;
            if ((f->stile & WIN_ST_CHIUDI) &&
                x >= (int)(f->x + f->w - BARRA_H)) *dove = 2;
            return (int)g_ordine[k];
        }
    }
    return -1;
}

static void manda_evento(const Finestra *f, unsigned int tipo,
                         int x, int y, unsigned int bottoni, unsigned int tasto)
{
    WinEvento e;

    e.id      = f->id;
    e.tipo    = tipo;
    e.x       = (unsigned int)(x - (int)f->x);
    e.y       = (unsigned int)(y - (int)f->y);
    e.bottoni = bottoni;
    e.tasto   = tasto;

    /* ! SE IL CLIENT NON RACCOGLIE, NON SI INSISTE. La mailbox e' profonda
     * quattro messaggi: un client fermo la riempirebbe, e da li' in poi ogni
     * ipc_send fallirebbe. Un evento perso e' meno grave di un server che si
     * blocca su un client che non risponde piu'. */
    (void)ipc_send(f->pid, WIN_MSG_EVENTO, &e, sizeof(e));
}

/* -----------------------------------------------------------------------------
 * Il mouse
 *
 * ! SI CERCA PRIMA IL SERVIZIO DEDICATO, POI IL PS/2, ed e' la regola gia'
 * scritta in kbd_proto.h: se c'e' un driver che fa SOLO il mouse vuol dire che
 * qualcuno l'ha avviato apposta. Cosi' lo stesso server funziona con PS/2,
 * seriale, UHCI e xHCI senza sapere quale sia.
 * --------------------------------------------------------------------------- */
/* -----------------------------------------------------------------------------
 * La tastiera
 *
 * ! NON SI RIFANNO LE MAPPE: IL SERVIZIO 'kbd' CONSEGNA GIA' TRADOTTO. In modo
 * raw un KBD_MSG_KEY porta il carattere nei bit 0..15, i tasti speciali da
 * 0x100 in su e i modificatori nei bit 16..18 — passati per la disposizione
 * scelta con `keymap`, che si cambia a caldo. Un server che leggesse scancode
 * grezzi si porterebbe dietro una seconda copia di keymaps.h, e le due
 * divergerebbero. E' la stessa regola del driver USB, applicata dall'altro
 * lato: li' si MANDANO scancode a chi serve 'kbd', qui si CONSUMA cio' che
 * 'kbd' ha gia' tradotto.
 *
 * ! IL MODO RAW E' PER CONSOLE, NON GLOBALE, ed e' cio' che rende la cosa
 * possibile: mettere in raw la propria console non tocca quella dove gira la
 * shell, che continua a ricevere righe intere con l'eco.
 *
 * ! MA PRENDERE LA TASTIERA E' UNA DECISIONE CHE RIGUARDA UN ALTRO PROGRAMMA,
 * quindi non si fa di nascosto. Se il server e' in PRIMO PIANO sulla propria
 * console, la tastiera e' sua per definizione. Se e' in sfondo non la tocca —
 * o lascerebbe muta la shell che sta usando quella stessa console — a meno che
 * non gliela si dia esplicitamente con -t.
 * --------------------------------------------------------------------------- */
static int g_kbd_pid = -1;
static unsigned int g_console = 0;
static unsigned int g_mio_pid = 0;
static unsigned int g_raw = 0;          /* la nostra console e' in raw */
static unsigned int g_forza_tastiera = 0;
static unsigned int g_key_chiesta = 0;  /* c'e' una READKEY in volo */
static unsigned int g_visibile = 1;     /* la nostra console e' quella a video */

static void kbd_modo(unsigned int raw)
{
    KbdSetMode m;

    if (g_kbd_pid < 0 || g_raw == raw) return;

    m.modo    = raw ? KBD_MODE_RAW : KBD_MODE_COOKED;
    m.console = g_console;
    if (ipc_send((unsigned int)g_kbd_pid, KBD_MSG_SETMODE, &m, sizeof(m)) < 0)
        return;

    g_raw = raw;
    g_key_chiesta = 0;
    if (g_verboso)
        printf("wserver: console %u in modo %s\n", g_console,
               raw ? "raw" : "cooked");
}

/* Decide a ogni giro se la tastiera e' nostra, e chiede il prossimo tasto. */
static void kbd_giro(void)
{
    ConsoleInfo ci;
    unsigned int nostra;

    if (console_info(&ci) != 0) return;

    /* ! LO SCHERMO SI COMPONE SOLO SE LA NOSTRA CONSOLE E' QUELLA A VIDEO.
     * La console di testo del kernel disegna nello STESSO framebuffer: se si
     * commuta con Alt+Fn e noi continuassimo a comporre, i due si
     * scriverebbero addosso a vicenda — e chi guarda vedrebbe finestre e
     * prompt sovrapposti senza capire di chi sia la colpa. */
    {
        unsigned int ora = (ci.visibile == g_console);

        /* ! TORNANDO VISIBILI SI RIDIPINGE TUTTO, e non e' un di piu': mentre
         * eravamo nascosti la console di TESTO ha scritto nello stesso
         * framebuffer. Senza questo, Alt+Fn e ritorno lascerebbe lo schermo
         * com'era — cioe' il prompt della shell sopra le finestre — finche'
         * qualcosa non cambia per conto suo. */
        if (ora && !g_visibile) g_sporco = 1;
        g_visibile = ora;
    }

    if (g_kbd_pid < 0) return;
    if (!g_visibile) { kbd_modo(0); return; }

    nostra = g_forza_tastiera || (ci.fg == g_mio_pid);
    kbd_modo(nostra);

    if (g_raw && !g_key_chiesta) {
        unsigned int c = g_console;
        if (ipc_send((unsigned int)g_kbd_pid, KBD_MSG_READKEY, &c, sizeof(c)) >= 0)
            g_key_chiesta = 1;
    }
}

/* Il tasto va alla finestra in cima, che e' il fuoco. */
static void kbd_tasto(unsigned int k)
{
    g_key_chiesta = 0;

    if (g_n_ordine == 0) return;

    {
        Finestra *f = &g_fin[g_ordine[g_n_ordine - 1]];
        WinEvento e;

        if (!f->usata) return;

        e.id      = f->id;
        e.tipo    = WIN_EV_TASTO;
        e.x = 0; e.y = 0;
        e.bottoni = 0;
        e.tasto   = k;          /* gia' tradotto: NON e' uno scancode */
        (void)ipc_send(f->pid, WIN_MSG_EVENTO, &e, sizeof(e));
    }
}

static int g_mouse_pid = -1;

static void mouse_trova(void)
{
    /* ! IL MESSAGGIO E' LO STESSO PER TUTT'E DUE, e cambia solo a chi si
     * manda: MOUSE_MSG_LEGGI lo capisce sia un driver dedicato sia il driver
     * della tastiera, che il mouse PS/2 lo serve dallo stesso 8042. */
    g_mouse_pid = ipc_lookup(MOUSE_SERVICE_NAME);
    if (g_mouse_pid >= 0) return;

    g_mouse_pid = ipc_lookup(KBD_SERVICE_NAME);
}

/* ! CHIEDE E BASTA: LA RISPOSTA LA RACCOGLIE CHI LEGGE LA MAILBOX.
 *
 * Qui c'era una ipc_recv_timeout() sua, e leggeva QUALUNQUE messaggio
 * trovasse — compresa la richiesta di un client che stava chiedendo una
 * finestra, che finiva interpretata come uno stato del mouse e spariva. Il
 * sintomo era un'applicazione che una volta su tre diceva «il server non
 * risponde», e non somigliava per niente a «qualcuno mi ruba i messaggi».
 *
 * ! LA REGOLA E' UNA SOLA: LA MAILBOX LA LEGGE UN POSTO SOLO. Due funzioni
 * che pescano dalla stessa coda si rubano i messaggi a vicenda, e chi perde
 * dipende da quale delle due e' arrivata prima — cioe' dal caso. */
/* ! UNA RICHIESTA PER VOLTA, E NON E' EDUCAZIONE: E' CORRETTEZZA.
 *
 * Chiedendo lo stato del mouse a ogni giro senza aspettare la risposta, le
 * risposte si accumulavano nella NOSTRA mailbox — che e' profonda quattro
 * messaggi. Piena quella, ogni ipc_send verso di noi falliva, compresa la
 * consegna di un tasto. E il servizio 'kbd', quando la consegna di un tasto
 * fallisce, RIMETTE LA CONSOLE IN COOKED:
 *
 *     c->keyreader_pid = 0;
 *     kbd_set_mode(n, KBD_MODE_COOKED);
 *
 * Da li' i tasti tornavano alla shell. Il sintomo era «il server prende il
 * modo raw ma i tasti vanno alla shell» — e la causa non era nel modo raw
 * ne' nella tastiera: era che ci eravamo riempiti la posta da soli. */
static unsigned int g_mouse_chiesto = 0;

static void mouse_chiedi(void)
{
    unsigned int attendi = 0;

    if (g_mouse_pid < 0 || g_mouse_chiesto) return;
    if (ipc_send((unsigned int)g_mouse_pid, MOUSE_MSG_LEGGI,
                 &attendi, sizeof(attendi)) >= 0)
        g_mouse_chiesto = 1;
}

static void mouse_stato(const MouseStato *s)
{
    if (s->dx || s->dy || s->bottoni != g_bottoni) g_sporco = 1;

    g_px += s->dx;
    g_py += s->dy;
    if (g_px < 0) g_px = 0;
    if (g_py < 0) g_py = 0;
    if (g_px >= (int)g_fb_w) g_px = (int)g_fb_w - 1;
    if (g_py >= (int)g_fb_h) g_py = (int)g_fb_h - 1;

    g_bottoni = s->bottoni;
}

static void mouse_agisci(void)
{
    unsigned int giu = (g_bottoni & MOUSE_BTN_SIN) &&
                       !(g_bottoni_prec & MOUSE_BTN_SIN);
    unsigned int su  = !(g_bottoni & MOUSE_BTN_SIN) &&
                        (g_bottoni_prec & MOUSE_BTN_SIN);
    unsigned int dove = 0;
    int idx;

    if (g_trascino >= 0) {
        if (su) { g_trascino = -1; }
        else {
            g_fin[g_trascino].x = (unsigned int)(g_px - g_tr_dx);
            g_fin[g_trascino].y = (unsigned int)(g_py - g_tr_dy);
            g_sporco = 1;
        }
        g_bottoni_prec = g_bottoni;
        return;
    }

    idx = sotto(g_px, g_py, &dove);
    if (idx < 0) { g_bottoni_prec = g_bottoni; return; }

    if (giu) {
        in_cima(idx);
        g_sporco = 1;

        if (dove == 2) {
            manda_evento(&g_fin[idx], WIN_EV_CHIUDI, g_px, g_py, 0, 0);
        } else if (dove == 1) {
            /* ! SOLO LA BARRA TRASCINA, e non tutta la finestra: trascinare
             * dall'area del client vorrebbe dire che un'applicazione non puo'
             * mai ricevere un clic. */
            g_trascino = idx;
            g_tr_dx = g_px - (int)g_fin[idx].x;
            g_tr_dy = g_py - (int)g_fin[idx].y;
        } else {
            manda_evento(&g_fin[idx], WIN_EV_MOUSE_GIU, g_px, g_py,
                         g_bottoni, 0);
        }
    } else if (su && dove == 0) {
        manda_evento(&g_fin[idx], WIN_EV_MOUSE_SU, g_px, g_py, g_bottoni, 0);
    }

    g_bottoni_prec = g_bottoni;
}

/* -----------------------------------------------------------------------------
 * Le richieste dei client
 * --------------------------------------------------------------------------- */
static void crea(unsigned int pid, const WinCrea *c)
{
    WinCreata r;
    ShmZona   z;
    int i;

    memset(&r, 0, sizeof(r));

    for (i = 0; i < FINESTRE_MAX; i++) if (!g_fin[i].usata) break;
    if (i == FINESTRE_MAX) {
        (void)ipc_send(pid, WIN_MSG_CREATA, &r, sizeof(r));
        return;
    }

    /* ! LA MISURA SI STRINGE, NON SI RIFIUTA. Una finestra piu' grande dello
     * schermo e' una richiesta ragionevole scritta da chi non sa quanto e'
     * grande lo schermo; rifiutarla vorrebbe dire che ogni applicazione deve
     * chiedere prima. La misura CONCESSA torna indietro nella risposta, come
     * fa gia' shm_apri con i byte. */
    r.larghezza = c->larghezza ? c->larghezza : 100;
    r.altezza   = c->altezza   ? c->altezza   : 100;
    if (r.larghezza > g_fb_w) r.larghezza = g_fb_w;
    if (r.altezza   > g_fb_h) r.altezza   = g_fb_h;

    memset(&z, 0, sizeof(z));
    win_nome_zona(z.nome, g_prossimo_id);
    z.byte = r.larghezza * r.altezza * 4;
    z.flag = SHM_CREA;

    if (shm_apri(&z) != 0) {
        (void)ipc_send(pid, WIN_MSG_CREATA, &r, sizeof(r));
        return;
    }

    g_fin[i].usata     = 1;
    g_fin[i].id        = g_prossimo_id++;
    g_fin[i].pid       = pid;
    g_fin[i].x         = c->x;
    g_fin[i].y         = c->y;
    g_fin[i].w         = r.larghezza;
    g_fin[i].h         = r.altezza;
    g_fin[i].stile     = c->stile;
    g_fin[i].zona_virt = z.virt;
    g_fin[i].zona_byte = z.byte;
    memcpy(g_fin[i].titolo, c->titolo, WIN_TITOLO_LEN);
    g_fin[i].titolo[WIN_TITOLO_LEN - 1] = '\0';

    /* Il client parte con un'area di un colore noto invece che con la
     * memoria che c'era: una finestra nuova piena di spazzatura sembra un
     * difetto del client. */
    {
        unsigned int *p = (unsigned int *)z.virt;
        unsigned int n = r.larghezza * r.altezza, k;
        for (k = 0; k < n; k++) p[k] = C_CLIENT;
    }

    in_cima(i);
    g_sporco = 1;

    r.id    = g_fin[i].id;
    r.byte  = z.byte;
    r.passo = r.larghezza * 4;

    if (g_verboso)
        printf("wserver: finestra %u per il PID %u, %ux%u «%s»\n",
               r.id, pid, r.larghezza, r.altezza, g_fin[i].titolo);

    (void)ipc_send(pid, WIN_MSG_CREATA, &r, sizeof(r));
}

static void distruggi(int idx)
{
    unsigned int k, j = 0;

    if (idx < 0) return;

    if (g_fin[idx].zona_virt) shm_chiudi((void *)g_fin[idx].zona_virt);
    memset(&g_fin[idx], 0, sizeof(Finestra));

    for (k = 0; k < g_n_ordine; k++)
        if (g_ordine[k] != (unsigned int)idx) g_ordine[j++] = g_ordine[k];
    g_n_ordine = j;
    if (g_trascino == idx) g_trascino = -1;
    g_sporco = 1;
}

/* Rende 1 se ha servito un messaggio, 0 se la coda era vuota. */
static int servi_messaggio(void)
{
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];

    if (ipc_recv_timeout(&meta, buf, sizeof(buf), 5) < 0) return 0;

    /* La risposta del mouse arriva nella stessa coda di tutto il resto: e'
     * qui che si distingue, e da nessun'altra parte. */
    if (meta.tipo == KBD_MSG_KEY) {
        unsigned int k = 0;
        if (meta.len >= sizeof(k)) { memcpy(&k, buf, sizeof(k)); kbd_tasto(k); }
        else g_key_chiesta = 0;
        return 1;
    }

    if (meta.tipo == MOUSE_MSG_STATO) {
        MouseStato s;
        g_mouse_chiesto = 0;
        if (meta.len >= sizeof(s)) {
            memcpy(&s, buf, sizeof(s));
            mouse_stato(&s);
            mouse_agisci();
        }
        return 1;
    }

    switch (meta.tipo) {
    case WIN_MSG_CREA:
        if (meta.len >= sizeof(WinCrea)) crea(meta.sender_pid, (WinCrea *)buf);
        break;

    case WIN_MSG_DISTRUGGI: {
        WinRegione *w = (WinRegione *)buf;
        int idx;
        if (meta.len < sizeof(WinRegione)) break;
        idx = trova_id(w->id);
        /* ! SI CONTROLLA CHE SIA SUA. Senza, un processo qualunque potrebbe
         * chiudere le finestre di un altro conoscendone il numero — e i
         * numeri sono piccoli e consecutivi. */
        if (idx >= 0 && g_fin[idx].pid == meta.sender_pid) distruggi(idx);
        break;
    }

    case WIN_MSG_SPOSTA: {
        WinRegione *w = (WinRegione *)buf;
        int idx;
        if (meta.len < sizeof(WinRegione)) break;
        idx = trova_id(w->id);
        if (idx >= 0 && g_fin[idx].pid == meta.sender_pid) {
            g_fin[idx].x = w->x;
            g_fin[idx].y = w->y;
            g_sporco = 1;
        }
        break;
    }

    case WIN_MSG_TITOLO: {
        WinTitolo *t = (WinTitolo *)buf;
        int idx;
        if (meta.len < sizeof(WinTitolo)) break;
        idx = trova_id(t->id);
        if (idx >= 0 && g_fin[idx].pid == meta.sender_pid) {
            memcpy(g_fin[idx].titolo, t->titolo, WIN_TITOLO_LEN);
            g_fin[idx].titolo[WIN_TITOLO_LEN - 1] = '\0';
            g_sporco = 1;
        }
        break;
    }

    case WIN_MSG_PRIMO: {
        WinRegione *w = (WinRegione *)buf;
        int idx;
        if (meta.len < sizeof(WinRegione)) break;
        idx = trova_id(w->id);
        if (idx >= 0 && g_fin[idx].pid == meta.sender_pid) { in_cima(idx); g_sporco = 1; }
        break;
    }

    case WIN_MSG_AGGIORNA:
        g_sporco = 1;
        /* Il client ha finito di disegnare. Componendo a ogni giro non c'e'
         * niente da fare: resta nel protocollo perche' quando ci sara' la
         * lista delle regioni sporche sara' QUESTO il messaggio che la
         * riempie, e cambiare il protocollo dopo costa piu' che prevederlo. */
        break;

    default:
        break;
    }
    return 1;
}

/* -----------------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    VideoInfo v;
    MmioZona  m;
    int i, chiesta = -1;

    /* ! CHI SIAMO E DOVE SIAMO, PRIMA DI DIRE QUALUNQUE COSA. Il primo
     * messaggio con lo schermo citava g_console mentre era ancora zero, e
     * diceva «console 0» di un processo che girava sulla 1: un numero
     * sbagliato dentro una diagnostica costa piu' di nessun numero, perche'
     * lo si crede. */
    {
        ConsoleInfo ci;

        g_mio_pid = (unsigned int)getpid();
        if (console_info(&ci) == 0) g_console = ci.mia;
    }

    log_seriale("wserver: sono partito");

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'v') g_verboso = 1;
        if (argv[i][0] == '-' && argv[i][1] == 't') g_forza_tastiera = 1;
        if (argv[i][0] == '-' && argv[i][1] == 'c' && i + 1 < argc)
            chiesta = atoi(argv[++i]);
    }

    /* ! -c N: SI RIPARTE SU UN'ALTRA CONSOLE, E POI SI ESCE.
     *
     * E' il modo di stare su una console tutta propria mentre su un'altra
     * continua a girare una shell — con Alt+Fn si passa dall'una all'altra.
     * Una console non si puo' cambiare a se stessi: la si sceglie quando un
     * processo NASCE, ed e' per questo che serve rilanciarsi.
     *
     * ! E SI CONTROLLA DI NON ESSERCI GIA', o si rinascerebbe per sempre. */
    {
        ConsoleInfo ci;

        if (chiesta >= 0 && console_info(&ci) == 0 && ci.mia != (unsigned int)chiesta) {
            int pid = spawn_su_console(argv[0], argv, 0, 0, 0, chiesta);

            if (pid < 0) {
                printf("wserver: non riesco a nascere sulla console %d\n", chiesta);
                return 1;
            }
            printf("wserver: riparto sulla console %d (PID %d); "
                   "con Alt+F%d ci si va\n", chiesta, pid, chiesta + 1);
            return 0;
        }
    }

    if (video_info(&v) != 0) {
        log_seriale("wserver: video_info non risponde");
        printf("wserver: video_info non risponde\n");
        return 1;
    }
    if (v.larghezza == 0 || v.fisico == 0) {
        log_seriale("wserver: lo schermo e' in modo TESTO");
        printf("wserver: lo schermo e' in modo TESTO.\n");
        printf("         Scegli una risoluzione e riavvia:  /dev/svga.drv 800x600\n");
        return 1;
    }

    m.fisico = v.fisico;
    m.byte   = v.passo * v.altezza;
    if (mmio_map(&m) != 0) {
        log_seriale("wserver: mmio_map del framebuffer rifiutata");
        printf("wserver: mmio_map del framebuffer rifiutata.\n");
        printf("         L'eseguibile dev'essere caricato da un file .drv\n");
        return 1;
    }

    g_fb       = (unsigned char *)m.virt;
    g_fb_passo = v.passo;
    g_fb_w     = v.larghezza;
    g_fb_h     = v.altezza;
    g_fb_bit   = v.bit;

    printf("wserver: schermo %ux%u a %u bit, framebuffer fisico 0x%x\n",
           g_fb_w, g_fb_h, g_fb_bit, v.fisico);
    {
        /* ! ANCHE SULLA SERIALE, perche' se giriamo su una console non
         * visibile la riga qui sopra non la legge nessuno. */
        char m[120];
        sprintf(m, "wserver: schermo %ux%u a %u bit, fb 0x%x, console %u",
                g_fb_w, g_fb_h, g_fb_bit, v.fisico, g_console);
        log_seriale(m);
    }

    if (ipc_register(WIN_SERVIZIO) < 0) {
        printf("wserver: ipc_register('%s') fallita — ce n'e' gia' uno?\n",
               WIN_SERVIZIO);
        return 1;
    }

    {
        g_kbd_pid = ipc_lookup(KBD_SERVICE_NAME);
        if (g_kbd_pid < 0)
            printf("wserver: !  servizio 'kbd' assente: niente tastiera\n");
        else if (!g_forza_tastiera)
            printf("wserver: la tastiera e' mia solo in primo piano "
                   "(console %u); con -t la prendo comunque\n", g_console);
    }

    mouse_trova();
    if (g_mouse_pid < 0)
        printf("wserver: !  nessun mouse: le finestre non si potranno muovere\n");

    g_px = (int)g_fb_w / 2;
    g_py = (int)g_fb_h / 2;

    printf("wserver: servizio '%s' attivo\n", WIN_SERVIZIO);
    log_seriale("wserver: servizio attivo, entro nel ciclo");

    /* ! IL CICLO NON ASPETTA A LUNGO SU NIENTE. Ha tre cose da fare — leggere
     * il mouse, servire i client, ricomporre — e fermarsi a lungo su una vuol
     * dire non fare le altre. Quando ci sara' la lista delle regioni sporche
     * questo diventera' un poll() vero su FD_IPC piu' il mouse, che e'
     * esattamente cio' per cui SYS_POLL esiste. */
    for (;;) {
        int n;

        mouse_chiedi();
        kbd_giro();

        /* ! SI SVUOTA LA CODA, NON SE NE PRENDE UNO PER GIRO. Lasciarci
         * dentro dei messaggi vuol dire tenerla piena, e una mailbox piena
         * fa fallire chi ci scrive — che qui vuol dire perdere i tasti e,
         * peggio, farsi rimettere la console in cooked dal servizio 'kbd'.
         * Il tetto c'e' perche' un client impazzito non ci tenga fermi. */
        for (n = 0; n < 16 && servi_messaggio(); n++) { }

        if (g_sporco && g_visibile) { componi(); g_sporco = 0; }
        usleep(20000);
    }
}
