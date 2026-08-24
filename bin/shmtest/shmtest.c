/* =============================================================================
 * bin/shmtest/shmtest.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Prova della memoria condivisa fra processi.
 *
 *     shmtest         il padre: crea la zona, lancia il figlio, verifica
 *     shmtest -f      il figlio: si attacca, verifica, risponde e MUORE
 *
 * ! IL FIGLIO NON CHIUDE LA ZONA, ed e' apposta: uscire senza chiudere e' il
 * caso che prova la pulizia alla morte del processo. Se quel pezzo non
 * funzionasse, il conteggio dei processi resterebbe alto e l'ultimo passo del
 * padre — riaprire il nome e ottenere -ENOENT — fallirebbe.
 *
 * ! I NUMERI SONO NOTI IN ANTICIPO. La zona si riempie con una funzione dei
 * soli indici, quindi ogni byte ha un valore atteso che non dipende
 * dall'esecuzione: «sembra giusto» qui non e' possibile.
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `shmtest -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("shmtest", "0.001");

#define NOME        "provashm"
#define BYTE        8192            /* due pagine: si prova anche il confine */
#define PASSO_PADRE 0x5A
#define PASSO_FIGLIO 0xA5

static int errori = 0;

static void esito(const char *cosa, int ok)
{
    printf("  %-46s %s\n", cosa, ok ? "ok" : "FALLITA");
    if (!ok) errori++;
}

/* Il byte atteso all'indice i, per ciascuno dei due scrittori. Dipende solo
 * da i: nessun valore casuale, nessun contatore. */
static unsigned char atteso_padre(unsigned int i)
{
    return (unsigned char)((i * 7u + PASSO_PADRE) & 0xFF);
}

static unsigned char atteso_figlio(unsigned int i)
{
    return (unsigned char)((i * 13u + PASSO_FIGLIO) & 0xFF);
}

/* -----------------------------------------------------------------------------
 * Il figlio
 * --------------------------------------------------------------------------- */
static int figlio(void)
{
    ShmZona z;
    unsigned char *p;
    unsigned int i;
    int rc;

    strcpy(z.nome, NOME);
    z.byte = 1;             /* apposta SBAGLIATO: chi si attacca non decide */
    z.flag = 0;             /* niente SHM_CREA: deve esserci gia' */
    z.virt = 0;

    rc = shm_apri(&z);
    if (rc != 0) {
        printf("figlio: shm_apri('%s') fallita (%d)\n", NOME, rc);
        return 1;
    }

    /* La dimensione ricevuta e' quella VERA, non quella chiesta. */
    if (z.byte != BYTE) {
        printf("figlio: dimensione %u, attesa %u\n", z.byte, (unsigned)BYTE);
        return 1;
    }

    p = (unsigned char *)z.virt;

    for (i = 0; i < BYTE; i++)
        if (p[i] != atteso_padre(i)) {
            printf("figlio: byte %u vale 0x%02x, atteso 0x%02x\n",
                   i, p[i], atteso_padre(i));
            return 1;
        }

    /* Risponde riscrivendo tutto con la propria funzione. */
    for (i = 0; i < BYTE; i++) p[i] = atteso_figlio(i);

    printf("figlio: %u byte letti e riscritti, esco SENZA chiudere\n",
           (unsigned)BYTE);
    return 0;
}

/* -----------------------------------------------------------------------------
 * Il padre
 * --------------------------------------------------------------------------- */
