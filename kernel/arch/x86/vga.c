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
#include "vga.h"

/* =============================================================================
 * Costanti VGA
 * ============================================================================= */
#define VGA_BASE        ((volatile uint16_t *)0xB8000)  /* Indirizzo memoria VGA */
#define VGA_COLS        80
#define VGA_ROWS        25
#define VGA_TOTAL       (VGA_COLS * VGA_ROWS)           /* 2000 celle */

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
    uint16_t  cella[VGA_TOTAL];   /* carattere + attributo, come in memoria video */
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
static inline int e_visibile(const Console *c)
{
    return c == &g_console[g_visibile];
}

static inline void riversa_cella(const Console *c, uint32_t i)
{
    if (e_visibile(c) && i < VGA_TOTAL) vga_buf[i] = c->cella[i];
}

static void riversa_tutto(const Console *c)
{
    uint32_t i;

    if (!e_visibile(c)) return;
    for (i = 0; i < VGA_TOTAL; i++) vga_buf[i] = c->cella[i];
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

    pos = (uint16_t)(c->row * VGA_COLS + c->col);
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

    if (a > VGA_TOTAL) a = VGA_TOTAL;
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

    for (i = 0; i < (VGA_ROWS - 1) * VGA_COLS; i++) {
        c->cella[i] = c->cella[i + VGA_COLS];
    }
    for (i = (VGA_ROWS - 1) * VGA_COLS; i < VGA_TOTAL; i++) {
        c->cella[i] = blank;
    }

    c->row = VGA_ROWS - 1;
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
            c->row = (uint8_t)((n >= VGA_ROWS) ? (VGA_ROWS - 1) : n);
            break;

        case 'C':   /* CUF — avanti */
            n = ansi_param(c, 0, 1); if (n < 1) n = 1;
            n += c->col;
            c->col = (uint8_t)((n >= VGA_COLS) ? (VGA_COLS - 1) : n);
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
            if (riga > VGA_ROWS) riga = VGA_ROWS;
            if (col  > VGA_COLS) col  = VGA_COLS;
            c->row = (uint8_t)(riga - 1);
            c->col = (uint8_t)(col - 1);
            break;
        }

        case 'J': { /* ED — cancella nello schermo */
            uint32_t pos = (uint32_t)c->row * VGA_COLS + c->col;
            switch (ansi_param(c, 0, 0)) {
                case 0: erase_cells(c, pos, VGA_TOTAL); break;
                case 1: erase_cells(c, 0, pos + 1);     break;
                case 2:
                default:
                    erase_cells(c, 0, VGA_TOTAL);
                    /* ESC[2J non muove il cursore: chi vuole anche
                     * l'angolo scrive ESC[2J ESC[H, come da standard. */
                    break;
            }
            break;
        }

        case 'K': { /* EL — cancella nella riga */
            uint32_t inizio = (uint32_t)c->row * VGA_COLS;
            switch (ansi_param(c, 0, 0)) {
                case 0: erase_cells(c, inizio + c->col, inizio + VGA_COLS); break;
                case 1: erase_cells(c, inizio, inizio + c->col + 1);        break;
                case 2:
                default: erase_cells(c, inizio, inizio + VGA_COLS);         break;
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
            if (c->col >= VGA_COLS) {
                c->col = 0;
                c->row++;
            }
            break;

        case '\b':  /* Backspace */
            if (c->col > 0) {
                uint32_t i;
                c->col--;
                i = (uint32_t)c->row * VGA_COLS + c->col;
                c->cella[i] = vga_make_entry(' ', c->colore);
                riversa_cella(c, i);
            }
            break;

        default: {  /* Carattere normale */
            uint32_t i = (uint32_t)c->row * VGA_COLS + c->col;
            c->cella[i] = vga_make_entry(ch, c->colore);
            riversa_cella(c, i);
            c->col++;

            if (c->col >= VGA_COLS) {
                c->col = 0;
                c->row++;
            }
            break;
        }
    }

    /* Scrolla se necessario */
    if (c->row >= VGA_ROWS) {
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
        for (i = 0; i < VGA_TOTAL; i++) {
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

    while (*s && col < VGA_COLS && row < VGA_ROWS) {
        uint32_t i = (uint32_t)row * VGA_COLS + col;
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

    for (i = 0; i < VGA_TOTAL; i++) c->cella[i] = blank;

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

    if (row >= VGA_ROWS) row = VGA_ROWS - 1;
    if (col >= VGA_COLS) col = VGA_COLS - 1;

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
