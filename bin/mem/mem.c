/* =============================================================================
 * bin/mem/mem.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Stato della memoria fisica, per fascia dell'architettura PC.
 *
 *   mem       tabella per fascia, valori in KB
 *   mem -b    valori in byte
 *
 * I dati vengono da SYS_MEMINFO, che li ricava interrogando la bitmap del
 * PMM: sono la situazione reale della memoria fisica, non una stima.
 *
 * NOTA SUI FORMATI: la printf della libc di EX-OS supporta i flag '-' e
 * '0' e una larghezza NUMERICA, ma non la larghezza dinamica '%*u'. Le
 * colonne sono quindi scritte a mano ("%10u", "%-14s"). Cambiando una
 * larghezza vanno aggiornate tutte le righe, intestazione compresa.
 *
 * Dieci caratteri piu' uno SPAZIO esplicito fra le colonne: con "mem -b" i
 * valori arrivano a dieci cifre (4294967295) e riempiono il campo esatto,
 * quindi senza lo spazio due colonne adiacenti si fondono in un unico
 * numero illeggibile. E' successo davvero, alla prima prova di "mem -b".
 * ============================================================================= */
#include "libc.h"

/* Barra di occupazione: mostra a colpo d'occhio quale fascia è satura,
 * cosa che tre numeri in fila non comunicano altrettanto bene. */
#define BARRA 20

/* Tante spaziature quanto "[" + BARRA + "]", per allineare le righe che
 * la barra non ce l'hanno. */
static const char VUOTO[] = "                      ";

static void stampa_barra(unsigned int usata, unsigned int totale)
{
    unsigned int piene, i;

    if (totale == 0) { printf("%s", VUOTO); return; }

    /* Il calcolo è in KB e resta sotto i 32 bit anche con 4 GB
     * (4194304 * 20 non trabocca), quindi non serve dividere prima. */
    piene = (usata * BARRA) / totale;
    if (piene > BARRA) piene = BARRA;

    /* Una fascia usata anche solo in parte mostra almeno un blocco: una
     * barra vuota accanto a un numero diverso da zero sarebbe bugiarda. */
    if (piene == 0 && usata > 0) piene = 1;

    putchar('[');
    for (i = 0; i < BARRA; i++) putchar(i < piene ? '#' : '.');
    putchar(']');
}

/* Una riga della tabella. tot == 0 significa "fascia non presente": si
 * stampano trattini invece di tre zeri, che sembrerebbero una misura
 * valida fatta su una fascia esistente e vuota. */
static void riga(const char *nome, unsigned int tot, unsigned int libera,
                 unsigned int mult, const char *unita)
{
    unsigned int usata = tot - libera;   /* tot >= libera per costruzione */

    printf("%-14s", nome);

    if (tot == 0) {
        printf("%10s %10s %10s  ", "-", "-", "-");
        printf("%s", VUOTO);
        printf("  %s\n", unita);
        return;
    }

    printf("%10u %10u %10u  ", tot * mult, usata * mult, libera * mult);
    stampa_barra(usata, tot);
    printf("  %s\n", unita);
}

int main(int argc, char **argv)
{
    MemInfo      mi;
    int          rc;
    int          byte = 0;
    unsigned int mult;
    const char  *unita;
    int          i;

    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-' && argv[i][1] == 'b') byte = 1;
    }

    rc = meminfo(&mi);
    if (rc < 0) {
        printf("mem: impossibile leggere lo stato della memoria (%d)\n", rc);
        /* -22 = EINVAL: le due copie della struttura MemInfo (kernel e
         * libc) non hanno la stessa dimensione. È l'unico modo in cui una
         * desincronizzazione fra i due header si manifesta, ed è meglio
         * dirlo che stampare numeri senza senso. */
        if (rc == -22) {
            printf("mem: MemInfo non coincide fra kernel e libc — ricompilare tutto\n");
        }
        return 1;
    }

    mult  = byte ? 1024u : 1u;
    unita = byte ? "byte" : "KB";

    printf("\n");
    printf("%-14s%10s %10s %10s\n", "Fascia", "totale", "usata", "libera");
    printf("----------------------------------------------------------------------------\n");

    riga("convenzionale", mi.conv_total_kb, mi.conv_free_kb, mult, unita);
    riga("superiore",     mi.uma_total_kb,  mi.uma_free_kb,  mult, unita);
    riga("estesa",        mi.ext_total_kb,  mi.ext_free_kb,  mult, unita);
    riga("espansa",       mi.ems_total_kb,  mi.ems_free_kb,  mult, unita);

    printf("----------------------------------------------------------------------------\n");
    riga("TOTALE",        mi.total_kb,      mi.free_kb,      mult, unita);

    printf("\n");
    printf("convenzionale = sotto 640 KB   superiore = 640 KB-1 MB (BIOS, video)\n");
    printf("estesa        = da 1 MB in su  pagina    = %u byte\n", mi.page_size);
    printf("\n");

    /* La riga "espansa" è a trattini e senza questa nota sembrerebbe una
     * funzione mancante. Non lo è: è una cosa che su questa macchina non
     * ha ragione di esistere, e va detto invece di lasciarlo intuire. */
    printf("La memoria ESPANSA (EMS) non esiste su questo sistema, e non e' una\n");
    printf("lacuna: era il meccanismo a banchi commutati con cui 8086 e 286\n");
    printf("superavano il limite di 1 MB del modo reale. EX-OS gira in modo\n");
    printf("protetto con paginazione, dove quel limite non c'e': tutta la RAM\n");
    printf("oltre 1 MB e' gia' direttamente indirizzabile come memoria estesa.\n");
    printf("\n");

    return 0;
}
