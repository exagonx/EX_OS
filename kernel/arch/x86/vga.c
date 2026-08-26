/* =============================================================================
 * kernel/arch/x86/vga.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * VGA in modo testo, e le CONSOLE VIRTUALI.
 *
 * Fino ad agosto 2026 qui c'era un solo schermo: le variabili vga_row,
 * vga_col e vga_color erano globali, e chiunque scrivesse finiva sugli
 * stessi 4000 byte a 0xB8000. Bastava finché un programma alla volta
 * teneva la console — che è esattamente il motivo per cui la shell
 * aspettava in waitpid() e non c'era modo di tornare al prompt senza
 * chiudere il programma in esecuzione.
 *
 * Ora ci sono VGA_N_CONSOLE schermi indipendenti. Ognuno ha il proprio
 * buffer da 4000 byte, il proprio cursore, il proprio colore e il
 * proprio stato del parser ANSI; uno solo per volta è VISIBILE, cioè
 * copiato nella memoria video vera. Scrivere su una console nascosta è
 * legittimo e non si vede: il programma continua a lavorare e ritrova
 * il proprio schermo intatto quando ci si torna sopra.
 *
 * Perché lo stato del parser ANSI è per-console e non globale: due
 * programmi su due console diverse possono avere ciascuno una sequenza
 * di escape a metà nello stesso istante — uno preemptato dopo aver
 * scritto "ESC[" e l'altro schedulato subito dopo. Con un parser solo,
 * i parametri del secondo verrebbero attribuiti alla sequenza del primo.
 *
 * Chi decide su quale console finisce una scrittura è il CHIAMANTE, che
 * la passa come parametro: sys_write usa proc->console. Non c'è una
 * variabile globale "console corrente" proprio perché il kernel è
 * preemptabile — il timer può interrompere una scrittura a metà e dare
 * la CPU a un processo di un'altra console, che quella variabile la
 * troverebbe cambiata sotto i piedi.
 * ============================================================================= */

#include "kernel.h"
#include "pmm.h"
#include "paging.h"
#include "vga.h"

/* =============================================================================
 * Costanti VGA
 * ============================================================================= */
#define VGA_BASE        ((volatile uint16_t *)0xB8000)  /* Indirizzo memoria VGA */
/* ! LE DIMENSIONI SONO A RUNTIME, e non lo erano fino ad agosto 2026.
 * In modo testo restano 80x25, che e' cio' che la scheda offre; in
 * modalita' grafica VESA le decide la risoluzione: 640x480 da' 80x30,
 * 800x600 da' 100x37, 1024x768 da' 128x48 — con il font 8x16 di
 * font8x16.c. E' tutto il punto di avere una modalita' grafica: piu'
 * testo, non testo piu' grande. */
#define VGA_MAX_COLS    128
#define VGA_MAX_ROWS    48
#define VGA_MAX_TOTAL   (VGA_MAX_COLS * VGA_MAX_ROWS)

static uint32_t g_cols   = 80;
static uint32_t g_righe  = 25;
static uint32_t g_totale = 80 * 25;

/* Porte hardware cursore VGA */
#define VGA_CTRL_PORT   0x3D4   /* Control register index */
#define VGA_DATA_PORT   0x3D5   /* Control register data */
#define VGA_CURSOR_HIGH 0x0E    /* Registro posizione cursore (byte alto) */
#define VGA_CURSOR_LOW  0x0F    /* Registro posizione cursore (byte basso) */

/* =============================================================================
 * Stato di una console
 * ============================================================================= */
typedef enum { ANSI_NONE, ANSI_ESC, ANSI_CSI } AnsiState;

typedef struct {
    /* Dimensionato al MASSIMO, non alla risoluzione corrente: la memoria
     * delle console si alloca una volta sola, all'avvio, prima che si sappia
     * quale modalita' verra' impostata. 128*48*2 byte per console, 49 KB in
     * tutto per quattro: il prezzo di non avere un allocatore qui dentro. */
    uint16_t  cella[VGA_MAX_TOTAL];
    uint8_t   row, col;
    uint8_t   colore;
    uint8_t   cursore_on;

    /* Parser CSI: ESC [ [?] <param>[;<param>...] <finale>
     *
     * Nato minimale per il solo SGR (i colori del prompt della shell).
     * Da agosto 2026 copre anche posizionamento del cursore e
     * cancellazioni, perché senza quelli un programma a schermo intero
     * non può esistere: /bin/gfedit disegna passando di qui, e
     * nient'altro in ring3 può raggiungere la memoria VGA (che nello
     * spazio utente non è mappata). */
    AnsiState state;
    int       params[4];
    uint8_t   param_count;
    uint8_t   cur_param_empty;
    uint8_t   privata;            /* '?' subito dopo il '[' */
} Console;

static Console  g_console[VGA_N_CONSOLE];
static uint32_t g_visibile = 0;
static volatile uint16_t *vga_buf = VGA_BASE;

/* Specchio seriale dell'output: vedi vga_set_serial_mirror(). */
static uint8_t serial_mirror = 1;

static Console *cons(uint32_t n)
{
    if (n >= VGA_N_CONSOLE) n = 0;
    return &g_console[n];
}

/* La console 0 è quella di SISTEMA: ci finiscono i messaggi del kernel
 * (klog, kprintf) e tutto ciò che viene scritto prima che esista un
 * processo. Le API storiche senza numero di console lavorano su questa,
 * così ogni chiamante già esistente continua a comportarsi come prima. */
#define SISTEMA (&g_console[0])

/* =============================================================================
 * Console seriale di debug (COM1, 38400 8N1)
 * Specchia l'output della console di sistema sulla porta seriale, così
 * il log completo di boot resta leggibile anche dopo lo scroll dello schermo
 * (QEMU: -serial file:log.txt). Nessun effetto sul comportamento del kernel.
 * ============================================================================= */
