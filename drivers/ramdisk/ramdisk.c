/* =============================================================================
 * drivers/ramdisk/ramdisk.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * UN DISCO DI MEMORIA, SERVITO DA RING 3 — la prova che la cucitura regge
 *
 *     /dev/ramdisk.drv &          offre ram0 da 1 MB e resta li' a servirlo
 *     /dev/ramdisk.drv 4096 &     4 MB
 *     /dev/ramdisk.drv -v &       dice ogni richiesta che serve
 *
 * ! PERCHE' ESISTE, VISTO CHE UN DISCO IN RAM NON SERVE A NESSUNO
 *
 * Serve a provare blk_offri/blk_attendi/blk_risposta — la strada che permette
 * a un dispositivo a blocchi di essere servito da un processo invece che dal
 * kernel (il progetto sta in kernel/include/blkr3.h). Quella strada nasce per
 * le chiavette USB, ma un driver USB e' tre cose insieme: un controller, una
 * classe e questa cucitura. Provarle tutte e tre in una volta vuol dire, al
 * primo guasto, non sapere quale delle tre.
 *
 * Qui non c'e' nessun hardware: se `mkfs ram0`, `mount` e `cp` funzionano su
 * questo, la cucitura e' a posto e tutto il resto e' driver. E' la stessa
 * ragione per cui esiste vgaprova.drv.
 *
 * ! ED E' UTILE ANCHE DOPO. Un disco in RAM e' un posto scrivibile su una
 * macchina avviata da CD, dove la radice e' di sola lettura: `mkfs ram0`,
 * `mount ram0 /tmp`, e i file temporanei hanno dove stare fino allo
 * spegnimento.
 *
 * -----------------------------------------------------------------------------
 * ! IL GIRO E' TUTTO QUI, e vale identico per una chiavetta vera
 *
 *     blk_offri()      «esisto, mi chiamo ram0, sono grande cosi'»
 *     blk_attendi()    dorme finche' qualcuno non legge o scrive
 *     blk_risposta()   «fatto», e il kernel sveglia chi aspettava
 *
 * Il driver USB fara' le stesse tre chiamate: al posto della memcpy ci sara'
 * un comando SCSI dentro un trasferimento bulk.
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `ramdisk.drv -version` la stampa. Vedi
 * EX_VERSIONE in libc.h. */
EX_VERSIONE("ramdisk.drv", "0.001");

#define KB_PRED       1024          /* 1 MB: ci sta una FAT16 con dentro qualcosa */
#define KB_MAX        16384         /* 16 MB: oltre, e' la RAM della macchina */
#define SETTORE       512

/* Il buffer di una richiesta. ! NON PUO' ESSERE PIU' PICCOLO di
 * BLKR3_SETTORI_RICHIESTA settori: il kernel spezza le richieste piu' lunghe
 * di cosi', ma non quelle, e blk_attendi() risponderebbe -EINVAL su ogni
 * scrittura — cioe' un disco che si monta e non si scrive. */
static unsigned char g_buf[BLKR3_SETTORI_RICHIESTA * SETTORE];

static unsigned char *g_disco = 0;
static unsigned int   g_settori = 0;
static int            g_verboso = 0;

/* -vita <s>: dopo tanti secondi il driver esce. ! NON E' UNA STRAVAGANZA, e'
 * l'unico modo di provare A COMANDO il caso che conta davvero — il servente
 * che sparisce mentre qualcuno lo tiene montato, cioe' la chiavetta sfilata
 * senza smontarla. Senza, quel pezzo di kernel (blk_ritira e il risveglio con
 * -EIO in blkr3.c) resterebbe codice mai eseguito. */
static unsigned int   g_vita = 0;

static void aiuto(void)
{
    printf("ramdisk.drv - un disco a blocchi tenuto in memoria, servito da\n");
    printf("              questo processo. Provalo cosi':\n\n");
    printf("  /dev/ramdisk.drv &      offre ram0 da %d KB\n", KB_PRED);
    printf("  mkfs -t fat16 ram0\n");
    printf("  mount ram0 /mnt\n");
    printf("  ...\n");
    printf("  umount /mnt\n\n");
    printf("  ramdisk.drv [KB] [-v]   KB fino a %d, -v dice ogni richiesta\n",
           KB_MAX);
    printf("  ramdisk.drv -vita <s>   esce dopo <s> secondi: serve a provare\n");
    printf("                          cosa succede a chi lo teneva montato\n");
    printf("\n! SI LANCIA IN SECONDO PIANO con '&': finche' gira, il disco\n");
    printf("  esiste; quando muore, sparisce - e chi lo teneva montato se lo\n");
    printf("  ritrova guasto, che e' il caso che questa prova deve mostrare.\n");
}

