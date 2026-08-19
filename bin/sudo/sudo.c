/* =============================================================================
 * bin/sudo/sudo.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * sudo — ESEGUIRE UN COMANDO da root avendone il diritto
 *
 *     sudo <comando> [argomenti]   esegue quel comando come root
 *     sudo -s                      apre una shell di root
 *
 * ! `sudo` ESEGUE UN COMANDO, `su` APRE UNA SHELL, e non sono la stessa cosa
 * detta in due modi: con `sudo` i poteri durano quanto il comando e finiscono
 * da soli, con una shell durano finche' qualcuno si ricorda di uscire. La
 * seconda si ottiene comunque — `sudo -s` — ma bisogna CHIEDERLA, e quello e'
 * il punto: e' una cosa diversa, e chi la vuole lo dice.
 *
 * ! SENZA ARGOMENTI NON FA NIENTE E SPIEGA. Aprire una shell di root perche'
 * qualcuno ha battuto `sudo` e basta sarebbe dare la cosa piu' pericolosa
 * delle due a chi non l'ha chiesta.
 *
 * ! SI CHIEDE LA TUA PASSWORD, NON QUELLA DI root, se sei elencato in
 * /boot/amministratori. E' la differenza fra `sudo` e `su`, ed e' quella che
 * conta: la password di root non deve girare fra le persone. Chi non e'
 * amministratore puo' comunque passare dando la password DI root — cosi' una
 * macchina si ripara anche quando l'elenco e' vuoto.
 *
 * ! QUESTO PROGRAMMA NON HA NESSUN PRIVILEGIO, E NON DEVE AVERNE. Non e'
 * setuid — su EX-OS quel bit non esiste, ed e' una fortuna: renderebbe
 * pericoloso ogni eseguibile che lo porta. Qui si passano nome e password al
 * kernel, e SOLO IL KERNEL decide. Sostituire questo file non fa guadagnare
 * niente a nessuno: la porta non e' qui.
 *
 * ! E I POTERI FINISCONO CON IL COMANDO. Diventa root questo processo, che
 * esegue una cosa sola e muore; la shell da cui sei partito resta la tua, con
 * il tuo uid. Non c'e' niente da ricordarsi di riabbassare.
 * ============================================================================= */

#include "libc.h"
#include "exuser.h"

/* =============================================================================
 * ambiente_di_root — l'ambiente che vede il figlio
 *
 * Si parte da quello di chi chiama e si riscrivono due voci: `USER` e `LOGNAME`
 * diventano root, e per la shell anche `HOME`.
 *
 * ! SENZA QUESTO IL FIGLIO E' root MA L'AMBIENTE DICE ANCORA IL NOME DI PRIMA,
 * e la differenza si vede: `id` deve avvisare che $USER e il kernel non vanno
 * d'accordo. Peggio, un programma che si fida di $HOME scriverebbe i file di
 * root dentro la casa dell'utente, di proprieta' di root — e da domani quello
 * non puo' piu' cancellarseli.
 *
 * ! `HOME` SI CAMBIA SOLO PER LA SHELL. `sudo un-comando` deve poter lavorare
 * sui file di chi lo chiama: e' quasi sempre il motivo per cui lo si chiama.
 * Spostargli la casa sotto i piedi renderebbe `sudo` inutile per il suo uso
 * normale. Con `-s` invece si e' chiesto proprio di ANDARE dall'altra parte.
 *
 * Rende NULL se non c'e' memoria: chi chiama tira dritto con l'ambiente suo,
 * che e' un difetto piccolo e non un motivo per non fare la cosa.
 * ============================================================================= */
static char **ambiente_di_root(int guscio)
{
    extern char **environ;
    char **fuori;
    int    n = 0, i, k = 0;

    while (environ && environ[n]) n++;

    /* +3: le voci che si aggiungono se non c'erano, +1 per il NULL finale. */
    fuori = (char **)malloc((unsigned)(n + 4) * sizeof(char *));
    if (fuori == 0) return 0;

    for (i = 0; i < n; i++) {
        const char *v = environ[i];
        if (strncmp(v, "USER=", 5) == 0)    continue;
        if (strncmp(v, "LOGNAME=", 8) == 0) continue;
        if (guscio && strncmp(v, "HOME=", 5) == 0) continue;
        fuori[k++] = (char *)v;
    }
    fuori[k++] = (char *)"USER=root";
    fuori[k++] = (char *)"LOGNAME=root";
    if (guscio) fuori[k++] = (char *)"HOME=/root";
    fuori[k] = 0;
    return fuori;
}