#define COM1_BASE       0x3F8

static void serial_init(void)
{
    port_outb(COM1_BASE + 1, 0x00);   /* interrupt off */
    port_outb(COM1_BASE + 3, 0x80);   /* DLAB on */
    port_outb(COM1_BASE + 0, 0x03);   /* divisor lo: 115200/3 = 38400 baud */
    port_outb(COM1_BASE + 1, 0x00);   /* divisor hi */
    port_outb(COM1_BASE + 3, 0x03);   /* DLAB off, 8N1 */
    port_outb(COM1_BASE + 2, 0xC7);   /* FIFO on, clear, 14-byte threshold */
    port_outb(COM1_BASE + 4, 0x03);   /* DTR + RTS */
}

static void serial_putchar(char c)
{
    uint32_t spin = 0;

    if (!serial_mirror) return;

    /* Attende che il THR sia libero, con guardia per non bloccare il kernel
     * se la seriale non esiste (hardware reale senza COM1). */
    while (!(port_inb(COM1_BASE + 5) & 0x20)) {
        if (++spin > 100000) return;
    }
    port_outb(COM1_BASE, (uint8_t)c);

    if (c == '\n') serial_putchar('\r');
}

/* =============================================================================
 * vga_make_entry — Combina carattere e attributo in un word VGA
 * ============================================================================= */
static inline uint16_t vga_make_entry(char c, uint8_t color)
{
    return (uint16_t)((uint16_t)color << 8) | (uint16_t)(uint8_t)c;
}

/* =============================================================================
 * vga_make_color — Combina colori fg e bg in un attributo VGA
 * ============================================================================= */
static inline uint8_t vga_make_color(uint8_t fg, uint8_t bg)
{
    return (uint8_t)(fg | (bg << 4));
}

/* Mappa i 8 colori ANSI di base (indice 0-7) ai colori VGA equivalenti */
static const uint8_t ansi_to_vga[8] = {
    VGA_COLOR_BLACK, VGA_COLOR_RED, VGA_COLOR_GREEN, VGA_COLOR_BROWN,
    VGA_COLOR_BLUE,  VGA_COLOR_MAGENTA, VGA_COLOR_CYAN, VGA_COLOR_LIGHT_GREY
};

/* =============================================================================
 * Riversamento sulla memoria video
 *
 * Una console scrive SEMPRE nel proprio buffer; sulla memoria video ci
 * finisce solo se è quella visibile. È tutta qui la differenza fra uno
 * schermo in primo piano e uno che continua a lavorare in disparte.
 * ============================================================================= */
/* =============================================================================
 * BACKEND GRAFICO — la stessa console, disegnata invece che scritta
 *
 * In modo testo una cella e' due byte in memoria video e la scheda ci mette
 * il glifo. In modalita' grafica non esiste nessun glifo: la cella e' 8x16
 * pixel che qualcuno deve accendere uno per uno, ed e' cio' che fa
 * disegna_cella() con il font di font8x16.c.
 *
 * ! IL RESTO DEL FILE NON SE NE ACCORGE. Tutta la console — lo scorrimento,
 * il parser ANSI, le quattro console virtuali, le cancellazioni — lavora sul
 * proprio array di celle e chiama riversa_cella()/riversa_tutto() per farle
 * arrivare allo schermo. Sono quelle due funzioni, e il cursore, a sapere
 * dove finiscono davvero: e' il motivo per cui aggiungere la grafica non ha
 * richiesto di toccare niente di tutto il resto.
 *
 * ! IL FRAMEBUFFER SI MAPPA, NON C'E' GIA'. Sta dove lo mette la scheda —
 * su QEMU 0xFD000000 — cioe' molto sopra la RAM, in una regione che
 * paging_init() non mappa perche' non e' memoria. Va mappato a mano, ed e'
 * il motivo per cui la grafica si accende DOPO il PASSO 9 e non dentro
 * vga_init(): prima della paginazione l'indirizzo funzionerebbe, subito
 * dopo darebbe un page fault al primo carattere stampato.
 * ============================================================================= */

extern const uint8_t font8x16[256 * 16];

#define CELLA_W  8
#define CELLA_H  16

static uint8_t  *g_fb       = 0;     /* 0 = modo testo */
static uint32_t  g_fb_pitch = 0;
static uint32_t  g_fb_bpp   = 0;
/* ! LA GEOMETRIA SI TIENE ANCHE SE LA CONSOLE NON LA USA. Serve a chi in
 * ring 3 vuole disegnare: senza larghezza e altezza, un framebuffer mappato
 * e' un blocco di byte di cui non si sa la forma. Vedi vga_info_fb(). */
static uint32_t  g_fb_w     = 0;
static uint32_t  g_fb_h     = 0;
static uint32_t  g_cur_cella = 0;    /* dove sta disegnato il cursore */
static uint8_t   g_cur_disegnato = 0;

/* I 16 colori del testo VGA in RGB. Sono quelli veri della tavolozza,
 * non un'approssimazione: il prompt della shell li usa gia' e deve
 * restare lo stesso colore in entrambe le modalita'. */
