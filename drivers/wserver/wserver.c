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

/* Il nome con cui ci si registra: «wserver» per root, «<uid>:wserver» per
 * chiunque altro. Vedi win_nome_servizio() in win_proto.h. */
static char g_servizio[32];

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


/* =============================================================================
 * MMX NEL COMPOSITORE — otto byte per volta invece di quattro
 *
 * ! IL COMPOSITORE E' L'UNICO POSTO DEL SISTEMA DOVE LA LARGHEZZA DELLA COPIA
 * SI VEDE. A 800x600x32 un fotogramma sono 1,83 MB scritti nel framebuffer, e
 * si riscrive tutto a ogni cambiamento: e' gia' stato il difetto che teneva il
 * server occupato tanto da far scadere le richieste dei client. Ovunque altro
 * in EX-OS si copiano decine di byte e non conta niente.
 *
 * ! I REGISTRI MMX SONO QUELLI DELL'x87, e per questo si puo' fare: il kernel
 * salva lo stato FPU al cambio di contesto (fnsave/fxsave, con commutazione
 * pigra via CR0_TS), quindi copre anche MMX. Senza quel salvataggio, due
 * processi che usassero MMX si calpesterebbero i registri a vicenda.
 *
 * ! E SI CHIAMA emms ALLA FINE DI OGNI GIRO. Dopo un'istruzione MMX i registri
 * x87 restano marcati «in uso»: la prima istruzione in virgola mobile che
 * arriva dopo — anche in un ALTRO processo, se lo scheduler entra prima —
 * trova uno stack che non e' suo. E' l'errore classico di MMX, e non da'
 * nessun sintomo finche' qualcuno non usa la virgola mobile.
 *
 * ! SI CONTROLLA A RUNTIME E SI RIPIEGA. La CPU di base dichiarata ha MMX, ma
 * un Pentium liscio no: senza il controllo, su quella macchina il server
 * morirebbe con un'istruzione non valida invece di andare piu' piano.
 * ============================================================================= */
static int g_mmx = 0;

static int mmx_c_e(void)
{
    unsigned int a = 0, b = 0, c = 0, d = 0;

    /* CPUID funzione 1: il bit 23 di EDX dice MMX. Che CPUID stessa esista e'
     * garantito dalla CPU di base (Pentium): sotto quella non si arriva
     * nemmeno qui, perche' il resto del sistema e' compilato per lei. */
    __asm__ __volatile__("cpuid"
                         : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                         : "a"(1));
    return (d & (1u << 23)) != 0;
}

/* Riempie `n` pixel a 32 bit col colore `c`. */
static void mmx_riempi32(unsigned int *d, unsigned int n, unsigned int c)
{
    unsigned int coppie = n >> 1;

    if (coppie) {
        __asm__ __volatile__(
            "movd      %2, %%mm0\n\t"
            "punpckldq %%mm0, %%mm0\n\t"   /* mm0 = colore:colore */
            "1:\n\t"
            "movq      %%mm0, (%0)\n\t"
            "addl      $8, %0\n\t"
            "decl      %1\n\t"
            "jnz       1b\n\t"
            "emms"
            : "+r"(d), "+r"(coppie)
            : "rm"(c)
            : "memory", "cc");
    }

    /* Il pixel dispari in coda, se c'e'. */
    if (n & 1u) *d = c;
}

/* Copia `n` pixel a 32 bit. */
static void mmx_copia32(unsigned int *d, const unsigned int *s, unsigned int n)
{
    unsigned int coppie = n >> 1;

    if (coppie) {
        __asm__ __volatile__(
            "1:\n\t"
            "movq      (%1), %%mm0\n\t"
            "movq      %%mm0, (%0)\n\t"
            "addl      $8, %0\n\t"
            "addl      $8, %1\n\t"
            "decl      %2\n\t"
            "jnz       1b\n\t"
            "emms"
            : "+r"(d), "+r"(s), "+r"(coppie)
            :
            : "memory", "cc");
    }

    if (n & 1u) *d = *s;
}

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

            if (g_mmx) { mmx_riempi32(p, w, c); continue; }
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

                if (g_mmx) { mmx_copia32(d, sr, ww); continue; }
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

