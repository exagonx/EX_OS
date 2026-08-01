/* =============================================================================
 * kernel/arch/x86/vga.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
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
 * Stato interno
 * ============================================================================= */
static uint8_t vga_row     = 0;     /* Riga corrente (0-24) */
static uint8_t vga_col     = 0;     /* Colonna corrente (0-79) */
static uint8_t vga_color   = 0;     /* Colore corrente (attributo) */
static volatile uint16_t *vga_buf = (volatile uint16_t *)0xB8000;

/* Parser per sequenze ANSI CSI: ESC [ [?] <param>[;<param>...] <finale>
 *
 * Nato minimale per il solo SGR (ESC[...m, i colori del prompt della
 * shell). Da agosto 2026 copre anche posizionamento del cursore e
 * cancellazioni, perché senza quelli un programma a schermo intero non
 * può esistere: /bin/gfedit disegna passando di qui, e nient'altro in
 * ring3 può raggiungere la memoria VGA (che nello spazio utente non è
 * mappata). Vedi drivers/tty/tty.c, che è la porta d'ingresso.
 *
 * I terminatori non riconosciuti restano scartati in silenzio: meglio
 * ignorare una sequenza che stamparne i parametri come testo. */
typedef enum { ANSI_NONE, ANSI_ESC, ANSI_CSI } AnsiState;
static AnsiState ansi_state           = ANSI_NONE;
static int       ansi_params[4]       = {0,0,0,0};
static uint8_t   ansi_param_count     = 0;
static uint8_t   ansi_cur_param_empty = 1;
static uint8_t   ansi_private         = 0;  /* '?' subito dopo il '[' */

/* Specchio seriale dell'output: vedi vga_set_serial_mirror(). */
static uint8_t   serial_mirror        = 1;

/* =============================================================================
 * Console seriale di debug (COM1, 38400 8N1)
 * Specchia tutto l'output che passa da vga_putchar sulla porta seriale, così
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
 * ansi_apply_sgr — Applica i parametri SGR raccolti (ESC[...m) al colore
 * corrente. Supporta i codici standard 0/1/30-37/39/40-47/49 e
 * l'estensione bright 90-97; altri codici (underline ecc.) sono ignorati
 * perché non rappresentabili nell'attributo VGA testuale.
 * ============================================================================= */
