/* =============================================================================
 * bin/shutdown/shutdown.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Spegnere, riavviare, fermare — come PROGRAMMA e non solo come builtin
 *
 *     shutdown          spegne
 *     shutdown -r       riavvia
 *     shutdown -h       ferma la macchina senza togliere corrente
 *     poweroff / reboot / halt      lo stesso binario, guarda argv[0]
 *
 * ! ESISTE PERCHE' UN BUILTIN NON SI PUO' PASSARE A `sudo`. La shell aveva
 * gia' `shutdown`, `poweroff`, `reboot` e `halt`, e funzionavano — ma
 * `sudo shutdown` diceva «comando non trovato», perche' sudo fa uno spawn e
 * di un builtin non c'e' nessun file da eseguire. Un comando che esiste solo
 * dentro la shell e' un comando che NESSUN altro programma puo' usare: non
 * sudo, non un autoexec eseguito da un altro interprete, non un'applicazione
 * grafica che voglia lanciarlo.
 *
 * ! E IL BUILTIN RESTA. Non e' un doppione per sbadataggine: la shell di
 * recovery e' statica apposta, e deve poter spegnere una macchina il cui /bin
 * non si legge piu' — che e' esattamente la situazione in cui ci si trova
 * quando si sta riparando. Il builtin e' quello che c'e' sempre; questo
 * programma e' quello che si puo' passare a qualcun altro.
 *
 * ! CHI PUO' SPEGNERE LO DECIDE IL KERNEL, non questo file: root sempre, e
 * chiunque stia a una console di questa macchina. Da una sessione remota no.
 * Vedi puo_spegnere() in kernel/syscall/syscall_impl.c — qui si chiede e si
 * riporta la risposta, e non c'e' un secondo controllo che possa divergere dal
 * primo.
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `shutdown -version` la stampa. Vedi EX_VERSIONE. */
EX_VERSIONE("shutdown", "0.001");

static void uso(void)
{
    printf("uso: shutdown [-r|-h|-p]\n\n");
    printf("  (nessuna opzione)  spegne la macchina\n");
    printf("  -r                 riavvia\n");
    printf("  -h                 ferma il sistema senza togliere corrente\n");
    printf("  -p                 spegne (esplicito)\n\n");
    printf("Gli stessi tre gesti hanno anche il nome che tutti conoscono:\n");
    printf("  poweroff    reboot    halt\n\n");
    printf("Lo puo' chiedere root, oppure chi sta a una console di questa\n");
    printf("macchina. Da una sessione remota no: spegnere interromperebbe\n");
    printf("il lavoro di chi ci sta davanti.\n");
}

/* La parte finale di argv[0]: chi lo lancia col percorso intero deve avere
 * lo stesso comportamento di chi batte il nome. Stessa regola di id/whoami. */
static const char *nome_di(const char *p)
{
    const char *b = p;

    for (; p && *p; p++) if (*p == '/') b = p + 1;
    return b ? b : "shutdown";
}

int main(int argc, char **argv)
{
    const char  *nome = nome_di(argc > 0 ? argv[0] : "shutdown");
    unsigned int cosa = EXOS_RB_POWEROFF;
    int          i;

    if (strcmp(nome, "reboot") == 0)    cosa = EXOS_RB_RESTART;
    else if (strcmp(nome, "halt") == 0) cosa = EXOS_RB_HALT;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0)      cosa = EXOS_RB_RESTART;
        else if (strcmp(argv[i], "-h") == 0) cosa = EXOS_RB_HALT;
        else if (strcmp(argv[i], "-p") == 0) cosa = EXOS_RB_POWEROFF;
        else { uso(); return strcmp(argv[i], "-?") == 0 ? 0 : 1; }
    }

    /* Da qui in poi parla il kernel: ha il conto alla rovescia e l'esito
     * della sincronizzazione, e non torna piu' indietro. */
    reboot((int)cosa);

    /* ! SI ARRIVA QUI SOLO SE IL KERNEL HA DETTO DI NO, e la ragione e' una
     * sola. Dirlo per intero e' cio' che distingue una REGOLA da un guasto:
     * «rifiutato» manda a cercare un difetto, questo manda alla console. */
    printf("%s: non da qui.\n\n", nome);
    printf("  Lo spegnimento lo puo' chiedere root, oppure chi si trova a una\n");
    printf("  console di questa macchina. Questa sessione non e' ne' l'uno ne'\n");
    printf("  l'altro: da un terminale remoto non si spegne il computer di chi\n");
    printf("  ci sta lavorando davanti.\n\n");
    printf("  Con i privilegi:  sudo %s\n", nome);
    return 1;
}