/* =============================================================================
 * IL FUOCO NON E' L'ORDINE DI DISEGNO — corretto il 17 agosto 2026
 *
 * Fino a oggi il tasto andava a `g_ordine[g_n_ordine - 1]`, cioe' alla
 * finestra disegnata per ultima, con il commento «la finestra in cima, che e'
 * il fuoco». Era vero finche' l'ordine di disegno dipendeva solo da chi si era
 * portato davanti.
 *
 * Ha smesso di esserlo il 14 agosto, con WIN_ST_SOPRA: in_cima() rimette in
 * fondo all'ordine — cioe' in cima allo schermo — tutte le finestre «sopra»,
 * qualunque cosa sia appena salita. E la barra delle applicazioni e' «sopra».
 *
 * ! QUINDI DA QUEL GIORNO LA BARRA SI PRENDEVA OGNI TASTO, e nessuna finestra
 * poteva riceverne uno finche' il program manager era acceso. L'editor
 * sembrava sordo: non arrivava niente, ne' le lettere ne' Ctrl+S. Il difetto
 * non era nell'editor ne' in WIN_ST_SOPRA — era in questa funzione, che
 * decideva DUE cose mentre il suo nome ne prometteva una.
 *
 * Adesso il fuoco e' una variabile sua. Chi sale prende il fuoco; le finestre
 * «sopra» e lo sfondo restano dove devono stare a schermo senza portarselo
 * via, e lo prendono solo se ci si clicca dentro davvero.
 * ============================================================================= */
static int g_fuoco = -1;        /* indice della finestra che riceve i tasti */

/* Chi puo' tenere il fuoco per conto suo, cioe' senza esserci stato messo da
 * un clic: non lo sfondo (non e' una finestra con cui si parla) e non la barra
 * (c'e' sempre, e vincerebbe sempre). */
static int prende_fuoco_da_solo(int idx)
{
    return !(g_fin[idx].stile & (WIN_ST_SFONDO | WIN_ST_SOPRA));
}

/* Il fuoco quando quello di prima se n'e' andato: la finestra normale piu' in
 * alto. Se non ce n'e' nessuna, nessun fuoco — meglio di uno a caso. */
static void fuoco_ricalcola(void)
{
    int k;

    g_fuoco = -1;
    for (k = (int)g_n_ordine - 1; k >= 0; k--) {
        int idx = (int)g_ordine[k];
        if (g_fin[idx].usata && prende_fuoco_da_solo(idx)) { g_fuoco = idx; return; }
    }
}

/* La finestra modale di un processo, se ne ha una aperta, o -1.
 *
 * ! SI CERCA DALLA CIMA, cosi' con due dialoghi annidati — «salva?» che apre
 * «sovrascrivo?» — risponde l'ultimo aperto, che e' quello che sta chiedendo.
 *
 * ! E LA RICERCA E' PER PROCESSO E NON PER FINESTRA PADRE, perche' il server
 * non sa niente di parentele: dal suo punto di vista un'applicazione e' un
 * pid con delle finestre. Legare il blocco alla parentela vorrebbe dire
 * portare l'albero delle finestre dentro il protocollo per usarlo qui, e qui
 * soltanto. */
static int modale_di(unsigned int pid)
{
    int k;

    for (k = (int)g_n_ordine - 1; k >= 0; k--) {
        int idx = (int)g_ordine[k];

        if (g_fin[idx].usata && (g_fin[idx].stile & WIN_ST_MODALE) &&
            g_fin[idx].pid == pid)
            return idx;
    }
    return -1;
}

