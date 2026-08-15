/* =============================================================================
 * kernel/block/atapi.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Vedi kernel/include/atapi.h per che cos'e' un dispositivo ATAPI e per le
 * tre differenze che contano rispetto a un disco. Qui sotto ci sono le
 * trappole del protocollo a pacchetto.
 *
 * IL CICLO DELLA FASE DATI, che e' il punto in cui quasi tutte le
 * implementazioni si bloccano o corrompono il bus:
 *
 *   Un dispositivo ATAPI NON dice in anticipo quanti byte manda. Consegna
 *   una raffica per volta, e prima di ogni raffica scrive in LBA1/LBA2
 *   quanti byte contiene QUELLA. Ne discendono tre regole:
 *
 *     - non si puo' contare "quanti DRQ mi aspetto": si cicla finche' il
 *       dispositivo abbassa DRQ con BSY basso, e quello e' il fine
 *       trasferimento;
 *     - il conteggio di ogni raffica va RILETTO ogni volta. Un lettore
 *       puo' spezzare 2048 byte in due raffiche da 1024 e ha ragione lui:
 *       il limite lo abbiamo chiesto noi scrivendo LBA1/LBA2 prima del
 *       comando;
 *     - i byte che eccedono il buffer del chiamante vanno letti e
 *       BUTTATI, non lasciati li'. Interrompere la lettura a meta' di una
 *       raffica lascia il dispositivo in attesa e il canale inutilizzabile
 *       per chiunque altro, compreso il disco rigido che ci sta accanto.
 *
 * PERCHE' NON SI USA IL CONTEGGIO DI SETTORI COME SU ATA. Su un disco si
 * sa che ogni DRQ vale esattamente un settore. Qui no, ed e' il motivo per
 * cui questo file non e' una variante di ata_rw() con un comando diverso.
 *
 * TUTTE LE ATTESE SONO IN TEMPO REALE (ata_attesa_ms, che usa il PIT). Un
 * lettore ottico e' l'hardware piu' lento del sistema: uno spin-up dopo
 * l'inserimento di un disco puo' durare parecchi secondi, e un timeout
 * tarato sul disco rigido si presenterebbe come "supporto assente" su un
 * disco perfettamente valido.
 * ============================================================================= */

#include "kernel.h"
#include "ata.h"
#include "atapi.h"

/* Un pacchetto ATAPI e' di 12 byte. Ne esistono anche da 16, ma li
 * chiedono solo dispositivi che dichiarano di volerli in IDENTIFY PACKET:
 * i lettori CD/DVD usano i 12. */
#define CDB_LEN             12

/* Comandi SCSI usati (il pacchetto E' un comando SCSI). */
#define CMD_TEST_UNIT_READY 0x00
#define CMD_REQUEST_SENSE   0x03
#define CMD_START_STOP      0x1B
#define CMD_READ_CAPACITY   0x25
#define CMD_READ10          0x28

#define ATA_CMD_PACKET      0xA0

/* Chiavi di SENSE che questo driver distingue. Il resto e' "errore". */
#define SK_NO_SENSE         0x00
#define SK_NOT_READY        0x02
#define SK_MEDIUM_ERROR     0x03
#define SK_ILLEGAL_REQUEST  0x05
#define SK_UNIT_ATTENTION   0x06

#define ASC_NOT_READY       0x04    /* + ASCQ 1: sta prendendo giri */
#define ASC_MEDIA_CHANGED   0x28
#define ASC_MEDIUM_ASSENTE  0x3A

/* Esiti interni di atapi_comando(). */
#define AT_OK               0
#define AT_ERRORE          -1   /* bus muto, timeout, dispositivo sbagliato */
#define AT_CHECK           -2   /* CHECK CONDITION: il motivo sta nella SENSE */

/* Scadenze reali, in millisecondi. */
#define TMO_PACCHETTO_MS    5000    /* attesa che il lettore chieda il CDB  */
#define TMO_DATI_MS        10000    /* attesa di una raffica di dati        */
#define TMO_PRONTO_MS       8000    /* quanto si aspetta uno spin-up        */

/* Tetto al numero di raffiche di una singola fase dati. Un dispositivo
 * guasto che alzasse DRQ all'infinito bloccherebbe il kernel: qui il
 * ciclo finisce comunque, e con un errore invece che con un sistema
 * fermo. 2048 raffiche coprono 4 MB anche nel caso pessimo di 2 KB per
 * raffica, cioe' molto piu' di qualunque lettura che facciamo. */
