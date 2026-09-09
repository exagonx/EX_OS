/* =============================================================================
 * drivers/usb/usb_massa.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * LE CHIAVETTE — l'attuazione. Il progetto sta in usb_massa.h.
 * ============================================================================= */

#include "libc.h"
#include "usb_massa.h"

/* --- Il trasporto bulk-only ------------------------------------------------ */
#define CBW_FIRMA     0x43425355u   /* "USBC", in ordine di byte little endian */
#define CSW_FIRMA     0x53425355u   /* "USBS" */
#define CBW_LEN       31
#define CSW_LEN       13

#define CSW_OK        0
#define CSW_FALLITO   1
#define CSW_FASE      2

/* --- I comandi SCSI che servono, e nessuno di piu' ------------------------- */
#define SCSI_TEST_UNIT_READY  0x00
#define SCSI_REQUEST_SENSE    0x03
#define SCSI_INQUIRY          0x12
#define SCSI_READ_CAPACITY10  0x25
#define SCSI_READ10           0x28
#define SCSI_WRITE10          0x2A

/* Richieste di classe del trasporto bulk-only, sull'endpoint 0. */
#define BOT_REQ_RESET         0xFF
#define BOT_REQ_MAX_LUN       0xFE
#define USB_REQ_CLEAR_FEATURE 0x01
#define USB_FEAT_EP_HALT      0x00

/* Quanti settori al massimo in un comando solo. ! DEVE BASTARE A UNA
 * RICHIESTA INTERA dello strato a blocchi (BLKR3_SETTORI_RICHIESTA): spezzare
 * qui una richiesta gia' spezzata dal kernel raddoppierebbe i giri senza
 * guadagnare niente. */
#define MASSA_SETT_MAX        8
#define MASSA_BUF_BYTE        (MASSA_SETT_MAX * 512)

static unsigned char g_buf[MASSA_BUF_BYTE];

/* -----------------------------------------------------------------------------
 * Byte, nei due ordini
 *
 * ! IL CBW E' LITTLE ENDIAN, I COMANDI SCSI SONO BIG ENDIAN. Nello stesso
 * pacchetto di 31 byte convivono due convenzioni opposte: la lunghezza dei
 * dati (byte 8-11) va scritta al contrario del numero di blocco (byte 17-20).
 * Confonderle da' un comando che chiede il settore 0x00000001 al posto del
 * 0x01000000 — cioe' legge il posto sbagliato senza dare nessun errore.
 * --------------------------------------------------------------------------- */
static void le32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v);       p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static void be32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)(v);
}

static unsigned int leggi_le32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static unsigned int leggi_be32(const unsigned char *p)
{
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8) | (unsigned int)p[3];
}

/* -----------------------------------------------------------------------------
 * Il recupero
 *
 * ! SENZA QUESTO UNA CHIAVETTA FUNZIONA FINCHE' NON SBAGLIA UNA VOLTA SOLA.
 * Quando il dispositivo non ha i dati che gli abbiamo chiesto, non risponde
 * «no»: BLOCCA l'endpoint. Da quel momento ogni trasferimento su quell'endpoint
 * fallisce allo stesso modo, e il sintomo — tutto morto dopo un errore
 * qualunque — non somiglia per niente alla sua causa.
 * --------------------------------------------------------------------------- */
static void sblocca(UsbMassa *m, unsigned int ep, int in)
{
    unsigned int ind = ep | (in ? 0x80u : 0x00u);

    /* Destinatario ENDPOINT (0x02), CLEAR_FEATURE, ENDPOINT_HALT. */
    (void)m->ctl(m->dev, 0x02, USB_REQ_CLEAR_FEATURE,
                 USB_FEAT_EP_HALT, ind, 0, 0, 0);

    /* Il dispositivo ha appena azzerato il suo conto dei pacchetti: chi lo
     * tiene anche dalla propria parte deve saperlo. Vedi UsbToggleAzzera. */
    if (m->toggle_azzera != NULL) m->toggle_azzera(m->dev, ep, in);
}

/* Il reset del trasporto: si usa quando il dispositivo ha perso il filo
 * («errore di fase»), non per un errore qualunque. Dopo, i due endpoint vanno
 * comunque sbloccati. */
