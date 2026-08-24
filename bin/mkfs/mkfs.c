/* =============================================================================
 * bin/mkfs/mkfs.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Crea un filesystem dentro una partizione.
 *
 *   mkfs -t fat32 -L DATI hd0p1
 *   mkfs -t fat16 hd0p2
 *   mkfs -t ext2  hd0p3
 *
 * Qui c'e' il contorno — argomenti, ricognizione del dispositivo,
 * conferma, spiegazione degli errori — e il ramo FAT. Il ramo ext2 sta in
 * bin/mkfs/ext2.c, che e' un formato senza niente in comune con questo e
 * merita un file suo.
 *
 * -----------------------------------------------------------------------
 * PERCHE' STA IN USERSPACE E bootinst.c NO
 *
 * Sono la stessa domanda con risposte opposte, e la differenza e' dove si
 * scrive. L'installatore dell'avvio scrive nel settore 0, FUORI da ogni
 * filesystem, dove un errore rende irraggiungibile un disco intero: quella
 * logica sta nel kernel perche' non esiste un controllo in userspace che
 * un programma non possa semplicemente non usare.
 *
 * Un formattatore scrive solo DENTRO una partizione — cioe' dentro una
 * finestra che kernel/block/blk.c fa gia' rispettare su ogni singolo
 * accesso. Non c'e' niente da proteggere che blk_write() non protegga, e
 * mettere 500 righe di generazione di tabelle nel kernel per una garanzia
 * che il kernel gia' offre sarebbe pagare due volte.
 *
 * Le syscall SYS_BLKREAD/SYS_BLKWRITE accettano SOLO nomi di partizione:
 * il settore 0 non appartiene a nessuna partizione, quindi non esiste una
 * coppia (nome, lba) che lo raggiunga. La tabella delle partizioni resta
 * irraggiungibile da qui, e non per una regola ma per costruzione.
 *
 * -----------------------------------------------------------------------
 * IL PUNTO IN CUI E' PIU' FACILE SBAGLIARE: IL NUMERO DI CLUSTER
 *
 * Il tipo di un volume FAT — 12, 16 o 32 — NON e' scritto da nessuna
 * parte. La stringa "FAT16   " nel settore di avvio e' decorativa, e
 * kernel/block/vol.c giustamente non la guarda: il tipo si DEDUCE dal
 * numero di cluster dell'area dati, che e' l'unico criterio corretto.
 *
 *     cluster < 4085     FAT12
 *     cluster < 65525    FAT16
 *     altrimenti         FAT32
 *
 * Ne discende che un formattatore che sceglie male i settori per cluster
 * produce un volume che DICE FAT16 e CADE nella banda FAT12. Il nostro
 * driver lo leggerebbe in un modo, Linux in un altro, e nessuno dei due
 * segnalerebbe niente finche' i dati non sono gia' rovinati.
 *
 * Per questo la geometria viene calcolata, poi il numero di cluster viene
 * RIVERIFICATO contro le soglie prima di scrivere un solo byte, e mostrato
 * all'utente: e' il numero da cui dipende tutto il resto.
 *
 * -----------------------------------------------------------------------
 * L'ORDINE DI SCRITTURA PROTEGGE DA UN'INTERRUZIONE
 *
 * Il settore di avvio del volume vecchio viene azzerato PER PRIMO e quello
 * nuovo scritto PER ULTIMO. In mezzo il volume non e' riconoscibile da
 * nessuno.
 *
 * L'ordine opposto — tabelle prima, settore di avvio dopo — sembra
 * equivalente e non lo e': una formattazione interrotta a meta' lascerebbe
 * un settore di avvio che descrive il filesystem VECCHIO sopra delle
 * tabelle FAT gia' azzerate. Il volume verrebbe montato, sembrerebbe
 * funzionante e restituirebbe file vuoti. Meglio un volume che nessuno
 * riconosce che uno che mente.
 * ============================================================================= */
#include "libc.h"
#include "ext2.h"

/* =============================================================================
 * Identità del programma
 *
 * ▲ INCREMENTARE MKFS_VERSION DI 0.001 A OGNI MODIFICA ▲
 * Stessa regola del kernel e di /bin/fdisk.
 * ============================================================================= */
#define MKFS_NAME       "mkfs"
#define MKFS_VERSION    "0.001"


/* ! LA VERSIONE STA IN UN POSTO SOLO, ed e' MKFS_VERSION qui sopra:
 * `mkfs -version` la stampa dall'avvio della libc (vedi EX_VERSIONE in
 * libc.h) e il banner la stampa da dentro. Due letterali uguali
 * diventano due letterali diversi al primo incremento. */
EX_VERSIONE("mkfs", MKFS_VERSION);
#define SETT_BYTE       512u
#define SETT_PER_MB     2048u

/* 2 GB in settori da 512 byte. E' il tetto di FAT16 con i cluster piu'
 * grandi che scegli() e' disposto a usare (64 settori = 32 KB x 65524
 * cluster), non un numero tondo scelto per comodita'. */
