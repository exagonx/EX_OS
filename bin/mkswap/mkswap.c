/* =============================================================================
 * bin/mkswap/mkswap.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * mkswap — prepara una partizione per la memoria virtuale
 *
 *     mkswap hd0p2                 la prepara (chiede conferma)
 *     mkswap -f hd0p2              senza chiedere
 *     mkswap -n "scambio" hd0p2    con un'etichetta
 *
 * ! STA IN USERSPACE PER LA STESSA RAGIONE DI mkfs, ed e' la stessa domanda
 * con la stessa risposta: scrive solo DENTRO una partizione, cioe' dentro una
 * finestra che il livello a blocchi controlla gia'. Non c'e' niente da
 * proteggere che blk_write() non protegga.
 *
 * ! E SCRIVE UNA COSA SOLA: L'INTESTAZIONE. Gli slot non si azzerano — sono
 * milioni di settori e azzerarli costerebbe minuti per nascondere dati che
 * verranno comunque riscritti prima di essere letti (uno slot si legge solo
 * dopo che qualcuno ce l'ha scritto sopra). Chi ci tiene passa `-z`.
 *
 * ! IL KERNEL NON CERCA LE AREE DI SWAP DA SOLO. Dopo mkswap va scritta la
 * riga in /boot/kernel.cfg:
 *
 *     [kernel]
 *     swap = hd0p2
 *
 * ed e' voluto: un kernel che andasse a caccia di partizioni utilizzabili
 * prima o poi ne troverebbe una che non era per lui.
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `mkswap -version` la stampa. Vedi EX_VERSIONE. */
EX_VERSIONE("mkswap", "0.001");

/* ! DUPLICATA A MANO da kernel/include/swap.h, come ogni struttura che passa
 * fra kernel e spazio utente in questo sistema. Devono restare identiche: il
 * kernel legge questi byte e si fida solo della firma. */
#define SWAP_FIRMA          "EXOSSWAP"
#define SWAP_FIRMA_LEN      8
#define SWAP_VERSIONE       1
#define SWAP_PAGINA         4096
#define SWAP_SETTORI_TESTA  8
#define SWAP_SETTORI_SLOT   (SWAP_PAGINA / 512)
#define SWAP_SLOT_MAX       (1u << 20)

typedef struct {
    char         firma[SWAP_FIRMA_LEN];
    unsigned int versione;
    unsigned int pagina;
    unsigned int slot;
    unsigned int primo;
    char         etichetta[16];
} SwapTesta;

static int trova(const char *nome, BlkInfo *out)
{
    BlkInfo v[16];
    int n, i;

    n = blkinfo(v, 16, 0);
    if (n < 0) return -1;

    for (i = 0; i < n; i++)
        if (strcmp(v[i].nome, nome) == 0) { *out = v[i]; return 0; }

    return -1;
}

static void uso(void)
{
    printf("uso: mkswap [-f] [-z] [-n ETICHETTA] DISPOSITIVO\n");
    printf("  prepara una partizione per la memoria virtuale.\n\n");
    printf("  -f  non chiede conferma\n");
    printf("  -z  azzera anche gli slot (lento, e non serve: vedi il sorgente)\n");
    printf("  -n  etichetta, fino a 15 caratteri\n\n");
    printf("  dopo, in /boot/kernel.cfg:   [kernel]\n");
    printf("                               swap = DISPOSITIVO\n");
}

