/* =============================================================================
 * bin/disk/disk.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Mostra i dischi rigidi e le loro partizioni.
 *
 * SOLA LETTURA. Non modifica nulla, e in questa fase e' voluto: prima si
 * dimostra di leggere e criticare correttamente una tabella esistente,
 * poi si acquisisce il diritto di riscriverne una. Un errore in lettura
 * fa vedere numeri sbagliati; lo stesso errore in scrittura rende
 * irraggiungibili i dati di un disco.
 *
 * I dati NON passano dal BIOS: vengono da IDENTIFY DEVICE e READ NATIVE
 * MAX ADDRESS mandati direttamente al disco (kernel/block/ata.c).
 * ============================================================================= */
#include "libc.h"

/* Un settore e' 512 byte: 2048 settori = 1 MB, 2097152 = 1 GB.
 * La divisione si fa sui settori e non sui byte perche' i byte di un
 * disco da 64 GB non entrano in 32 bit. */
#define SETT_PER_MB     2048u

static const char *nome_tipo(unsigned int t)
{
    switch (t) {
        case 0x01: return "FAT12";
        case 0x04: return "FAT16 <32M";
        case 0x05: return "estesa CHS";
        case 0x06: return "FAT16";
        case 0x07: return "NTFS/exFAT";
        case 0x0B: return "FAT32 CHS";
        case 0x0C: return "FAT32 LBA";
        case 0x0E: return "FAT16 LBA";
        case 0x0F: return "estesa LBA";
        case 0x82: return "swap Linux";
        case 0x83: return "Linux";
        case 0x85: return "estesa Linux";
        case 0xEE: return "GPT protett.";
        case 0xEF: return "EFI (FAT)";
        default:   return "sconosciuto";
    }
}

/* Somma i due mezzi di un valore a 64 bit e lo esprime in MB.
 * Il calcolo resta a 32 bit: settori/2048 di un disco da 2 TB sono
 * 1048576 MB, che ci stanno; farlo in byte no. */
static unsigned int in_mb(unsigned int lo, unsigned int hi)
{
    /* hi conta blocchi da 2^32 settori = 2^32/2048 MB = 2097152 MB ognuno.
     * Oltre i 2 TB il valore satura, e per un disco di quell'epoca non e'
     * un caso da gestire diversamente. */
    return (lo / SETT_PER_MB) + (hi * 2097152u);
}

static const char *nome_fs(unsigned int t)
{
    switch (t) {
        case 2:   return "ext2";
        case 9:   return "ISO 9660";
        case 12:  return "FAT12";
        case 16:  return "FAT16";
        case 32:  return "FAT32";
        case 255: return "illeggib.";
        default:  return "-";
    }
}

static void stampa_problemi(unsigned int pb)
{
    if (pb == 0) {
        printf("  Anomalie      : nessuna\n");
        return;
    }

    printf("  Anomalie      :\n");
    if (pb & PT_PROB_FIRMA)
        printf("    - firma 0x55AA assente: il settore 0 NON e' un MBR\n");
    if (pb & PT_PROB_GPT)
        printf("    - MBR protettivo: il disco e' GPT, non toccare la tabella MBR\n");
    if (pb & PT_PROB_OLTRE_FINE)
        printf("    - una partizione finisce OLTRE l'ultimo settore del disco\n");
    if (pb & PT_PROB_SOVRAPP)
        printf("    - due partizioni SI SOVRAPPONGONO: scrivere in una corrompe l'altra\n");
    if (pb & PT_PROB_CATENA)
        printf("    - catena delle partizioni logiche rotta o CIRCOLARE\n");
    if (pb & PT_PROB_BOOTFLAG)
        printf("    - flag di avvio diverso da 0x00/0x80: tabella danneggiata\n");
    if (pb & PT_PROB_VUOTA)
        printf("    - voce con tipo valido ma dimensione zero\n");
    if (pb & PT_PROB_TROPPE_EXT)
        printf("    - piu' di una partizione estesa fra le primarie (fuori specifica)\n");
    if (pb & PT_PROB_TRONCATA)
        printf("    - piu' partizioni di quante ne possa mostrare\n");
    if (pb & PT_PROB_SETTORE0)
        printf("    - una partizione comincia dall'LBA 0: contiene l'MBR stesso,\n"
               "      e il primo filesystem che ci scrive cancella la tabella\n");
}

/* `idx` e' lo SLOT ATA (0-3) ed e' anche il numero di un DISCO: il livello
 * a blocchi chiama hd2 il disco nello slot 2. I lettori no — sono
 * numerati a parte (cd0, cd1) nell'ordine in cui compaiono — e chiamare
 * "hd2" un CD manderebbe a cercare un disco rigido che non esiste, con un
 * nome che per giunta non si puo' montare. */