#define MAX_RAFFICHE        2048

/* Lo stato di ogni lettore. Indicizzato come l'array di ata.c: l'indice
 * di un dispositivo e' lo stesso in tutto il kernel, e tradurlo avanti e
 * indietro sarebbe l'occasione per sbagliarlo. */
typedef struct {
    uint8_t  lettore;       /* 1 = questo slot ATA e' un ATAPI */
    uint8_t  supporto;      /* ultimo esito noto di atapi_supporto() */
    uint32_t blocchi;       /* capacita' dell'ultimo supporto visto */
    uint32_t dim_blocco;
} StatoLettore;

static StatoLettore g_st[ATA_MAX_DEVICES];
static int          g_n = 0;

/* =============================================================================
 * Il comando a pacchetto
 *
 * `buf`/`max` descrivono lo spazio del chiamante, che puo' essere piu'
 * piccolo di cio' che il dispositivo manda: l'eccesso viene letto e
 * buttato (vedi la terza regola in testa al file). `letti` riceve i byte
 * effettivamente CONSEGNATI al chiamante, mai piu' di `max`.
 * ============================================================================= */
static int atapi_comando(int indice, const uint8_t *cdb, void *buf,
                         uint32_t max, uint32_t *letti, uint32_t timeout_ms)
{
    const AtaDevice *d = ata_get_device(indice);
    uint8_t         *p = (uint8_t *)buf;
    uint16_t         io;
    uint32_t         tot = 0, giri = 0, limite;
    int              canale, unita, i, r;
    uint8_t          st = 0;

    if (letti) *letti = 0;

    if (d == NULL || !d->presente || d->tipo != ATA_TYPE_ATAPI) return AT_ERRORE;

    canale = d->canale;
    unita  = d->unita;
    io     = ata_base_io(canale);

    /* Il limite per raffica lo decidiamo NOI, e va scritto prima del
     * comando. Un valore dispari o nullo e' vietato dalla specifica: zero
     * significherebbe "65536" e un dispari spezzerebbe le parole a 16 bit
     * con cui i dati si leggono. */
    limite = (max > 0xFFFEu) ? 0xFFFEu : max;
    if (limite < ATAPI_DIM_BLOCCO) limite = ATAPI_DIM_BLOCCO;
    limite &= ~1u;

    /* Polling, come in ata.c: niente IRQ14/15 da coordinare. */
    port_outb(ata_base_ctrl(canale), ATA_CTRL_NIEN);

    ata_seleziona(canale, unita, 0);
    if (ata_attendi_non_bsy(canale, TMO_PACCHETTO_MS) < 0) return AT_ERRORE;

    port_outb(io + ATA_REG_FEATURES, 0);    /* PIO: niente DMA, niente overlap */
    port_outb(io + ATA_REG_SECCOUNT, 0);
    port_outb(io + ATA_REG_LBA0,     0);
    port_outb(io + ATA_REG_LBA1, (uint8_t)(limite & 0xFF));
    port_outb(io + ATA_REG_LBA2, (uint8_t)((limite >> 8) & 0xFF));

    port_outb(io + ATA_REG_COMMAND, ATA_CMD_PACKET);
    ata_ritardo(canale);

    /* Il dispositivo alza DRQ per CHIEDERE il pacchetto. Un ERR gia' qui
     * significa che ha rifiutato il comando prima ancora di leggerlo. */
    r = ata_attendi_drq_muto(canale, TMO_PACCHETTO_MS, &st);
    if (r == -2) return AT_CHECK;
    if (r <  0)  return AT_ERRORE;

    for (i = 0; i < CDB_LEN; i += 2) {
        port_outw(io + ATA_REG_DATA,
                  (uint16_t)((uint16_t)cdb[i] | ((uint16_t)cdb[i + 1] << 8)));
    }

    /* --- Fase dati: si cicla finche' il dispositivo abbassa DRQ --- */
    for (;;) {
        uint32_t n;
        int      stato;

        if (++giri > MAX_RAFFICHE) {
            klog(LOG_ERROR, "ATAPI: il dispositivo non smette di consegnare dati");
            return AT_ERRORE;
        }

        ata_ritardo(canale);

        stato = ata_attendi_non_bsy(canale, timeout_ms);
        if (stato < 0) return AT_ERRORE;
        st = (uint8_t)stato;

        if (st & (ATA_SR_ERR | ATA_SR_DF)) return AT_CHECK;

        /* DRQ basso con BSY basso: il comando e' finito. E' l'UNICA
         * condizione di uscita normale — vedi la prima regola in testa al
         * file. */
        if (!(st & ATA_SR_DRQ)) break;

        n = (uint32_t)port_inb(io + ATA_REG_LBA1)
          | ((uint32_t)port_inb(io + ATA_REG_LBA2) << 8);

        /* Una raffica dichiarata di zero byte con DRQ alto non e' prevista
         * dalla specifica: uscire e' l'unica mossa che non diventa un
         * ciclo infinito. */
        if (n == 0) break;

        /* ! LA VIA VELOCE VALE SOLO SE LA RAFFICA CI STA TUTTA. `rep insw`
         * scrive e basta: non sa saltare i byte in eccesso. Quando la
         * raffica supera il buffer si torna al ciclo, che legge tutto e
         * butta il di piu' — il trasferimento va SEMPRE consumato per
         * intero, o il canale resta inutilizzabile.
         *
         * Il caso normale e' il primo, ed e' quello che conta: e' da qui
         * che passa ogni byte letto dal CD, compresi i 34 MB di cc1. */
        if ((uint32_t)(n & ~1u) > 0 && tot + (uint32_t)(n & ~1u) <= max) {
            port_insw(io + ATA_REG_DATA, p + tot, (uint32_t)(n & ~1u) / 2);
            tot += (uint32_t)(n & ~1u);
            i = (int)(n & ~1u);
        } else {
            i = 0;
        }

        for (; (uint32_t)i < n; i += 2) {
            uint16_t w = port_inw(io + ATA_REG_DATA);

            if (tot     < max) p[tot]     = (uint8_t)(w & 0xFF);
            if (tot + 1 < max) p[tot + 1] = (uint8_t)((w >> 8) & 0xFF);
            tot += 2;
        }
    }

    if (letti) *letti = (tot > max) ? max : tot;
    return AT_OK;
}