static const uint32_t vga_rgb[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

static inline void pixel(uint32_t x, uint32_t y, uint32_t rgb)
{
    uint8_t *p = g_fb + y * g_fb_pitch + x * (g_fb_bpp >> 3);

    if (g_fb_bpp == 32) {
        *(volatile uint32_t *)p = rgb;
    } else if (g_fb_bpp == 24) {
        p[0] = (uint8_t)rgb;
        p[1] = (uint8_t)(rgb >> 8);
        p[2] = (uint8_t)(rgb >> 16);
    } else {
        /* 5-6-5: si buttano i bit bassi di ogni componente. */
        *(volatile uint16_t *)p = (uint16_t)
            (((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
    }
}

static void disegna_cella(uint32_t i, uint16_t cella)
{
    uint32_t x0 = (i % g_cols) * CELLA_W;
    uint32_t y0 = (i / g_cols) * CELLA_H;
    uint8_t  ch   = (uint8_t)(cella & 0xFF);
    uint8_t  attr = (uint8_t)(cella >> 8);
    uint32_t fg = vga_rgb[attr & 0x0F];
    uint32_t bg = vga_rgb[(attr >> 4) & 0x07];
    const uint8_t *g = &font8x16[(uint32_t)ch * CELLA_H];
    uint32_t r, b;

    for (r = 0; r < CELLA_H; r++) {
        uint8_t riga = g[r];
        for (b = 0; b < CELLA_W; b++)
            pixel(x0 + b, y0 + r, (riga & (0x80u >> b)) ? fg : bg);
    }
}

/* Il cursore e' due righe di pixel in fondo alla cella, del colore del
 * testo. Si cancella ridisegnando la cella che ci stava sotto: e' il
 * motivo per cui serve ricordarsi DOVE era. */
static void disegna_cursore(const Console *c, int acceso)
{
    uint32_t x0, y0, r, b;
    uint32_t colore;

    if (!g_fb) return;

    if (g_cur_disegnato) {
        disegna_cella(g_cur_cella, c->cella[g_cur_cella]);
        g_cur_disegnato = 0;
    }
    if (!acceso) return;

    g_cur_cella = (uint32_t)c->row * g_cols + c->col;
    if (g_cur_cella >= g_totale) return;

    colore = vga_rgb[c->cella[g_cur_cella] >> 8 & 0x0F];
    x0 = (g_cur_cella % g_cols) * CELLA_W;
    y0 = (g_cur_cella / g_cols) * CELLA_H;
    for (r = CELLA_H - 2; r < CELLA_H; r++)
        for (b = 0; b < CELLA_W; b++) pixel(x0 + b, y0 + r, colore);
    g_cur_disegnato = 1;
}

static inline int e_visibile(const Console *c)
{
    return c == &g_console[g_visibile];
}

static inline void riversa_cella(const Console *c, uint32_t i)
{
    if (!e_visibile(c) || i >= g_totale) return;
    if (g_fb) disegna_cella(i, c->cella[i]);
    else      vga_buf[i] = c->cella[i];
}

static void riversa_tutto(const Console *c)
{
    uint32_t i;

    if (!e_visibile(c)) return;
    if (g_fb) {
        g_cur_disegnato = 0;   /* quello di prima e' stato ricoperto */
        for (i = 0; i < g_totale; i++) disegna_cella(i, c->cella[i]);
    } else {
        for (i = 0; i < g_totale; i++) vga_buf[i] = c->cella[i];
    }
}

/* =============================================================================
 * vga_update_cursor — Aggiorna la posizione del cursore hardware
 *
 * Il cursore è uno solo — è hardware — quindi lo muove solo la console
 * visibile. Le altre tengono il proprio in c->row/c->col e se lo
 * ritrovano quando tornano in primo piano.
 * ============================================================================= */
static void vga_update_cursor(const Console *c)
{
    uint16_t pos;

    if (!e_visibile(c)) return;

    if (g_fb) { disegna_cursore(c, c->cursore_on); return; }

    pos = (uint16_t)(c->row * g_cols + c->col);
    port_outb(VGA_CTRL_PORT, VGA_CURSOR_HIGH);
    port_outb(VGA_DATA_PORT, (uint8_t)((pos >> 8) & 0xFF));
    port_outb(VGA_CTRL_PORT, VGA_CURSOR_LOW);
    port_outb(VGA_DATA_PORT, (uint8_t)(pos & 0xFF));
}

/* =============================================================================
 * cursore_hw — Accende o spegne il cursore hardware
 *
 * Bit 5 del registro CRTC 0x0A (Cursor Start): 1 = cursore NASCOSTO. Il
 * resto del registro è la riga di scansione iniziale del glifo e va
 * conservata, altrimenti il cursore riappare di forma diversa.
 * ============================================================================= */
static void cursore_hw(int on)
{
    uint8_t start;

    /* In grafica il cursore lo disegniamo noi: non c'e' nessun registro
     * CRTC da toccare, e toccarlo comunque scriverebbe su una scheda che
     * in questo momento sta mostrando tutt'altro. */
    if (g_fb) { disegna_cursore(SISTEMA, on); return; }

    port_outb(VGA_CTRL_PORT, 0x0A);
    start = port_inb(VGA_DATA_PORT);

    if (on) start = (uint8_t)(start & ~0x20);
    else    start = (uint8_t)(start |  0x20);

    port_outb(VGA_CTRL_PORT, 0x0A);
    port_outb(VGA_DATA_PORT, start);
}

/* =============================================================================
 * Cancellazioni — le celle azzerate prendono il colore CORRENTE della
 * console, non il nero: è il comportamento ANSI, e permette di dipingere
 * uno sfondo (barra dei menu, riquadro di un dialogo) con una sola ESC[K.
 * ============================================================================= */
static void erase_cells(Console *c, uint32_t da, uint32_t a)
{
    uint16_t blank = vga_make_entry(' ', c->colore);

    if (a > g_totale) a = g_totale;
    while (da < a) c->cella[da++] = blank;

    riversa_tutto(c);
}

/* =============================================================================
 * vga_scroll — Scrolla la console di una riga verso l'alto
 * ============================================================================= */
static void vga_scroll(Console *c)
{
    uint16_t blank = vga_make_entry(' ', c->colore);
    uint32_t i;

    for (i = 0; i < (g_righe - 1) * g_cols; i++) {
        c->cella[i] = c->cella[i + g_cols];
    }
    for (i = (g_righe - 1) * g_cols; i < g_totale; i++) {
        c->cella[i] = blank;
    }

    c->row = g_righe - 1;

    /* ! IN GRAFICA NON SI RIDISEGNA TUTTO, SI FA SCORRERE IL FRAMEBUFFER.
     *
     * riversa_tutto() a 100x37 vuol dire 3700 celle per 128 pixel ciascuna,
     * cioe' quasi mezzo milione di scritture — e sono scritture in memoria
     * VIDEO, non in RAM: non passano dalla cache e su una scheda vera vanno
     * sul bus una per una. Il primo tentativo faceva esattamente questo, e
     * il sistema arrivava al prompt in decine di secondi invece che subito.
     *
     * Far scorrere la memoria video di una riga di celle costa una copia
     * sola, in blocchi da 32 bit, piu' l'ultima riga da ridipingere. E'
     * lo stesso lavoro che in modo testo fa la scheda per conto suo. */
    if (g_fb) {
        uint32_t riga_px = (uint32_t)CELLA_H * g_fb_pitch;
        uint32_t alte    = (g_righe - 1) * riga_px;
        uint32_t *d = (uint32_t *)g_fb;
        uint32_t *o = (uint32_t *)(g_fb + riga_px);
        uint32_t  n = alte >> 2, k;

        /* =====================================================================
         * ! IL CURSORE SI CANCELLA PRIMA DI FAR SCORRERE, non si dichiara
         * sparito dopo.
         *
         * Qui c'era solo `g_cur_disegnato = 0` in fondo, che vuol dire «non c'e'
         * nessun cursore da cancellare». Ma la copia qui sotto sposta i PIXEL, e
         * i pixel del cursore erano gia' sullo schermo: salivano insieme al
         * resto e restavano li' per sempre. Il risultato era un trattino basso
         * in coda a OGNI riga scorsa — segnalato guardando la console d'avvio,
         * dove le righe scorrono a decine.
         *
         * ! E NON SI RIPARA TOGLIENDO IL CURSORE: quel trattino e' il cursore, e
         * senza non si vede piu' dove si sta scrivendo. Si ripara cancellandolo
         * mentre e' ancora suo, cioe' adesso.
         * ===================================================================== */
        disegna_cursore(c, 0);

        for (k = 0; k < n; k++) d[k] = o[k];

        for (i = (g_righe - 1) * g_cols; i < g_totale; i++)
            disegna_cella(i, c->cella[i]);
        g_cur_disegnato = 0;
        return;
    }

    riversa_tutto(c);
}

/* =============================================================================
 * ansi_apply_sgr — Applica i parametri SGR raccolti (ESC[...m) al colore
 * corrente. Supporta i codici standard 0/1/30-37/39/40-47/49 e
 * l'estensione bright 90-97; altri codici (underline ecc.) sono ignorati
 * perché non rappresentabili nell'attributo VGA testuale.
 * ============================================================================= */
static void ansi_apply_sgr(Console *c)
{
    uint8_t fg = c->colore & 0x0F;
    uint8_t bg = (c->colore >> 4) & 0x0F;
    uint8_t i;

    if (c->param_count == 0) {
        /* ESC[m senza parametri equivale a ESC[0m: reset */
        fg = VGA_COLOR_WHITE;
        bg = VGA_COLOR_BLACK;
    }

    for (i = 0; i < c->param_count; i++) {
        int p = c->params[i];
        if (p == 0) {
            fg = VGA_COLOR_WHITE;
            bg = VGA_COLOR_BLACK;
        } else if (p == 1) {
            fg |= 0x08;
        } else if (p >= 30 && p <= 37) {
            fg = ansi_to_vga[p - 30];
        } else if (p == 39) {
            fg = VGA_COLOR_WHITE;
        } else if (p >= 40 && p <= 47) {
            bg = ansi_to_vga[p - 40];
        } else if (p == 49) {
            bg = VGA_COLOR_BLACK;
        } else if (p >= 90 && p <= 97) {
            fg = ansi_to_vga[p - 90] | 0x08;
        }
    }

    c->colore = vga_make_color(fg, bg);
}

/* =============================================================================
 * ansi_param — Parametro idx della sequenza corrente, o 'def' se assente.
 *
 * Uno zero esplicito è indistinguibile da un parametro vuoto (il parser
 * accumula su un accumulatore azzerato), ed è giusto così: per le
 * sequenze di movimento lo standard ANSI fa già valere 0 come 1, e per
 * quelle di cancellazione il valore predefinito È zero.
 * ============================================================================= */
static int ansi_param(const Console *c, uint8_t idx, int def)
{
    if (idx >= c->param_count) return def;
    return c->params[idx];
}

/* =============================================================================
 * ansi_apply_csi — Applica una sequenza CSI diversa da SGR
 *
 * Coperte: CUU/CUD/CUF/CUB (movimento relativo), CUP/HVP (posizione
 * assoluta), ED (cancella schermo), EL (cancella riga), e la privata
 * DECTCEM (ESC[?25h/l, cursore visibile).
 * ============================================================================= */
static void ansi_apply_csi(Console *c, char finale)
{
    int n;

    if (c->privata) {
        /* Sequenze private DEC: qui serve solo il cursore. */
        if ((finale == 'h' || finale == 'l') && ansi_param(c, 0, 0) == 25) {
            c->cursore_on = (finale == 'h');
            if (e_visibile(c)) cursore_hw(c->cursore_on);
        }
        return;
    }

    switch (finale) {
        case 'A':   /* CUU — su */
            n = ansi_param(c, 0, 1); if (n < 1) n = 1;
            c->row = (uint8_t)((c->row > n) ? (c->row - n) : 0);
            break;

        case 'B':   /* CUD — giù */
            n = ansi_param(c, 0, 1); if (n < 1) n = 1;
            n += c->row;
            c->row = (uint8_t)((n >= g_righe) ? (g_righe - 1) : n);
            break;

        case 'C':   /* CUF — avanti */
            n = ansi_param(c, 0, 1); if (n < 1) n = 1;
            n += c->col;
            c->col = (uint8_t)((n >= g_cols) ? (g_cols - 1) : n);
            break;

        case 'D':   /* CUB — indietro */
            n = ansi_param(c, 0, 1); if (n < 1) n = 1;
            c->col = (uint8_t)((c->col > n) ? (c->col - n) : 0);
            break;

        case 'H':   /* CUP */
        case 'f': { /* HVP — sinonimo */
            int riga = ansi_param(c, 0, 1);
            int col  = ansi_param(c, 1, 1);
            if (riga < 1) riga = 1;
            if (col  < 1) col  = 1;
            /* I parametri ANSI sono 1-based, lo stato interno 0-based.
             * Fuori schermo si aggancia al bordo invece di rifiutare: la
             * sequenza arriva da un programma utente, e un parametro
             * sbagliato non deve poter scrivere fuori dal buffer. */
            if (riga > g_righe) riga = g_righe;
            if (col  > g_cols) col  = g_cols;
            c->row = (uint8_t)(riga - 1);
            c->col = (uint8_t)(col - 1);
            break;
        }

        case 'J': { /* ED — cancella nello schermo */
            uint32_t pos = (uint32_t)c->row * g_cols + c->col;
            switch (ansi_param(c, 0, 0)) {
                case 0: erase_cells(c, pos, g_totale); break;
                case 1: erase_cells(c, 0, pos + 1);     break;
                case 2:
                default:
                    erase_cells(c, 0, g_totale);
                    /* ESC[2J non muove il cursore: chi vuole anche
                     * l'angolo scrive ESC[2J ESC[H, come da standard. */
                    break;
            }
            break;
        }

        case 'K': { /* EL — cancella nella riga */
            uint32_t inizio = (uint32_t)c->row * g_cols;
            switch (ansi_param(c, 0, 0)) {
                case 0: erase_cells(c, inizio + c->col, inizio + g_cols); break;
                case 1: erase_cells(c, inizio, inizio + c->col + 1);        break;
                case 2:
                default: erase_cells(c, inizio, inizio + g_cols);         break;
            }
            break;
        }

        default:
            return;     /* terminatore non gestito: sequenza scartata */
    }

    vga_update_cursor(c);
}

/* =============================================================================
 * putchar_su — il vero motore di scrittura, su una console qualunque
 * ============================================================================= */
static void putchar_su(Console *c, char ch)
{
    /* Parser sequenze ANSI: intercetta i caratteri della sequenza prima
     * che finiscano stampati come testo letterale (era il bug per cui il
     * prompt mostrava "<-[32m" invece di applicare il colore verde). */
    if (c->state == ANSI_NONE) {
        if (ch == '\x1B') {
            c->state = ANSI_ESC;
            return;
        }
    } else if (c->state == ANSI_ESC) {
        if (ch == '[') {
            c->state           = ANSI_CSI;
            c->param_count     = 0;
            c->params[0]       = 0;
            c->cur_param_empty = 1;
            c->privata         = 0;
            return;
        }
        /* Sequenza non riconosciuta (non CSI): scarta e riprendi */
        c->state = ANSI_NONE;
        /* continua a processare 'ch' normalmente sotto */
    } else if (c->state == ANSI_CSI) {
        /* '?' introduce le sequenze private DEC, e può stare solo in
         * testa ai parametri: ESC[?25l. */
        if (ch == '?' && c->param_count == 0 && c->cur_param_empty) {
            c->privata = 1;
            return;
        }
        if (ch >= '0' && ch <= '9') {
            if (c->param_count < 4) {
                c->params[c->param_count] =
                    c->params[c->param_count] * 10 + (ch - '0');
                c->cur_param_empty = 0;
            }
            return;
        }
        if (ch == ';') {
            if (c->param_count < 3) {
                c->param_count++;
                c->params[c->param_count] = 0;
            }
            c->cur_param_empty = 1;
            return;
        }
        /* Qualunque lettera chiude la sequenza. La normalizzazione del
         * conteggio dei parametri è la stessa per tutti i terminatori:
         * l'ultimo accumulatore conta come parametro se ha ricevuto
         * cifre, oppure se era preceduto da un ';' (ESC[1; ha due
         * parametri, il secondo vuoto). */
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
            if (!c->cur_param_empty || c->param_count > 0) {
                c->param_count++;
            }
            if (ch == 'm' && !c->privata) ansi_apply_sgr(c);
            else                          ansi_apply_csi(c, ch);
            c->state = ANSI_NONE;
            return;
        }
        return;  /* Parametro malformato: continua a scartare */
    }

    switch (ch) {
        case '\n':  /* Newline */
            c->col = 0;
            c->row++;
            break;

        case '\r':  /* Carriage return */
            c->col = 0;
            break;

        case '\t':  /* Tab: allinea alla prossima tabulazione di 8 */
            c->col = (uint8_t)((c->col + 8) & ~7);
            if (c->col >= g_cols) {
                c->col = 0;
                c->row++;
            }
            break;

        case '\b':  /* Backspace */
            if (c->col > 0) {
                uint32_t i;
                c->col--;
                i = (uint32_t)c->row * g_cols + c->col;
                c->cella[i] = vga_make_entry(' ', c->colore);
                riversa_cella(c, i);
            }
            break;

        default: {  /* Carattere normale */
            uint32_t i = (uint32_t)c->row * g_cols + c->col;
            c->cella[i] = vga_make_entry(ch, c->colore);
            riversa_cella(c, i);
            c->col++;

            if (c->col >= g_cols) {
                c->col = 0;
                c->row++;
            }
            break;
        }
    }

    /* Scrolla se necessario */
    if (c->row >= g_righe) {
        vga_scroll(c);
    }

    vga_update_cursor(c);
}

/* =============================================================================
 * API PUBBLICA
 *
 * Le funzioni senza numero di console lavorano sulla console di SISTEMA
 * (la 0): sono quelle che il kernel usava già prima che ne esistesse più
 * d'una, e continuano a comportarsi esattamente come allora.
 * ============================================================================= */

void vga_init(void)
{
    uint32_t n, i;

    serial_init();

    for (n = 0; n < VGA_N_CONSOLE; n++) {
        Console *c = &g_console[n];
        c->colore     = vga_make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        c->row        = 0;
        c->col        = 0;
        c->cursore_on = 1;
        c->state      = ANSI_NONE;
        for (i = 0; i < g_totale; i++) {
            c->cella[i] = vga_make_entry(' ', c->colore);
        }
    }

    g_visibile = 0;

    /* Abilita cursore hardware (shape: linea solida, righe 14-15) */
    port_outb(VGA_CTRL_PORT, 0x0A);
    port_outb(VGA_DATA_PORT, (port_inb(VGA_DATA_PORT) & 0xC0) | 14);
    port_outb(VGA_CTRL_PORT, 0x0B);
    port_outb(VGA_DATA_PORT, (port_inb(VGA_DATA_PORT) & 0xE0) | 15);

    riversa_tutto(SISTEMA);
    vga_update_cursor(SISTEMA);
}

/* =============================================================================
 * vga_init_grafica — passa la console al framebuffer, se ce n'e' uno
 *
 * ! VA CHIAMATA DOPO LA PAGINAZIONE, e non e' un dettaglio d'ordine: il
 * framebuffer sta fuori dalla RAM, in una regione che paging_init() non
 * mappa, e qui la si mappa. Chiamarla prima del PASSO 9 sembrerebbe
 * funzionare — senza paginazione ogni indirizzo fisico e' raggiungibile — e
 * si romperebbe al primo carattere stampato dopo, con un page fault su un
 * indirizzo che nessuno collegherebbe allo schermo.
 *
 * Quello che era stato stampato prima non si perde: ogni console tiene le
 * proprie celle, e riversa_tutto() le ridisegna tutte sul framebuffer.
 * ============================================================================= */
void vga_init_grafica(const BootInfo *info)
{
    uint32_t righe, colonne, byte_tot, a;

    if (info == NULL || info->fb_addr == 0) return;      /* modo testo */
    if (info->fb_bpp != 16 && info->fb_bpp != 24 && info->fb_bpp != 32) return;

    colonne = info->fb_width  / CELLA_W;
    righe   = info->fb_height / CELLA_H;
    if (colonne < 40 || righe < 10) return;              /* non ci si scrive */
    if (colonne > VGA_MAX_COLS) colonne = VGA_MAX_COLS;
    if (righe   > VGA_MAX_ROWS) righe   = VGA_MAX_ROWS;

    /* Mappatura identita' del framebuffer. Scrittura sì, utente no: in
     * ring3 non deve essere raggiungibile, come non lo e' 0xB8000. */
    byte_tot = info->fb_pitch * info->fb_height;
    if (paging_mappa_framebuffer(info->fb_addr, byte_tot) != 0) {
        klog(LOG_ERROR, "VGA: framebuffer 0x%08x non mappabile - "
                        "resto in modo testo", info->fb_addr);
        return;
    }
    (void)a;

    g_fb       = (uint8_t *)info->fb_addr;
    g_fb_pitch = info->fb_pitch;
    g_fb_bpp   = info->fb_bpp;
    g_fb_w     = info->fb_width;
    g_fb_h     = info->fb_height;
    g_cols     = colonne;
    g_righe    = righe;
    g_totale   = colonne * righe;

    /* Le console erano state riempite a 80x25: le loro celle stanno ancora
     * dove le ha messe il vecchio passo di riga, che ora e' diverso. Si
     * riparte pulito — il log di avvio resta comunque sulla seriale. */
    {
        uint32_t n, i;

        for (n = 0; n < VGA_N_CONSOLE; n++) {
            Console *c = &g_console[n];

            for (i = 0; i < g_totale; i++)
                c->cella[i] = vga_make_entry(' ', c->colore);
            c->row = 0;
            c->col = 0;
        }
    }

    riversa_tutto(SISTEMA);
    klog(LOG_INFO, "VGA: console grafica %ux%u caratteri (%ux%u pixel, %u bpp)",
         g_cols, g_righe, info->fb_width, info->fb_height, info->fb_bpp);
}

/* =============================================================================
 * vga_ripristina_testo — rimette lo schermo, e funziona anche se il server
 * grafico e' morto
 *
 * Gradino 0 punto 4 di DIREZIONE.md. Il pezzo che parla all'hardware sta in
 * kernel/arch/x86/vga_modo3.c — li' c'e' il perche' per esteso; qui c'e' la
 * meta' che riguarda la console.
 *
 * ! UNA STRADA SOLA PER I DUE CASI, ed e' una decisione. Il sistema puo'
 * trovarsi in modo testo (una scheda riprogrammata da qualcuno) oppure dentro
 * una VESA lineare con la console disegnata nel framebuffer. Si potrebbe
 * distinguere e, nel secondo caso, limitarsi a ridisegnare — ma se si e'
 * arrivati qui e' perche' lo stato della scheda NON e' noto, e ridisegnare
 * dentro una modalita' che qualcun altro ha cambiato sotto di noi vuol dire
 * scrivere pixel dove non c'e' piu' niente. Il modo 3 e' l'unico stato che si
 * puo' RAGGIUNGERE senza sapere da dove si parte.
 *
 * ! LA CONSOLE TORNA A 80x25, e le celle di prima non si conservano. Erano
 * disposte per un passo di riga diverso: tenerle vorrebbe dire testo
 * mescolato. Si riparte pulito, come fa vga_init_grafica nel verso opposto —
 * e il log completo resta sulla seriale, che di tutto questo non si accorge.
 * ========================================================================== */
void vga_ripristina_testo(void)
{
    uint32_t n, i;
    uint32_t eflags;

    /* =====================================================================
     * ! DA QUI IN GIU' NON DEVE ENTRARE NESSUNO, e non e' prudenza: e' la
     * differenza fra rimettere il testo e riempire lo schermo di colori.
     *
     * `int 0x80` passa da un TRAP gate (vedi idt.c): durante una syscall gli
     * interrupt restano ACCESI, quindi il timer puo' togliere la CPU proprio
     * qui in mezzo e darla a un altro processo — che magari sta scrivendo
     * sulla console. E in mezzo ci sono due momenti in cui una scrittura
     * fatta da qualcun altro non e' un carattere fuori posto, e' un guasto:
     *
     *   - fra il cambio di modalita' e l'azzeramento di g_fb, chi scrive
     *     disegna PIXEL dentro una memoria video che ormai e' letta come
     *     testo — ottanta colonne di simboli e colori casuali;
     *   - dentro carattere_ricarica(), che apre il solo piano 2 a 0xA0000
     *     per ricopiarci i disegni dei caratteri. Una scrittura a 0xB8000
     *     mentre la maschera dei piani e' quella finisce DENTRO il generatore
     *     di caratteri: da li' in poi ogni lettera e' un disegno diverso.
     *
     * ! E CHI VOLESSE SCRIVERE PROPRIO ADESSO NON E' UN CASO RARO. Uscendo
     * dalla scrivania, `exwin --attendi` sta interrogando il kernel ogni
     * mezzo secondo per sapere se la grafica e' finita, e quando se ne
     * accorge chiama console_switch(), che ridisegna una console intera.
     *
     * ! ONESTA': QUESTA FINESTRA NON SI E' MAI VISTA SBATTERE. I colori
     * casuali dopo l'uscita dalla scrivania — 26 agosto 2026 — venivano da
     * tutt'altro: dal PMM che restituiva all'allocatore le pagine del
     * framebuffer (vedi pmm_init, Passo 1). Questa e' una porta trovata
     * aperta mentre si cercava quella, e chiusa per quello che e': una
     * funzione che riprogramma la scheda non deve poter essere interrotta
     * da chi scrive sulla scheda.
     *
     * Il costo e' qualche millisecondo a interrupt chiusi, una volta per
     * ripristino. Si rimette IF com'era invece di farlo a `sti`: questa
     * funzione la si chiama anche da posti dove era gia' chiuso.
     * ===================================================================== */
    __asm__ volatile ("pushf; pop %0" : "=r"(eflags));
    __asm__ volatile ("cli");

    /* ! PRIMA SI SMETTE DI CREDERE AL FRAMEBUFFER, POI SI TOCCA LA SCHEDA.
     * L'ordine inverso lasciava una finestra in cui il kernel disegnava
     * ancora pixel su una scheda gia' tornata al testo. Adesso, se anche
     * qualcosa riuscisse a scrivere qui in mezzo, scriverebbe caratteri a
     * 0xB8000 — cioe' la cosa giusta, magari un po' presto: riversa_tutto()
     * qui sotto ridisegna comunque tutto. */
    g_fb       = 0;             /* da qui in poi si scrive a 0xB8000 */
    g_fb_pitch = 0;
    g_fb_w     = 0;
    g_fb_h     = 0;
    g_fb_bpp   = 0;
    g_cur_disegnato = 0;

    g_cols   = 80;
    g_righe  = 25;
    g_totale = 80 * 25;

    vga_hw_modo3();

    for (n = 0; n < VGA_N_CONSOLE; n++) {
        Console *c = &g_console[n];

        for (i = 0; i < g_totale; i++)
            c->cella[i] = vga_make_entry(' ', c->colore);
        c->row = 0;
        c->col = 0;
    }

    {
        Console *c = cons(g_visibile);
        riversa_tutto(c);
        cursore_hw(c->cursore_on);
        vga_update_cursor(c);
    }

    if (eflags & (1u << 9)) __asm__ volatile ("sti");

    /* ! IL LOG DOPO AVER RIAPERTO, non prima: klog scrive sulla console e
     * sulla seriale, e la seriale aspetta il registro di trasmissione. Non
     * e' un posto in cui restare a interrupt chiusi quando non serve piu'. */
    klog(LOG_INFO, "VGA: modo testo 80x25 ripristinato senza BIOS");
}

void vga_putchar(char c)
{
    /* Lo specchio seriale segue la console di SISTEMA e non quella
     * visibile: serve ad avere il log di boot completo, e il log di boot
     * è quello che passa di qui. */
    serial_putchar(c);
    putchar_su(SISTEMA, c);
}

void vga_putchar_su(uint32_t n, char c)
{
    Console *cs = cons(n);

    if (cs == SISTEMA) serial_putchar(c);
    putchar_su(cs, c);
}

void vga_puts(const char *s)
{
    while (*s) vga_putchar(*s++);
}

/* Scrive una stringa a una posizione specifica della console di
 * sistema, senza spostarne il cursore. */
void vga_puts_at(const char *s, uint8_t row, uint8_t col)
{
    Console *c = SISTEMA;

    while (*s && col < g_cols && row < g_righe) {
        uint32_t i = (uint32_t)row * g_cols + col;
        c->cella[i] = vga_make_entry(*s, c->colore);
        riversa_cella(c, i);
        col++;
        s++;
    }
}

void vga_clear(void)          { vga_clear_su(0); }
void vga_setcolor(uint8_t fg, uint8_t bg) { vga_setcolor_su(0, fg, bg); }

void vga_clear_su(uint32_t n)
{
    Console *c = cons(n);
    uint16_t blank = vga_make_entry(' ', c->colore);
    uint32_t i;

    for (i = 0; i < g_totale; i++) c->cella[i] = blank;

    c->row = 0;
    c->col = 0;
    riversa_tutto(c);
    vga_update_cursor(c);
}

void vga_setcolor_su(uint32_t n, uint8_t fg, uint8_t bg)
{
    cons(n)->colore = vga_make_color(fg, bg);
}

void vga_gotoxy(uint8_t row, uint8_t col)
{
    Console *c = SISTEMA;

    if (row >= g_righe) row = g_righe - 1;
    if (col >= g_cols) col = g_cols - 1;

    c->row = row;
    c->col = col;
    vga_update_cursor(c);
}

void vga_show_cursor(int on)
{
    SISTEMA->cursore_on = on ? 1 : 0;
    if (g_visibile == 0) cursore_hw(on);
}

uint8_t vga_get_row(void) { return SISTEMA->row; }
uint8_t vga_get_col(void) { return SISTEMA->col; }

/* =============================================================================
 * vga_set_serial_mirror — Accende/spegne la copia su COM1 dell'output
 *
 * Lo specchio seriale esiste per avere il log di boot completo anche
 * dopo lo scroll. Per un programma a schermo intero è invece un danno
 * doppio: il log si riempie di sequenze di controllo illeggibili, e
 * soprattutto ogni carattere costa l'attesa del THR — a 38400 baud sono
 * ~260 µs, che moltiplicati per le 2000 celle di una schermata fanno
 * mezzo secondo di ridisegno. In QEMU non si nota (senza una seriale
 * collegata port_inb torna 0xFF e il THR sembra sempre libero); su
 * hardware reale l'editor sarebbe inusabile.
 *
 * Lo spegne drv_ioctl(TTY_IOCTL_SETRAW) e lo riaccende SETCOOKED.
 * ============================================================================= */
void vga_set_serial_mirror(int on)
{
    serial_mirror = on ? 1 : 0;
}

/* =============================================================================
 * vga_switch_console — Porta in primo piano la console n
 *
 * Il buffer della console che entra viene riversato per intero sulla
 * memoria video, e il cursore hardware prende la posizione e la
 * visibilità che quella console si era tenuta da parte. Chi esce non
 * viene toccato: continua a scrivere nel proprio buffer come se niente
 * fosse, ed è esattamente questo che permette a un programma di
 * restare in vita mentre si lavora altrove.
 *
 * Ritorna 0, o -1 se il numero non esiste.
 * ============================================================================= */
int vga_switch_console(uint32_t n)
{
    Console *c;

    if (n >= VGA_N_CONSOLE) return -1;
    if (n == g_visibile)    return 0;

    g_visibile = n;
    c = &g_console[n];

    riversa_tutto(c);
    cursore_hw(c->cursore_on);
    vga_update_cursor(c);
    return 0;
}

uint32_t vga_visible_console(void)
{
    return g_visibile;
}

/* =============================================================================
 * Dire a chi sta in ring 3 dov'e' il framebuffer e che forma ha
 *
 * ! SENZA QUESTO, SYS_MMIO_MAP NON BASTA. Quella syscall sa mappare una
 * finestra fisica qualunque, ma l'indirizzo del framebuffer lo conosce solo
 * il kernel — gliel'ha dato Stage 2, che e' l'unico che poteva chiederlo al
 * BIOS. Un server grafico in ring 3 senza questa informazione dovrebbe
 * indovinare un indirizzo fisico, che e' esattamente cio' che il varco dei
 * driver serve a impedire.
 *
 * ! NON DA' ACCESSO A NIENTE: dice dei numeri. Mapparlo resta un privilegio
 * da .drv, e chi non lo e' con questi numeri non ci fa niente.
 * ========================================================================== */
void vga_geometria(uint32_t *colonne, uint32_t *righe, uint32_t *px_w,
                   uint32_t *px_h)
{
    if (colonne) *colonne = g_cols;
    if (righe)   *righe   = g_righe;

    /* ! I PIXEL SONO QUELLI VERI SOLO IN GRAFICA. In modo testo il framebuffer
     * non c'e' e la misura in pixel di un carattere non e' una cosa che questo
     * file conosca: si rende quella del modo 3 (720x400), che e' cio' che la
     * scheda sta davvero mostrando. */
    if (px_w) *px_w = g_fb ? g_fb_w : (uint32_t)(g_cols * CELLA_W);
    if (px_h) *px_h = g_fb ? g_fb_h : (uint32_t)(g_righe * CELLA_H);
}

void vga_info_fb(uint32_t *addr, uint32_t *pitch, uint32_t *w, uint32_t *h,
                 uint32_t *bpp)
{
    if (addr)  *addr  = (uint32_t)(uintptr_t)g_fb;
    if (pitch) *pitch = g_fb_pitch;
    if (w)     *w     = g_fb_w;
    if (h)     *h     = g_fb_h;
    if (bpp)   *bpp   = g_fb_bpp;
}
