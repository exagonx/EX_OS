/* =============================================================================
 * kernel/block/blk.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Vedi kernel/include/blk.h per il razionale e per la proprieta' della
 * finestra, che e' la ragione principale per cui questo file esiste.
 * ============================================================================= */

#include "kernel.h"
#include "blk.h"
#include "ata.h"
#include "mbr.h"
#include "fat12.h"
#include "syscall.h"    /* ERR() e i codici errno, per blk_rescan */

/* g_n e' il MASSIMO INDICE MAI USATO, non il numero di dispositivi vivi.
 * Da quando esiste blk_rescan() l'array puo' avere buchi: uno slot
 * liberato resta li' con usato = 0 invece di far scalare quelli dopo,
 * perche' gli indici sono in mano ai montaggi (vedi blk.h). Chiunque
 * scorra l'array DEVE saltare le voci con usato == 0. */
static BlkDev g_dev[BLK_MAX_DEV];
static int    g_n = 0;

/* Copia un nome corto senza dipendere da una libreria di stringhe. */
static void nome_copia(char *dst, const char *src)
{
    int i = 0;
    while (src[i] && i < BLK_NOME_MAX - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* Compone "hd0p12" senza printf: il kernel non ha uno sprintf. */
static void nome_componi(char *dst, const char *pre, int a, char sep, int b)
{
    int i = 0, j;
    char num[4];

    while (pre[i] && i < BLK_NOME_MAX - 1) { dst[i] = pre[i]; i++; }

    if (a >= 0 && i < BLK_NOME_MAX - 1) dst[i++] = (char)('0' + (a % 10));

    if (sep && i < BLK_NOME_MAX - 1) {
        dst[i++] = sep;
        j = 0;
        if (b == 0) num[j++] = '0';
        while (b > 0 && j < 3) { num[j++] = (char)('0' + (b % 10)); b /= 10; }
        while (j > 0 && i < BLK_NOME_MAX - 1) dst[i++] = num[--j];
    }

    dst[i] = '\0';
}

/* Prende uno slot: prima uno liberato da un rescan, poi in coda. Il riuso
 * non e' un'ottimizzazione ma una necessita': senza, ripartizionare lo
 * stesso disco una decina di volte esaurirebbe BLK_MAX_DEV. */
static BlkDev *nuovo(void)
{
    BlkDev *b;
    int     i, k;

    for (i = 0; i < g_n; i++) {
        if (!g_dev[i].usato) { b = &g_dev[i]; goto azzera; }
    }

    if (g_n >= BLK_MAX_DEV) return NULL;
    b = &g_dev[g_n++];

azzera:
    /* Uno slot riusato non deve portarsi dietro nulla del precedente: un
     * in_uso o un sola_lettura rimasti dal dispositivo di prima
     * varrebbero per quello nuovo, che non c'entra niente. */
    for (k = 0; k < BLK_NOME_MAX; k++) b->nome[k] = 0;
    b->usato = 0; b->tipo = BLK_TIPO_NESSUNO; b->disco = 0;
    b->sola_lettura = 0; b->in_uso = 0; b->primo = 0; b->settori = 0;
    return b;
}

/* Registra le partizioni del disco ATA `d` leggendone la tabella.
 * Ritorna quante ne ha registrate. Condivisa da blk_init() e blk_rescan()
 * perche' le due devono produrre esattamente gli stessi dispositivi: se
 * divergessero, un disco ripartizionato a caldo si presenterebbe diverso
 * da come si presenta dopo un riavvio. */
static int registra_partizioni(int d, const AtaDevice *a)
{
    TabellaPartizioni tab;
    BlkDev           *b;
    int               p, n = 0;

    if (mbr_leggi(d, &tab) != 0) return 0;

    /* Un disco GPT non ha partizioni MBR utilizzabili: registrare
     * finestre ricavate dall'MBR protettivo significherebbe offrire
     * un dispositivo che copre l'intero disco spacciandolo per una
     * partizione. */
    if (tab.schema != PT_SCHEMA_MBR) return 0;

    for (p = 0; p < tab.n; p++) {
        /* Le ESTESE sono contenitori, non spazio utilizzabile:
         * registrarle darebbe un dispositivo che si sovrappone alle
         * proprie logiche, cioe' esattamente la situazione che la
         * finestra serve a impedire. */
        if (tab.p[p].tipo == 0x05 || tab.p[p].tipo == 0x0F ||
            tab.p[p].tipo == 0x85) continue;

        /* Una partizione che esce dal disco non diventa un
         * dispositivo: sarebbe una finestra che promette settori
         * inesistenti. Meglio non offrirla affatto. */
        if (tab.p[p].inizio >= a->settori ||
            tab.p[p].inizio + tab.p[p].settori > a->settori) {
            klog(LOG_WARN, "BLK: partizione %u di hd%d fuori dal disco, ignorata",
                 tab.p[p].numero, d);
            continue;
        }

        b = nuovo();
        if (b == NULL) break;

        nome_componi(b->nome, "hd", d, 'p', tab.p[p].numero);
        b->usato        = 1;
        b->tipo         = BLK_TIPO_PART;
        b->disco        = (uint8_t)d;
        b->sola_lettura = 0;
        b->primo        = tab.p[p].inizio;
        b->settori      = tab.p[p].settori;
        n++;
    }

    return n;
}

int blk_init(void)
{
    int d;

    g_n = 0;

    /* --- Il floppy. Resta un dispositivo come gli altri anche se il suo
     * filesystem continua a usare la strada diretta: serve per poterlo
     * nominare (fd0) e, in prospettiva, per montarlo dal VFS. --- */
    {
        BlkDev *b = nuovo();
        if (b) {
            nome_copia(b->nome, "fd0");
            b->usato        = 1;
            b->tipo         = BLK_TIPO_FLOPPY;
            b->disco        = 0;
            b->sola_lettura = 0;
            b->primo        = 0;
            b->settori      = 2880;    /* 1.44 MB */
        }
    }

    /* --- Dischi ATA e loro partizioni --- */
    for (d = 0; d < ATA_MAX_DEVICES; d++) {
        const AtaDevice *a = ata_get_device(d);
        BlkDev          *b;

        if (a == NULL || !a->presente || a->tipo != ATA_TYPE_ATA) continue;

        b = nuovo();
        if (b == NULL) break;
        nome_componi(b->nome, "hd", d, 0, 0);
        b->usato        = 1;
        b->tipo         = BLK_TIPO_DISCO;
        b->disco        = (uint8_t)d;
        b->sola_lettura = 0;
        b->primo        = 0;
        b->settori      = a->settori;

        registra_partizioni(d, a);
    }

    klog(LOG_INFO, "BLK: %d dispositivi a blocchi registrati", g_n);
    return g_n;
}

int blk_rescan(int disco)
{
    const AtaDevice *a;
    int              i, n;

    a = ata_get_device(disco);
    if (a == NULL || !a->presente || a->tipo != ATA_TYPE_ATA) return ERR(ENODEV);

    /* PRIMA si controlla che nessuna sia in uso, POI si libera. Fondere i
     * due cicli significherebbe aver gia' smontato mezzo disco quando si
     * scopre che la meta' successiva non si puo' toccare. */
    for (i = 0; i < g_n; i++) {
        if (!g_dev[i].usato) continue;
        if (g_dev[i].tipo != BLK_TIPO_PART) continue;
        if (g_dev[i].disco != (uint8_t)disco) continue;

        if (g_dev[i].in_uso) {
            klog(LOG_ERROR, "BLK: %s e' in uso: rilettura di hd%d rifiutata",
                 g_dev[i].nome, disco);
            return ERR(EBUSY);
        }
    }

    for (i = 0; i < g_n; i++) {
        if (!g_dev[i].usato) continue;
        if (g_dev[i].tipo != BLK_TIPO_PART) continue;
        if (g_dev[i].disco != (uint8_t)disco) continue;
        g_dev[i].usato = 0;
    }

    /* Il dispositivo del disco INTERO non si tocca: la sua finestra e'
     * il disco e non dipende dalla tabella. */
    n = registra_partizioni(disco, a);

    klog(LOG_INFO, "BLK: hd%d riletto, %d partizioni", disco, n);
    return n;
}

int blk_ripartiziona(int disco, const Partizione *voci, int n,
                     uint32_t *problemi)
{
    int i, r;

    if (problemi) *problemi = 0;

    /* Il controllo va fatto PRIMA di scrivere. Scoprirlo dopo — cioe'
     * lasciandolo al solo blk_rescan() — significherebbe avere la tabella
     * gia' cambiata sul disco mentre i dispositivi in memoria descrivono
     * ancora quella vecchia: una scrittura attraverso una di quelle
     * finestre finirebbe dentro un'altra partizione, con il livello a
     * blocchi convinto di stare facendo il suo lavoro. */
    for (i = 0; i < g_n; i++) {
        if (!g_dev[i].usato) continue;
        if (g_dev[i].tipo != BLK_TIPO_PART) continue;
        if (g_dev[i].disco != (uint8_t)disco) continue;

        if (g_dev[i].in_uso) {
            klog(LOG_ERROR, "BLK: %s e' montato: hd%d non si ripartiziona",
                 g_dev[i].nome, disco);
            return ERR(EBUSY);
        }
    }

    r = mbr_scrivi(disco, voci, n, problemi);
    if (r != 0) return r;

    r = blk_rescan(disco);
    if (r < 0) {
        /* La tabella e' cambiata ma i dispositivi no. Non e' una
         * situazione da nascondere: chi chiama deve sapere che il disco
         * va riletto — nel dubbio riavviando — prima di usarlo. */
        klog(LOG_ERROR, "BLK: hd%d ripartizionato ma la rilettura e' fallita: "
                        "i dispositivi in memoria sono OBSOLETI", disco);
        return r;
    }

    return 0;
}

int blk_acquisisci(int i)
{
    if (i < 0 || i >= g_n || !g_dev[i].usato) return ERR(ENODEV);
    if (g_dev[i].in_uso == 0xFF)              return ERR(EBUSY);
    g_dev[i].in_uso++;
    return 0;
}

int blk_rilascia(int i)
{
    if (i < 0 || i >= g_n || !g_dev[i].usato) return ERR(ENODEV);
    /* Un rilascio in piu' azzererebbe il conteggio e renderebbe
     * ripartizionabile un disco ancora montato: meglio un conteggio che
     * resta alto — al massimo si nega un rescan legittimo — che uno che
     * scende sotto zero. */
    if (g_dev[i].in_uso == 0) {
        klog(LOG_WARN, "BLK: %s rilasciato ma non era in uso", g_dev[i].nome);
        return ERR(EINVAL);
    }
    g_dev[i].in_uso--;
    return 0;
}

int blk_occupato(int i)
{
    if (i < 0 || i >= g_n || !g_dev[i].usato) return 0;
    return g_dev[i].in_uso != 0;
}

int blk_conta(void) { return g_n; }

const BlkDev *blk_get(int i)
{
    if (i < 0 || i >= g_n) return NULL;
    /* Uno slot liberato da un rescan e' un dispositivo che NON esiste
     * piu': restituirlo lascerebbe leggere il nome e la finestra di una
     * partizione cancellata. */
    if (!g_dev[i].usato)   return NULL;
    return &g_dev[i];
}

int blk_trova(const char *nome)
{
    int i, j;

    if (nome == NULL) return -1;

    for (i = 0; i < g_n; i++) {
        if (!g_dev[i].usato) continue;      /* slot liberato da un rescan */
        for (j = 0; ; j++) {
            if (g_dev[i].nome[j] != nome[j]) break;
            if (nome[j] == '\0') return i;
        }
    }
    return -1;
}

int blk_per_partizione(int disco, int numero)
{
    char cerca[BLK_NOME_MAX];

    nome_componi(cerca, "hd", disco, 'p', numero);
    return blk_trova(cerca);
}

/* =============================================================================
 * Traduzione e CONTROLLO DELLA FINESTRA
 *
 * Unico punto in cui un LBA relativo diventa assoluto. Il controllo di
 * limite e' qui e non nei chiamanti proprio perche' i chiamanti saranno
 * molti — un driver FAT, un formattatore, un installatore — e ognuno di
 * loro puo' avere un bug nei calcoli o ricevere metadati corrotti da un
 * disco malformato. Nessuno di quei bug deve poter uscire dalla propria
 * partizione.
 * ============================================================================= */
static int traduci(int i, uint64_t lba, uint32_t n, uint64_t *assoluto)
{
    const BlkDev *b = blk_get(i);

    if (b == NULL || !b->usato) return -1;
    if (n == 0) return -1;

    /* L'overflow va controllato PRIMA della somma: lba+n puo' traboccare
     * e far sembrare interna una richiesta assurda. */
    if (lba + n < lba)      return -1;
    if (lba + n > b->settori) {
        klog(LOG_ERROR, "BLK: %s: accesso fuori finestra (lba=%u n=%u, settori=%u)",
             b->nome, (uint32_t)lba, n, (uint32_t)b->settori);
        return -1;
    }

    *assoluto = b->primo + lba;
    return 0;
}

int blk_read(int i, uint64_t lba, uint32_t n, void *buf)
{
    const BlkDev *b = blk_get(i);
    uint64_t      abs;
    uint32_t      k;

    if (traduci(i, lba, n, &abs) != 0) return -1;

    if (b->tipo == BLK_TIPO_FLOPPY) {
        uint8_t *p = (uint8_t *)buf;
        for (k = 0; k < n; k++) {
            if (fat12_dev_read((uint32_t)(abs + k), p + k * 512) != 0) return -1;
        }
        return 0;
    }

    return ata_read(b->disco, abs, n, buf);
}

int blk_write(int i, uint64_t lba, uint32_t n, const void *buf)
{
    const BlkDev *b = blk_get(i);
    uint64_t      abs;
    uint32_t      k;

    if (traduci(i, lba, n, &abs) != 0) return -1;

    if (b->sola_lettura) {
        klog(LOG_ERROR, "BLK: %s e' in sola lettura", b->nome);
        return -1;
    }

    if (b->tipo == BLK_TIPO_FLOPPY) {
        const uint8_t *p = (const uint8_t *)buf;
        for (k = 0; k < n; k++) {
            if (fat12_dev_write((uint32_t)(abs + k), p + k * 512) != 0) return -1;
        }
        return 0;
    }

    return ata_write(b->disco, abs, n, buf);
}

int blk_flush(int i)
{
    const BlkDev *b = blk_get(i);

    if (b == NULL || !b->usato) return -1;
    if (b->tipo == BLK_TIPO_FLOPPY) return fat12_sync();
    return ata_flush(b->disco);
}
