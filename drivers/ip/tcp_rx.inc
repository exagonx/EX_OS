/* =============================================================================
 * drivers/ip/tcp_rx.inc - il buffer di ricezione di una connessione TCP
 * EX-OS - Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * ! STA IN UN FILE A PARTE PER POTERLO PROVARE (9 ottobre 2026). E' la parte
 * di un TCP dove si prendono i bug peggiori - pezzi in anticipo, pezzi che si
 * sovrappongono, il buffer che si compatta mentre ci sono tratti in attesa -
 * e dentro ip.drv non si puo' far arrivare un pacchetto in disordine a
 * comando. tools/prove/tcprxprova.c lo include con una Conn sua e gli manda
 * migliaia di flussi mescolati.
 *
 * Chi lo include definisce prima: Conn (coi campi rx, rx_dim, rx_riserva,
 * rx_len, rx_off, rcv_nxt, fs, fs_n), TCP_BUF, TCP_FS e g_rx_dim.
 * ============================================================================= */

/* Quanto e' grande il buffer di ricezione di c; lo prende, la prima volta. */
static unsigned int rx_cap(Conn *c)
{
    if (c->rx == NULL) {
        c->rx = (g_rx_dim > TCP_BUF) ? (unsigned char *)malloc(g_rx_dim) : NULL;
        c->rx_dim = c->rx ? g_rx_dim : TCP_BUF;
        if (c->rx == NULL) c->rx = c->rx_riserva;
    }
    return c->rx_dim;
}

/* Porta i byte buoni in testa al buffer, e con loro i tratti fuori ordine,
 * che stanno piu' avanti a distanze che non devono cambiare. */
static void rx_compatta(Conn *c)
{
    unsigned int est = 0, i;

    if (c->rx_off == 0) return;
    for (i = 0; i < c->fs_n; i++)
        if (c->fs[i].a - c->rcv_nxt > est) est = c->fs[i].a - c->rcv_nxt;
    memmove(c->rx, c->rx + c->rx_off, c->rx_len + est);
    c->rx_off = 0;
}

/* Dopo che rcv_nxt e' avanzato: i tratti tenuti da parte che ora sono
 * contigui diventano byte buoni. Sono gia' al loro posto. */
static void rx_assorbi(Conn *c)
{
    unsigned int i;

    for (i = 0; i < c->fs_n; ) {
        if ((int)(c->fs[i].da - c->rcv_nxt) > 0) { i++; continue; }
        if ((int)(c->fs[i].a - c->rcv_nxt) > 0) {
            c->rx_len += c->fs[i].a - c->rcv_nxt;
            c->rcv_nxt = c->fs[i].a;
        }
        c->fs[i] = c->fs[--c->fs_n];
        i = 0;                      /* quello che si e' aggiunto puo' toccarne un altro */
    }
}

/* Tiene da parte un pezzo arrivato in anticipo di `d` byte. */
static void rx_tieni(Conn *c, unsigned int seq, unsigned int d,
                     const unsigned char *dati, unsigned int n)
{
    unsigned int cap = rx_cap(c), i;

    rx_compatta(c);
    if (c->rx_len + d + n > cap) return;            /* oltre la finestra: lo rimandera' */
    for (i = 0; i < c->fs_n; i++)
        if (c->fs[i].da == seq && c->fs[i].a == seq + n) break;
    if (i == c->fs_n) {
        if (c->fs_n >= TCP_FS) return;
        c->fs[c->fs_n].da = seq;
        c->fs[c->fs_n].a  = seq + n;
        c->fs_n++;
    }
    memcpy(c->rx + c->rx_len + d, dati, n);
}

/* Un segmento di dati arrivato: lo mette dove va. Rende
 *   RX_VECCHIO   tutto gia' visto (una ritrasmissione): niente di nuovo
 *   RX_BUONO     in ordine: accodato, e rcv_nxt e' avanzato
 *   RX_TENUTO    in anticipo: tenuto da parte (o scartato, se non ci stava)
 *   RX_PIENO     in ordine ma senza posto: non accodato
 * Chi chiama manda l'ACK in tutti e quattro i casi. */
#define RX_VECCHIO 0
#define RX_BUONO   1
#define RX_TENUTO  2
#define RX_PIENO   3
static int rx_dati(Conn *c, unsigned int seq, const unsigned char *dati, unsigned int n)
{
    unsigned int cap = rx_cap(c);
    int          d = (int)(seq - c->rcv_nxt);

    /* Gia' visto, tutto o in parte: una ritrasmissione che si sovrappone.
     * Si tiene solo la coda nuova. */
    if (d < 0) {
        unsigned int visti = (unsigned int)(-d);

        if (visti >= n) return RX_VECCHIO;
        dati += visti;
        n -= visti;
        seq += visti;
        d = 0;
    }
    if (d > 0) {
        rx_tieni(c, seq, (unsigned int)d, dati, n);
        return RX_TENUTO;
    }
    if (c->rx_len + n > cap) return RX_PIENO;
    if (c->rx_off > 0 && c->rx_off + c->rx_len + n > cap) rx_compatta(c);
    memcpy(c->rx + c->rx_off + c->rx_len, dati, n);
    c->rx_len  += n;
    c->rcv_nxt += n;
    rx_assorbi(c);
    return RX_BUONO;
}
