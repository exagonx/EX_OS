/* =============================================================================
 * lib/exqjs/quickjs.ld
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ! IL FILE SI CHIAMA COME LA LIBRERIA, E NON COME LA DIRECTORY, ed e' una
 * regola che non si vede finche' non la si rompe: `tools/fette.py` ricava il
 * nome della libreria dal nome del .ld e va a cercare quel .so. Chiamandolo
 * exqjs.ld, la mappa delle fette avrebbe dichiarato «non costruita» proprio la
 * libreria piu' grossa di tutte — cioe' l'unica che puo' davvero scavalcare
 * una vicina.
 *
 * ! LA BASE E' 0x04A00000, e l'ha detta `python3 tools/fette.py --libera`. La
 * mappa completa sta in lib/exwin/exwin.ld: e' li' che si assegnano, in un
 * posto solo, perche' due librerie alla stessa base si sovrascriverebbero a
 * vicenda dentro il processo che le usa tutt'e due.
 *
 * ! QUESTA E' LA PRIMA LIBRERIA CHE RIEMPIE QUASI TUTTA LA SUA FETTA: 648 KB
 * su 1024. Le altre stanno fra i 18 e i 120. La fetta seguente resta libera e
 * va lasciata libera finche' QuickJS non e' misurato di nuovo — un motore
 * cresce a ogni versione di upstream, e la prima cosa che farebbe crescendo e'
 * scavalcare il vicino senza dire niente. `tools/fette.py` se ne accorge: si
 * lancia dopo ogni aggiornamento.
 *
 * ! E LA TABELLA DI ESPORTAZIONE E' LA STESSA DI exjs.so, LO STESSO FILE.
 * Non e' una comodita': e' cio' che garantisce che le due librerie esportino
 * gli stessi nomi. Se una delle due si allontanasse dall'interfaccia, non
 * compilerebbe piu' — invece di scoprirlo il giorno che il browser apre
 * l'altra e non trova un simbolo.
 * ============================================================================= */

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(exjs_tabella)

SECTIONS
{
    . = 0x04A00000;

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
