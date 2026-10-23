/* =============================================================================
 * bin/ps/ps.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * =============================================================================
 *
 * I processi, e chi usa un montaggio (@RDBOOT passo 4, 22 ottobre 2026)
 *
 *   ps               tutti: pid, padre, utente, stato, file aperti, da quale
 *                    montaggio viene il programma, nome
 *   ps -m <punto>    solo chi usa quel montaggio, e perche': e' la risposta a
 *                    «umount dice occupato: chi e'?»
 *
 * ! STA NEL SISTEMA DI BASE (/rdboot/bin), come kill: servono proprio quando
 * il disco di sistema sta per andarsene (system -unload, passo 5), e un
 * programma che vive sul disco lo terrebbe occupato lui stesso.
 *
 * ! L'ELENCO E' PER TUTTI, chiudere un processo no (scelta dell'utente, 22
 * ottobre 2026): vedere chi c'e' non toglie niente a nessuno; kill di un
 * processo altrui lo rifiuta il kernel (segnali.c), non questo programma.
 *
 * I fili non compaiono: condividono file e directory col capogruppo, e lo
 * ripeterebbero. Li conta `stack`, che li elenca uno per uno.
 * ============================================================================= */
#include "libc.h"

/* +0.001 a ogni modifica: `ps -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("ps", "0.002");

#define BLOCCO PROCINFO_MAX_BATCH

static const char *nome_stato(unsigned int s)
{
    switch (s) {
        case 1:  return "pronto";
        case 2:  return "esecuz";
        case 3:  return "blocc.";
        case 4:  return "zombie";
        case 5:  return "dorme";
        case 6:  return "nasce";
        default: return "?";
    }
}

/* Il nome da /boot/utenti (nome_utente della libc); senza il file - da
 * floppy, o col disco smontato - il numero. */
static const char *utente_di(unsigned int uid, char *out, unsigned int max)
{
    if (!nome_utente(uid, out, max)) snprintf(out, max, "%u", uid);
    return out;
}

static void uso(void)
{
    printf("uso: ps                 i processi\n");
    printf("     ps -m <punto>      chi usa quel montaggio (es. ps -m /)\n");
}

int main(int argc, char **argv)
{
    ProcUso      v[BLOCCO];
    const char  *punto = NULL;
    unsigned int start = 0, tot = 0;
    char         u[16];
    int          n, i;

    if (argc > 1) {
        if (strcmp(argv[1], "-m") == 0 && argc > 2) punto = argv[2];
        else { uso(); return argv[1][1] == 'h' ? 0 : 1; }
    }

    if (punto)
        printf("%-6s%-10s%-20s%s\n", "PID", "UTENTE", "NOME", "USA " );
    else
        printf("%-6s%-6s%-10s%-8s%-5s%-9s%s\n",
               "PID", "PADRE", "UTENTE", "STATO", "FILE", "DA", "NOME");

    while ((n = proc_uso(v, BLOCCO, start, punto)) > 0) {
        for (i = 0; i < n; i++) {
            ProcUso *q = &v[i];

            utente_di(q->uid, u, sizeof(u));
            if (punto) {
                printf("%-6u%-10s%-19s ", q->pid, u, q->name);   /* a space even after a long name */
                if (q->perche & PROCUSO_PROGRAMMA) printf("il programma ");
                if (q->perche & PROCUSO_FILE)
                    printf("%u file apert%s ", q->n_file, q->n_file == 1 ? "o" : "i");
                if (q->perche & PROCUSO_CARTELLA)  printf("la directory corrente");
                /* ! UNO ZOMBIE TIENE ANCORA IL PROGRAMMA: l'eseguibile si
                 * chiude quando il padre lo raccoglie (proc_reap_zombie), e
                 * fino ad allora umount ha ragione a dire occupato. */
                if (q->state == 4) printf("(terminato: aspetta che il padre lo raccolga)");
                printf("\n");
            } else {
                printf("%-6u%-6u%-10s%-8s%-5u%-9s%s\n", q->pid, q->ppid, u,
                       nome_stato(q->state), q->n_file,
                       q->punto_prog[0] ? q->punto_prog : "(RAM)", q->name);
            }
            tot++;
        }
        start += (unsigned int)n;
    }
    if (n < 0) {
        printf("ps: il kernel non risponde (%d): e' piu' vecchio del 0.270?\n", n);
        return 1;
    }

    if (punto && tot == 0) printf("(nessuno usa %s)\n", punto);
    return 0;
}
