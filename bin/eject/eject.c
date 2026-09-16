/* =============================================================================
 * bin/eject/eject.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * eject — togliere una chiavetta invece di strapparla
 *
 *     eject                  dice che cosa si puo' togliere
 *     eject /USB/DRIVE0      smonta e ritira
 *     eject usb0             lo stesso, chiamandola per nome
 *
 * -----------------------------------------------------------------------------
 * ! PERCHE' SERVE UN COMANDO, E NON BASTA `umount`
 *
 * Il driver che serve una chiavetta sta fermo dentro blk_attendi() e da quel
 * momento NON GUARDA PIU' LE PORTE: e' scritto nero su bianco in cima ad
 * automount.c, «una chiavetta sfilata di brutto non sparisce». Quindi:
 *
 *   - `umount /USB/DRIVE0` libera il punto di montaggio, ma il dispositivo
 *     resta li' offerto da un driver che lo crede ancora infilato;
 *   - sfilare la chiavetta a quel punto non lo fa sparire: ogni accesso rende
 *     errore, e automount non ha niente da smontare perche' e' gia' smontato;
 *   - reinfilarla non produce NIENTE, perche' il driver non sta guardando.
 *
 * Questo comando chiude il giro: smonta tutto quel che sta sopra il
 * dispositivo, poi lo RITIRA (blk_espelli). L'attesa del driver rende -ENODEV,
 * lui torna al suo ciclo delle porte — che «vuota per tre secondi» e «riempita»
 * le sa gia' distinguere — e la stessa chiavetta, tolta e rimessa, viene
 * ripresa da capo.
 *
 * -----------------------------------------------------------------------------
 * ! L'ORDINE E' SMONTA-POI-RITIRA, E NON E' INVERTIBILE
 *
 * Un dispositivo ritirato mentre e' ancora montato non sparisce: resta nello
 * strato a blocchi marcato GUASTO, e ogni accesso rende errore finche' non lo
 * si smonta (vedi `guasto` in kernel/include/blk.h, e la ragione per cui lo
 * slot non si libera: il prossimo dispositivo lo riuserebbe SOTTO un montaggio
 * aperto). Il kernel infatti rifiuta con -EBUSY, e fa bene: meglio un errore
 * che qualcuno sa spiegare di un dispositivo zombie.
 * ============================================================================= */
#include "libc.h"

/* +0.001 a ogni modifica: `eject -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("eject", "0.001");

#define MAX_PUNTI   8       /* quanti montaggi puo' avere un dispositivo */

/* Gli stessi errno del kernel: la syscall rende il valore negato. */
#define E_PERM      1
#define E_NOENT     2
#define E_BUSY     16
#define E_INVAL    22
#define E_NODEV    19

static void uso(void)
{
    printf("eject - toglie una chiavetta USB come si deve\n\n");
    printf("  eject                  che cosa si puo' togliere\n");
    printf("  eject /USB/DRIVE0      smonta e ritira, poi la si sfila\n");
    printf("  eject usb0             lo stesso, per nome di dispositivo\n\n");
    printf("Smonta prima e ritira dopo: il driver torna a guardare le porte,\n");
    printf("quindi la stessa chiavetta rimessa viene ripresa da automount.\n");
    printf("Va dato da root, come mount.\n");
}

/* =============================================================================
 * IL NOME DEL SUPPORTO, non quello della finestra
 *
 * Una partizione si chiama «usb0p1»: il supporto sotto e' «usb0». Chi scrive
 * `eject /USB/HDD0p1` intende togliere la chiavetta, non smettere di vedere la
 * sua prima partizione — una finestra senza il supporto sotto non e' niente.
 *
 * ! IL TAGLIO E' SU «p» SEGUITA DA SOLE CIFRE, e non sulla prima «p» che
 * capita: un dispositivo che un domani si chiamasse «sdp0» perderebbe meta'
 * nome. Si guarda dalla fine, che e' dove il numero sta.
 * ========================================================================== */
