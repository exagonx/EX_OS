/* =============================================================================
 * lib/exwin/exwin.ld
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il collegamento di una LIBRERIA CONDIVISA, che non e' quello di un programma
 *
 * ! LA BASE E' 0x04000000 E NON SI SPOSTA. La libreria non e' PIC: i suoi
 * salti e i suoi puntatori sono indirizzi assoluti, quindi deve trovarsi
 * sempre allo stesso posto in ogni processo che la usa. Quei 64 MB fra
 * USER_SPACE_BASE e l'indirizzo dei programmi (0x08000000) sono riservati alle
 * librerie proprio per questo — vedi kernel/loader/lib.c.
 *
 * ! OGNI LIBRERIA HA LA SUA FETTA, e vanno assegnate qui, in un posto solo:
 *
 *     0x04000000   exwin.so    il toolkit
 *     0x04400000   exdlg.so    i dialoghi (Apri/Salva)
 *     0x04800000   libc.so     la libreria C
 *     0x04C00000   eximg.so    i formati d'immagine (PNG, JPG, ICO)
 *     0x05000000   exfont.so   il TrueType
 *     0x05400000   exhttp.so   la rete
 *     0x05800000   exhtml.so   l'albero del marcatore
 *     0x05C00000   libere, fino a 0x08000000 (nove fette)
 *
 * ! QUESTA MAPPA VA TENUTA IN PARI, e per due volte non lo e' stata: diceva
 * «0x05000000 libere» mentre exfont e exhttp erano gia' li'. Una fetta
 * assegnata due volte non da' un errore di collegamento — da' due librerie che
 * si sovrascrivono dentro il processo che le apre tutt'e due, cioe' un guasto
 * che si manifesta lontano da dove e' stato fatto.
 *
 * Quattro megabyte a testa: una page table esatta, e nessuna libreria di
 * EX-OS e' lontanamente vicina a riempirla.
 *
 * ! LA TABELLA STA PRIMA DI TUTTO IL RESTO, e con KEEP(). E' il punto
 * d'ingresso dichiarato con ENTRY(): metterla in .rodata insieme al resto
 * vorrebbe dire che una riorganizzazione del collegamento puo' spostarla, e
 * allora l'indirizzo che il kernel rende non sarebbe piu' il suo. Qui e'
 * esattamente a 0x04000000, e si puo' verificare con readelf.
 * ============================================================================= */

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(exwin_tabella)

SECTIONS
{
    . = 0x04000000;

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

    /* ! .data E .bss VANNO SU UNA PAGINA LORO, e l'allineamento non e'
     * cosmesi: il kernel condivide le pagine di SOLA LETTURA e ne da' una
     * copia privata di quelle scrivibili, e lavora a pagine intere. Se la fine
     * di .rodata e l'inizio di .data stessero nella stessa pagina, quella
     * pagina sarebbe scrivibile — cioe' copiata per ogni processo — e mezza
     * .rodata smetterebbe di essere condivisa senza che nessuno lo dica. */
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