#define LIMITE_FAT16_SETTORI  4194304u

/* Le soglie che DEFINISCONO il tipo di un volume FAT. Non sono una
 * convenzione di questo programma: sono la regola, e kernel/block/vol.c
 * usa esattamente le stesse per dedurre il tipo di cio' che trova. Se qui
 * e li' divergessero, EX-OS formatterebbe volumi che poi non riconosce. */
#define CLUSTER_MIN_FAT16   4085u
#define CLUSTER_MIN_FAT32   65525u
#define CLUSTER_MAX_FAT16   65524u
#define CLUSTER_MAX_FAT32   0x0FFFFFF5u

#define N_FAT               2u
#define ROOT_ENTRIES_16     512u
#define RISERVATI_16        1u
#define RISERVATI_32        32u
#define BK_BOOT_SETT        6u      /* copia del settore di avvio, su FAT32 */

/* Un settore di lavoro e un blocco di zeri grande quanto una chiamata.
 * Stanno in BSS e non sullo stack: il loader ELF mappa p_memsz e li azzera
 * al caricamento, quindi non costano niente sul supporto e `zeri` e' gia'
 * pronto senza doverlo riempire. */
static unsigned char sett[SETT_BYTE];
static unsigned char zeri[BLKIO_MAX_SETT * SETT_BYTE];

/* 1 appena una scrittura e' andata a buon fine.
 *
 * Serve a non mentire nel messaggio d'errore. Se a fallire e' la PRIMA
 * scrittura — il caso normale quando la partizione e' montata, perche' il
 * kernel rifiuta subito — sul volume non e' successo niente, e dirgli
 * "ora il volume non e' riconoscibile" lo manderebbe a cercare un danno
 * che non c'e'. Peggio: quella partizione puo' essere la root del sistema
 * che sta girando. */
static int toccato = 0;

typedef struct {
    unsigned int tipo;          /* 16 o 32 */
    unsigned int tot_sett;
    unsigned int spc;           /* settori per cluster */
    unsigned int riservati;
    unsigned int root_entries;  /* 0 su FAT32 */
    unsigned int root_sett;     /* 0 su FAT32 */
    unsigned int fat_sett;
    unsigned int n_cluster;
    unsigned int primo_dato;    /* primo settore dell'area dati */
} Geo;

/* =============================================================================
 * Piccole utilita'
 * ============================================================================= */

static void put16(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
}

static void put32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)v;         p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static unsigned int in_mb(unsigned int settori) { return settori / SETT_PER_MB; }