static int padre(const char *io)
{
    ShmZona z, z2;
    unsigned char *p;
    unsigned int i;
    int rc, pid, stato;
    MemInfo m0, m1;
    char *argv[3];

    printf("Memoria condivisa fra processi\n\n");

    meminfo(&m0);

    /* --- 1. una zona che non c'e', senza SHM_CREA ------------------------- */
    strcpy(z.nome, "nonesiste");
    z.byte = 4096; z.flag = 0; z.virt = 0;
    esito("aprire una zona inesistente da' -ENOENT",
          shm_apri(&z) == -ENOENT);

    /* --- 2. creazione ----------------------------------------------------- */
    strcpy(z.nome, NOME);
    z.byte = BYTE; z.flag = SHM_CREA; z.virt = 0;
    rc = shm_apri(&z);
    esito("creare la zona", rc == 0 && z.virt != 0 && z.byte == BYTE);
    if (rc != 0) return 1;

    p = (unsigned char *)z.virt;

    /* --- 3. la stessa zona due volte nello stesso processo ---------------- */
    strcpy(z2.nome, NOME);
    z2.byte = BYTE; z2.flag = SHM_CREA; z2.virt = 0;
    esito("riaprirla nello stesso processo da' -EEXIST",
          shm_apri(&z2) == -EEXIST);

    /* --- 4. si riempie con valori noti ------------------------------------ */
    for (i = 0; i < BYTE; i++) p[i] = atteso_padre(i);

    /* --- 5. il figlio ----------------------------------------------------- */
    argv[0] = (char *)io;
    argv[1] = "-f";
    argv[2] = 0;

    pid = spawn(io, argv);
    if (pid < 0) {
        printf("  spawn('%s') fallita (%d)\n", io, pid);
        return 1;
    }
    console_setfg((unsigned int)pid);
    waitpid(pid, &stato, 0);
    console_setfg((unsigned int)getpid());

    esito("il figlio ha letto e verificato quello che avevo scritto",
          stato == 0);

    /* --- 6. cio' che il figlio ha scritto si vede da qui ------------------ */
    {
        int ok = 1;
        for (i = 0; i < BYTE; i++)
            if (p[i] != atteso_figlio(i)) { ok = 0; break; }
        esito("vedo le modifiche del figlio (stessa memoria fisica)", ok);
        if (!ok)
            printf("      primo byte diverso: %u vale 0x%02x, atteso 0x%02x\n",
                   i, p[i], atteso_figlio(i));
    }

    /* --- 7. la zona e' sopravvissuta alla MORTE del figlio ----------------- */
    /* Se il conteggio dei riferimenti nel PMM non funzionasse, le pagine
     * sarebbero tornate al sistema quando il figlio e' morto — e questa
     * lettura leggerebbe la memoria di qualcun altro. */
    {
        int ok = 1;
        for (i = 0; i < BYTE; i++)
            if (p[i] != atteso_figlio(i)) { ok = 0; break; }
        esito("la zona e' viva dopo la morte del figlio", ok);
    }

    /* --- 8. chiusura, e il nome deve morire con l'ultimo utente ------------ */
    esito("chiuderla rende 0", shm_chiudi(p) == 0);
    esito("chiuderla due volte da' -EINVAL", shm_chiudi(p) == -EINVAL);

    strcpy(z2.nome, NOME);
    z2.byte = 0; z2.flag = 0; z2.virt = 0;
    esito("il nome e' tornato libero (ora da' -ENOENT)",
          shm_apri(&z2) == -ENOENT);

    /* --- 9. e la memoria fisica e' tornata al sistema ---------------------- */
    meminfo(&m1);
    esito("la memoria libera e' quella di partenza",
          m1.free_kb == m0.free_kb);
    if (m1.free_kb != m0.free_kb)
        printf("      prima %u KB, dopo %u KB\n", m0.free_kb, m1.free_kb);

    printf("\n");
    if (errori == 0) printf("esito   tutto a posto\n");
    else             printf("esito   %d prove FALLITE\n", errori);

    return errori == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    /* ! SERVE UN PERCORSO, NON IL NOME DEL COMANDO. La ricerca nel PATH la fa
     * la shell, non spawn(): rilanciarsi con l'argv[0] che si e' ricevuto
     * funziona solo se chi ha lanciato ha scritto il percorso per intero. */
    const char *io = (argv[0] != 0 && argv[0][0] == '/') ? argv[0]
                                                         : "/bin/shmtest";

    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'f') return figlio();
    return padre(io);
}
