/* =============================================================================
 * drivers/fetta/fetta.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * fetta.drv — una FETTA di un altro disco, offerta come disco a se'
 *
 *     /dev/fetta.drv usb0 2880 &        offre «fetta0»: usb0 dal settore 2880
 *     mount fetta0 /sistema
 *
 *     fetta.drv <disco> <primo settore> [-aspetta S] [-monta PUNTO] [-v]
 *
 * ! A CHE COSA SERVE (10 ottobre 2026). La chiavetta di avvio di EX-OS che
 * funziona su ogni BIOS e' un'immagine di DISCHETTO scritta in testa alla
 * chiavetta: il BIOS la avvia come un floppy, il caricatore la copia in RAM,
 * e li' dentro ci stanno 1,44 MB - il kernel, la shell, i driver USB. Il resto
 * del sistema non ci sta. Sta sulla STESSA chiavetta, dopo il dischetto, dal
 * settore 2880 in poi: un secondo volume FAT. Ma quella chiavetta non ha una
 * tabella delle partizioni (se l'avesse, il BIOS la prenderebbe per un disco
 * e non la avvierebbe piu' come dischetto), quindi nessuno sa che il secondo
 * volume c'e'. Questo programma lo dice: prende il disco intero che il driver
 * USB ha offerto, e ne offre a sua volta la parte da un certo settore in poi.
 *
 * E' un disco servito da un processo (blk_offri, come ramdisk.drv): ogni
 * richiesta la gira al disco vero, col primo settore sommato. Niente cache,
 * niente di suo: se il disco sotto sparisce, le richieste falliscono.
 *
 * «cerca» al posto del nome del disco: lo si trova da soli. Si guardano tutti i
 * dischi e si prende il primo che al settore dato ha un volume FAT con
 * l'etichetta EXOS64 (la mette tools/mkchiave.sh). Serve perche' il nome non
 * e' prevedibile: su una macchina con un lettore di schede la chiavetta e'
 * usb1, non usb0.
 *
 * -aspetta S   il disco puo' non esserci ancora (il driver USB ci mette
 *              qualche secondo a trovarlo): si riprova per S secondi.
 * -monta P     appena offerto, lo monta in P - cosi' uno script di avvio lo
 *              fa con una riga sola, senza dover aspettare a mano.
 * ============================================================================= */
#include "libc.h"

EX_VERSIONE("fetta.drv", "0.001");

#define SETTORE 512

static unsigned char g_buf[BLKR3_SETTORI_RICHIESTA * SETTORE];

/* Il disco che al settore `primo` ha il volume di EX-OS: il suo nome in
 * `nome`, e si rende quanti settori ha; 0 se non c'e' (ancora). */
static unsigned int cerca(unsigned int primo, char *nome)
{
    static BlkInfo       v[32];
    static unsigned char s0[SETTORE];
    int                  n = blkinfo(v, 32, 0), i;

    for (i = 0; i < n; i++) {
        if (v[i].tipo == 3 || v[i].guasto) continue;        /* le partizioni no */
        if (v[i].settori_lo <= primo) continue;
        if (blkread(v[i].nome, primo, 1, s0) < 0) continue;
        if (s0[510] != 0x55 || s0[511] != 0xAA) continue;
        if (memcmp(s0 + 0x2B, "EXOS64", 6) != 0) continue;  /* l'etichetta del FAT16 */
        strcpy(nome, v[i].nome);
        return v[i].settori_lo;
    }
    return 0;
}

/* Quanti settori ha un disco, 0 se non c'e'. */
static unsigned int settori_di(const char *nome)
{
    static BlkInfo v[32];
    int            n = blkinfo(v, 32, 0), i;

    for (i = 0; i < n; i++)
        if (strcmp(v[i].nome, nome) == 0) return v[i].settori_lo;
    return 0;
}

