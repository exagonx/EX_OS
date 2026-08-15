/* =============================================================================
 * bin/textline/textline.ld
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Linker script per programmi utente generici.
 * Identico nello schema a bin/hello/hello.ld e bin/sh/shell.ld: stesso
 * indirizzo base (0x08000000), perche' i programmi utente non girano
 * mai in concorrenza allo stesso indirizzo — il loader ELF del kernel
 * li carica uno alla volta nello spazio utente del processo che li
 * esegue. ls.c viene compilato insieme a lib/libc.c (vedi Makefile),
 * ma resta un singolo binario statico: questo script non cambia.
 * ============================================================================= */

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    . = 0x08000000;

    .text : {
        *(.text._start)
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    /* Immagine di partenza delle variabili __thread: vanno tenute adiacenti
     * e in quest'ordine. Vedi bin/libctest/libctest.ld per il perche'. */
    .tdata : { *(.tdata) *(.tdata.*) }
    .tbss  : { *(.tbss)  *(.tbss.*) *(.tcommon) }

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