static char maiuscola(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* =============================================================================
 * Scelta dei settori per cluster
 *
 * Le tabelle sono quelle di Microsoft (specifica FAT, "fatgen103"). Non le
 * ho scelte io e non le miglioro: un volume formattato con valori diversi
 * da quelli funziona, ma smette di somigliare a cio' che ogni altro
 * sistema si aspetta, e il primo strumento di recupero che ci si punta
 * contro lo tratta come sospetto.
 *
 * Sono comunque solo un PUNTO DI PARTENZA: dopo il calcolo il numero di
 * cluster viene verificato contro le soglie, e se cade fuori banda la
 * dimensione viene corretta.
 * ============================================================================= */
static unsigned int spc_iniziale(unsigned int tipo, unsigned int tot)
{
    if (tipo == 16) {
        if (tot <=   32680u) return 2;      /*  16 MB */
        if (tot <=  262144u) return 4;      /* 128 MB */
        if (tot <=  524288u) return 8;      /* 256 MB */
        if (tot <= 1048576u) return 16;     /* 512 MB */
        if (tot <= 2097152u) return 32;     /*   1 GB */
        return 64;                          /*   2 GB, il massimo */
    }

    if (tot <=   532480u) return 1;         /* 260 MB */
    if (tot <= 16777216u) return 8;         /*   8 GB */
    if (tot <= 33554432u) return 16;        /*  16 GB */
    if (tot <= 67108864u) return 32;        /*  32 GB */
    return 64;
}

/* =============================================================================
 * Calcolo della geometria
 *
 * Il conto e' CIRCOLARE: la dimensione della FAT dipende da quanti cluster
 * ci sono, e quanti cluster ci sono dipende da quanto spazio si porta via
 * la FAT. La formula qui sotto e' quella della specifica: risolve il giro
 * per eccesso, cioe' produce una FAT che puo' avere qualche voce di troppo
 * ma mai una di meno. Una FAT anche solo di un settore troppo corta
 * significa cluster che esistono nell'area dati e non hanno una voce che
 * li descrive: il filesystem li alloca e poi non sa piu' rileggerli.
 *
 * Ritorna 0 se la geometria e' valida, <0 con il motivo gia' stampato.
 * ============================================================================= */
static int calcola(Geo *g, unsigned int tipo, unsigned int tot, unsigned int spc)
{
    unsigned int tmp1, tmp2, dati, voci_per_sett;

    /* Azzerati PRIMA di qualunque uscita anticipata: scegli() decide come
     * correggere la dimensione del cluster guardando g->n_cluster, e su
     * un ritorno di errore lo leggerebbe non inizializzato. Zero significa
     * "troppo pochi cluster", che e' la lettura giusta di ogni caso in cui
     * questa funzione si arrende. */
    g->n_cluster    = 0;
    g->fat_sett     = 0;
    g->primo_dato   = 0;

    g->tipo         = tipo;
    g->tot_sett     = tot;
    g->spc          = spc;
    g->riservati    = (tipo == 32) ? RISERVATI_32 : RISERVATI_16;
    g->root_entries = (tipo == 32) ? 0 : ROOT_ENTRIES_16;
    g->root_sett    = (g->root_entries * 32u + SETT_BYTE - 1u) / SETT_BYTE;

    if (tot <= g->riservati + g->root_sett) return -1;

    tmp1 = tot - (g->riservati + g->root_sett);
    tmp2 = 256u * spc + N_FAT;
    if (tipo == 32) tmp2 /= 2u;

    /* Divisione con arrotondamento in su fatta SENZA sommare tmp2-1 a
     * tmp1: su una partizione vicina ai 2 TiB quella somma traboccherebbe
     * a 32 bit e darebbe una FAT ridicolmente piccola. */
    g->fat_sett = tmp1 / tmp2;
    if (tmp1 % tmp2) g->fat_sett++;

    if (tot <= g->riservati + N_FAT * g->fat_sett + g->root_sett) return -1;

    dati         = tot - (g->riservati + N_FAT * g->fat_sett + g->root_sett);
    g->n_cluster = dati / spc;
    g->primo_dato = g->riservati + N_FAT * g->fat_sett + g->root_sett;

    /* La FAT deve contenere una voce per ogni cluster, piu' le due voci
     * riservate 0 e 1. La formula lo garantisce, ma verificarlo costa due
     * righe e copre il caso in cui qualcuno la cambi. */
    voci_per_sett = (tipo == 32) ? (SETT_BYTE / 4u) : (SETT_BYTE / 2u);
    if (g->fat_sett * voci_per_sett < g->n_cluster + 2u) return -1;

    return 0;
}

/* Sceglie la geometria: parte dalla tabella e corregge finche' il numero
 * di cluster non cade nella banda giusta. Cluster piu' grandi = meno
 * cluster, e viceversa. */
static int scegli(Geo *g, unsigned int tipo, unsigned int tot)
{
    unsigned int spc = spc_iniziale(tipo, tot);
    unsigned int min = (tipo == 32) ? CLUSTER_MIN_FAT32 : CLUSTER_MIN_FAT16;
    unsigned int max = (tipo == 32) ? CLUSTER_MAX_FAT32 : CLUSTER_MAX_FAT16;
    int giri;

    for (giri = 0; giri < 8; giri++) {
        if (calcola(g, tipo, tot, spc) == 0 &&
            g->n_cluster >= min && g->n_cluster <= max) {
            return 0;
        }

        /* Troppi cluster: cluster piu' grandi. Troppo pochi: piu' piccoli. */
        if (g->n_cluster > max) {
            if (spc >= 64) break;
            spc *= 2;
        } else {
            if (spc <= 1) break;
            spc /= 2;
        }
    }

    printf("mkfs: %u settori (%u MB) non stanno in un volume FAT%u.\n",
           tot, in_mb(tot), tipo);

    if (tipo == 16) {
        printf("\nFAT16 richiede fra %u e %u cluster. Con la dimensione di\n",
               CLUSTER_MIN_FAT16, CLUSTER_MAX_FAT16);
        printf("cluster piu' grande possibile (64 settori = 32 KB) il tetto e'\n");
        printf("circa 2 GB; sotto i ~16 MB restano troppo pochi cluster e il\n");
        printf("volume ricadrebbe in FAT12.\n");
        printf("\nPer questa partizione usa: mkfs -t fat32\n");
    } else {
        printf("\nFAT32 richiede almeno %u cluster. Sotto i ~33 MB non ce ne\n",
               CLUSTER_MIN_FAT32);
        printf("sono abbastanza nemmeno con cluster da un settore.\n");
        printf("\nPer questa partizione usa: mkfs -t fat16\n");
    }

    return -1;
}

/* =============================================================================
 * Scrittura
 * ============================================================================= */

/* Azzera `n` settori a partire da `lba`. Ritorna 0, o <0. */
static int azzera(const char *dev, unsigned int lba, unsigned int n)
{
    while (n > 0) {
        unsigned int q = (n > BLKIO_MAX_SETT) ? BLKIO_MAX_SETT : n;
        int r = blkwrite(dev, lba, q, zeri);

        if (r < 0) return r;
        if ((unsigned int)r != q) return -1;

        lba += q;
        n   -= q;
    }
    return 0;
}

/* Compone il settore di avvio in `sett`. */
static void componi_boot(const Geo *g, unsigned int primo_lba,
                         const char *etichetta, unsigned int volid)
{
    unsigned int i;

    for (i = 0; i < SETT_BYTE; i++) sett[i] = 0;

    /* Salto iniziale. Non c'e' codice di avvio da saltare — questo volume
     * lo rende avviabile /bin/install, non mkfs — ma il campo deve esserci
     * lo stesso: parecchi driver rifiutano un volume il cui primo byte non
     * sia 0xEB o 0xE9, perche' e' il primo indizio che il settore sia
     * davvero un settore di avvio e non dati qualunque. */
    sett[0] = 0xEB;
    sett[1] = (unsigned char)((g->tipo == 32) ? 0x58 : 0x3C);
    sett[2] = 0x90;

    /* OEM: 8 caratteri. Chi guarda un volume con uno strumento esterno
     * deve poter capire da cosa e' stato scritto. */
    {
        const char *oem = "EXOS    ";
        for (i = 0; i < 8; i++) sett[3 + i] = (unsigned char)oem[i];
    }

    put16(sett + 11, SETT_BYTE);            /* byte per settore */
    sett[13] = (unsigned char)g->spc;       /* settori per cluster */
    put16(sett + 14, g->riservati);
    sett[16] = (unsigned char)N_FAT;
    put16(sett + 17, g->root_entries);

    /* TotSec16 vale solo se ci sta: oltre i 65535 settori si usa TotSec32
     * e questo campo DEVE essere 0. Scriverli entrambi e' un errore
     * classico, e produce volumi che alcuni driver leggono troncati. */
    if (g->tot_sett < 0x10000u) put16(sett + 19, g->tot_sett);
    else                        put16(sett + 19, 0);

    sett[21] = 0xF8;                        /* supporto fisso */
    put16(sett + 22, (g->tipo == 32) ? 0 : g->fat_sett);
    put16(sett + 24, 63);                   /* settori per traccia */
    put16(sett + 26, 255);                  /* testine */

    /* Settori nascosti = LBA assoluto della partizione. Alcuni caricatori
     * ci contano per tradurre gli LBA relativi del volume. */
    put32(sett + 28, primo_lba);
    put32(sett + 32, (g->tot_sett < 0x10000u) ? 0 : g->tot_sett);

    if (g->tipo == 32) {
        put32(sett + 36, g->fat_sett);
        put16(sett + 40, 0);                /* ExtFlags: FAT rispecchiate */
        put16(sett + 42, 0);                /* versione */
        put32(sett + 44, 2);                /* cluster della radice */
        put16(sett + 48, 1);                /* settore FSInfo */

        /* BPB_BkBootSec sta all'offset 50, e i 12 byte da 52 a 63 sono
         * riservati e vanno lasciati a zero. Scriverlo a 52 — l'errore
         * facile — lo mette DENTRO l'area riservata e lascia a zero il
         * campo vero: il volume dichiara di non avere copia del settore
         * di avvio, e ogni strumento di verifica lo segnala. */
        put16(sett + 50, BK_BOOT_SETT);

        sett[64] = 0x80;                    /* unita' BIOS */
        sett[66] = 0x29;                    /* firma dei campi estesi */
        put32(sett + 67, volid);
        for (i = 0; i < 11; i++) sett[71 + i] = (unsigned char)etichetta[i];
        {
            const char *t = "FAT32   ";
            for (i = 0; i < 8; i++) sett[82 + i] = (unsigned char)t[i];
        }
    } else {
        sett[36] = 0x80;
        sett[38] = 0x29;
        put32(sett + 39, volid);
        for (i = 0; i < 11; i++) sett[43 + i] = (unsigned char)etichetta[i];
        {
            const char *t = "FAT16   ";
            for (i = 0; i < 8; i++) sett[54 + i] = (unsigned char)t[i];
        }
    }

    sett[510] = 0x55;
    sett[511] = 0xAA;
}

/* Voce di directory con l'etichetta del volume. Non e' un file: e' una
 * voce con l'attributo 0x08, ed e' l'unico posto in cui l'etichetta conta
 * davvero — quella nel settore di avvio e' una copia che gli strumenti
 * ignorano quando le due divergono. */
static void componi_etichetta(const char *etichetta)
{
    unsigned int i;

    for (i = 0; i < SETT_BYTE; i++) sett[i] = 0;
    for (i = 0; i < 11; i++) sett[i] = (unsigned char)etichetta[i];

    sett[11] = 0x08;                        /* ATTR_VOLUME_ID */
    put16(sett + 22, 0);                    /* ora */
    put16(sett + 24, 0x2821);               /* data: 1 gen 2000 */
}

static int scrivi_tutto(const char *dev, const Geo *g, unsigned int primo_lba,
                        const char *etichetta, unsigned int volid)
{
    unsigned int i, fat0;
    int r;

    /* --- 1. il volume vecchio smette di essere riconoscibile -----------
     * Vedi l'ordine di scrittura in testa al file: da qui in poi, se la
     * formattazione si interrompe, resta un volume che nessuno monta —
     * non uno che sembra buono e restituisce file vuoti. */
    printf("  azzero il settore di avvio precedente\n");
    r = azzera(dev, 0, 1);
    if (r < 0) return r;
    toccato = 1;

    if (g->tipo == 32) {
        r = azzera(dev, BK_BOOT_SETT, 1);
        if (r < 0) return r;
    }

    /* --- 2. area riservata, tabelle FAT, directory radice --------------
     * Le FAT vanno azzerate TUTTE: i byte che c'erano prima descrivono
     * catene di cluster del filesystem precedente, e una FAT nuova che ne
     * conservasse anche solo un pezzo direbbe che dei cluster sono
     * occupati da file che non esistono piu'. */
    printf("  azzero %u settori riservati\n", g->riservati);
    r = azzera(dev, 0, g->riservati);
    if (r < 0) return r;

    for (i = 0; i < N_FAT; i++) {
        printf("  azzero la FAT %u (%u settori)\n", i + 1, g->fat_sett);
        r = azzera(dev, g->riservati + i * g->fat_sett, g->fat_sett);
        if (r < 0) return r;
    }

    if (g->tipo == 32) {
        printf("  azzero il cluster della directory radice\n");
        r = azzera(dev, g->primo_dato, g->spc);
    } else {
        printf("  azzero la directory radice (%u settori)\n", g->root_sett);
        r = azzera(dev, g->riservati + N_FAT * g->fat_sett, g->root_sett);
    }
    if (r < 0) return r;

    /* --- 3. le voci iniziali delle FAT ---------------------------------
     * La voce 0 contiene il descrittore di supporto esteso a tutta la
     * larghezza della voce, la 1 e' il marcatore di fine catena. Su FAT32
     * la voce 2 chiude la catena della directory radice, che occupa
     * esattamente un cluster. */
    for (i = 0; i < SETT_BYTE; i++) sett[i] = 0;

    if (g->tipo == 32) {
        put32(sett + 0, 0x0FFFFFF8u);
        put32(sett + 4, 0x0FFFFFFFu);
        put32(sett + 8, 0x0FFFFFFFu);       /* radice: un cluster, e finisce */
    } else {
        put16(sett + 0, 0xFFF8u);
        put16(sett + 2, 0xFFFFu);
    }

    fat0 = g->riservati;
    for (i = 0; i < N_FAT; i++) {
        printf("  scrivo le voci iniziali della FAT %u\n", i + 1);
        if (blkwrite(dev, fat0 + i * g->fat_sett, 1, sett) < 0) return -1;
    }

    /* --- 4. l'etichetta nella directory radice ------------------------- */
    componi_etichetta(etichetta);
    printf("  scrivo l'etichetta del volume\n");
    if (g->tipo == 32) {
        if (blkwrite(dev, g->primo_dato, 1, sett) < 0) return -1;
    } else {
        if (blkwrite(dev, g->riservati + N_FAT * g->fat_sett, 1, sett) < 0)
            return -1;
    }

    /* --- 5. FSInfo, solo FAT32 -----------------------------------------
     * Contiene il conteggio dei cluster liberi. Il valore 0xFFFFFFFF
     * ("sconosciuto") sarebbe legale e obbligherebbe chi monta a
     * ricontare — ma qui il numero NON e' una stima: su un volume appena
     * creato i cluster occupati sono esattamente uno, quello della
     * directory radice. Scrivere "non lo so" quando lo si sa e' rumore, e
     * ogni verificatore standard lo segnala: un volume che nasce con una
     * segnalazione addosso e' un volume in cui la segnalazione VERA, il
     * giorno che arriva, passa inosservata. */
    if (g->tipo == 32) {
        for (i = 0; i < SETT_BYTE; i++) sett[i] = 0;
        put32(sett + 0,   0x41615252u);     /* "RRaA" */
        put32(sett + 484, 0x61417272u);     /* "rrAa" */
        put32(sett + 488, g->n_cluster - 1u);   /* liberi: tutti tranne la radice */
        put32(sett + 492, 3u);                  /* prossimo libero da provare */
        put32(sett + 508, 0xAA550000u);
        printf("  scrivo il settore FSInfo\n");
        if (blkwrite(dev, 1, 1, sett) < 0) return -1;
    }

    /* --- 6. il settore di avvio, PER ULTIMO ---------------------------- */
    componi_boot(g, primo_lba, etichetta, volid);
    printf("  scrivo il settore di avvio\n");
    if (blkwrite(dev, 0, 1, sett) < 0) return -1;

    if (g->tipo == 32) {
        printf("  scrivo la copia del settore di avvio (settore %u)\n",
               BK_BOOT_SETT);
        if (blkwrite(dev, BK_BOOT_SETT, 1, sett) < 0) return -1;
    }

    return 0;
}

/* =============================================================================
 * Ricognizione del dispositivo
 * ============================================================================= */

/* Trova la partizione per nome. Riempie primo e settori. */
static int trova(const char *nome, unsigned int *primo, unsigned int *settori)
{
    BlkInfo b[8];
    unsigned int start = 0;
    int n, k;

    for (;;) {
        n = blkinfo(b, 8, start);
        if (n <= 0) break;

        for (k = 0; k < n; k++) {
            if (strcmp(b[k].nome, nome) != 0) continue;

            if (b[k].tipo != 3) {
                printf("mkfs: '%s' non e' una partizione.\n\n", nome);
                printf("Si formattano le partizioni (hd0p1, hd0p2, ...), non i\n");
                printf("dischi interi e non il floppy. Il kernel accetta solo\n");
                printf("quelle: e' cio' che rende la tabella delle partizioni\n");
                printf("irraggiungibile da un formattatore.\n");
                return -1;
            }
            if (b[k].sola_lettura) {
                printf("mkfs: '%s' e' in sola lettura.\n", nome);
                return -1;
            }

            *primo   = b[k].primo_lo;
            *settori = b[k].settori_lo;
            return 0;
        }

        start += (unsigned int)n;
        if (n < 8) break;
    }

    printf("mkfs: '%s' non esiste. `disk` elenca i dispositivi.\n", nome);
    return -1;
}

/* Il byte di tipo nella tabella delle partizioni e' solo un suggerimento,
 * ma un suggerimento che contraddice il contenuto e' peggio di nessun
 * suggerimento: manda fuori strada chi guarda il disco da un altro
 * sistema. Non si corregge da qui — la tabella e' di `fdisk` — ma si dice. */
static void controlla_tipo_mbr(const char *nome, unsigned int tipo_fs)
{
    DiskInfo d;
    unsigned int disco, numero, i;
    unsigned int atteso_a, atteso_b;

    if (nome[0] != 'h' || nome[1] != 'd' || nome[3] != 'p') return;
    disco  = (unsigned int)(nome[2] - '0');
    numero = (unsigned int)(nome[4] - '0');
    if (disco > 3 || numero < 1 || numero > 9) return;
    if (nome[5] != '\0') return;

    if (diskinfo(disco, &d) < 0 || !d.presente) return;

    if (tipo_fs == 2) {                     /* ext2 */
        atteso_a = 0x83; atteso_b = 0x83;
    } else if (tipo_fs == 32) {
        atteso_a = 0x0C; atteso_b = 0x0B;
    } else {
        atteso_a = 0x06; atteso_b = 0x0E;
    }

    for (i = 0; i < d.n_part; i++) {
        if (d.part[i].numero != numero) continue;

        if (d.part[i].tipo == atteso_a || d.part[i].tipo == atteso_b) return;

        printf("\nNOTA: nella tabella delle partizioni %s e' di tipo 0x%02X,\n",
               nome, d.part[i].tipo);
        if (tipo_fs == 2) printf("che non corrisponde a ext2 (0x83). ");
        else              printf("che non corrisponde a FAT%u. ", tipo_fs);
        printf("Il byte di tipo e' solo un\n");
        printf("suggerimento e non impedisce niente, ma un suggerimento\n");
        printf("sbagliato manda fuori strada chi guarda il disco da un altro\n");
        printf("sistema. Correggilo con:  fdisk hd%u  poi  t  %u  %02X\n",
               disco, numero, atteso_a);
        return;
    }
}

/* =============================================================================
 * main
 * ============================================================================= */
static void uso(void)
{
    printf("uso: mkfs [-t fat16|fat32|ext2] [-L ETICHETTA] <partizione>\n\n");
    printf("  mkfs hd0p1                 sceglie dalla dimensione\n");
    printf("  mkfs -t fat32 hd0p1\n");
    printf("  mkfs -t fat16 -L DATI hd0p2\n");
    printf("  mkfs -t ext2  -L SISTEMA hd0p3\n\n");
    printf("SENZA -t: fino a 2 GB FAT16, oltre FAT32. Non e' una soglia\n");
    printf("arbitraria - FAT16 arriva a 65524 cluster, che con cluster da\n");
    printf("32 KB fanno poco piu' di 2 GB. ext2 non entra mai nella scelta\n");
    printf("automatica: e' un formato che si chiede, non uno in cui si\n");
    printf("finisce.\n\n");
    printf("La partizione NON dev'essere montata. `disk` elenca i\n");
    printf("dispositivi, `fdisk` crea le partizioni.\n\n");
    printf("EX-OS monta FAT12, FAT16, FAT32, ext2 e ISO 9660. `chkdsk`\n");
    printf("controlla e ripara i volumi FAT e ext2.\n");
}

/* Chiede conferma prima di distruggere. Ritorna 1 se l'utente ha
 * confermato. */
static int conferma(const char *dev)
{
    char risposta[16];
    int  r;

    printf("\nTUTTO IL CONTENUTO DI %s VERRA' PERSO.\n", dev);
    printf("Scrivere? (scrivi `si` per confermare): ");

    r = (int)read(0, risposta, sizeof(risposta) - 1);
    if (r <= 0) { printf("\nAnnullato.\n"); return 0; }
    risposta[r] = '\0';
    while (r > 0 && (risposta[r - 1] == '\n' || risposta[r - 1] == '\r'))
        risposta[--r] = '\0';

    if (strcmp(risposta, "si") != 0) {
        printf("Annullato. La partizione non e' stata toccata.\n");
        return 0;
    }
    return 1;
}

/* Il commento finale sull'esito, condiviso dai due rami. */
static void spiega_fallimento(const char *dev, int r)
{
    printf("\nFormattazione fallita (errore %d).\n", r);

    switch (-r) {
        case 16:
            printf("%s e' MONTATA. Il kernel rifiuta l'accesso grezzo a una\n", dev);
            printf("partizione montata: sopra c'e' una cache write-back, e\n");
            printf("scriverci sotto significa che il primo sync ci ricopre i\n");
            printf("settori vecchi. Smontala con `umount`.\n");
            break;
        case 1:
            printf("%s non e' una partizione.\n", dev);
            break;
        case 5:
            printf("Errore di I/O: il disco non ha accettato la scrittura.\n");
            break;
        default:
            break;
    }

    if (toccato) {
        printf("\nIl volume ora NON e' riconoscibile: le strutture del\n");
        printf("filesystem precedente sono state azzerate per prime, di\n");
        printf("proposito. Rilancia mkfs quando hai risolto.\n");
    } else {
        printf("\nNessuna scrittura e' andata a segno: %s e' esattamente\n", dev);
        printf("com'era prima.\n");
    }
}

/* =============================================================================
 * I due rami: FAT e ext2
 *
 * Sono separati perche' le due geometrie non hanno niente in comune, e
 * fondere il calcolo in una struttura sola darebbe una decina di campi
 * validi solo per meta' dei casi — il genere di struttura in cui prima o
 * poi si legge un campo che per quel filesystem non significa niente.
 *
 * Cio' che condividono e' il contorno, ed e' condiviso davvero: la
 * ricerca del dispositivo, la conferma, la spiegazione dell'errore, la
 * nota sul byte di tipo nella tabella delle partizioni.
 * ============================================================================= */
static int fai_fat(const char *dev, unsigned int tipo, unsigned int primo,
                   unsigned int settori, const char *etichetta_utente)
{
    Geo  g;
    char et[12];
    int  i, r;

    /* L'etichetta FAT e' di 11 caratteri, in maiuscolo, riempita di spazi:
     * non e' una stringa terminata da NUL ma un campo a lunghezza fissa
     * dentro una voce di directory. */
    for (i = 0; i < 11; i++) {
        char c = etichetta_utente[i];
        if (c == '\0') break;
        et[i] = maiuscola(c);
    }
    for (; i < 11; i++) et[i] = ' ';
    et[11] = '\0';

    if (scegli(&g, tipo, settori) != 0) return 1;

    printf("Partizione %s\n", dev);
    printf("  primo settore assoluto : %u\n", primo);
    printf("  dimensione             : %u settori (%u MB)\n",
           settori, in_mb(settori));
    printf("\nFilesystem FAT%u\n", g.tipo);

    if (g.spc * SETT_BYTE >= 1024u)
        printf("  settori per cluster    : %u  (%u KB per cluster)\n",
               g.spc, g.spc * SETT_BYTE / 1024u);
    else
        printf("  settori per cluster    : %u  (%u byte per cluster)\n",
               g.spc, g.spc * SETT_BYTE);

    printf("  settori riservati      : %u\n", g.riservati);
    printf("  tabelle FAT            : %u da %u settori\n", N_FAT, g.fat_sett);
    if (g.tipo == 16)
        printf("  voci nella radice      : %u (%u settori)\n",
               g.root_entries, g.root_sett);
    else
        printf("  radice                 : cluster 2\n");
    printf("  primo settore dei dati : %u\n", g.primo_dato);

    /* IL numero: e' da questo, e non dalla stringa "FAT32" nel settore di
     * avvio, che ogni driver serio deduce il tipo del volume. Mostrarlo
     * accanto alla soglia rende verificabile che sia quello giusto. */
    printf("  CLUSTER                : %u\n", g.n_cluster);
    if (g.tipo == 16)
        printf("    (FAT16 richiede fra %u e %u)\n",
               CLUSTER_MIN_FAT16, CLUSTER_MAX_FAT16);
    else
        printf("    (FAT32 richiede almeno %u)\n", CLUSTER_MIN_FAT32);
    printf("  etichetta              : '%s'\n", et);

    if (!conferma(dev)) return 1;

    printf("\n");
    {
        unsigned int volid = uptime_ms();
        if (volid == 0) volid = 0x45584F53u;    /* "EXOS", se il tempo e' fermo */
        r = scrivi_tutto(dev, &g, primo, et, volid);
    }

    if (r < 0) { spiega_fallimento(dev, r); return 1; }

    printf("\nFilesystem FAT%u creato su %s.\n", g.tipo, dev);
    controlla_tipo_mbr(dev, g.tipo);

    printf("\nOra puoi montarlo:\n");
    printf("  mount %s /disk\n", dev);
    return 0;
}

static int fai_ext2(const char *dev, unsigned int primo, unsigned int settori,
                    const char *etichetta_utente)
{
    Ext2Geo g;
    char    et[17];
    int     i, r;

    /* L'etichetta ext2 e' di 16 byte e conserva le minuscole: e' un campo
     * del superblocco, non una voce di directory in 8.3. */
    for (i = 0; i < 16; i++) {
        char c = etichetta_utente[i];
        if (c == '\0') break;
        et[i] = c;
    }
    for (; i < 16; i++) et[i] = '\0';
    et[16] = '\0';

    if (ext2_piano(&g, settori) != 0) return 1;

    printf("Partizione %s\n", dev);
    printf("  primo settore assoluto : %u\n", primo);
    printf("  dimensione             : %u settori (%u MB)\n",
           settori, in_mb(settori));

    ext2_mostra(&g);
    printf("  etichetta              : '%s'\n", et);

    /* L'avviso che stava qui — "EX-OS non sa ancora MONTARE un ext2" —
     * era vero quando questo formattatore e' nato e ha smesso di esserlo
     * con il driver ext2 (0.13x). Un messaggio che descrive una
     * limitazione superata non e' innocuo: manda a cercare un driver che
     * c'e', e fa dubitare di un `mount` che invece funziona. */
    printf("\nIl volume sara' leggibile e scrivibile da EX-OS (`mount %s <punto>`)\n",
           dev);
    printf("e da Linux: e' un ext2 revisione 1 senza estensioni.\n");

    if (!conferma(dev)) return 1;

    printf("\n");
    {
        unsigned int seme = uptime_ms();
        r = ext2_scrivi(dev, &g, et, seme, &toccato);
    }

    if (r < 0) { spiega_fallimento(dev, r); return 1; }

    printf("\nFilesystem ext2 creato su %s.\n", dev);
    controlla_tipo_mbr(dev, 2);

    printf("\nVerificalo da Linux con:  e2fsck -fn <immagine>\n");
    return 0;
}

int main(int argc, char **argv)
{
    const char  *dev = 0;
    const char  *etichetta = "";
    unsigned int tipo = 0, primo = 0, settori = 0;
    int          i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0) {
            if (++i >= argc) { uso(); return 1; }
            if      (strcmp(argv[i], "fat16") == 0) tipo = 16;
            else if (strcmp(argv[i], "fat32") == 0) tipo = 32;
            else if (strcmp(argv[i], "ext2")  == 0) tipo = 2;
            else {
                printf("mkfs: tipo '%s' sconosciuto. Sono fat16, fat32 e ext2.\n",
                       argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-L") == 0) {
            if (++i >= argc) { uso(); return 1; }
            etichetta = argv[i];
        } else if (argv[i][0] == '-') {
            printf("mkfs: opzione '%s' sconosciuta.\n\n", argv[i]);
            uso();
            return 1;
        } else {
            dev = argv[i];
        }
    }

    if (dev == 0) { uso(); return 1; }

    printf("%s %s - formattatore di EX-OS\n\n", MKFS_NAME, MKFS_VERSION);

    if (trova(dev, &primo, &settori) != 0) return 1;

    /* =====================================================================
     * SENZA -t SI SCEGLIE DALLA DIMENSIONE, e il confine non e' arbitrario:
     * e' il limite del formato.
     *
     * FAT16 arriva a 65524 cluster. Con i 64 settori per cluster che
     * scegli() usa al massimo — 32 KB — fanno poco piu' di 2 GB: oltre,
     * FAT16 non ci sta e basta. Sotto, e' preferibile a FAT32 perche' ha
     * una tabella meta' piu' piccola e una root directory a dimensione
     * fissa, cioe' meno settori da leggere per fare la stessa cosa.
     *
     * ! SI DICE COSA SI E' SCELTO E PERCHE'. Un formattatore che decide in
     * silenzio lascia chi guarda a chiedersi, sei mesi dopo, perche' quel
     * volume sia FAT32 e quell'altro FAT16.
     *
     * ext2 non entra mai nella scelta automatica: e' un formato che si
     * chiede, non uno in cui si finisce. Chi lo vuole scrive -t ext2.
     * ===================================================================== */
    if (tipo == 0) {
        tipo = (settori <= LIMITE_FAT16_SETTORI) ? 16u : 32u;
        printf("Nessun -t indicato: %u settori (%u MB) -> FAT%u\n",
               settori, settori / SETT_PER_MB, tipo);
        printf("  (FAT16 arriva a 65524 cluster, cioe' ~2 GB con cluster da\n");
        printf("   32 KB: oltre non ci sta. Per ext2 serve -t ext2.)\n\n");
    }

    if (tipo == 2) {
        if (etichetta[0] == '\0') etichetta = "";
        return fai_ext2(dev, primo, settori, etichetta);
    }

    if (etichetta[0] == '\0') etichetta = "NO NAME";
    return fai_fat(dev, tipo, primo, settori, etichetta);
}
