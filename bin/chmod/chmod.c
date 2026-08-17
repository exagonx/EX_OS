/* =============================================================================
 * bin/chmod/chmod.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * chmod e chown — governare i permessi dalla riga di comando
 *
 *     chmod 755 file [file...]      cambia i permessi
 *     chown mario file [file...]    cambia il proprietario (solo root)
 *     chown 1000 file              ... anche per numero
 *
 * ! UN BINARIO CON DUE NOMI, come id/whoami: guarda argv[0]. Le due funzioni
 * condividono la lettura di /boot/utenti e il ciclo sui file; separarle
 * vorrebbe dire due copie che divergono al primo cambiamento.
 *
 * ! ESISTONO PERCHE' SENZA NON C'ERA MODO DI SISTEMARE UN PERMESSO A MANO.
 * chmod e chown erano chiamate di libreria e basta: un file con i bit
 * sbagliati — e su un disco installato prima del 17 agosto 2026 sono tutti i
 * programmi — non si poteva rimettere a posto senza scrivere un programma
 * apposta. Un sistema di permessi senza gli attrezzi per governarli e' un
 * sistema in cui il primo errore e' definitivo.
 * ============================================================================= */

#include "libc.h"

/* Legge /boot/utenti — nome:uid:gid, il file PUBBLICO — e traduce un nome in
 * un numero. Rende -1 se il nome non c'e'. */
static int uid_di_nome(const char *nome)
{
    static char testo[4096];
    int fd, n, i = 0;

    fd = open("/boot/utenti", O_RDONLY, 0);
    if (fd < 0) return -1;
    n = (int)read(fd, testo, sizeof(testo) - 1);
    close(fd);
    if (n < 0) n = 0;
    testo[n] = '\0';

    while (i < n) {
        int p = i, due = -1;

        while (i < n && testo[i] != '\n') {
            if (testo[i] == ':' && due < 0) due = i;
            i++;
        }
        if (i < n) i++;
        if (due < 0) continue;

        testo[due] = '\0';
        if (strcmp(testo + p, nome) == 0) return atoi(testo + due + 1);
        testo[due] = ':';
    }
    return -1;
}

/* Un numero in base otto. Rende -1 se non lo e'.
 *
 * ! BASE OTTO E NON DIECI, ed e' la convenzione di ogni Unix: i permessi sono
 * tre gruppi da tre bit, e in ottale ogni cifra e' esattamente un gruppo.
 * `755` letto in decimale sarebbe 0o1363, cioe' bit sparsi a caso. */
static int ottale(const char *s)
{
    int v = 0;

    if (!s || !*s) return -1;
    for (; *s; s++) {
        if (*s < '0' || *s > '7') return -1;
        v = v * 8 + (*s - '0');
    }
    return v;
}

static void uso(const char *chi)
{
    printf("uso: %s ", chi);
    if (strcmp(chi, "chown") == 0) {
        printf("<utente|uid> <file> [file...]\n\n");
        printf("Cambia il proprietario. SOLO root: se un utente potesse\n");
        printf("regalare i propri file potrebbe anche prendersi quelli\n");
        printf("che trova.\n");
    } else {
        printf("<permessi in ottale> <file> [file...]\n\n");
        printf("  755   il padrone tutto, gli altri leggono ed eseguono\n");
        printf("  644   il padrone legge e scrive, gli altri leggono\n");
        printf("  600   solo il padrone\n\n");
        printf("Lo puo' fare il PROPRIETARIO, non solo root: cambiare i\n");
        printf("propri permessi non toglie niente a nessuno.\n");
    }
    printf("\nSu FAT e ISO 9660 i permessi non esistono: chmod non fa\n");
    printf("niente e chown lo dice invece di fingere.\n");
}

int main(int argc, char **argv)
{
    const char *chi = argv[0] ? argv[0] : "chmod";
    int  e_chown, i, errori = 0, valore;

    {
        const char *s;
        for (s = chi; *s; s++) if (*s == '/') chi = s + 1;
    }
    e_chown = (strcmp(chi, "chown") == 0);

    if (argc < 3) { uso(chi); return 1; }

    if (e_chown) {
        /* Prima si prova come nome, poi come numero: un utente che si chiama
         * «1000» e' improbabile, ma un uid battuto a mano e' normale quando il
         * file degli utenti non c'e' — su un volume appena montato. */
        valore = uid_di_nome(argv[1]);
        if (valore < 0) {
            const char *s = argv[1];
            int tutto_cifre = (*s != '\0');

            for (; *s; s++) if (*s < '0' || *s > '9') tutto_cifre = 0;
            if (!tutto_cifre) {
                printf("chown: utente sconosciuto: %s\n", argv[1]);
                return 1;
            }
            valore = atoi(argv[1]);
        }
    } else {
        valore = ottale(argv[1]);
        if (valore < 0) {
            printf("chmod: '%s' non e' un numero in base otto.\n", argv[1]);
            printf("       I permessi si scrivono cosi': 755, 644, 600.\n");
            return 1;
        }
    }

    for (i = 2; i < argc; i++) {
        int r = e_chown ? chown(argv[i], (unsigned int)valore, (unsigned int)valore)
                        : chmod(argv[i], (unsigned int)valore);

        if (r != 0) {
            /* ! IL MOTIVO SI DICE PER ESTESO. «operazione non riuscita» su un
             * permesso lascia chi legge a indovinare fra «non sono io il
             * padrone», «il file non c'e'» e «questo volume non ha i
             * permessi»: sono tre rimedi diversi. */
            printf("%s: %s: %s\n", chi, argv[i], strerror(errno));
            if (errno == ENOSYS)
                printf("      (questo volume non ha i proprietari: e' FAT o un CD)\n");
            errori++;
        }
    }

    return errori ? 1 : 0;
}