static void supporto_di(const char *dev, char *out, unsigned int max)
{
    unsigned int n = (unsigned int)strlen(dev), i, p;

    strncpy(out, dev, max - 1);
    out[max - 1] = '\0';

    if (n < 3) return;

    i = n;
    while (i > 0 && out[i - 1] >= '0' && out[i - 1] <= '9') i--;
    if (i == n) return;                 /* non finisce per cifre */
    if (i == 0 || out[i - 1] != 'p') return;

    p = i - 1;
    if (p == 0) return;                 /* «p1» non e' la finestra di niente */
    out[p] = '\0';
}

/* Il dispositivo montato su `punto`, o "" se li' non c'e' niente. */
static void dispositivo_del_punto(const char *punto, char *out, unsigned int max)
{
    MountInfo    m[4];
    unsigned int start = 0;
    int          n, i;

    out[0] = '\0';

    while ((n = mountinfo(m, 4, start)) > 0) {
        for (i = 0; i < n; i++)
            if (strcmp(m[i].punto, punto) == 0) {
                strncpy(out, m[i].dev, max - 1);
                out[max - 1] = '\0';
                return;
            }
        start += (unsigned int)n;
        if (n < 4) break;
    }
}

/* Rende 1 se `dev` e' il supporto `base` o una sua finestra (base + "p" + n). */
static int e_del_supporto(const char *dev, const char *base)
{
    unsigned int l = (unsigned int)strlen(base);

    if (strcmp(dev, base) == 0) return 1;
    return strncmp(dev, base, l) == 0 && dev[l] == 'p';
}

/* =============================================================================
 * Smonta tutto quello che sta sopra un supporto.
 *
 * ! I PUNTI SI RACCOLGONO PRIMA E SI SMONTANO DOPO. Smontare mentre si scorre
 * l'elenco vuol dire cambiare sotto i piedi la cosa che si sta scorrendo: la
 * syscall rende pagine da quattro, e togliere un montaggio in mezzo fa scalare
 * quelli dopo — il secondo punto di una chiavetta con due partizioni si
 * salterebbe in silenzio.
 *
 * Rende 0 se e' andato tutto bene, -1 se qualcosa non si e' smontato.
 * ========================================================================== */
static int smonta_sopra(const char *base, int *quanti)
{
    char         punti[MAX_PUNTI][MOUNTINFO_PUNTO_MAX];
    MountInfo    m[4];
    unsigned int start = 0;
    int          n, i, np = 0, guai = 0;

    *quanti = 0;

    while ((n = mountinfo(m, 4, start)) > 0) {
        for (i = 0; i < n && np < MAX_PUNTI; i++) {
            if (!e_del_supporto(m[i].dev, base)) continue;
            strncpy(punti[np], m[i].punto, MOUNTINFO_PUNTO_MAX - 1);
            punti[np][MOUNTINFO_PUNTO_MAX - 1] = '\0';
            np++;
        }
        start += (unsigned int)n;
        if (n < 4) break;
    }

    for (i = 0; i < np; i++) {
        int rc = umount(punti[i]);

        if (rc == 0) {
            printf("  smontato %s\n", punti[i]);
            (*quanti)++;
        } else {
            printf("  %s non si smonta (%d)", punti[i], rc);
            if (rc == -E_BUSY)
                printf(": qualcuno ci sta dentro - un file aperto, o una\n"
                       "  shell che ha li' la directory corrente");
            printf("\n");
            guai = 1;
        }
    }

    return guai ? -1 : 0;
}

