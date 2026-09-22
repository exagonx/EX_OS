/* =============================================================================
 * lib/exzip/exzip.ld
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ! THE BASE IS 0x04B00000, and it was not chosen by reading a comment: it was
 * asked for with `python3 tools/fette.py --libera`, which measures the real
 * ELF files instead of trusting a hand-written map. Two libraries on the same
 * base do not fail to link - they overwrite each other inside the process that
 * opens both, which is a fault that shows up a long way from where it was
 * made. The full story is at the top of lib/exwin/exwin.ld.
 * ============================================================================= */

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(exzip_tabella)

SECTIONS
{
    . = 0x04B00000;

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
