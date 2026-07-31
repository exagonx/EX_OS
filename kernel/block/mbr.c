/* =============================================================================
 * kernel/block/mbr.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * =============================================================================
 *
 * Vedi kernel/include/mbr.h per l'elenco dei controlli e il razionale.
 * ============================================================================= */

#include "kernel.h"
#include "mbr.h"
#include "ata.h"
#include "syscall.h"    /* ERR() e i codici errno, per mbr_scrivi */

/* Voce della tabella, 16 byte, come sta sul disco.
 *
 * I campi CHS non vengono usati per calcolare nulla, e non e' pigrizia:
 * su qualunque disco oltre 8,4 GB sono aritmeticamente incapaci di
 * esprimere la posizione e vengono riempiti con il valore di saturazione
 * 0xFE 0xFF 0xFF. Gli unici campi affidabili sono lba_inizio e n_settori.
 * Usare i CHS "quando ci sono" e' una fonte classica di tabelle
 * incoerenti. */
typedef struct PACKED {
    uint8_t  attiva;        /* 0x80 avviabile, 0x00 no, altro = corrotta */
    uint8_t  chs_inizio[3];
    uint8_t  tipo;
    uint8_t  chs_fine[3];
    uint32_t lba_inizio;    /* relativo: assoluto per le primarie */
    uint32_t n_settori;
} VoceMbr;

#define MBR_OFF_TABELLA     446
#define MBR_OFF_FIRMA       510

/* Tipi che indicano una partizione ESTESA (contenitore di logiche) */
static int tipo_esteso(uint8_t t)
{
    return (t == 0x05)   /* estesa CHS */
        || (t == 0x0F)   /* estesa LBA */
        || (t == 0x85);  /* estesa Linux */
}

/* Legge un uint32 little-endian senza assumere l'allineamento: il buffer
 * arriva da disco e le voci non sono allineate a 4 byte dentro il
 * settore. Su x86 un accesso disallineato funziona, ma dipenderci e' una
 * cattiva abitudine che si paga altrove. */
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/* Scrive un uint32 little-endian senza assumere l'allineamento, per la
 * stessa ragione di le32(). */
static void metti32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* =============================================================================
 * I tre byte CHS di una voce.
 *
 * Nessuno li usa per calcolare qualcosa — il nostro MBR legge con INT 13h
 * AH=42h, cioe' in LBA (bootloader/mbr/mbr.asm) — ma vanno riempiti lo
 * stesso: fdisk, Windows e parecchi BIOS li LEGGONO, e una voce con CHS a
 * zero viene segnalata come tabella danneggiata da strumenti che invece
 * dovrebbero solo confermare quello che abbiamo scritto.
 *
 * Geometria fittizia 255 teste x 63 settori, che e' la convenzione
 * universale dal 1996 in poi. Oltre il cilindro 1023 il campo non ce la
 * fa aritmeticamente: si mette il valore di saturazione 0xFE 0xFF 0xFF
 * (testa 254, settore 63, cilindro 1023), lo stesso che scrive fdisk.
 * ============================================================================= */
#define CHS_MAX_LBA     (1023u * 255u * 63u)     /* 16450560, sta in 32 bit */

static void chs_metti(uint8_t *out, uint64_t lba)
{
    uint32_t l, c, h, s;

    if (lba >= (uint64_t)CHS_MAX_LBA) {
        out[0] = 0xFE; out[1] = 0xFF; out[2] = 0xFF;
        return;
    }

    /* Sotto la soglia il valore sta in 32 bit: la divisione resta a 32
     * bit, e il kernel non ha bisogno di __udivdi3. */
    l = (uint32_t)lba;
    s = (l % 63u) + 1u;
    h = (l / 63u) % 255u;
    c = l / (63u * 255u);

    out[0] = (uint8_t)h;
    out[1] = (uint8_t)(((c >> 2) & 0xC0) | (s & 0x3F));
    out[2] = (uint8_t)(c & 0xFF);
}

/* Aggiunge una partizione al risultato, se c'e' posto. */
static void aggiungi(TabellaPartizioni *t, uint8_t attiva, uint8_t tipo,
                     uint64_t inizio, uint64_t settori, int logica,
                     int numero)
{
    Partizione *p;

    if (t->n >= MBR_MAX_PART) {
        t->problemi |= PT_PROB_TRONCATA;
        return;
    }

    p = &t->p[t->n++];
    p->attiva  = attiva;
    p->tipo    = tipo;
    p->logica  = (uint8_t)logica;
    p->numero  = (uint8_t)numero;
    p->inizio  = inizio;
    p->settori = settori;
}