int main(int argc, char **argv)
{
    BlkOfferta   o;
    BlkRichiesta r;
    unsigned int kb = KB_PRED;
    int          i, rc;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0) {
            /* ! `hwconfig -d` sonda ogni *.drv con -i, e chi risponde 0 si
             * ritrova installato ovunque. Questo non guida nessuna periferica:
             * e' una prova, e su un sistema installato non ci deve stare.
             * Stessa risposta di vgaprova.drv, per la stessa ragione. */
            printf("ramdisk: non guido nessuna periferica, sono una prova:\n");
            printf("         non installarmi.\n");
            return 1;
        }
        if (strcmp(argv[i], "-h") == 0) { aiuto(); return 0; }
        if (strcmp(argv[i], "-v") == 0) { g_verboso = 1; continue; }
        if (strcmp(argv[i], "-vita") == 0 && i + 1 < argc) {
            g_vita = (unsigned int)atoi(argv[++i]);
            continue;
        }
        kb = (unsigned int)atoi(argv[i]);
    }

    if (kb == 0 || kb > KB_MAX) {
        printf("ramdisk: %u KB non vanno bene (da 1 a %d)\n", kb, KB_MAX);
        return 1;
    }

    g_settori = kb * 2;                 /* 1 KB = due settori da 512 */
    g_disco   = (unsigned char *)malloc(kb * 1024);
    if (g_disco == NULL) {
        printf("ramdisk: non riesco a prendere %u KB di memoria\n", kb);
        return 1;
    }
    memset(g_disco, 0, kb * 1024);

    memset(&o, 0, sizeof(o));
    strcpy(o.nome, "ram0");
    o.settori_lo   = g_settori;
    o.settori_hi   = 0;
    o.byte_settore = SETTORE;
    o.sola_lettura = 0;

    rc = blk_offri(&o);
    if (rc < 0) {
        printf("ramdisk: blk_offri ha risposto %d\n", rc);
        printf("         mi chiamo *.drv e giro da root? il varco e' quello.\n");
        return 1;
    }

    printf("ramdisk: ram0 offerto, %u KB (%u settori). Adesso:\n", kb, g_settori);
    printf("         mkfs -t fat16 ram0   e poi   mount ram0 /mnt\n");

    r.dati     = g_buf;
    r.dati_max = sizeof(g_buf);

    if (g_vita > 0)
        printf("ramdisk: fra %u secondi me ne vado, montato o no.\n", g_vita);

    {
        unsigned int fine = (unsigned int)time(NULL) + g_vita;

    for (;;) {
        unsigned int off, len;

        /* Senza scadenza si dorme finche' non arriva qualcosa; con -vita si
         * guarda l'orologio ogni mezzo secondo. */
        rc = blk_attendi(&r, g_vita ? 500 : 0);

        if (rc == -ETIMEDOUT) {
            if (g_vita > 0 && (unsigned int)time(NULL) >= fine) {
                printf("ramdisk: tempo scaduto, esco. Chi mi teneva montato\n");
                printf("         adesso trova ram0 guasto: deve smontarlo.\n");
                break;
            }
            continue;
        }
        if (rc == -EINTR) continue;
        if (rc < 0) {
            printf("ramdisk: blk_attendi ha risposto %d, esco\n", rc);
            break;
        }

        /* ! IL CONTROLLO SI RIFA' QUI, anche se il kernel l'ha gia' fatto.
         * La finestra di blk.c protegge il DISCO da chi legge; questo
         * protegge la memoria di QUESTO processo da un kernel che un giorno
         * potrebbe chiedere un settore in piu' — e costa due confronti su
         * un'operazione che ne fa migliaia. */
        off = r.lba_lo * SETTORE;
        len = r.settori * SETTORE;

        if (r.op != BLKR3_SVUOTA &&
            (r.lba_hi != 0 || r.lba_lo >= g_settori ||
             r.lba_lo + r.settori > g_settori || len > sizeof(g_buf))) {
            printf("ramdisk: richiesta fuori dal disco (lba %u, %u settori)\n",
                   r.lba_lo, r.settori);
            blk_risposta(&r, -EIO);
            continue;
        }

        switch (r.op) {
        case BLKR3_LEGGI:
            memcpy(g_buf, g_disco + off, len);
            if (g_verboso) printf("ramdisk: letti %u settori da %u\n",
                                  r.settori, r.lba_lo);
            blk_risposta(&r, 0);
            break;

        case BLKR3_SCRIVI:
            memcpy(g_disco + off, g_buf, len);
            if (g_verboso) printf("ramdisk: scritti %u settori da %u\n",
                                  r.settori, r.lba_lo);
            blk_risposta(&r, 0);
            break;

        case BLKR3_SVUOTA:
            /* Non c'e' niente in sospeso: la memoria E' il disco. */
            blk_risposta(&r, 0);
            break;

        default:
            blk_risposta(&r, -EINVAL);
            break;
        }
    }
    }

    free(g_disco);
    return 0;
}
