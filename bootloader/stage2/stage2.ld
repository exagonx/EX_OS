/* =============================================================================
 * bootloader/stage2/stage2.ld
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

/* =============================================================================
 * stage2.ld — Linker Script Stage 2
 * ExOS x86 32-bit
 * =============================================================================
 *
 * Stage 2 è caricato da Stage 1 all'indirizzo fisico 0x0500 in Real Mode.
 * Output: binario flat (non ELF) pronto per essere copiato nel floppy.
 *
 * Layout in memoria:
 *   0x0500  : entry point (_start in entry.asm) — PRIMA istruzione eseguita
 *   0x0500+ : codice stage 2
 *   ????    : dati stage 2
 *   ????    : BSS (zero-inizializzato da _start)
 *
 * Vincoli:
 *   - Tutto deve stare sotto 0x7000 (dove inizia lo stack)
 *   - Il codice non deve sovrascrivere la root directory (0x7E00)
 *   - Dimensione massima teorica: 0x7000 - 0x0500 = ~26KB (abbondante)
 * ============================================================================= */

OUTPUT_FORMAT("binary")         /* Output binario flat, non ELF */
OUTPUT_ARCH(i8086)              /* Architettura target */

ENTRY(_start)                   /* Entry point = _start in entry.asm */

SECTIONS
{
    /* Stage 2 inizia a 0x0500 */
    . = 0x0500;

    /* -------------------------------------------------------------------------
     * Sezione .text — Codice eseguibile
     * entry.asm (_start) deve essere il PRIMO oggetto linkato
     * Questo è garantito dall'ordine nel Makefile:
     *   $(LD) ... entry.o fat12.o loader.o print.o ...
     * ------------------------------------------------------------------------- */
    .text : {
        *(.text._start)     /* _start per primo */
        *(.text)            /* Tutto il resto del codice */
        *(.text.*)
    }

    /* -------------------------------------------------------------------------
     * Sezione .rodata — Dati sola lettura (stringhe costanti, tabelle)
     * ------------------------------------------------------------------------- */
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    /* -------------------------------------------------------------------------
     * Sezione .data — Dati inizializzati
     * ------------------------------------------------------------------------- */
    .data : {
        *(.data)
        *(.data.*)
    }

    /* -------------------------------------------------------------------------
     * Sezione .bss — Dati non inizializzati (zeroed at runtime)
     * _start in entry.asm deve azzerare questa sezione prima di chiamare C.
     * Esportiamo _bss_start e _bss_end per entry.asm.
     * ------------------------------------------------------------------------- */
    .bss : {
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _bss_end = .;
    }

    /* -------------------------------------------------------------------------
     * Simbolo di fine Stage 2 (per calcolare dimensione se necessario)
     * ------------------------------------------------------------------------- */
    _stage2_end = .;

    /* -------------------------------------------------------------------------
     * Sezioni da scartare (non necessarie in binario flat)
     * ------------------------------------------------------------------------- */
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.rel*)
        *(.rela*)
        *(.gnu*)
    }
}
