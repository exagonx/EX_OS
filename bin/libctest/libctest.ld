/* =============================================================================
 * bin/libctest/libctest.ld
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Linker script di /bin/libctest, identico a quello degli altri
 * programmi utente: stesso indirizzo base (0x08000000), stesso schema di
 * sezioni. libctest.c viene compilato insieme a lib/libc.c e resta un
 * singolo binario statico.
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

    /* =========================================================================
     * Le variabili __thread — .tdata e .tbss
     *
     * Non sono sezioni normali: insieme formano l'IMMAGINE DI PARTENZA che
     * il caricatore copia in un blocco per processo (vedi PT_TLS in
     * kernel/loader/elf.c). Vanno tenute ADIACENTI e in quest'ordine,
     * perche' il modello local-exec di i386 indirizza le variabili con
     * offset negativi da un unico thread pointer.
     *
     * ! Senza queste due righe `ld` le sistema dove capita e il segmento
     * PT_TLS puo' non essere generato affatto: il programma compila, si
     * collega e legge una variabile thread-local che sta da un'altra parte.
     * ========================================================================= */
    .tdata : { *(.tdata) *(.tdata.*) }
    .tbss  : { *(.tbss)  *(.tbss.*) *(.tcommon) }

    /* =========================================================================
     * ! UNA PAGINA NUOVA PRIMA DEI DATI, E NON E' ALLINEAMENTO PER BELLEZZA.
     *
     * Il kernel mappa in sola lettura i segmenti senza PF_W e scrivibili gli
     * altri (kernel/loader/elf.c). Ma se .text e .data stanno nella STESSA
     * pagina, il collegatore ne fa un segmento solo con i permessi dell'unione
     * — RWE — e allora IL CODICE DEL PROGRAMMA E' SCRIVIBILE. Un difetto che
     * permette a un overflow di riscrivere le istruzioni invece che solo i
     * dati.
     *
     * Si vedeva a ogni collegamento, e nessuno lo leggeva:
     *
     *     ld: warning: build/bin/sh has a LOAD segment with RWX permissions
     *
     * Non e' un limite della CPU: su i386 non c'e' il bit NX, quindi una
     * pagina di dati resta eseguibile — ma il CODICE si puo' rendere non
     * scrivibile, ed e' meta' della difesa, disponibile su qualunque
     * processore. Costa una pagina per programma.
     * ========================================================================= */
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