static void ansi_apply_sgr(void)
{
    uint8_t fg = vga_color & 0x0F;
    uint8_t bg = (vga_color >> 4) & 0x0F;
    uint8_t i;

    if (ansi_param_count == 0) {
        /* ESC[m senza parametri equivale a ESC[0m: reset */
        fg = VGA_COLOR_WHITE;
        bg = VGA_COLOR_BLACK;
    }

    for (i = 0; i < ansi_param_count; i++) {
        int p = ansi_params[i];
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

    vga_color = vga_make_color(fg, bg);
}

/* =============================================================================
 * vga_update_cursor — Aggiorna la posizione del cursore hardware
 * ============================================================================= */
static void vga_update_cursor(void)
{
    uint16_t pos = (uint16_t)(vga_row * VGA_COLS + vga_col);

    port_outb(VGA_CTRL_PORT, VGA_CURSOR_HIGH);
    port_outb(VGA_DATA_PORT, (uint8_t)((pos >> 8) & 0xFF));
    port_outb(VGA_CTRL_PORT, VGA_CURSOR_LOW);
    port_outb(VGA_DATA_PORT, (uint8_t)(pos & 0xFF));
}

/* =============================================================================
 * vga_show_cursor — Accende o spegne il cursore hardware
 *
 * Bit 5 del registro CRTC 0x0A (Cursor Start): 1 = cursore NASCOSTO. Il
 * resto del registro è la riga di scansione iniziale del glifo e va
 * conservata, altrimenti il cursore riappare di forma diversa.
 *
 * Serve a chi ridisegna schermate intere: senza, il cursore rincorre
 * ogni carattere scritto e lascia una scia sfarfallante.
 * ============================================================================= */
void vga_show_cursor(int on)
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
 * vga_gotoxy — Posiziona il cursore logico (0-based, come le altre API)
 *
 * Le coordinate fuori schermo vengono AGGANCIATE al bordo invece che
 * rifiutate: una sequenza ANSI arriva da un programma utente, e un
 * parametro sbagliato non deve poter scrivere fuori dai 4000 byte della
 * memoria video.
 * ============================================================================= */
void vga_gotoxy(uint8_t row, uint8_t col)
{
    if (row >= VGA_ROWS) row = VGA_ROWS - 1;
    if (col >= VGA_COLS) col = VGA_COLS - 1;

    vga_row = row;
    vga_col = col;
    vga_update_cursor();
}

/* =============================================================================
 * vga_set_serial_mirror — Accende/spegne la copia su COM1 dell'output
 *
 * Lo specchio seriale (vedi serial_putchar) esiste per avere il log di
 * boot completo anche dopo lo scroll. Per un programma a schermo intero
 * è invece un danno doppio: il log si riempie di sequenze di controllo
 * illeggibili, e soprattutto ogni carattere costa l'attesa del THR — a
 * 38400 baud sono ~260 µs, che moltiplicati per le 2000 celle di una
 * schermata fanno mezzo secondo di ridisegno. In QEMU non si nota
 * (senza una seriale collegata port_inb torna 0xFF e il THR sembra
 * sempre libero); su hardware reale l'editor sarebbe inusabile.
 *
 * Lo spegne drv_ioctl(TTY_IOCTL_SETRAW) e lo riaccende SETCOOKED.
 * ============================================================================= */
void vga_set_serial_mirror(int on)
{
    serial_mirror = on ? 1 : 0;
}

/* =============================================================================
 * Cancellazioni — le celle azzerate prendono il colore CORRENTE, non il
 * nero: è il comportamento ANSI, e permette di dipingere uno sfondo
 * (barra dei menu, riquadro di un dialogo) con una sola ESC[K.
 * ============================================================================= */
static void vga_erase_cells(uint32_t da, uint32_t a)
{
    uint16_t blank = vga_make_entry(' ', vga_color);

    if (a > VGA_TOTAL) a = VGA_TOTAL;
    while (da < a) vga_buf[da++] = blank;
}

/* =============================================================================
 * ansi_param — Parametro idx della sequenza corrente, o 'def' se assente.
 *
 * Uno zero esplicito è indistinguibile da un parametro vuoto (il parser
 * accumula su un accumulatore azzerato), ed è giusto così: per le
 * sequenze di movimento lo standard ANSI fa già valere 0 come 1, e per
 * quelle di cancellazione il valore predefinito È zero.
 * ============================================================================= */
static int ansi_param(uint8_t idx, int def)
{
    if (idx >= ansi_param_count) return def;
    return ansi_params[idx];
}

/* =============================================================================
 * ansi_apply_csi — Applica una sequenza CSI diversa da SGR
 *
 * Coperte: CUU/CUD/CUF/CUB (movimento relativo), CUP/HVP (posizione
 * assoluta), ED (cancella schermo), EL (cancella riga), e la privata
 * DECTCEM (ESC[?25h/l, cursore visibile).
 * ============================================================================= */
static void ansi_apply_csi(char finale)
{
    int n;

    if (ansi_private) {
        /* Sequenze private DEC: qui serve solo il cursore. */
        if ((finale == 'h' || finale == 'l') && ansi_param(0, 0) == 25) {
            vga_show_cursor(finale == 'h');
        }
        return;
    }

    switch (finale) {
        case 'A':   /* CUU — su */
            n = ansi_param(0, 1); if (n < 1) n = 1;
            vga_row = (uint8_t)((vga_row > n) ? (vga_row - n) : 0);
            break;

        case 'B':   /* CUD — giù */
            n = ansi_param(0, 1); if (n < 1) n = 1;
            n += vga_row;
            vga_row = (uint8_t)((n >= VGA_ROWS) ? (VGA_ROWS - 1) : n);
            break;

        case 'C':   /* CUF — avanti */
            n = ansi_param(0, 1); if (n < 1) n = 1;
            n += vga_col;
            vga_col = (uint8_t)((n >= VGA_COLS) ? (VGA_COLS - 1) : n);
            break;

        case 'D':   /* CUB — indietro */
            n = ansi_param(0, 1); if (n < 1) n = 1;
            vga_col = (uint8_t)((vga_col > n) ? (vga_col - n) : 0);
            break;

        case 'H':   /* CUP */
        case 'f': { /* HVP — sinonimo */
            int riga = ansi_param(0, 1);
            int col  = ansi_param(1, 1);
            if (riga < 1) riga = 1;
            if (col  < 1) col  = 1;
            /* I parametri ANSI sono 1-based, lo stato interno 0-based. */
            vga_gotoxy((uint8_t)(riga - 1), (uint8_t)(col - 1));
            return;         /* vga_gotoxy ha già aggiornato il cursore */
        }

        case 'J': { /* ED — cancella nello schermo */
            uint32_t pos = (uint32_t)vga_row * VGA_COLS + vga_col;
            switch (ansi_param(0, 0)) {
                case 0: vga_erase_cells(pos, VGA_TOTAL); break;
                case 1: vga_erase_cells(0, pos + 1);     break;
                case 2:
                default:
                    vga_erase_cells(0, VGA_TOTAL);
                    /* ESC[2J non muove il cursore: chi vuole anche
                     * l'angolo scrive ESC[2J ESC[H, come da standard.
                     * vga_clear() invece azzera la posizione, ed è per
                     * questo che non la si richiama qui. */
                    break;
            }
            break;
        }

        case 'K': { /* EL — cancella nella riga */
            uint32_t inizio = (uint32_t)vga_row * VGA_COLS;
            switch (ansi_param(0, 0)) {
                case 0: vga_erase_cells(inizio + vga_col, inizio + VGA_COLS); break;
                case 1: vga_erase_cells(inizio, inizio + vga_col + 1);        break;
                case 2:
                default: vga_erase_cells(inizio, inizio + VGA_COLS);          break;
            }
            break;
        }

        default:
            return;     /* terminatore non gestito: sequenza scartata */
    }

    vga_update_cursor();
}

/* =============================================================================
 * vga_scroll — Scrolla lo schermo di una riga verso l'alto
 * ============================================================================= */
static void vga_scroll(void)
{
    uint16_t blank = vga_make_entry(' ', vga_color);
    uint32_t i;

    /* Sposta tutte le righe su di una */
    for (i = 0; i < (VGA_ROWS - 1) * VGA_COLS; i++) {
        vga_buf[i] = vga_buf[i + VGA_COLS];
    }

    /* Pulisce l'ultima riga */
    for (i = (VGA_ROWS - 1) * VGA_COLS; i < VGA_TOTAL; i++) {
        vga_buf[i] = blank;
    }

    /* Cursore sull'ultima riga */
    vga_row = VGA_ROWS - 1;
}

/* =============================================================================
 * vga_init — Inizializza il driver VGA
 * ============================================================================= */
void vga_init(void)
{
    serial_init();

    /* Colore default: testo bianco su sfondo nero */
    vga_color = vga_make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_row = 0;
    vga_col = 0;

    /* Abilita cursore hardware (shape: linea solida, righe 14-15) */
    port_outb(VGA_CTRL_PORT, 0x0A);
    port_outb(VGA_DATA_PORT, (port_inb(VGA_DATA_PORT) & 0xC0) | 14);
    port_outb(VGA_CTRL_PORT, 0x0B);
    port_outb(VGA_DATA_PORT, (port_inb(VGA_DATA_PORT) & 0xE0) | 15);

    vga_clear();
}

/* =============================================================================
 * vga_clear — Pulisce lo schermo
 * ============================================================================= */
void vga_clear(void)
{
    uint16_t blank = vga_make_entry(' ', vga_color);
    uint32_t i;

    for (i = 0; i < VGA_TOTAL; i++) {
        vga_buf[i] = blank;
    }

    vga_row = 0;
    vga_col = 0;
    vga_update_cursor();
}

/* =============================================================================
 * vga_setcolor — Imposta colore corrente
 * ============================================================================= */
void vga_setcolor(uint8_t fg, uint8_t bg)
{
    vga_color = vga_make_color(fg, bg);
}

/* =============================================================================
 * vga_putchar — Scrive un carattere nella posizione corrente
 * ============================================================================= */
void vga_putchar(char c)
{
    serial_putchar(c);

    /* Parser sequenze ANSI ESC[...m: intercetta i caratteri della
     * sequenza prima che finiscano stampati come testo letterale
     * (era il bug per cui il prompt mostrava "<-[32m" invece di
     * applicare il colore verde). */
    if (ansi_state == ANSI_NONE) {
        if (c == '\x1B') {
            ansi_state = ANSI_ESC;
            return;
        }
    } else if (ansi_state == ANSI_ESC) {
        if (c == '[') {
            ansi_state           = ANSI_CSI;
            ansi_param_count     = 0;
            ansi_params[0]       = 0;
            ansi_cur_param_empty = 1;
            ansi_private         = 0;
            return;
        }
        /* Sequenza non riconosciuta (non CSI): scarta e riprendi */
        ansi_state = ANSI_NONE;
        /* continua a processare 'c' normalmente sotto */
    } else if (ansi_state == ANSI_CSI) {
        /* '?' introduce le sequenze private DEC, e può stare solo in
         * testa ai parametri: ESC[?25l. */
        if (c == '?' && ansi_param_count == 0 && ansi_cur_param_empty) {
            ansi_private = 1;
            return;
        }
        if (c >= '0' && c <= '9') {
            if (ansi_param_count < 4) {
                ansi_params[ansi_param_count] =
                    ansi_params[ansi_param_count] * 10 + (c - '0');
                ansi_cur_param_empty = 0;
            }
            return;
        }
        if (c == ';') {
            if (ansi_param_count < 3) {
                ansi_param_count++;
                ansi_params[ansi_param_count] = 0;
            }
            ansi_cur_param_empty = 1;
            return;
        }
        /* Qualunque lettera chiude la sequenza. La normalizzazione del
         * conteggio dei parametri è la stessa per tutti i terminatori:
         * l'ultimo accumulatore conta come parametro se ha ricevuto
         * cifre, oppure se era preceduto da un ';' (ESC[1; ha due
         * parametri, il secondo vuoto). */
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            if (!ansi_cur_param_empty || ansi_param_count > 0) {
                ansi_param_count++;
            }
            if (c == 'm' && !ansi_private) ansi_apply_sgr();
            else                           ansi_apply_csi(c);
            ansi_state = ANSI_NONE;
            return;
        }
        return;  /* Parametro malformato: continua a scartare */
    }

    switch (c) {
        case '\n':  /* Newline */
            vga_col = 0;
            vga_row++;
            break;

        case '\r':  /* Carriage return */
            vga_col = 0;
            break;

        case '\t':  /* Tab: allinea alla prossima tabulazione di 8 */
            vga_col = (uint8_t)((vga_col + 8) & ~7);
            if (vga_col >= VGA_COLS) {
                vga_col = 0;
                vga_row++;
            }
            break;

        case '\b':  /* Backspace */
            if (vga_col > 0) {
                vga_col--;
                vga_buf[vga_row * VGA_COLS + vga_col] =
                    vga_make_entry(' ', vga_color);
            }
            break;

        default:    /* Carattere normale */
            vga_buf[vga_row * VGA_COLS + vga_col] =
                vga_make_entry(c, vga_color);
            vga_col++;

            if (vga_col >= VGA_COLS) {
                vga_col = 0;
                vga_row++;
            }
            break;
    }

    /* Scrolla se necessario */
    if (vga_row >= VGA_ROWS) {
        vga_scroll();
    }

    vga_update_cursor();
}

/* =============================================================================
 * vga_puts — Scrive una stringa ASCIIZ
 * ============================================================================= */
void vga_puts(const char *s)
{
    while (*s) {
        vga_putchar(*s++);
    }
}

/* =============================================================================
 * vga_puts_at — Scrive una stringa a una posizione specifica
 * Non aggiorna il cursore corrente
 * ============================================================================= */
void vga_puts_at(const char *s, uint8_t row, uint8_t col)
{
    while (*s && col < VGA_COLS) {
        vga_buf[row * VGA_COLS + col] = vga_make_entry(*s, vga_color);
        col++;
        s++;
    }
}

/* =============================================================================
 * vga_get_row / vga_get_col — Lettura posizione cursore corrente
 * ============================================================================= */
uint8_t vga_get_row(void) { return vga_row; }
uint8_t vga_get_col(void) { return vga_col; }
