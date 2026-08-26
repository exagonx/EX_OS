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
 * ! LA MAPPA NON STA PIU' QUI, E NON E' UNA PERDITA: STA NEI FILE.
 *
 * Qui c'era l'elenco delle fette assegnate, scritto a mano. Per TRE volte ha
 * detto «libere» di fette che erano gia' occupate — l'ultima il 26 agosto
 * 2026, con excss.so dentro «0x05C00000 libere». E una fetta assegnata due
 * volte non da' un errore di collegamento: da' due librerie che si
 * sovrascrivono dentro il processo che le apre tutt'e due, cioe' un guasto che
 * si manifesta lontanissimo da dove e' stato fatto.
 *
 * Il difetto non era la disattenzione: era che la mappa e la verita' erano due
 * cose diverse. La verita' sta nella riga «. = 0x...» di ogni .ld e nella
 * misura vera di ogni .so costruito, e adesso a leggerle e' un programma:
 *
 *     python3 tools/fette.py            la mappa, e dice se qualcosa si tocca
 *     python3 tools/fette.py --libera   il prossimo indirizzo libero
 *
 * Lo lancia anche il build. Una nuova libreria si assegna chiedendo a lui
 * l'indirizzo, non leggendo un commento.
 *
 * ! UN MEGABYTE A TESTA, NON QUATTRO. Le otto librerie di oggi stanno tutte
 * sotto i 120 KB e ognuna aveva una fetta da quattro megabyte: il 97 per cento
 * dello spazio era sprecato, e la fascia ne teneva sedici in tutto. A un
 * megabyte ne tiene sessantaquattro — quante ne dichiara LIB_MAX in
 * kernel/loader/lib.c, e le due cose devono potersi guardare in faccia: un
 * kernel che ne promette 64 sopra una fascia che ne tiene 16 e' una promessa
 * che gli indirizzi non mantengono.
 *
 * ! E CHI E' PIU' GRANDE DI UN MEGABYTE PRENDE PIU' FETTE DI SEGUITO. Non e'
 * un caso particolare da gestire: si sceglie una base che abbia dietro lo
 * spazio che serve, e tools/fette.py — che le misure le legge dagli ELF veri —
 * si accorge se quella dopo e' troppo vicina. E' il controllo che comincera' a
 * contare da solo il giorno che arrivera' un motore JavaScript, che in un
 * megabyte non ci sta per definizione.
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
