/* =============================================================================
 * lib/exdlg/exdlg.ld
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ! LA BASE E' 0x04400000, la seconda fetta della fascia delle librerie. La
 * mappa completa sta in lib/exwin/exwin.ld: e' li' che si assegnano le fette,
 * in un posto solo, perche' due librerie alla stessa base si sovrascriverebbero
 * a vicenda dentro il processo che le usa tutt'e due — e questa e' proprio la
 * coppia che si usa insieme.
 * ============================================================================= */

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(exdlg_tabella)

SECTIONS
{
    . = 0x04400000;

    .exlib_testa : {
        KEEP(*(.exlib_testa))
    }

    .text : {
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    /* Una pagina nuova prima dei dati scrivibili: il kernel condivide le
     * pagine di sola lettura e copia quelle scrivibili, e lavora a pagine
     * intere. Vedi la nota estesa in exwin.ld. */
    . = ALIGN(4096);

    .data : {
        *(.data)
        *(.data.*)
    }

    .bss : {
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _bss_end = .;
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.gnu*)
        *(.rel*)
        *(.rela*)
    }
}
