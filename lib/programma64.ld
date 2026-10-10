/* =============================================================================
 * lib/programma64.ld — come si collega un programma di EX-OS a 64 bit
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Il gemello di lib/programma.ld (@EXOS-64, tappa 4). Stesso indirizzo di
 * partenza e stessa regola: i dati cominciano su una pagina nuova, cosi' il
 * codice resta in un segmento senza permesso di scrittura.
 *
 * ! UNO PER TUTTI. A 32 bit ogni comando di /bin ha il suo .ld, quasi sempre
 * uguale a questo; a 64 bit si parte con uno solo, e chi avra' bisogno di
 * qualcosa di diverso se lo fara'.
 *
 * ! I PROGRAMMI STANNO SOTTO I 4 GB, e non e' un caso: il kernel a 64 bit da'
 * ai programmi lo stesso spazio di quello a 32 (da 64 MB a 3 GB), e gli
 * indirizzi che passano per le chiamate di sistema ci contano.
 * ============================================================================= */
OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
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

    /* ! PAGINA NUOVA GIA' QUI: i costruttori e l'immagine delle variabili
     * __thread sono dati. Nella stessa pagina del codice il collegatore ne
     * farebbe un segmento solo, scrivibile ED eseguibile. */
    . = ALIGN(4096);

    /* i costruttori globali: l'avvio (lib/libc_avvio.c) li chiama in ordine */
    .init_array : {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN(__fini_array_end = .);
    }

    /* le variabili __thread: il kernel ne fa una copia per ogni filo */
    .tdata : { *(.tdata) *(.tdata.*) }
    .tbss  : { *(.tbss)  *(.tbss.*) *(.tcommon) }

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
