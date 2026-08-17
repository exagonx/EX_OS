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

/* Cerca il nome corrispondente a un uid in /boot/utenti, che ha la forma
 *
 *     nome:uid:gid
 *
 * ! E' IL FILE PUBBLICO, non quello delle password. Le impronte stanno in
 * /boot/ombra, che e' 0600: se i due non fossero separati, questo programma
 * non riuscirebbe a tradurre un uid in un nome per nessuno tranne root — e
 * infatti prima diceva «uid=1000(uid1000)» a un utente che si chiamava mario.
 *
 * Rende 1 se l'ha trovato. Il file puo' non esserci — su un sistema avviato da
 * floppy non c'e' affatto — e non e' un errore. */
static int nome_di_uid(unsigned int uid, char *out, unsigned int max)
{
    static char testo[4096];
    int fd, n, i = 0;

    fd = open("/boot/utenti", O_RDONLY, 0);
    if (fd < 0) return 0;
    n = (int)read(fd, testo, sizeof(testo) - 1);
    close(fd);
    if (n < 0) n = 0;
    testo[n] = '\0';

    while (i < n) {
        int p = i, primo = -1, secondo = -1;

        while (i < n && testo[i] != '\n') {
            if (testo[i] == ':') {
                if (primo < 0)        primo = i;
                else if (secondo < 0) secondo = i;
            }
            i++;
        }
        if (i < n) i++;
        if (primo < 0 || secondo < 0) continue;

        if ((unsigned int)atoi(testo + primo + 1) == uid) {
            unsigned int l = (unsigned int)(primo - p);
            if (l >= max) l = max - 1;
            memcpy(out, testo + p, l);
            out[l] = '\0';
            return 1;
        }
    }
    return 0;
}

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

    if (!nome_di_uid(uid, nome, sizeof(nome))) {
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