/* =============================================================================
 * REQUEST SENSE — perche' il comando precedente e' fallito
 *
 * Il bit ERR dice solo "CHECK CONDITION". Senza questo secondo comando,
 * "non c'e' il disco" e "il disco e' rotto" sono indistinguibili — e il
 * primo dei due non e' un errore.
 * ============================================================================= */
static int atapi_sense(int indice, uint8_t *chiave, uint8_t *asc, uint8_t *ascq)
{
    uint8_t  cdb[CDB_LEN];
    uint8_t  dati[18];
    uint32_t letti = 0;
    int      i;

    for (i = 0; i < CDB_LEN;    i++) cdb[i]  = 0;
    for (i = 0; i < 18;         i++) dati[i] = 0;

    cdb[0] = CMD_REQUEST_SENSE;
    cdb[4] = 18;

    /* Un CHECK CONDITION sulla REQUEST SENSE stessa non si insegue: si
     * dichiara di non sapere. Rincorrerlo sarebbe una ricorsione senza
     * fondo su un dispositivo che gia' non risponde come dovrebbe. */
    if (atapi_comando(indice, cdb, dati, sizeof(dati), &letti,
                      TMO_DATI_MS) != AT_OK) return -1;
    if (letti < 14) return -1;

    if (chiave) *chiave = (uint8_t)(dati[2] & 0x0F);
    if (asc)    *asc    = dati[12];
    if (ascq)   *ascq   = dati[13];
    return 0;
}

/* =============================================================================
 * Presenza del supporto
 * ============================================================================= */