static void mostra(unsigned int idx, unsigned int n_cd, const DiskInfo *d)
{
    unsigned int i;

    printf("\n");
    if (d->tipo == 2) printf("cd%u  (slot ATA %u)", n_cd, idx);
    else              printf("hd%u", idx);
    printf("  canale %s, %s\n",
           d->canale ? "secondario" : "primario",
           d->unita  ? "slave" : "master");

    if (d->tipo == 2) {
        printf("  Lettore ottico ATAPI (CD/DVD)\n");
        if (d->modello[0]) printf("  Modello       : %s\n", d->modello);
        if (d->firmware[0]) printf("  Firmware      : %s\n", d->firmware);
        /* La capacita' NON si stampa qui, e non e' una dimenticanza: un
         * lettore non ne ha una: ce l'ha il disco che c'e' dentro adesso.
         * La si vede nella riga di cd0 fra i dispositivi a blocchi, dopo
         * che qualcuno ha sondato il supporto (per esempio montandolo). */
        printf("  Si monta come dispositivo a blocchi 'cd%u' (ISO 9660,\n", n_cd);
        printf("  sola lettura): mount cd%u /cdrom\n", n_cd);
        return;
    }
    if (d->tipo != 1) {
        printf("  Dispositivo non riconosciuto.\n");
        return;
    }

    printf("  Modello       : %s\n", d->modello);
    printf("  Seriale       : %s\n", d->seriale);
    printf("  Firmware      : %s\n", d->firmware);
    printf("  Indirizzamento: %s%s\n",
           d->lba48 ? "LBA48" : "LBA28",
           d->hpa   ? ", supporta HPA" : "");
    printf("  Capacita'     : %u settori (%u MB)\n",
           d->settori_lo, in_mb(d->settori_lo, d->settori_hi));

    /* IL PUNTO CHIAVE della richiesta: se il disco e' limitato, dirlo
     * chiaramente e quantificare lo spazio nascosto. */
    if (d->clippato) {
        unsigned int nasc = in_mb(d->nativi_lo, d->nativi_hi)
                          - in_mb(d->settori_lo, d->settori_hi);
        printf("  Di fabbrica   : %u settori (%u MB)\n",
               d->nativi_lo, in_mb(d->nativi_lo, d->nativi_hi));
        printf("  >>> SPAZIO NASCOSTO: %u MB non accessibili <<<\n", nasc);
        printf("      Causa tipica: Host Protected Area attiva, oppure un\n");
        printf("      jumper di limitazione sul disco (i modelli dell'epoca\n");
        printf("      ne avevano uno per farsi accettare dai BIOS con la\n");
        printf("      barriera dei 32 GB). Rimuoverlo e' una modifica\n");
        printf("      PERSISTENTE al disco e non viene fatta in automatico.\n");
    } else if (d->nativi_lo != 0 || d->nativi_hi != 0) {
        printf("  Di fabbrica   : coincide, nessuno spazio nascosto\n");
    }

    switch (d->schema) {
        case PT_SCHEMA_MBR: printf("  Schema        : MBR\n");            break;
        case PT_SCHEMA_GPT: printf("  Schema        : GPT (MBR protettivo)\n"); break;
        default:            printf("  Schema        : nessuno riconosciuto\n"); break;
    }

    stampa_problemi(d->problemi);

    if (d->n_part == 0) {
        printf("  Nessuna partizione.\n");
        return;
    }

    printf("\n");
    printf("  %-4s%-14s%-4s%11s%9s  %-10s%-12s\n",
           "n.", "tipo MBR", "avv", "inizio", "MB", "filesys.", "etichetta");
    printf("  --------------------------------------------------------------------\n");

    for (i = 0; i < d->n_part; i++) {
        const PartInfo *p = &d->part[i];

        printf("  %-2u%s%-14s%-4s%11u%9u  %-10s%-12s\n",
               p->numero, p->logica ? "L " : "  ",
               nome_tipo(p->tipo),
               (p->attiva == 0x80) ? "si" : "",
               p->inizio_lo,
               in_mb(p->settori_lo, p->settori_hi),
               nome_fs(p->fs_tipo),
               p->fs_etichetta);

        /* Il dettaglio si stampa solo dove c'e' davvero un filesystem:
         * su un contenitore esteso o su una partizione non formattata
         * sarebbero zeri senza significato. */
        if (p->fs_tipo == 12 || p->fs_tipo == 16 || p->fs_tipo == 32) {
            printf("        %u cluster da %u settori%s\n",
                   p->fs_n_cluster, p->fs_sett_per_clu,
                   p->fs_incoerente ? "  << CAMPI INCOERENTI col tipo dedotto >>" : "");
        } else if (p->fs_tipo == 2) {
            /* Su ext2 il conto e' di BLOCCHI, non di cluster: sono la
             * stessa idea con un altro nome, ma stampare "cluster" su un
             * ext2 farebbe cercare all'utente un campo che li' non esiste. */
            printf("        %u blocchi da %u settori%s\n",
                   p->fs_n_cluster, p->fs_sett_per_clu,
                   p->fs_incoerente ? "  << dimensione di blocco non gestita >>" : "");
        }
    }
    printf("  'L' = partizione logica. Il numero e' quello di fdisk: 1-4 sono\n");
    printf("  gli SLOT delle primarie, le logiche partono da 5. Il dispositivo\n");
    printf("  a blocchi si chiama hd<disco>p<numero>.\n");
    printf("  Il filesystem e' dedotto dal NUMERO DI CLUSTER (regola FAT), non\n");
    printf("  dal byte di tipo MBR, che e' solo un suggerimento e puo' mentire.\n");
}

