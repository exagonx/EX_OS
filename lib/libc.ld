/* =============================================================================
 * lib/libc.ld
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il collegamento della LIBC CONDIVISA
 *
 * ! LA BASE E' 0x04800000, la terza fetta della fascia delle librerie. La mappa
 * completa sta in lib/exwin/exwin.ld: e' li' che si assegnano le fette, in un
 * posto solo, perche' due librerie alla stessa base si sovrascriverebbero a
 * vicenda dentro il processo che le usa tutt'e due.
 *
 * ! NIENTE --gc-sections QUANDO SI COLLEGA QUESTA. Una libreria condivisa non
 * sa chi la usera': una funzione che oggi non chiama nessuno la chiamera' il
 * programma installato domani, e allora dovrebbe esserci senza ricostruire
 * niente. E' il contrario di un programma, dove buttare cio' che non si usa e'
 * proprio quello che si vuole.
 *
 * ! QUATTRO MEGABYTE BASTANO: 322 funzioni sono circa 70 KB di testo. Il tetto
 * e' la fetta, non la libreria.
 * ============================================================================= */

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(libc_tabella)

SECTIONS
{
    . = 0x04800000;

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
     * intere. Vedi la nota estesa in lib/exwin/exwin.ld. */
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
