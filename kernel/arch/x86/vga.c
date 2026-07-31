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

/* Parser minimale per sequenze ANSI SGR: ESC [ <param>[;<param>...] m
 * (le uniche sequenze che la shell genera per i colori del prompt). */
typedef enum { ANSI_NONE, ANSI_ESC, ANSI_CSI } AnsiState;
static AnsiState ansi_state           = ANSI_NONE;
static int       ansi_params[4]       = {0,0,0,0};
static uint8_t   ansi_param_count     = 0;
static uint8_t   ansi_cur_param_empty = 1;

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
            return;
        }
        /* Sequenza non riconosciuta (non CSI): scarta e riprendi */
        ansi_state = ANSI_NONE;
        /* continua a processare 'c' normalmente sotto */
    } else if (ansi_state == ANSI_CSI) {
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
        if (c == 'm') {
            if (!ansi_cur_param_empty || ansi_param_count > 0) {
                ansi_param_count++;
            }
            ansi_apply_sgr();
            ansi_state = ANSI_NONE;
            return;
        }
        /* Altri terminatori CSI (es. 'H', 'J' per cursore/clear) non
         * supportati: consumiamo la sequenza e torniamo a NONE senza
         * stampare nulla, per evitare di mostrare i parametri come
         * testo letterale. */
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
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