static void reset_bot(UsbMassa *m)
{
    if (m->verboso) printf("massa: reset del trasporto\n");

    (void)m->ctl(m->dev, 0x21, BOT_REQ_RESET, 0, m->interfaccia, 0, 0, 0);
    sblocca(m, m->ep_in, 1);
    sblocca(m, m->ep_out, 0);
}

/* -----------------------------------------------------------------------------
 * Un comando, dall'inizio alla fine
 *
 * `cmd` e' il blocco SCSI, `cmd_len` la sua lunghezza. `dati` puo' essere
 * nullo. Rende 0 se il dispositivo ha detto che e' andata, -1 altrimenti.
 * --------------------------------------------------------------------------- */
static int comando(UsbMassa *m, const unsigned char *cmd, unsigned int cmd_len,
                   void *dati, unsigned int len, int in)
{
    unsigned char cbw[CBW_LEN];
    unsigned char csw[CSW_LEN];
    unsigned int  i, tag;
    int           r;

    if (cmd_len == 0 || cmd_len > 16) return -1;

    tag = ++m->tag;

    for (i = 0; i < CBW_LEN; i++) cbw[i] = 0;
    le32(cbw + 0, CBW_FIRMA);
    le32(cbw + 4, tag);
    le32(cbw + 8, len);
    cbw[12] = (unsigned char)(in ? 0x80 : 0x00);
    cbw[13] = 0;                     /* LUN 0: le chiavette ne hanno una */
    cbw[14] = (unsigned char)cmd_len;
    for (i = 0; i < cmd_len; i++) cbw[15 + i] = cmd[i];

    /* --- 1. il comando ---------------------------------------------------- */
    r = m->bulk(m->dev, m->ep_out, cbw, CBW_LEN, 0);
    if (r < 0) {
        /* Un CBW che stalla vuol dire che il dispositivo e' fuori fase: da
         * li' non si esce sbloccando un endpoint. */
        reset_bot(m);
        return -1;
    }

    /* --- 2. i dati -------------------------------------------------------- */
    if (len > 0 && dati != NULL) {
        r = m->bulk(m->dev, in ? m->ep_in : m->ep_out, dati, len, in);
        if (r == USB_MASSA_STALLO) {
            /* ! NON SI RINUNCIA QUI. Il dispositivo ha bloccato l'endpoint
             * perche' non ha tutti i dati, ma il CSW lo deve ancora dare — ed
             * e' li' che dice PERCHE'. Si sblocca e si va a leggerlo. */
            sblocca(m, in ? m->ep_in : m->ep_out, in);
        } else if (r < 0) {
            reset_bot(m);
            return -1;
        }
    }

    /* --- 3. lo stato ------------------------------------------------------ */
    for (i = 0; i < CSW_LEN; i++) csw[i] = 0;

    r = m->bulk(m->dev, m->ep_in, csw, CSW_LEN, 1);
    if (r == USB_MASSA_STALLO) {
        /* Un CSW che stalla si ritenta UNA volta dopo aver sbloccato: e' il
         * caso previsto dalla specifica, e la seconda volta o arriva o e'
         * errore di fase. */
        sblocca(m, m->ep_in, 1);
        r = m->bulk(m->dev, m->ep_in, csw, CSW_LEN, 1);
    }
    if (r < 0) { reset_bot(m); return -1; }

    if (leggi_le32(csw + 0) != CSW_FIRMA) {
        if (m->verboso) printf("massa: CSW senza firma\n");
        reset_bot(m);
        return -1;
    }

    /* ! IL TAG SI CONTROLLA, e non e' pignoleria: un CSW con il numero di un
     * comando precedente vuol dire che siamo sfasati di una risposta, cioe'
     * che da qui in avanti ogni esito appartiene al comando sbagliato. */
    if (leggi_le32(csw + 4) != tag) {
        if (m->verboso) printf("massa: CSW di un altro comando\n");
        reset_bot(m);
        return -1;
    }

    if (csw[12] == CSW_FASE) { reset_bot(m); return -1; }
    if (csw[12] != CSW_OK)   return -1;

    return 0;
}

/* -----------------------------------------------------------------------------
 * I comandi SCSI
 * --------------------------------------------------------------------------- */