int main(int argc, char **argv)
{
    DiskInfo     d;
    unsigned int i;
    unsigned int n_cd = 0;
    int          trovati = 0;

    (void)argc; (void)argv;

    for (i = 0; i < 4; i++) {
        if (diskinfo(i, &d) < 0) {
            printf("disk: lettura dello slot %u fallita\n", i);
            printf("disk: se l'errore e' -22, DiskInfo non coincide fra kernel\n");
            printf("      e libc: ricompilare tutto.\n");
            return 1;
        }
        if (!d.presente) continue;

        /* I lettori si contano a parte, nello stesso ordine in cui li
         * numera il livello a blocchi: primo canale prima, master prima
         * dello slave. */
        if (d.tipo != 2) trovati++;
        mostra(i, n_cd, &d);
        if (d.tipo == 2) n_cd++;
    }

    /* --- Dispositivi a blocchi ---------------------------------------
     * E' la vista che conta per montare: nomi stabili e, soprattutto, la
     * FINESTRA di ciascuno. Un filesystem montato su una partizione non
     * puo' uscire da quei due numeri, e vederli scritti serve proprio a
     * poterlo verificare. */
    {
        BlkInfo      b[8];
        unsigned int start = 0;
        int          n, k;

        printf("\n");
        printf("Dispositivi a blocchi\n");
        printf("  %-8s%-12s%12s%12s%10s\n",
               "nome", "tipo", "primo LBA", "settori", "MB");
        printf("  ----------------------------------------------------------\n");

        for (;;) {
            n = blkinfo(b, 8, start);
            if (n <= 0) break;

            for (k = 0; k < n; k++) {
                const char *t = (b[k].tipo == 1) ? "floppy"
                              : (b[k].tipo == 2) ? "disco"
                              : (b[k].tipo == 3) ? "partizione"
                              : (b[k].tipo == 4) ? "CD/DVD" : "?";
                printf("  %-8s%-12s%12u%12u%10u%s\n",
                       b[k].nome, t, b[k].primo_lo, b[k].settori_lo,
                       in_mb(b[k].settori_lo, b[k].settori_hi),
                       b[k].sola_lettura ? "  (sola lettura)" : "");
            }
            start += (unsigned int)n;
            if (n < 8) break;
        }
        printf("  Un cd0 con zero settori non e' un lettore rotto: e' un lettore\n");
        printf("  che nessuno ha ancora sondato, o con il vassoio vuoto. La sua\n");
        printf("  lunghezza appartiene al disco inserito, non al dispositivo.\n");
        printf("  Ogni accesso e' relativo al dispositivo e viene RIFIUTATO se\n");
        printf("  esce dalla sua finestra: e' cio' che impedisce a un filesystem\n");
        printf("  di toccare un'altra partizione o la tabella delle partizioni.\n");
    }

    if (trovati == 0) {
        printf("\ndisk: nessun disco rigido rilevato sui canali IDE.\n");
        printf("Il sistema si e' avviato da floppy: e' normale se non ci sono\n");
        printf("dischi collegati.\n");
    }

    printf("\n");
    printf("Sola lettura: questo programma non modifica nulla.\n");
    printf("I dati vengono dal disco (IDENTIFY DEVICE), non dal BIOS.\n");
    printf("\n");
    return 0;
}
