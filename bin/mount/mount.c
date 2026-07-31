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

/* Gli stessi errno del kernel (kernel/include/syscall.h): la syscall
 * ritorna il valore negato. */
#define E_NOENT     2
#define E_IO        5
#define E_EXIST    17
#define E_INVAL    22
#define E_BUSY     16
#define E_MFILE    24
#define E_ROFS     30

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
            printf("%-24s %-12s FAT%-8u %s\n",
                   m[i].punto, m[i].dev, m[i].fs,
                   m[i].sola_lettura ? "sola lettura" : "lettura/scrittura");
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
            printf("montato %s su %s (%s)\n", argv[a], argv[a + 1],
                   flag ? "sola lettura" : "lettura/scrittura");
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
