/* =============================================================================
 * lib/excss/excss.ld
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ! LA BASE E' 0x05C00000, l'ottava fetta della fascia delle librerie. La
 * mappa completa sta in lib/exwin/exwin.ld: e' li' che si assegnano, in un
 * posto solo, perche' due librerie alla stessa base si sovrascriverebbero a
 * vicenda dentro il processo che le usa tutt'e due — e questa si usa proprio
 * insieme a exwin, alla libc, a exhttp e a exhtml — di cui e' anche l'unica
 * libreria che si serve: uno stile si applica a un albero, e l'albero lo fa
 * exhtml.
 * ============================================================================= */

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(excss_tabella)

SECTIONS
{
    . = 0x04700000;

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