int atapi_supporto(int indice)
{
    uint8_t cdb[CDB_LEN];
    int     i, tentativo;
    int     chiuso = 0;         /* il vassoio l'abbiamo gia' chiuso noi? */
    int     assenti = 0;        /* quante volte ha detto "nessun supporto" */

    if (!atapi_e_lettore(indice)) return -1;

    for (i = 0; i < CDB_LEN; i++) cdb[i] = 0;
    cdb[0] = CMD_TEST_UNIT_READY;

    /* Otto tentativi, con le attese decise caso per caso qui sotto,
     * coprono i quattro modi in cui un lettore dice "non ora" senza che
     * ci sia niente di sbagliato: l'UNIT ATTENTION del primo accesso dopo
     * un cambio di supporto (che si consuma leggendola), il vassoio
     * aperto, il disco che sta ancora prendendo giri, e il lettore appena
     * rifornito che risponde ancora con lo stato di un attimo prima. */
    for (tentativo = 0; tentativo < 8; tentativo++) {
        uint8_t k = 0, asc = 0, ascq = 0;
        int     r = atapi_comando(indice, cdb, NULL, 0, NULL, TMO_PRONTO_MS);

        if (r == AT_OK) {
            g_st[indice].supporto = 1;
            return 1;
        }
        if (r == AT_ERRORE) {
            g_st[indice].supporto = 0;
            return -1;
        }

        if (atapi_sense(indice, &k, &asc, &ascq) != 0) {
            g_st[indice].supporto = 0;
            return -1;
        }

        /* Il supporto e' cambiato: la notifica si consuma leggendola, e il
         * tentativo successivo dice come stanno le cose ADESSO. La
         * capacita' nota da prima non vale piu'. */
        if (k == SK_UNIT_ATTENTION) {
            g_st[indice].blocchi = 0;
            continue;
        }

        if (k == SK_NOT_READY) {
            /* "Sto diventando pronto": e' un'attesa, non un no. */
            if (asc == ASC_NOT_READY) {
                ata_attesa_ms(500);
                continue;
            }
            /* "Supporto assente" — che pero' vuol dire DUE cose, e la
             * differenza e' l'unico punto di questa funzione che non si
             * indovina leggendo la specifica.
             *
             * Un vassoio APERTO risponde "nessun supporto" anche quando
             * dentro c'e' un disco: finche' non e' chiuso, il lettore non
             * lo ha nemmeno guardato. Sulle macchine vere il vassoio lo
             * chiude la persona che infila il disco; in emulazione no —
             * QEMU inserisce l'immagine lasciando il vassoio aperto, e un
             * driver che si fermasse qui non vedrebbe MAI un disco
             * inserito a sistema avviato.
             *
             * Alcuni lettori distinguono i due casi con ASCQ 0x02 ("tray
             * open"), ma non tutti — QEMU risponde 0x00 in entrambi — e
             * fidarsi di un campo facoltativo per decidere significa
             * funzionare su un lettore su due.
             *
             * Quindi si CHIUDE il vassoio, una volta sola, e si richiede.
             * E' quello che fa Linux quando si apre un lettore, e la
             * conseguenza va detta: montare un CD su un lettore lasciato
             * aperto e vuoto lo chiude. Meglio questo che un disco
             * inserito che il sistema non vede. */
            if (asc == ASC_MEDIUM_ASSENTE) {
                if (!chiuso) {
                    chiuso = 1;
                    atapi_vassoio(indice, 0);
                    ata_attesa_ms(250);
                    continue;
                }

                /* Un lettore appena rifornito risponde "assente" ancora
                 * per un comando o due prima di ammettere il cambio: la
                 * prima risposta e' quella di un attimo fa. Si insiste
                 * qualche volta prima di crederci — e' il motivo per cui
                 * "vassoio vuoto" costa qualche decimo di secondo invece
                 * di essere immediato. */
                if (++assenti < 3) {
                    ata_attesa_ms(250);
                    continue;
                }

                g_st[indice].supporto = 0;
                g_st[indice].blocchi  = 0;
                return 0;
            }
        }

        /* Qualunque altra cosa: non c'e' un supporto usabile, e vale la
         * pena dire quale codice l'ha detto — su un lettore vero e' l'unico
         * appiglio per capire cosa sta succedendo. */
        klog(LOG_WARN, "ATAPI: unita' non pronta (sense=%u asc=0x%02x ascq=0x%02x)",
             k, asc, ascq);
        g_st[indice].supporto = 0;
        g_st[indice].blocchi  = 0;
        return 0;
    }

    /* Otto tentativi e ancora "aspetta": si riporta assente invece di
     * restare qui. Chi chiama puo' riprovare; il kernel no, deve avviarsi. */
    g_st[indice].supporto = 0;
    return 0;
}

/* =============================================================================
 * Capacita' del supporto attuale
 * ============================================================================= */