/* Che cosa si puo' togliere: i dispositivi serviti da un processo. */
static void elenca(void)
{
    BlkInfo      b[8];
    MountInfo    m[4];
    unsigned int start = 0;
    int          n, i, trovati = 0;

    while ((n = blkinfo(b, 8, start)) > 0) {
        for (i = 0; i < n; i++) {
            /* tipo 5 = servito da un processo: vedi BlkInfo in libc.h. Solo
             * quelli si espellono — un disco ATA non ha un driver da
             * rimandare a guardare le porte. */
            if (b[i].tipo != 5) continue;

            if (!trovati) {
                printf("Si possono togliere:\n\n");
                printf("Dispositivo  Settori     Montato su\n");
                printf("------------ ----------- ------------------------\n");
                trovati = 1;
            }

            printf("%-12s %-11u ", b[i].nome, b[i].settori_lo);

            {
                unsigned int s2 = 0;
                int          k, n2, primo = 1;

                while ((n2 = mountinfo(m, 4, s2)) > 0) {
                    for (k = 0; k < n2; k++) {
                        if (!e_del_supporto(m[k].dev, b[i].nome)) continue;
                        printf("%s%s", primo ? "" : ", ", m[k].punto);
                        primo = 0;
                    }
                    s2 += (unsigned int)n2;
                    if (n2 < 4) break;
                }
                if (primo) printf("(non montato)");
            }
            printf("%s\n", b[i].guasto ? "  GUASTO" : "");
        }
        start += (unsigned int)n;
        if (n < 8) break;
    }

    if (!trovati) {
        printf("Non c'e' niente da togliere: nessuna chiavetta servita da un\n");
        printf("driver. `blkscan` e `mount` dicono che cosa c'e'.\n");
        return;
    }

    printf("\n  eject <punto>   oppure   eject <dispositivo>\n");
}

int main(int argc, char **argv)
{
    char base[BLKINFO_NOME_MAX];
    char dev[BLKINFO_NOME_MAX];
    int  rc, smontati = 0, i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            uso();
            return 0;
        }
    }

    if (argc < 2) { elenca(); return 0; }

    /* Un percorso e' un punto di montaggio, il resto e' un nome di
     * dispositivo: il taglio e' netto, e non c'e' da indovinare. */
    if (argv[1][0] == '/') {
        dispositivo_del_punto(argv[1], dev, sizeof(dev));
        if (dev[0] == '\0') {
            printf("eject: su %s non c'e' niente montato.\n", argv[1]);
            printf("       `mount` senza argomenti dice che cosa c'e'.\n");
            return 1;
        }
    } else {
        strncpy(dev, argv[1], sizeof(dev) - 1);
        dev[sizeof(dev) - 1] = '\0';
    }

    supporto_di(dev, base, sizeof(base));

    if (smonta_sopra(base, &smontati) != 0) {
        printf("eject: %s resta com'e': prima si smonta, poi si toglie.\n", base);
        return 1;
    }

    rc = blk_espelli(base);

    if (rc == 0) {
        printf("eject: %s ritirato", base);
        if (smontati > 0) printf(" (%d montaggi chiusi)", smontati);
        printf(".\n");
        printf("       Adesso la chiavetta si puo' sfilare. Rimettendola,\n");
        printf("       il driver se ne accorge e automount la rimonta.\n");
        return 0;
    }

    switch (rc) {
    case -E_BUSY:
        printf("eject: %s e' ancora in uso: qualcosa lo tiene.\n", base);
        break;
    case -E_INVAL:
        printf("eject: %s non e' servito da un driver, e non si espelle.\n", base);
        printf("       Si tolgono cosi' le chiavette USB; un disco o un\n");
        printf("       floppy si smontano e basta (`umount`).\n");
        break;
    case -E_NOENT:
    case -E_NODEV:
        printf("eject: non conosco nessun dispositivo '%s'.\n", base);
        printf("       `eject` senza argomenti dice che cosa si puo' togliere.\n");
        break;
    case -E_PERM:
        printf("eject: solo root puo' togliere un dispositivo.\n");
        break;
    default:
        printf("eject: %s non si ritira (errore %d).\n", base, rc);
        break;
    }

    /* ! SI DICE CHE I MONTAGGI SONO GIA' CHIUSI, e non e' un dettaglio: chi
     * legge l'errore deve sapere che meta' del lavoro e' stata fatta, o
     * ritentera' senza capire perche' «non e' montato» invece dell'errore di
     * prima. */
    if (smontati > 0)
        printf("       (i %d montaggi sono gia' stati chiusi)\n", smontati);

    return 1;
}