int main(int argc, char **argv)
{
    const char   *dev = 0, *etichetta = "";
    int           i, forza = 0, azzera = 0;
    BlkInfo       b;
    SwapTesta     t;
    unsigned char settore[512];
    unsigned int  settori, slot;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0)      forza = 1;
        else if (strcmp(argv[i], "-z") == 0) azzera = 1;
        else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) etichetta = argv[++i];
        else if (strcmp(argv[i], "-h") == 0) { uso(); return 0; }
        else if (argv[i][0] == '-')          { uso(); return 1; }
        else dev = argv[i];
    }

    if (!dev) { uso(); return 1; }

    if (trova(dev, &b) != 0) {
        printf("mkswap: '%s' non esiste. L'elenco lo da'  disk\n", dev);
        return 1;
    }

    /* ! SOLO UNA PARTIZIONE, MAI UN DISCO INTERO. Preparare hd0 vorrebbe dire
     * scrivere l'intestazione sopra la tabella delle partizioni: il disco
     * diventerebbe non avviabile e i suoi filesystem irraggiungibili, e
     * l'errore sarebbe una lettera di differenza dal comando giusto. */
    if (b.tipo != 3) {
        printf("mkswap: '%s' non e' una partizione.\n", dev);
        printf("        L'area di scambio va su una partizione (hd0p2), non\n");
        printf("        su un disco intero: li' ci sta la tabella.\n");
        return 1;
    }

    if (b.sola_lettura) {
        printf("mkswap: '%s' e' in sola lettura.\n", dev);
        return 1;
    }

    settori = b.settori_lo;
    if (b.settori_hi != 0 || settori <= SWAP_SETTORI_TESTA) {
        printf("mkswap: '%s' e' di %u settori: troppo %s\n", dev, settori,
               b.settori_hi ? "grande per questa versione" : "piccola");
        return 1;
    }

    slot = (settori - SWAP_SETTORI_TESTA) / SWAP_SETTORI_SLOT;
    if (slot == 0) {
        printf("mkswap: '%s' non contiene nemmeno una pagina.\n", dev);
        return 1;
    }
    if (slot > SWAP_SLOT_MAX) slot = SWAP_SLOT_MAX;

    printf("mkswap: %s — %u settori, %u pagine da %u KB (%u MB di scambio)\n",
           dev, settori, slot, SWAP_PAGINA / 1024,
           (unsigned)((unsigned long long)slot * SWAP_PAGINA / (1024 * 1024)));

    /* ! SI CHIEDE, e non e' burocrazia: se la partizione avesse dentro un
     * filesystem, questa intestazione lo rende irriconoscibile. Chi sa cosa
     * sta facendo ha `-f`; chi ha sbagliato numero di partizione ha una
     * domanda che glielo fa notare. */
    if (!forza) {
        char risposta[8];

        printf("        cancella tutto cio' che c'e' su %s. Sicuro? [s/N] ", dev);
        if (fgets(risposta, sizeof(risposta), stdin) == 0 ||
            (risposta[0] != 's' && risposta[0] != 'S')) {
            printf("mkswap: lasciato com'era.\n");
            return 1;
        }
    }

    if (azzera) {
        unsigned int s;

        memset(settore, 0, sizeof(settore));
        for (s = SWAP_SETTORI_TESTA; s < settori; s++)
            if (blkwrite(dev, s, 1, settore) < 0) {
                printf("mkswap: scrittura fallita al settore %u\n", s);
                return 1;
            }
    }

    memset(&t, 0, sizeof(t));
    memcpy(t.firma, SWAP_FIRMA, SWAP_FIRMA_LEN);
    t.versione = SWAP_VERSIONE;
    t.pagina   = SWAP_PAGINA;
    t.slot     = slot;
    t.primo    = SWAP_SETTORI_TESTA;
    strncpy(t.etichetta, etichetta, sizeof(t.etichetta) - 1);

    /* ! L'INTESTAZIONE VA IN UN SETTORE INTERO, azzerato prima: scrivere i
     * soli byte della struttura lascerebbe nel resto del settore quello che
     * c'era — e quello che c'era puo' essere il superblocco di un filesystem,
     * che qualche strumento riconoscerebbe ancora. */
    memset(settore, 0, sizeof(settore));
    memcpy(settore, &t, sizeof(t));

    /* ! LA FIRMA PER ULTIMA E' UNA CURA CHE QUI NON SERVE: il settore e' uno
     * solo, quindi o si scrive tutto o niente. Vale la pena saperlo per il
     * giorno che l'intestazione ne occupera' due. */
    if (blkwrite(dev, 0, 1, settore) < 0) {
        printf("mkswap: non riesco a scrivere su %s\n", dev);
        return 1;
    }

    printf("mkswap: %s pronta.\n", dev);
    printf("        Adesso in /boot/kernel.cfg, sezione [kernel]:\n");
    printf("            swap = %s\n", dev);
    printf("        e al prossimo avvio la memoria virtuale e' accesa.\n");
    return 0;
}
