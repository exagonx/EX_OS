/* =============================================================================
 * drivers/wserver/wserver.ld — EX-OS window server linker script
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * ET_EXEC statico, non più ET_DYN.
 *
 * Fino a luglio 2026 questo script produceva un oggetto condiviso
 * (ORG 0, sezioni .dynsym/.dynamic/.rel.*, ENTRY(drv_init)) pensato per
 * essere mappato nello spazio del KERNEL da dynlink.c, che ne risolveva
 * le rilocazioni contro i simboli del kernel. elf_load(), che carica i
 * processi ring3, accetta solo ET_EXEC e lo rifiutava con
 * "tipo non supportato (type=3)".
 *
 * Ora il driver è un normale eseguibile utente: stesso schema di
 * bin/ls/ls.ld, stesso indirizzo base. L'indirizzo può coincidere con
 * quello di shell/ls/hello perché ogni processo ha la propria page
 * directory — non girano mai nello stesso spazio di indirizzi.
 *
 * ENTRY(_start): il punto di ingresso è quello di lib/start.S (weak),
 * che legge argc/argv e chiama _libc_start -> main(). Non più drv_init:
 * il driver non è più una libreria di funzioni chiamate dal kernel, ma
 * un processo con un proprio loop di servizio.
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
