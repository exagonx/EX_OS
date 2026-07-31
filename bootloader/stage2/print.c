/* =============================================================================
 * bootloader/stage2/print.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#include "stage2.h"

/* Dichiarazione esterna: print16 è in entry.asm */
extern void print16(const char *s);

/* Posizione cursore corrente (per print_hex e print_dec) */

/* =============================================================================
 * print_string — Stampa stringa ASCIIZ
 * ============================================================================= */
void print_string(const char *s)
{
    print16(s);
}

/* =============================================================================
 * print_char — Stampa un singolo carattere
 * ============================================================================= */
void print_char(char c)
{
    char buf[2];
    buf[0] = c;
    buf[1] = '\0';
    print16(buf);
}

/* =============================================================================
 * print_newline — Va a capo
 * ============================================================================= */
void print_newline(void)
{
    print16("\r\n");
}

/* =============================================================================
 * print_hex8 — Stampa un byte in esadecimale (es. "0xFF")
 * ============================================================================= */
void print_hex8(uint8_t val)
{
    static const char hex_chars[] = "0123456789ABCDEF";
    char buf[5];
    buf[0] = '0';
    buf[1] = 'x';
    buf[2] = hex_chars[(val >> 4) & 0x0F];
    buf[3] = hex_chars[val & 0x0F];
    buf[4] = '\0';
    print16(buf);
}

/* =============================================================================
 * print_hex16 — Stampa un word in esadecimale (es. "0x1234")
 * ============================================================================= */
void print_hex16(uint16_t val)
{
    static const char hex_chars[] = "0123456789ABCDEF";
    char buf[7];
    buf[0] = '0';
    buf[1] = 'x';
    buf[2] = hex_chars[(val >> 12) & 0x0F];
    buf[3] = hex_chars[(val >>  8) & 0x0F];
    buf[4] = hex_chars[(val >>  4) & 0x0F];
    buf[5] = hex_chars[val & 0x0F];
    buf[6] = '\0';
    print16(buf);
}

/* =============================================================================
 * print_hex32 — Stampa un dword in esadecimale (es. "0x00100000")
 * ============================================================================= */
void print_hex32(uint32_t val)
{
    static const char hex_chars[] = "0123456789ABCDEF";
    char buf[11];
    uint8_t i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 8; i++) {
        buf[2 + i] = hex_chars[(val >> (28 - i * 4)) & 0x0F];
    }
    buf[10] = '\0';
    print16(buf);
}

/* =============================================================================
 * print_dec — Stampa un valore uint32_t in decimale
 * ============================================================================= */
void print_dec(uint32_t val)
{
    char buf[12];   /* max 10 cifre + segno + null */
    uint8_t i = 10;
    buf[11] = '\0';

    if (val == 0) {
        print16("0");
        return;
    }

    while (val > 0 && i > 0) {
        buf[i--] = '0' + (val % 10);
        val /= 10;
    }
    print16(&buf[i + 1]);
}

/* =============================================================================
 * print_status — Stampa un messaggio di stato formattato
 * Formato: "[  OK  ] messaggio" oppure "[ FAIL ] messaggio"
 * ============================================================================= */
void print_status(const char *msg, int ok)
{
    if (ok) {
        print16("[  OK  ] ");
    } else {
        print16("[ FAIL ] ");
    }
    print16(msg);
    print16("\r\n");
}

/* =============================================================================
 * print_progress — Stampa messaggio di avanzamento
 * Formato: "[  >>  ] messaggio"
 * ============================================================================= */
void print_progress(const char *msg)
{
    print16("[  >>  ] ");
    print16(msg);
    print16("\r\n");
}

/* =============================================================================
 * print_error — Stampa messaggio di errore e si ferma
 * ============================================================================= */
void print_error(const char *msg)
{
    print16("\r\n[ ERR  ] ");
    print16(msg);
    print16("\r\n");
    print16("Sistema fermato. Riavvia il computer.\r\n");

    /* halt16 è in entry.asm */
    extern void halt16(void);
    halt16();
}

/* =============================================================================
 * print_banner — Stampa banner di avvio Stage 2
 * ============================================================================= */
void print_banner(void)
{
    print16("\r\n");
    print16("  ============================================\r\n");
    print16("   ExOS Stage 2 Loader v0.1\r\n");
    print16("   x86 32-bit Baremental OS\r\n");
    print16("  ============================================\r\n");
    print16("\r\n");
}