/* Chiede al dispositivo perche' ha detto di no. Non si guarda il risultato:
 * serve a SBLOCCARE la condizione, che altrimenti resta e fa fallire anche il
 * comando dopo. E' il motivo per cui una chiavetta appena infilata risponde
 * «non pronta» al primo tentativo e bene al secondo. */
static void chiedi_motivo(UsbMassa *m)
{
    unsigned char cmd[6];
    unsigned char sense[18];
    unsigned int  i;

    for (i = 0; i < 6; i++) cmd[i] = 0;
    cmd[0] = SCSI_REQUEST_SENSE;
    cmd[4] = sizeof(sense);

    if (comando(m, cmd, 6, sense, sizeof(sense), 1) == 0 && m->verboso)
        printf("massa: sense %02x/%02x/%02x\n",
               sense[2] & 0x0F, sense[12], sense[13]);
}

int usb_massa_pronta(UsbMassa *m)
{
    unsigned char cmd[6];
    unsigned int  i, giro;

    for (i = 0; i < 6; i++) cmd[i] = 0;
    cmd[0] = SCSI_TEST_UNIT_READY;

    /* ! I TENTATIVI SERVONO DAVVERO. Una chiavetta appena alimentata risponde
     * «unita' non pronta» finche' non ha finito di svegliarsi, e un disco
     * esterno a piatti ci mette anche qualche secondo a girare. Rinunciare al
     * primo no vuol dire non vedere mai i dischi lenti. */
    for (giro = 0; giro < 20; giro++) {
        if (comando(m, cmd, 6, 0, 0, 0) == 0) return 1;
        chiedi_motivo(m);
        usleep(200000);
    }

    printf("massa: il supporto non diventa pronto\n");
    return 0;
}

int usb_massa_nome(UsbMassa *m, char *nome, unsigned int max)
{
    unsigned char cmd[6];
    unsigned char d[36];
    unsigned int  i, j = 0;

    for (i = 0; i < 6; i++) cmd[i] = 0;
    cmd[0] = SCSI_INQUIRY;
    cmd[4] = sizeof(d);

    for (i = 0; i < sizeof(d); i++) d[i] = 0;
    if (comando(m, cmd, 6, d, sizeof(d), 1) != 0) return 0;

    /* Venditore (8 byte) e prodotto (16), riempiti di spazi a destra. */
    for (i = 8; i < 32 && j + 1 < max; i++) {
        if (d[i] < 32 || d[i] > 126) continue;
        nome[j++] = (char)d[i];
    }
    while (j > 0 && nome[j - 1] == ' ') j--;
    nome[j] = '\0';
    return 1;
}

int usb_massa_capacita(UsbMassa *m)
{
    unsigned char cmd[10];
    unsigned char d[8];
    unsigned int  i, ultimo;

    for (i = 0; i < 10; i++) cmd[i] = 0;
    cmd[0] = SCSI_READ_CAPACITY10;

    for (i = 0; i < sizeof(d); i++) d[i] = 0;
    if (comando(m, cmd, 10, d, sizeof(d), 1) != 0) return 0;

    ultimo         = leggi_be32(d + 0);      /* l'ULTIMO blocco, non quanti */
    m->byte_blocco = leggi_be32(d + 4);

    if (m->byte_blocco == 0) return 0;

    /* ! ULTIMO + 1, ed e' l'errore classico: READ CAPACITY rende l'indirizzo
     * dell'ultimo blocco. Prenderlo per la capacita' vuol dire un dispositivo
     * piu' corto di un blocco, e l'unico settore che non si legge mai e'
     * proprio l'ultimo — dove certi filesystem tengono una copia dei
     * metadati. */
    m->blocchi = ultimo + 1;

    if (m->byte_blocco == 512) {
        m->settori = m->blocchi;
    } else if (m->byte_blocco == 4096) {
        /* Un supporto a 4096 si presenta lo stesso in settori da 512: la
         * traduzione sta qui, in un punto solo. Vedi usb_massa.h. */
        m->settori = m->blocchi * 8;
    } else {
        printf("massa: blocchi da %u byte: non so tradurli in settori\n",
               m->byte_blocco);
        return 0;
    }

    return 1;
}

/* Lettura e scrittura condividono tutto tranne un byte di comando e il verso:
 * scriverle due volte avrebbe voluto dire due posti dove sbagliare la
 * traduzione dei blocchi. */