/* =============================================================================
 * Percorre la catena degli EBR delle partizioni logiche.
 *
 * COME E' FATTA, perche' e' la parte in cui e' piu' facile sbagliare.
 * Ogni EBR e' un settore con due voci utili:
 *   voce 0 = la partizione logica, con lba_inizio RELATIVO all'EBR stesso;
 *   voce 1 = il prossimo EBR, con lba_inizio RELATIVO all'INIZIO DELLA
 *            PARTIZIONE ESTESA, non all'EBR corrente.
 * I due riferimenti sono diversi, e confonderli produce una catena che
 * sembra funzionare sul primo elemento e sbaglia da li' in poi.
 *
 * PROTEZIONE DAL CICLO: un puntatore che rimanda a un EBR gia' visitato
 * rende la lista circolare. Senza difesa il kernel ci gira dentro per
 * sempre, cioe' si blocca a ogni avvio con quel disco collegato. Qui si
 * tengono gli LBA gia' visti e si esce segnalando l'anomalia.
 * ============================================================================= */
static void percorri_estesa(int indice, TabellaPartizioni *t,
                            uint64_t ext_inizio, uint64_t ext_settori,
                            uint64_t disco_settori)
{
    uint64_t visti[MBR_MAX_PART + 1];
    int      n_visti = 0;
    uint64_t ebr = ext_inizio;
    uint8_t  sett[512];
    int      giri;

    for (giri = 0; giri < MBR_MAX_PART + 1; giri++) {
        const uint8_t *v0, *v1;
        uint32_t r0_lba, r0_n, r1_lba, r1_n;
        uint8_t  t0, t1, a0;
        int      i;

        /* Il puntatore deve stare dentro il disco E dentro l'estesa. */
        if (ebr >= disco_settori ||
            ebr <  ext_inizio    ||
            ebr >= ext_inizio + ext_settori) {
            t->problemi |= PT_PROB_CATENA;
            return;
        }

        for (i = 0; i < n_visti; i++) {
            if (visti[i] == ebr) {           /* gia' passato di qui */
                t->problemi |= PT_PROB_CATENA;
                return;
            }
        }
        visti[n_visti++] = ebr;

        if (ata_read(indice, ebr, 1, sett) != 0) {
            t->problemi |= PT_PROB_CATENA;
            return;
        }

        if (sett[MBR_OFF_FIRMA] != 0x55 || sett[MBR_OFF_FIRMA + 1] != 0xAA) {
            t->problemi |= PT_PROB_CATENA;
            return;
        }

        v0 = sett + MBR_OFF_TABELLA;
        v1 = sett + MBR_OFF_TABELLA + 16;

        a0     = v0[0];
        t0     = v0[4];
        r0_lba = le32(v0 + 8);
        r0_n   = le32(v0 + 12);

        t1     = v1[4];
        r1_lba = le32(v1 + 8);
        r1_n   = le32(v1 + 12);
        (void)r1_n;

        if (t0 != 0x00 && r0_n > 0) {
            /* RELATIVO ALL'EBR CORRENTE */
            aggiungi(t, a0, t0, ebr + (uint64_t)r0_lba, (uint64_t)r0_n, 1,
                     5 + giri);   /* le logiche partono da 5 */
        } else if (t0 != 0x00) {
            t->problemi |= PT_PROB_VUOTA;
        }

        if (t1 == 0x00 || r1_lba == 0) return;   /* fine della catena */

        if (!tipo_esteso(t1)) {
            /* La seconda voce, se presente, deve essere un puntatore a
             * un altro EBR: qualunque altro tipo indica una tabella
             * malformata, non una partizione in piu'. */
            t->problemi |= PT_PROB_CATENA;
            return;
        }

        /* RELATIVO ALL'INIZIO DELL'ESTESA, non all'EBR corrente. */
        ebr = ext_inizio + (uint64_t)r1_lba;
    }

    /* Usciti per numero massimo di giri: la catena e' piu' lunga di
     * quanto sia ragionevole, oppure e' circolare in un modo che
     * l'elenco dei visitati non ha colto. */
    t->problemi |= PT_PROB_CATENA;
}