int main(int argc, char **argv)
{
    BlkOfferta   o;
    BlkRichiesta r;
    const char  *disco = NULL, *punto = NULL;
    unsigned int primo = 0, tutti = 0, aspetta = 0, verboso = 0;
    int          i, rc, ha_primo = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            printf("fetta.drv <disco> <primo settore> [-aspetta S] [-monta PUNTO] [-v]\n");
            printf("  offre fetta0: il disco dal settore dato in poi. In secondo piano, con &.\n");
            return 0;
        }
        if (strcmp(argv[i], "-i") == 0) { printf("fetta: non guido una periferica.\n"); return 1; }
        if (strcmp(argv[i], "-v") == 0) { verboso = 1; continue; }
        if (strcmp(argv[i], "-aspetta") == 0 && i + 1 < argc) { aspetta = (unsigned int)atoi(argv[++i]); continue; }
        if (strcmp(argv[i], "-monta") == 0 && i + 1 < argc)   { punto = argv[++i]; continue; }
        if (disco == NULL)   { disco = argv[i]; continue; }
        if (!ha_primo)       { primo = (unsigned int)atoi(argv[i]); ha_primo = 1; continue; }
    }
    if (disco == NULL || !ha_primo) {
        printf("fetta: uso  fetta.drv <disco> <primo settore>  (-h per il resto)\n");
        return 1;
    }

    for (;;) {
        if (strcmp(disco, "cerca") == 0) {
            static char trovato[BLKINFO_NOME_MAX];

            tutti = cerca(primo, trovato);
            if (tutti > primo) { disco = trovato; break; }
        } else {
            tutti = settori_di(disco);
            if (tutti > primo) break;
        }
        if (aspetta == 0) {
            if (strcmp(disco, "cerca") == 0)
                printf("fetta: nessun disco ha il volume di EX-OS al settore %u.\n", primo);
            else if (tutti == 0) printf("fetta: il disco '%s' non c'e'.\n", disco);
            else            printf("fetta: '%s' ha %u settori, la fetta comincerebbe a %u.\n", disco, tutti, primo);
            return 1;
        }
        sleep(1);
        aspetta--;
    }

    memset(&o, 0, sizeof(o));
    strcpy(o.nome, "fetta0");
    o.settori_lo   = tutti - primo;
    o.settori_hi   = 0;
    o.byte_settore = SETTORE;
    o.sola_lettura = 0;
    rc = blk_offri(&o);
    if (rc < 0) {
        printf("fetta: blk_offri ha risposto %d (mi chiamo *.drv e giro da root?)\n", rc);
        return 1;
    }
    printf("fetta: fetta0 offerto - %s dal settore %u, %u settori (%u MB)\n",
           disco, primo, tutti - primo, (tutti - primo) / 2048);

    /* Il montaggio lo fa un figlio: chi monta chiede settori a QUESTO
     * processo, che deve essere gia' qui sotto a rispondere. */
    if (punto != NULL) {
        char *av[4];

        /* niente mkdir: mount rifiuta un punto che esiste gia' */
        av[0] = "/bin/mount"; av[1] = "fetta0"; av[2] = (char *)punto; av[3] = NULL;
        if (spawn("/bin/mount", av) < 0)
            printf("fetta: non riesco a lanciare mount: fallo a mano,  mount fetta0 %s\n", punto);
    }

    r.dati     = g_buf;
    r.dati_max = sizeof(g_buf);
    for (;;) {
        rc = blk_attendi(&r, 0);
        if (rc == -EINTR) continue;
        if (rc < 0) { printf("fetta: blk_attendi ha risposto %d, esco\n", rc); break; }

        if (r.op == BLKR3_SVUOTA) { blk_risposta(&r, 0); continue; }
        if (r.lba_hi != 0 || r.lba_lo >= tutti - primo ||
            r.lba_lo + r.settori > tutti - primo || r.settori * SETTORE > sizeof(g_buf)) {
            blk_risposta(&r, -EIO);
            continue;
        }
        if (r.op == BLKR3_LEGGI)       rc = blkread(disco, primo + r.lba_lo, r.settori, g_buf);
        else if (r.op == BLKR3_SCRIVI) rc = blkwrite(disco, primo + r.lba_lo, r.settori, g_buf);
        else                           rc = -EINVAL;
        if (verboso) printf("fetta: op %u, %u settori da %u: %d\n", r.op, r.settori, r.lba_lo, rc);
        blk_risposta(&r, rc < 0 ? rc : 0);
    }
    return 0;
}
