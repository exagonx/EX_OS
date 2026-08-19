/* =============================================================================
 * bin/id/id.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Chi sto girando come
 *
 *     id            uid, gid e nome
 *     whoami        solo il nome (stesso binario, guarda argv[0])
 *
 * ! ESISTE PERCHE' SENZA NON C'ERA MODO DI GUARDARE L'uid. Il kernel lo tiene
 * nel PCB e lo eredita a ogni spawn, `login` ci scende con setuid(): tutto
 * corretto e completamente invisibile. Un meccanismo che non si puo' osservare
 * e' un meccanismo di cui si crede di sapere lo stato — ed e' esattamente
 * quello che rende difficile trovare i difetti dei permessi.
 *
 * ! IL NUMERO VIENE DAL KERNEL, IL NOME DALL'AMBIENTE, e la differenza conta:
 * `getuid()` non puo' mentire perche' e' una syscall; `$USER` lo puo' cambiare
 * chiunque con `set`. Se i due non coincidono lo si vede — ed e' proprio il
 * genere di cosa che si vuole vedere.
 * ============================================================================= */

#include "libc.h"

/* ! LA TRADUZIONE uid -> NOME E' PASSATA NELLA libc il 19 agosto, quando gli
 * utenti sono diventati due: la vuole anche `ls -l`. Stava qui, ed era giusto
 * finche' a chiederla c'era solo questo programma — due copie di un parser di
 * un formato di file divergono al primo campo aggiunto. Vedi nome_utente() in
 * lib/include/libc.h. */

int main(int argc, char **argv)
{
    unsigned int uid = (unsigned int)getuid();
    unsigned int gid = (unsigned int)getgid();
    const char  *amb = getenv("USER");
    char         nome[64];
    int          solo_nome = 0;
    const char  *base;

    /* whoami e id sono lo stesso programma con due nomi, come su Unix. Si
     * guarda la parte finale di argv[0]: chi lo lancia con un percorso intero
     * si aspetta comunque il comportamento del nome che ha battuto. */
    base = argv[0] ? argv[0] : "id";
    {
        const char *s;
        for (s = base; *s; s++) if (*s == '/') base = s + 1;
    }
    if (strcmp(base, "whoami") == 0) solo_nome = 1;
    if (argc >= 2 && strcmp(argv[1], "-n") == 0) solo_nome = 1;

    if (!nome_utente(uid, nome, sizeof(nome))) {
        /* Nessun elenco utenti: e' il caso normale avviando da floppy o da CD,
         * dove si e' root perche' i proprietari non esistono. */
        if (uid == 0) strcpy(nome, "root");
        else          sprintf(nome, "uid%u", uid);
    }

    if (solo_nome) {
        printf("%s\n", nome);
        return 0;
    }

    printf("uid=%u(%s) gid=%u\n", uid, nome, gid);

    /* ! SI DICE ANCHE COSA NE PENSA L'AMBIENTE, e solo quando NON coincide.
     * Stamparlo sempre sarebbe rumore; stamparlo quando diverge e' l'unico
     * momento in cui qualcuno deve saperlo — vuol dire che $USER e' stato
     * cambiato a mano, e che qualunque programma che ci si fida sta guardando
     * un utente che non e' quello vero. */
    if (amb && amb[0] && strcmp(amb, nome) != 0)
        printf("  ! $USER dice '%s', il kernel dice '%s'\n", amb, nome);

    if (uid == 0)
        printf("  root: i controlli sui permessi non si applicano\n");

    return 0;
}