static int leggi_scrivi(UsbMassa *m, unsigned int lba, unsigned int n,
                        void *buf, int scrittura)
{
    unsigned char cmd[10];
    unsigned int  i, blocco, quanti, byte;

    if (n == 0 || n > MASSA_SETT_MAX) return -1;
    if (lba + n > m->settori)         return -1;

    /* Da settori da 512 ai blocchi veri del supporto. */
    if (m->byte_blocco == 512) {
        blocco = lba;
        quanti = n;
    } else {
        /* ! A 4096 SI PUO' LEGGERE SOLO A BLOCCHI INTERI. Una richiesta che
         * non comincia su un multiplo di otto settori, o che non ne chiede un
         * multiplo, andrebbe letta piu' larga e ritagliata. Non capita —
         * blk_read chiede sempre settori allineati alla richiesta del
         * filesystem — ma se capitasse va detto, non arrotondato in silenzio. */
        if ((lba % 8) != 0 || (n % 8) != 0) {
            printf("massa: richiesta non allineata al blocco da 4096\n");
            return -1;
        }
        blocco = lba / 8;
        quanti = n / 8;
    }

    byte = n * 512;

    for (i = 0; i < 10; i++) cmd[i] = 0;
    cmd[0] = (unsigned char)(scrittura ? SCSI_WRITE10 : SCSI_READ10);
    be32(cmd + 2, blocco);
    cmd[7] = (unsigned char)(quanti >> 8);
    cmd[8] = (unsigned char)(quanti);

    if (scrittura) memcpy(g_buf, buf, byte);

    if (comando(m, cmd, 10, g_buf, byte, scrittura ? 0 : 1) != 0) {
        chiedi_motivo(m);
        return -1;
    }

    if (!scrittura) memcpy(buf, g_buf, byte);
    return 0;
}

int usb_massa_leggi(UsbMassa *m, unsigned int lba, unsigned int n, void *buf)
{
    return leggi_scrivi(m, lba, n, buf, 0);
}

int usb_massa_scrivi(UsbMassa *m, unsigned int lba, unsigned int n,
                     const void *buf)
{
    return leggi_scrivi(m, lba, n, (void *)buf, 1);
}

/* -----------------------------------------------------------------------------
 * Il ciclo di servizio: da richiesta a blocchi a comando SCSI
 * --------------------------------------------------------------------------- */
int usb_massa_servi(UsbMassa *m, const char *nome)
{
    static unsigned char buf[BLKR3_SETTORI_RICHIESTA * 512];
    BlkOfferta   o;
    BlkRichiesta r;
    int          rc;

    memset(&o, 0, sizeof(o));
    strncpy(o.nome, nome, sizeof(o.nome) - 1);
    o.settori_lo   = m->settori;
    o.settori_hi   = 0;
    o.byte_settore = 512;
    o.sola_lettura = 0;

    rc = blk_offri(&o);
    if (rc < 0) {
        printf("massa: blk_offri('%s') ha risposto %d\n", nome, rc);
        return 0;
    }

    printf("massa: %s offerto, %u settori (%u MB)\n",
           nome, m->settori, m->settori / 2048);
    printf("       blkscan %s   e poi   mount %s <punto>\n", nome, nome);

    r.dati     = buf;
    r.dati_max = sizeof(buf);

    for (;;) {
        rc = blk_attendi(&r, 0);
        if (rc == -EINTR) continue;
        if (rc < 0) {
            printf("massa: blk_attendi ha risposto %d, esco\n", rc);
            return 0;
        }

        switch (r.op) {
        case BLKR3_LEGGI:
            blk_risposta(&r, usb_massa_leggi(m, r.lba_lo, r.settori, buf) == 0
                             ? 0 : -EIO);
            break;

        case BLKR3_SCRIVI:
            blk_risposta(&r, usb_massa_scrivi(m, r.lba_lo, r.settori, buf) == 0
                             ? 0 : -EIO);
            break;

        case BLKR3_SVUOTA:
            /* Una chiavetta tiene i suoi dati per conto proprio: qui non c'e'
             * niente in sospeso da riversare. Il giorno che si volesse essere
             * sicuri fino in fondo, il comando e' SYNCHRONIZE CACHE (0x35). */
            blk_risposta(&r, 0);
            break;

        default:
            blk_risposta(&r, -EINVAL);
            break;
        }
    }
}