int atapi_capacita(int indice, uint32_t *blocchi, uint32_t *dim_blocco)
{
    uint8_t  cdb[CDB_LEN];
    uint8_t  dati[8];
    uint32_t letti = 0, ultimo, dim;
    int      i, r;

    if (!atapi_e_lettore(indice)) return -1;

    for (i = 0; i < CDB_LEN; i++) cdb[i]  = 0;
    for (i = 0; i < 8;       i++) dati[i] = 0;
    cdb[0] = CMD_READ_CAPACITY;

    r = atapi_comando(indice, cdb, dati, sizeof(dati), &letti, TMO_DATI_MS);

    /* Un CHECK qui e' quasi sempre "il disco e' cambiato" oppure "non c'e'
     * un disco": si passa da atapi_supporto(), che sa distinguerli e
     * consumare l'UNIT ATTENTION, e si riprova una volta sola. */
    if (r == AT_CHECK) {
        if (atapi_supporto(indice) != 1) return -1;
        r = atapi_comando(indice, cdb, dati, sizeof(dati), &letti, TMO_DATI_MS);
    }

    if (r != AT_OK || letti < 8) return -1;

    /* I campi sono BIG endian: e' SCSI, non x86. Leggerli come little
     * darebbe capacita' assurde invece di un errore. */
    ultimo = ((uint32_t)dati[0] << 24) | ((uint32_t)dati[1] << 16)
           | ((uint32_t)dati[2] << 8)  |  (uint32_t)dati[3];
    dim    = ((uint32_t)dati[4] << 24) | ((uint32_t)dati[5] << 16)
           | ((uint32_t)dati[6] << 8)  |  (uint32_t)dati[7];

    /* READ CAPACITY ritorna l'INDIRIZZO dell'ultimo blocco, non quanti ce
     * ne sono: il +1 non e' un arrotondamento. Un ultimo blocco 0xFFFFFFFF
     * e' il valore che i dispositivi usano per dire "non lo so", e
     * sommarci 1 darebbe zero. */
    if (ultimo == 0xFFFFFFFFu) return -1;

    /* Un blocco che non e' da 2048 byte non e' un CD dati: rifiutare e'
     * meglio che leggerlo con la geometria sbagliata, che non darebbe
     * errore ma dati presi dal posto sbagliato. */
    if (dim != ATAPI_DIM_BLOCCO) {
        klog(LOG_WARN, "ATAPI: blocchi da %u byte: supporto non gestito", dim);
        return -1;
    }

    g_st[indice].blocchi    = ultimo + 1u;
    g_st[indice].dim_blocco = dim;

    if (blocchi)    *blocchi    = ultimo + 1u;
    if (dim_blocco) *dim_blocco = dim;
    return 0;
}

/* =============================================================================
 * Lettura
 * ============================================================================= */