/* =============================================================================
 * mbr_valida — la critica di un INSIEME di partizioni.
 *
 * Sta in una funzione a se' perche' ha DUE chiamanti che devono
 * comportarsi allo stesso modo: mbr_leggi() su cio' che trova sul disco e
 * mbr_scrivi() su cio' che gli viene proposto. Se fossero due elenchi di
 * controlli distinti, prima o poi divergerebbero, e il giorno in cui
 * divergono la scrittura accetta una tabella che la lettura segnala come
 * rotta — cioe' il partizionatore produce dischi che il sistema stesso
 * critica.
 *
 * Ritorna la maschera PT_PROB_*, 0 se non ha nulla da ridire.
 * ============================================================================= */
uint32_t mbr_valida(const Partizione *p, int n, uint64_t disco_settori)
{
    uint32_t pb = 0;
    int      i, j, n_estese = 0;

    if (p == NULL || n <= 0) return 0;

    for (i = 0; i < n; i++) {
        const Partizione *a = &p[i];

        if (a->tipo == 0x00) continue;         /* voce libera */

        if (a->attiva != 0x00 && a->attiva != 0x80) pb |= PT_PROB_BOOTFLAG;

        if (a->settori == 0) { pb |= PT_PROB_VUOTA; continue; }

        /* Una partizione che comincia dall'LBA 0 contiene il settore che
         * la descrive. Non e' un caso limite teorico: e' cio' che
         * succede formattando "tutto il disco" senza allineamento, e il
         * primo filesystem che ci scrive dentro cancella la tabella. */
        if (a->inizio == 0) pb |= PT_PROB_SETTORE0;

        /* L'overflow va controllato PRIMA della somma: inizio + settori
         * puo' traboccare e far sembrare valida una voce assurda. */
        if (a->inizio >= disco_settori ||
            a->inizio + a->settori > disco_settori ||
            a->inizio + a->settori < a->inizio) {
            pb |= PT_PROB_OLTRE_FINE;
        }

        /* L'MBR esprime inizio e lunghezza in 32 bit: oltre i 2 TiB una
         * voce non e' rappresentabile. In LETTURA non puo' succedere —
         * i campi sul disco SONO larghi 32 bit — quindi questo controllo
         * esiste per la scrittura, dove l'alternativa e' troncare il
         * numero in silenzio e produrre una partizione che comincia da
         * tutt'altra parte.
         *
         * Il confronto e' sull'ULTIMO SETTORE (inizio + settori - 1 deve
         * stare in 32 bit), non sulla somma: una partizione che arriva
         * esattamente all'ultimo settore esprimibile e' legittima. */
        if (a->inizio + a->settori > 0x100000000ull) {
            klog(LOG_ERROR, "MBR: partizione oltre i 2 TiB: non esprimibile "
                            "in una tabella MBR");
            pb |= PT_PROB_OLTRE_FINE;
        }

        if (!a->logica && tipo_esteso(a->tipo)) {
            n_estese++;
            /* Piu' di una estesa fra le primarie e' fuori specifica:
             * quale delle due contiene le logiche e' indeterminato. */
            if (n_estese > 1) pb |= PT_PROB_TROPPE_EXT;
        }

        for (j = i + 1; j < n; j++) {
            const Partizione *b = &p[j];
            uint64_t a_fine, b_fine;

            if (b->tipo == 0x00 || b->settori == 0) continue;

            /* Un'estesa CONTIENE le proprie logiche: quella non e' una
             * sovrapposizione ma la struttura normale del formato, e
             * segnalarla sarebbe un falso allarme a ogni disco con
             * partizioni logiche. */
            if (tipo_esteso(a->tipo) && b->logica) continue;
            if (tipo_esteso(b->tipo) && a->logica) continue;

            a_fine = a->inizio + a->settori;
            b_fine = b->inizio + b->settori;

            if (a->inizio < b_fine && b->inizio < a_fine) pb |= PT_PROB_SOVRAPP;
        }
    }

    return pb;
}

