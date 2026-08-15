/* =============================================================================
 * kernel/kernel.ld
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

/* =============================================================================
 * kernel.ld — Linker Script Kernel
 * MyOS x86 32-bit
 * =============================================================================
 *
 * Il kernel è caricato da Stage 2 all'indirizzo fisico 0x100000 (1MB).
 * Gira in Protected Mode 32-bit, ring 0.
 * Output: ELF32 (non flat binary — il kernel gestisce il proprio formato).
 *
 * Layout in memoria:
 *   0x100000  : entry point (kernel_entry in arch/x86/entry.asm)
 *   0x100000+ : .text  (codice kernel)
 *   ????????  : .rodata (costanti, stringhe)
 *   ????????  : .data  (dati inizializzati)
 *   ????????  : .bss   (dati non inizializzati — azzerati da entry.asm)
 *
 * Simboli esportati usati dal codice C:
 *   _kernel_start   : inizio kernel
 *   _kernel_end     : fine kernel (per calcolare dimensione)
 *   _bss_start      : inizio BSS (da azzerare)
 *   _bss_end        : fine BSS
 *   _stack_top      : top dello stack kernel iniziale
 * ============================================================================= */

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(kernel_entry)

SECTIONS
{
    /* Indirizzo fisico di caricamento del kernel */
    . = 0x00100000;

    _kernel_start = .;

    /* -------------------------------------------------------------------------
     * .text — Codice eseguibile
     * kernel_entry (ASM) deve essere il primo simbolo
     * ------------------------------------------------------------------------- */
    .text ALIGN(4096) : {
        *(.text.kernel_entry)   /* Entry point per primo */
        *(.text)
        *(.text.*)
    }

    /* -------------------------------------------------------------------------
     * .rodata — Dati sola lettura
     * ------------------------------------------------------------------------- */
    .rodata ALIGN(4096) : {
        *(.rodata)
        *(.rodata.*)
    }

    /* -------------------------------------------------------------------------
     * .data — Dati inizializzati
     * ------------------------------------------------------------------------- */
    .data ALIGN(4096) : {
        *(.data)
        *(.data.*)
    }

    /* -------------------------------------------------------------------------
     * .bss — Dati non inizializzati
     * Azzerati da kernel_entry prima di chiamare kernel_main()
     * ------------------------------------------------------------------------- */
    .bss ALIGN(4096) : {
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _bss_end = .;
    }

    /* -------------------------------------------------------------------------
     * Stack kernel iniziale (16KB riservati nel BSS)
     * Usato solo fino a quando lo scheduler non imposta gli stack dei task
     * ------------------------------------------------------------------------- */
    .stack ALIGN(4096) : {
        . += 16384;             /* 16KB stack */
        _stack_top = .;
    }

    _kernel_end = .;

    /* -------------------------------------------------------------------------
     * Scarta sezioni non necessarie nel kernel
     * ------------------------------------------------------------------------- */
    /* Marca lo stack come non-eseguibile (elimina warning linker) */
    .note.GNU-stack (INFO) : { }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.gnu*)
    }
}
