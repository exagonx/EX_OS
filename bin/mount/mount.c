/* =============================================================================
 * bin/mount/mount.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Elenca, monta e smonta i filesystem.
 *
 *   mount                    elenca i montaggi attivi
 *   mount <dispositivo> <punto>   monta
 *   umount <punto>           smonta   (stesso binario, vedi sotto)
 *
 * PERCHE' UN SOLO BINARIO. Il nome con cui il programma e' stato invocato
 * arriva in argv[0]: chiamarlo `umount` smonta, chiamarlo `mount` monta.
 * Su un floppy da 1.44 MB ogni programma costa ~12 KB, e due binari che
 * condividono tutto tranne tre righe non li valgono.
 *
 * I punti di montaggio sono VIRTUALI: /disk non deve esistere sul floppy,
 * e compare nell'elenco della root perche' lo aggiunge il VFS. Montare su
 * un nome che esiste gia' viene rifiutato — su Unix quel caso nasconde
 * dei file senza dirlo.
 * ============================================================================= */
#include "libc.h"

/* +0.001 a ogni modifica: `mount -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("mount", "0.001");

/* Gli stessi errno del kernel (kernel/include/syscall.h): la syscall
 * ritorna il valore negato. */
#define E_NOENT     2
#define E_IO        5
#define E_EXIST    17
#define E_INVAL    22
#define E_BUSY     16
#define E_MFILE    24
#define E_ROFS     30
#define E_NOMEDIUM 123  /* il lettore c'e', il disco dentro no */

static const char *spiega(int err)
{
    switch (-err) {
        case E_NOENT: return "dispositivo o punto inesistente";
        case E_IO:    return "errore di lettura dal dispositivo";
        case E_EXIST: return "il punto di montaggio esiste gia'";
        case E_INVAL: return "punto non valido o filesystem non riconosciuto";
        case E_BUSY:  return "punto o dispositivo gia' in uso, oppure file aperti";
        case E_MFILE: return "nessuno slot di montaggio libero";
        case E_ROFS:  return "filesystem in sola lettura";
        case E_NOMEDIUM: return "nessun disco nel lettore";
        default:      return "errore";
    }
}

static void elenca(void)
{
    MountInfo m[4];
    unsigned int start = 0;
    int n;

    printf("Punto                    Dispositivo  Filesystem  Accesso\n");
    printf("------------------------ ------------ ----------- -------------\n");

    /* A pagine: la syscall ne restituisce al massimo 4 per chiamata, e
     * fermarsi alla prima pagina nasconderebbe i montaggi successivi. */
    while ((n = mountinfo(m, 4, start)) > 0) {
        int i;
        for (i = 0; i < n; i++) {
            /* Il campo `fs` non e' piu' solo la larghezza di una FAT: 2 e'
             * ext2 e 9 e' ISO 9660. Stampare "FAT2" o "FAT9" sarebbe un
             * filesystem che non esiste. */
            if (m[i].fs == 2 || m[i].fs == 9) {
                printf("%-24s %-12s %-11s %s\n",
                       m[i].punto, m[i].dev,
                       (m[i].fs == 2) ? "ext2" : "ISO 9660",
                       m[i].sola_lettura ? "sola lettura" : "lettura/scrittura");
            } else {
                printf("%-24s %-12s FAT%-8u %s\n",
                       m[i].punto, m[i].dev, m[i].fs,
                       m[i].sola_lettura ? "sola lettura" : "lettura/scrittura");
            }
        }
        start += (unsigned int)n;
        if (n < 4) break;
    }

    if (n < 0) printf("mount: impossibile leggere i montaggi (errore %d)\n", n);
}

static int e_umount(const char *nome)
{
    const char *b = nome;
    const char *p;

    if (b == NULL) return 0;

    /* Solo l'ultima componente: il programma puo' essere stato invocato
     * come /bin/umount. */
    for (p = b; *p; p++) if (*p == '/') b = p + 1;

    return strcmp(b, "umount") == 0;
}

int main(int argc, char **argv)
{
    int r;

    if (e_umount(argc > 0 ? argv[0] : NULL)) {
        if (argc != 2) {
            printf("uso: umount <punto>\n");
            return 1;
        }
        r = umount(argv[1]);
        if (r != 0) {
            printf("umount: %s: %s (errore %d)\n", argv[1], spiega(r), r);
            return 1;
        }
        printf("smontato %s\n", argv[1]);
        return 0;
    }

    if (argc == 1) {
        elenca();
        return 0;
    }

    /* -r davanti agli argomenti: monta senza permettere modifiche. Utile
     * per guardare dentro un disco che non si vuole rischiare di toccare. */
    {
        unsigned int flag = 0;
        int a = 1;

        if (strcmp(argv[1], "-r") == 0) { flag = MNT_SOLA_LETTURA; a = 2; }

        if (argc - a == 2) {
            r = mount(argv[a], argv[a + 1], flag);
            if (r != 0) {
                printf("mount: %s su %s: %s (errore %d)\n",
                       argv[a], argv[a + 1], spiega(r), r);
                return 1;
            }
            /* Cosa e' successo DAVVERO, non cosa si era chiesto. Il
             * kernel puo' imporre la sola lettura da solo — lo fa su
             * ext2, che il driver non sa scrivere — e riferire il flag
             * richiesto direbbe "lettura/scrittura" su un volume dove la
             * prima scrittura fallira'. */
            {
                MountInfo    m[4];
                unsigned int start = 0;
                int          n, i, detto = 0;

                while (!detto && (n = mountinfo(m, 4, start)) > 0) {
                    for (i = 0; i < n; i++) {
                        if (strcmp(m[i].punto, argv[a + 1]) != 0) continue;
                        printf("montato %s su %s (%s)\n", argv[a], argv[a + 1],
                               m[i].sola_lettura ? "sola lettura"
                                                 : "lettura/scrittura");
                        detto = 1;
                        break;
                    }
                    start += (unsigned int)n;
                    if (n < 4) break;
                }

                /* Il montaggio e' riuscito: se non lo si e' ritrovato
                 * nell'elenco e' un problema di mountinfo, non del
                 * montaggio, e non va fatto sembrare un errore. */
                if (!detto)
                    printf("montato %s su %s\n", argv[a], argv[a + 1]);
            }
            return 0;
        }
    }

    if (argc != 3) {
        printf("uso: mount                        elenca i montaggi\n");
        printf("     mount [-r] <dispositivo> <punto>  monta (-r sola lettura)\n");
        printf("     umount <punto>               smonta\n");
        printf("\nI nomi dei dispositivi si vedono con `disk`: fd0, hd0p1, ...\n");
        return 1;
    }

    return 1;
}