int atapi_read(int indice, uint32_t lba, uint32_t n, void *buf)
{
    uint8_t  *p = (uint8_t *)buf;
    uint8_t   cdb[CDB_LEN];
    int       i;

    if (!atapi_e_lettore(indice)) return -1;
    if (n == 0 || buf == NULL)    return -1;

    /* La capacita' si controlla se la si conosce. Se non la si conosce
     * (supporto appena inserito) NON si inventa un limite: si lascia
     * rispondere il dispositivo, che sa. */
    if (g_st[indice].blocchi != 0) {
        if (lba + n > g_st[indice].blocchi || lba + n < lba) {
            klog(LOG_ERROR, "ATAPI: lettura fuori supporto (lba=%u+%u di %u)",
                 lba, n, g_st[indice].blocchi);
            return -1;
        }
    }

    while (n > 0) {
        /* 16 blocchi (32 KB) per comando. Il limite non e' del protocollo
         * ma nostro: e' quanto si accetta di stare dentro un solo comando
         * in polling, con gli interrupt abilitati ma senza cedere la CPU. */
        uint32_t blocco = (n > 16u) ? 16u : n;
        uint32_t letti  = 0;
        int      r, riprovato = 0;

        for (i = 0; i < CDB_LEN; i++) cdb[i] = 0;

        cdb[0] = CMD_READ10;
        cdb[2] = (uint8_t)((lba >> 24) & 0xFF);   /* LBA, big endian */
        cdb[3] = (uint8_t)((lba >> 16) & 0xFF);
        cdb[4] = (uint8_t)((lba >>  8) & 0xFF);
        cdb[5] = (uint8_t)( lba        & 0xFF);
        cdb[7] = (uint8_t)((blocco >> 8) & 0xFF); /* quanti blocchi */
        cdb[8] = (uint8_t)( blocco       & 0xFF);

        for (;;) {
            r = atapi_comando(indice, cdb, p, blocco * ATAPI_DIM_BLOCCO,
                              &letti, TMO_DATI_MS);

            /* Un CHECK al primo colpo dopo un cambio di supporto e'
             * l'UNIT ATTENTION, che si consuma leggendola: si riprova UNA
             * volta. Riprovare all'infinito trasformerebbe un disco
             * illeggibile in un blocco del sistema. */
            if (r == AT_CHECK && !riprovato) {
                uint8_t k = 0, asc = 0, ascq = 0;

                riprovato = 1;
                if (atapi_sense(indice, &k, &asc, &ascq) == 0 &&
                    (k == SK_UNIT_ATTENTION || k == SK_NO_SENSE)) {
                    g_st[indice].blocchi = 0;   /* il disco puo' essere un altro */
                    continue;
                }
                klog(LOG_ERROR, "ATAPI: lettura fallita a lba=%u "
                                "(sense=%u asc=0x%02x ascq=0x%02x)",
                     lba, k, asc, ascq);
                return -1;
            }
            break;
        }

        if (r != AT_OK) {
            klog(LOG_ERROR, "ATAPI: lettura fallita a lba=%u", lba);
            return -1;
        }

        /* Meno byte del richiesto significa che il supporto finisce prima:
         * completare con zeri darebbe un file pieno di buchi silenziosi. */
        if (letti < blocco * ATAPI_DIM_BLOCCO) {
            klog(LOG_ERROR, "ATAPI: lba=%u ha reso %u byte invece di %u",
                 lba, letti, blocco * ATAPI_DIM_BLOCCO);
            return -1;
        }

        p   += blocco * ATAPI_DIM_BLOCCO;
        lba += blocco;
        n   -= blocco;
    }

    return 0;
}

/* =============================================================================
 * Vassoio
 * ============================================================================= */
int atapi_vassoio(int indice, int apri)
{
    uint8_t cdb[CDB_LEN];
    int     i;

    if (!atapi_e_lettore(indice)) return -1;

    for (i = 0; i < CDB_LEN; i++) cdb[i] = 0;
    cdb[0] = CMD_START_STOP;
    cdb[4] = (uint8_t)(apri ? 0x02 : 0x03);   /* LoEj + Start */

    if (atapi_comando(indice, cdb, NULL, 0, NULL, TMO_PRONTO_MS) != AT_OK)
        return -1;

    /* Aprendo il vassoio la capacita' nota non vale piu': lasciarla in
     * memoria significherebbe accettare letture su un supporto che non
     * c'e' piu'. */
    g_st[indice].blocchi  = 0;
    g_st[indice].supporto = 0;
    return 0;
}

/* =============================================================================
 * Inventario
 * ============================================================================= */
int atapi_e_lettore(int indice)
{
    if (indice < 0 || indice >= ATA_MAX_DEVICES) return 0;
    return g_st[indice].lettore;
}

int atapi_conta(void) { return g_n; }

int atapi_init(void)
{
    int i;

    g_n = 0;

    for (i = 0; i < ATA_MAX_DEVICES; i++) {
        const AtaDevice *d = ata_get_device(i);

        g_st[i].lettore    = 0;
        g_st[i].supporto   = 0;
        g_st[i].blocchi    = 0;
        g_st[i].dim_blocco = ATAPI_DIM_BLOCCO;

        if (d == NULL || !d->presente || d->tipo != ATA_TYPE_ATAPI) continue;

        g_st[i].lettore = 1;
        g_n++;

        /* NON si sonda il supporto qui, e non e' una dimenticanza: un
         * lettore con un disco dentro puo' metterci diversi secondi a
         * dichiararsi pronto, e farlo a ogni avvio per ogni lettore
         * significherebbe pagare quell'attesa anche quando nessuno
         * chiedera' mai di leggerlo. Chi vuole sapere se c'e' un disco lo
         * chiede — kernel/block/blk.c lo fa quando serve montare. */
        klog(LOG_INFO, "ATAPI: lettore ottico su canale %d %s",
             d->canale, d->unita ? "slave" : "master");
    }

    return g_n;
}