static void in_cima(int idx)
{
    unsigned int k, j = 0;

    /* ! IL FUOCO SI DA' QUI, PRIMA DEL RIORDINO, e non si legge dall'ordine
     * dopo: e' proprio il riordino a mentire. Chi si porta davanti prende i
     * tasti anche se poi lo schermo lo disegna sotto alla barra. */
    if (g_fin[idx].usata && !(g_fin[idx].stile & WIN_ST_SFONDO)) g_fuoco = idx;

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

/* Il tasto va alla finestra col FUOCO, che non e' quella disegnata per ultima:
 * vedi il blocco sopra in_cima(). */
static void kbd_tasto(unsigned int k)
{
    g_key_chiesta = 0;

    if (g_n_ordine == 0) return;
    if (g_fuoco < 0 || !g_fin[g_fuoco].usata) fuoco_ricalcola();
    if (g_fuoco < 0) return;

    /* ! I TASTI SEGUONO LA STESSA REGOLA DEI CLIC, e dimenticarlo sarebbe il
     * modo piu' facile di fare un modale finto: si blocca il mouse, si prova
     * col mouse, sembra fatto — e intanto nel testo dietro si continua a
     * scrivere. Qui non si buttano: si dirottano alla modale, che e' l'unica
     * che possa farci qualcosa (Invio, Esc). */
    {
        int md = modale_di(g_fin[g_fuoco].pid);

        if (md >= 0) g_fuoco = md;
    }

    {
        Finestra *f = &g_fin[g_fuoco];
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

    /* ! IL CLIC SU UNA FINESTRA BLOCCATA NON SI PERDE IN SILENZIO: porta
     * davanti la modale. Buttarlo e basta darebbe un'applicazione che non
     * risponde e nessun indizio sul perche' — e la finestra che aspetta la
     * risposta potrebbe essere finita sotto un'altra. */
    {
        int md = modale_di(g_fin[idx].pid);

        if (md >= 0 && md != idx) {
            if (giu) { in_cima(md); g_sporco = 1; }
            g_bottoni_prec = g_bottoni;
            return;
        }
    }

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

    /* ! CHI SE NE VA SI PORTA VIA IL FUOCO, e va ridato a qualcuno. Senza,
     * g_fuoco resterebbe l'indice di uno slot azzerato: i tasti finirebbero a
     * una finestra che non c'e' piu' — cioe' da nessuna parte, e chiudere un
     * editor renderebbe muto quello rimasto aperto. */
    if (g_fuoco == idx) fuoco_ricalcola();

    g_sporco = 1;
}

/* -----------------------------------------------------------------------------
 * Le finestre dei client morti
 *
 * ! UN'APPLICAZIONE CHE ESCE NON DICE NIENTE AL SERVER, e non c'e' modo di
 * obbligarla: se muore per un errore non ha nemmeno la possibilita' di dirlo.
 * La sua finestra resta disegnata dov'era, con dentro l'ultima cosa che aveva
 * scritto — e chi la guarda ci clicca sopra e non capisce perche' non risponde.
 * Trovato provando il dialogo modale: dopo Ctrl+Q la shell dice «terminato» e
 * il rettangolo bianco dell'editor resta sullo schermo.
 *
 * ! SI CHIEDE AL KERNEL CHI E' VIVO, NON SI ASPETTA CHE ipc_send FALLISCA.
 * Aspettare l'errore vuol dire accorgersene solo quando c'e' qualcosa da
 * mandare, cioe' quando qualcuno clicca sul fantasma: la finestra sparirebbe
 * al primo clic, che e' PEGGIO di non farla sparire — sembrerebbe che il clic
 * abbia fatto qualcosa.
 *
 * ! GLI ZOMBIE CONTANO COME MORTI. Un processo uscito e non ancora raccolto dal
 * padre resta nell'elenco: e' li' solo perche' qualcuno legga il suo codice di
 * uscita, non ha piu' niente sullo schermo da difendere.
 *
 * ! E SE L'ELENCO NON CI STA, NON SI RACCOGLIE NIENTE. Un elenco troncato vuol
 * dire pid vivi che non risultano, cioe' finestre VIVE distrutte: il danno
 * peggiore possibile qui. Fra il non pulire e il pulire in base a un elenco di
 * cui non ci si fida, si non pulisce.
 * --------------------------------------------------------------------------- */
#define PROC_ZOMBIE     4
#define PID_TRACCIATI   64

static void raccogli_morti(void)
{
    ProcInfo     v[PROCINFO_MAX_BATCH];
    unsigned int vivi[PID_TRACCIATI];
    unsigned int n_vivi = 0, start = 0;
    int n, i, k;

    for (;;) {
        n = procinfo(v, PROCINFO_MAX_BATCH, start);
        if (n <= 0) break;

        for (i = 0; i < n; i++) {
            if (v[i].state == PROC_ZOMBIE) continue;
            if (n_vivi >= PID_TRACCIATI) return;    /* elenco troncato: basta */
            vivi[n_vivi++] = v[i].pid;
        }
        start += (unsigned int)n;
        if (n < PROCINFO_MAX_BATCH) break;
    }

    if (n_vivi == 0) return;        /* non ho saputo niente: non tocco niente */

    for (i = 0; i < FINESTRE_MAX; i++) {
        int vivo = 0;

        if (!g_fin[i].usata) continue;

        for (k = 0; k < (int)n_vivi; k++)
            if (vivi[k] == g_fin[i].pid) { vivo = 1; break; }

        if (!vivo) {
            if (g_verboso)
                printf("wserver: il pid %u non c'e' piu', tolgo la sua finestra\n",
                       g_fin[i].pid);
            distruggi(i);
        }
    }
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

    /* =========================================================================
     * ! «-i» SI RISPONDE E SI ESCE, PRIMA DI QUALUNQUE ALTRA COSA.
     *
     * `hwconfig -d` sceglie quali driver installare provandoli uno per uno con
     * `-i`: si aspetta che ognuno dica cosa fa e ritorni. Fino al 17 agosto
     * 2026 wserver non conosceva quel flag, lo ignorava, e PARTIVA PER DAVVERO
     * — e un server non esce mai. L'installazione dal CD si fermava li', dopo
     * aver gia' sostituito kernel e stage2: un disco a meta', e un log che
     * finiva con «wserver: entro nel ciclo» invece che con un errore.
     *
     * Non si e' mai visto installando dal floppy, che nel suo catalogo
     * wserver.drv non ce l'ha. E' bastato che l'installazione dal CD diventasse
     * una cosa che si prova.
     * ========================================================================= */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0) {
            printf("wserver: il server a finestre di EX-OS.\n");
            printf("         Nessuna periferica propria: usa il framebuffer\n");
            printf("         che ha impostato Stage 2. Serve solo a chi vuole\n");
            printf("         l'interfaccia grafica — si avvia con  exwin\n");
            return 0;
        }
    }

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

    /* =====================================================================
     * ! SI CHIEDE fb_map(), NON PIU' mmio_map(), dal 17 agosto 2026.
     *
     * mmio_map mappa un indirizzo fisico QUALUNQUE scelto da chi chiama, e per
     * questo e' riservata a root: con i registri di un dispositivo si arriva
     * al DMA, e col DMA a tutta la RAM. Finche' bastava chiamarsi `.drv` per
     * ottenerla, un utente normale che eseguiva un driver possedeva la
     * macchina — i permessi sui file accanto a una porta che non chiedeva
     * niente.
     *
     * fb_map non prende argomenti: il framebuffer lo sa il kernel. Chi chiama
     * non puo' sbagliare indirizzo e chi attacca non puo' sceglierne uno. E'
     * anche cio' che permette a QUESTO server di girare senza privilegi —
     * cioe' alla grafica di esistere in multiutenza.
     * ===================================================================== */
    (void)m;
    g_fb = (unsigned char *)fb_map();
    if (g_fb == 0) {
        log_seriale("wserver: fb_map del framebuffer rifiutata");
        printf("wserver: non riesco a mappare il framebuffer (%s).\n",
               strerror(errno));
        printf("         Serve un modo grafico: /dev/svga.drv 800x600\n");
        return 1;
    }

    /* ! SI GUARDA UNA VOLTA SOLA, ALL'AVVIO. Chiedere CPUID a ogni riga
     * costerebbe piu' di quanto MMX faccia risparmiare. */
    g_mmx = mmx_c_e();

    /* ! «-nommx» SPEGNE LA STRADA VELOCE, e non e' un'opzione per curiosi: la
     * strada di ripiego esiste per le macchine senza MMX, e su una macchina
     * CON MMX non verrebbe mai eseguita. Un codice che non si esegue mai e' un
     * codice di cui non si sa se funziona — e questo flag e' anche il modo di
     * provare che le due strade disegnano lo STESSO schermo, confrontando due
     * fotografie byte per byte. */
    for (i = 1; i < argc; i++)
        if (strcmp(argv[i], "-nommx") == 0) g_mmx = 0;

    g_fb_passo = v.passo;
    g_fb_w     = v.larghezza;
    g_fb_h     = v.altezza;
    g_fb_bit   = v.bit;

    /* ! SULLA SERIALE, non con printf: questo processo gira su una console
     * sua, e un messaggio scritto li' non lo legge nessuno. E' lo stesso
     * motivo per cui esiste SYS_LOG. */
    log_seriale(g_mmx ? "wserver: MMX attivo, otto byte per volta"
                      : "wserver: MMX assente, quattro byte per volta");
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

    win_nome_servizio(g_servizio, sizeof(g_servizio));

    if (ipc_register(g_servizio) < 0) {
        printf("wserver: ipc_register('%s') fallita — ce n'e' gia' uno?\n",
               g_servizio);
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

    printf("wserver: servizio '%s' attivo\n", g_servizio);
    log_seriale("wserver: servizio attivo, entro nel ciclo");

    /* ! IL CICLO NON ASPETTA A LUNGO SU NIENTE. Ha tre cose da fare — leggere
     * il mouse, servire i client, ricomporre — e fermarsi a lungo su una vuol
     * dire non fare le altre. Quando ci sara' la lista delle regioni sporche
     * questo diventera' un poll() vero su FD_IPC piu' il mouse, che e'
     * esattamente cio' per cui SYS_POLL esiste. */
    for (;;) {
        static unsigned int giri = 0;
        int n;

        mouse_chiedi();
        kbd_giro();

        /* ! SI SVUOTA LA CODA, NON SE NE PRENDE UNO PER GIRO. Lasciarci
         * dentro dei messaggi vuol dire tenerla piena, e una mailbox piena
         * fa fallire chi ci scrive — che qui vuol dire perdere i tasti e,
         * peggio, farsi rimettere la console in cooked dal servizio 'kbd'.
         * Il tetto c'e' perche' un client impazzito non ci tenga fermi. */
        for (n = 0; n < 16 && servi_messaggio(); n++) { }

        /* ! UNA VOLTA AL SECONDO, NON A OGNI GIRO. Il giro e' di 20 ms e
         * procinfo e' una syscall che copia una tabella: farla cinquanta volte
         * al secondo per una cosa che cambia quando un'applicazione si chiude
         * sarebbe spendere sempre per accorgersi prima di qualcosa che non ha
         * fretta. */
        if (++giri >= 50) { giri = 0; raccogli_morti(); }

        if (g_sporco && g_visibile) { componi(); g_sporco = 0; }
        usleep(20000);
    }
}