int mbr_leggi(int indice, TabellaPartizioni *out)
{
    const AtaDevice *d;
    uint8_t   sett[512];
    uint64_t  disco;
    uint64_t  ext_inizio = 0, ext_settori = 0;
    int       n_estese = 0;
    int       i;

    out->schema   = PT_SCHEMA_NESSUNO;
    out->problemi = 0;
    out->n        = 0;

    d = ata_get_device(indice);
    if (d == NULL || !d->presente || d->tipo != ATA_TYPE_ATA) return -1;
    disco = d->settori;

    if (ata_read(indice, 0, 1, sett) != 0) return -1;

    if (sett[MBR_OFF_FIRMA] != 0x55 || sett[MBR_OFF_FIRMA + 1] != 0xAA) {
        out->problemi |= PT_PROB_FIRMA;
        return 0;    /* letto, ma non e' un MBR */
    }

    /* MBR PROTETTIVO = disco GPT. Va riconosciuto PRIMA di leggere le
     * voci come partizioni vere: un disco GPT ha una sola voce di tipo
     * 0xEE che copre l'intero disco, e trattarla come una partizione
     * normale — peggio, riscriverla — distrugge la mappa GPT. */
    for (i = 0; i < 4; i++) {
        if (sett[MBR_OFF_TABELLA + i * 16 + 4] == 0xEE) {
            out->schema    = PT_SCHEMA_GPT;
            out->problemi |= PT_PROB_GPT;
            return 0;
        }
    }

    out->schema = PT_SCHEMA_MBR;

    for (i = 0; i < 4; i++) {
        const uint8_t *v = sett + MBR_OFF_TABELLA + i * 16;
        uint8_t   attiva = v[0];
        uint8_t   tipo   = v[4];
        uint32_t  lba    = le32(v + 8);
        uint32_t  n      = le32(v + 12);

        if (tipo == 0x00) continue;           /* voce libera */

        if (attiva != 0x00 && attiva != 0x80) {
            out->problemi |= PT_PROB_BOOTFLAG;
        }

        if (n == 0) {
            out->problemi |= PT_PROB_VUOTA;
            continue;
        }

        if (tipo_esteso(tipo)) {
            n_estese++;
            if (n_estese > 1) {
                /* Piu' di una estesa fra le primarie e' fuori specifica:
                 * quale delle due contiene le logiche e' indeterminato. */
                out->problemi |= PT_PROB_TROPPE_EXT;
            } else {
                ext_inizio  = (uint64_t)lba;
                ext_settori = (uint64_t)n;
            }
            /* L'estesa viene comunque elencata: e' un contenitore, e
             * nasconderla renderebbe incomprensibile la mappa del disco. */
        }

        aggiungi(out, attiva, tipo, (uint64_t)lba, (uint64_t)n, 0, i + 1);
    }

    if (n_estese == 1 && ext_settori > 0) {
        percorri_estesa(indice, out, ext_inizio, ext_settori, disco);
    }

    /* -------------------------------------------------------------------
     * Controlli sull'insieme, non piu' sulla singola voce.
     *
     * I controlli qui sopra, dentro il ciclo di lettura, NON sono un
     * doppione di questi: colgono le voci che nell'array non ci
     * arrivano nemmeno — quelle con settori == 0, che vengono saltate.
     * Una voce di tipo 0x05 e dimensione zero e' una estesa a tutti gli
     * effetti per chi legge la tabella, ma per mbr_valida() non esiste.
     * ------------------------------------------------------------------- */
    out->problemi |= mbr_valida(out->p, out->n, disco);

    return 0;
}

/* =============================================================================
 * Scrittura. Il contratto e le tre garanzie stanno in kernel/include/mbr.h.
 * ============================================================================= */
