/* =============================================================================
 * bin/stack/stack.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * =============================================================================
 *
 * Mostra, per ogni processo vivo, come sono allocati i suoi stack.
 *
 *   stack       tabella riassuntiva
 *   stack -v    aggiunge gli indirizzi grezzi
 *
 * La distinzione che questo programma esiste per rendere visibile:
 *
 *   IMPEGNATO  RAM fisica davvero occupata adesso (top - base). Lo stack
 *              utente parte da USER_STACK_INIT e cresce su page fault.
 *   RISERVATO  spazio di indirizzamento prenotato (top - limit). NON costa
 *              RAM: e' solo il confine oltre il quale il processo muore.
 *
 * Lo stack KERNEL non ha questa distinzione: viene allocato per intero
 * alla creazione del processo e non cresce mai. E' il motivo per cui in
 * quella colonna impegnato e riservato coincidono sempre.
 *
 * NOTA SUI FORMATI: la printf della libc non supporta la larghezza
 * dinamica '%*u'. Le colonne sono scritte a mano.
 * ============================================================================= */
#include "libc.h"

/* +0.001 a ogni modifica: `stack -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("stack", "0.001");

#define BLOCCO PROCINFO_MAX_BATCH

static const char *nome_stato(unsigned int s)
{
    switch (s) {
        case 1:  return "pronto";
        case 2:  return "esecuz";
        case 3:  return "blocc.";
        case 4:  return "zombie";
        case 5:  return "dorme ";
        /* Creato ma non ancora caricato: si vede solo cogliendo un `spawn` a
         * meta'. Vedi PROC_NASCENTE in kernel/include/sched.h. */
        case 6:  return "nasce ";
        default: return "?     ";
    }
}

int main(int argc, char **argv)
{
    ProcInfo     v[BLOCCO];
    int          n, i;
    unsigned int start = 0;
    int          verboso = 0;
    unsigned int tot_imp = 0, tot_ris = 0, tot_k = 0;
    unsigned int processi = 0;

    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-' && argv[i][1] == 'v') verboso = 1;
    }

    printf("\n");
    printf("%-5s%-14s%-8s%9s%9s%9s\n",
           "PID", "nome", "stato", "u.imp.", "u.ris.", "kernel");
    printf("--------------------------------------------------------------\n");

    /* Paginazione: il kernel restituisce al massimo PROCINFO_MAX_BATCH voci
     * per chiamata. Fermarsi al primo blocco significherebbe nascondere i
     * processi successivi senza dirlo — lo stesso troncamento silenzioso
     * gia' corretto in ls e delete. */
    for (;;) {
        n = procinfo(v, BLOCCO, start);

        if (n < 0) {
            printf("stack: impossibile leggere l'elenco dei processi (%d)\n", n);
            if (n == -22) {
                printf("stack: ProcInfo non coincide fra kernel e libc - ricompilare tutto\n");
            }
            return 1;
        }
        if (n == 0) break;

        for (i = 0; i < n; i++) {
            ProcInfo    *p = &v[i];
            unsigned int imp_kb, ris_kb, k_kb;

            /* Un task kernel (idle, init) non ha stack utente: top e limit
             * valgono 0. Mostrare "0 KB" sarebbe una misura, un trattino
             * dice la cosa giusta, cioe' che la voce non si applica. */
            imp_kb = (p->ustack_top && p->ustack_base && p->ustack_top > p->ustack_base)
                     ? (p->ustack_top - p->ustack_base) / 1024 : 0;
            ris_kb = (p->ustack_top && p->ustack_limit && p->ustack_top > p->ustack_limit)
                     ? (p->ustack_top - p->ustack_limit) / 1024 : 0;
            k_kb   = (p->kstack_top > p->kstack_base)
                     ? (p->kstack_top - p->kstack_base) / 1024 : 0;

            printf("%-5u%-14s%-8s", p->pid, p->name, nome_stato(p->state));

            if (ris_kb == 0) printf("%9s%9s", "-", "-");
            else             printf("%8uK%8uK", imp_kb, ris_kb);

            printf("%8uK\n", k_kb);

            if (verboso) {
                printf("     top=0x%08x base=0x%08x limite=0x%08x  kstack 0x%08x-0x%08x\n",
                       p->ustack_top, p->ustack_base, p->ustack_limit,
                       p->kstack_base, p->kstack_top);
            }

            tot_imp += imp_kb;
            tot_ris += ris_kb;
            tot_k   += k_kb;
            processi++;
        }

        start += (unsigned int)n;
        if (n < BLOCCO) break;   /* blocco incompleto: erano gli ultimi */
    }

    printf("--------------------------------------------------------------\n");
    printf("%-27s%8uK%8uK%8uK\n", "TOTALE", tot_imp, tot_ris, tot_k);
    printf("\n");
    printf("processi: %u\n", processi);
    printf("\n");
    printf("u.imp. = stack UTENTE impegnato: RAM occupata adesso. Parte piccolo\n");
    printf("         e cresce una pagina alla volta, su page fault, solo se il\n");
    printf("         programma la tocca davvero.\n");
    printf("u.ris. = stack UTENTE riservato: spazio di indirizzamento prenotato.\n");
    printf("         NON costa RAM, e' il confine oltre il quale il processo\n");
    printf("         viene terminato per stack esaurito.\n");
    printf("kernel = stack KERNEL: allocato per intero alla creazione del\n");
    printf("         processo e mai cresciuto, quindi qui impegnato = riservato.\n");
    printf("\n");
    printf("Un trattino indica un task kernel (idle, init): non ha stack utente.\n");
    printf("\n");

    return 0;
}