int main(int argc, char **argv)
{
    char         io_nome[EXUSER_NOME_MAX];
    char         pass[EXUSER_PASS_MAX];
    const char  *chi;
    char       **amb;
    char        *sv[3];
    int          pid, stato = -1, rc, i, guscio;

    if (argc < 2 || strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0) {
        printf("uso: sudo <comando> [argomenti]   esegue il comando come root\n");
        printf("     sudo -s                      apre una shell di root\n\n");
        printf("Chiede la TUA password se sei elencato in /boot/amministratori,\n");
        printf("altrimenti quella di root.  L'elenco lo scrive `install`, e si\n");
        printf("puo' modificare a mano: un nome per riga.\n\n");
        printf("I poteri durano quanto il comando.  Con -s durano finche' non\n");
        printf("esci dalla shell: e' una cosa diversa, e va chiesta.\n");
        return (argc < 2) ? 1 : 0;
    }

    guscio = (strcmp(argv[1], "-s") == 0);

    if (getuid() == 0) {
        /* Gia' root: non si chiede niente e si fa e basta. Rifiutarsi sarebbe
         * solo un ostacolo in uno script che gira dal recovery. */
        if (guscio) { printf("sudo: sei gia' root.\n"); return 0; }
        pid = spawn(argv[1], &argv[1]);
        if (pid < 0) { printf("sudo: %s: %s\n", argv[1], strerror(errno)); return 1; }
        waitpid(pid, &stato, 0);
        return stato;
    }

    /* ! CHI SONO IO: il numero non basta, al kernel serve il nome — e' con
     * quello che cerca dentro /boot/ombra. Se non si sa tradurre l'uid non si
     * puo' chiedere la propria password, e si ripiega su root. */
    chi = "root";
    if (nome_utente((unsigned int)getuid(), io_nome, sizeof(io_nome)) &&
        exuser_e_amministratore(0, io_nome))
        chi = io_nome;

    /* ! IL PRIMO PIANO SI PRENDE, o la password non arriva: il driver di
     * tastiera consegna i tasti a chi e' dichiarato in primo piano, e la shell
     * ce l'ha ceduto lanciandoci. Senza, il prompt resterebbe li' per sempre. */
    exuser_prendi_console();

    printf("password di %s: ", chi);
    if (exuser_leggi_password(pass, sizeof(pass)) < 0) {
        printf("\nsudo: non riesco a chiedere una password senza mostrarla.\n");
        printf("      Serve /dev/kbd.drv - vedi [modules] in kernel.cfg.\n");
        return 1;
    }

    rc = diventa_root(chi, pass);

    /* ! LA PASSWORD SI CANCELLA APPENA SERVITA. Non protegge da granche' — non
     * c'e' swap e la pagina e' di questo processo — ma costa tre righe e toglie
     * una copia in giro. */
    for (i = 0; i < (int)sizeof(pass); i++) pass[i] = '\0';

    if (rc != 0) {
        /* ! NON SI DICE QUALE DELLE DUE COSE E' ANDATA STORTA — password
         * sbagliata, oppure «non sei fra gli amministratori» — perche' la
         * differenza serve solo a chi sta provando. */
        printf("sudo: non se ne fa niente.\n");
        return 1;
    }

    amb = ambiente_di_root(guscio);

    if (guscio) {
        printf("\n");
        sv[0] = "/bin/sh";
        sv[1] = 0;
        pid = amb ? spawn_ex(sv[0], sv, amb, 0, 0) : spawn(sv[0], sv);
    } else {
        pid = amb ? spawn_ex(argv[1], &argv[1], amb, 0, 0)
                  : spawn(argv[1], &argv[1]);
    }

    if (pid < 0) {
        printf("sudo: %s: %s\n", guscio ? "/bin/sh" : argv[1],
               strerror(errno));
        return 1;
    }

    console_setfg((unsigned)pid);
    waitpid(pid, &stato, 0);
    console_setfg((unsigned)getpid());
    return stato;
}