int mbr_scrivi(int indice, const Partizione *voci, int n, uint32_t *problemi)
{
    const AtaDevice  *d;
    TabellaPartizioni attuale;
    uint8_t           sett[512];
    uint32_t          pb;
    int               i, firma_ok;

    if (problemi) *problemi = 0;
    if (voci == NULL || n < 0 || n > 4) return ERR(EINVAL);

    d = ata_get_device(indice);
    if (d == NULL || !d->presente || d->tipo != ATA_TYPE_ATA) return ERR(ENODEV);

    /* (2) La proposta passa dagli STESSI controlli della lettura. Prima
     * di qualunque I/O: una tabella rifiutata non deve aver toccato il
     * disco nemmeno per sbaglio. */
    pb = mbr_valida(voci, n, d->settori);
    if (pb != 0) {
        if (problemi) *problemi = pb;
        klog(LOG_ERROR, "MBR: hd%d: tabella proposta rifiutata (problemi 0x%x)",
             indice, pb);
        return ERR(EINVAL);
    }

    if (mbr_leggi(indice, &attuale) != 0) return ERR(EIO);

    /* (3) Un disco GPT ha una tabella MBR protettiva: riscriverla e' il
     * modo classico di rendere irraggiungibile un disco intero. */
    if (attuale.schema == PT_SCHEMA_GPT) {
        if (problemi) *problemi = PT_PROB_GPT;
        klog(LOG_ERROR, "MBR: hd%d e' GPT: la tabella MBR non si tocca", indice);
        return ERR(EPERM);
    }

    /* --- Le logiche esistenti ---------------------------------------
     * Questa funzione non scrive EBR. Se sul disco ci sono partizioni
     * logiche, la voce estesa che le contiene deve ricomparire IDENTICA
     * nella proposta: spostarla, rimpicciolirla o cancellarla lascerebbe
     * una catena di EBR viva sul disco ma irraggiungibile, e nessun
     * messaggio direbbe all'utente che quei dati ci sono ancora. */
    {
        const Partizione *est = NULL;
        int ha_logiche = 0, j;

        for (i = 0; i < attuale.n; i++) {
            if (attuale.p[i].logica) ha_logiche = 1;
            else if (tipo_esteso(attuale.p[i].tipo)) est = &attuale.p[i];
        }

        if (ha_logiche) {
            for (j = 0; j < n; j++) {
                if (est != NULL &&
                    voci[j].tipo    == est->tipo &&
                    voci[j].inizio  == est->inizio &&
                    voci[j].settori == est->settori) break;
            }
            if (est == NULL || j >= n) {
                klog(LOG_ERROR, "MBR: hd%d ha partizioni logiche: l'estesa che le "
                                "contiene non puo' essere spostata o rimossa", indice);
                return ERR(EBUSY);
            }
        }
    }

    if (ata_read(indice, 0, 1, sett) != 0) return ERR(EIO);

    firma_ok = (sett[MBR_OFF_FIRMA] == 0x55 && sett[MBR_OFF_FIRMA + 1] == 0xAA);

    /* (1) I 446 byte del codice di avvio restano quelli che erano —
     * tranne quando la firma non c'era. Aggiungerla significa dire al
     * BIOS "esegui questi byte", e fino a un attimo prima nessuno li
     * eseguiva: lasciarli com'erano vorrebbe dire far saltare la
     * macchina dentro il contenuto casuale di un disco mai inizializzato. */
    if (!firma_ok) {
        for (i = 0; i < MBR_OFF_TABELLA; i++) sett[i] = 0;
        klog(LOG_INFO, "MBR: hd%d non aveva firma 0x55AA: area di avvio azzerata",
             indice);
    }

    /* Le quattro voci si compongono TUTTE, anche quelle libere: una voce
     * lasciata com'era mentre le altre cambiano e' il modo piu' semplice
     * di produrre una sovrapposizione che la validazione non ha visto,
     * perche' non le era stata mostrata. */
    for (i = 0; i < 4; i++) {
        uint8_t *v = sett + MBR_OFF_TABELLA + i * 16;
        int      k;

        for (k = 0; k < 16; k++) v[k] = 0;

        if (i >= n || voci[i].tipo == 0x00 || voci[i].settori == 0) continue;

        v[0] = voci[i].attiva;
        v[4] = voci[i].tipo;
        chs_metti(v + 1, voci[i].inizio);
        chs_metti(v + 5, voci[i].inizio + voci[i].settori - 1);
        metti32(v +  8, (uint32_t)voci[i].inizio);
        metti32(v + 12, (uint32_t)voci[i].settori);
    }

    sett[MBR_OFF_FIRMA]     = 0x55;
    sett[MBR_OFF_FIRMA + 1] = 0xAA;

    if (ata_write(indice, 0, 1, sett) != 0) return ERR(EIO);
    ata_flush(indice);

    klog(LOG_INFO, "MBR: hd%d: tabella delle partizioni riscritta", indice);
    return 0;
}
