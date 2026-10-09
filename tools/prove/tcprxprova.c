/* =============================================================================
 * tools/prove/tcprxprova.c - il buffer di ricezione di TCP, sull'host
 * (9 ottobre 2026)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     cc -I drivers/ip -o /tmp/tcprxprova tools/prove/tcprxprova.c && /tmp/tcprxprova
 *
 * Un flusso di byte noti viene tagliato in segmenti e consegnato a
 * rx_dati() mescolato: in anticipo, due volte, sovrapposto, perso e
 * rimandato - come farebbe una rete che perde - mentre un lettore svuota il
 * buffer a scatti. Quel che esce dev'essere il flusso, byte per byte.
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TCP_BUF 4096
#define TCP_FS  8
static unsigned int g_rx_dim = 64512;

typedef struct {
    unsigned char *rx;
    unsigned int   rx_dim;
    unsigned char  rx_riserva[TCP_BUF];
    unsigned int   rx_len, rx_off, rcv_nxt;
    struct { unsigned int da, a; } fs[TCP_FS];
    unsigned int   fs_n;
} Conn;

#include "tcp_rx.inc"

#define FLUSSO (1u << 20)
static unsigned char g_f[FLUSSO], g_out[FLUSSO];
static unsigned int g_seme = 12345;
static unsigned int caso(void) { g_seme = g_seme * 1103515245u + 12345u; return (g_seme >> 8) & 0xFFFFFF; }

/* Il lettore: toglie fino a `max` byte, come tcp_consegna. */
static unsigned int leggi(Conn *c, unsigned char *out, unsigned int max)
{
    unsigned int n = c->rx_len < max ? c->rx_len : max;

    memcpy(out, c->rx + c->rx_off, n);
    c->rx_off += n;
    c->rx_len -= n;
    if (c->rx_len == 0 && c->fs_n == 0) c->rx_off = 0;
    return n;
}

static int giro(unsigned int dim, unsigned int perdita, unsigned int isn, unsigned int lettura)
{
    Conn c;
    unsigned int mandato = 0, letto = 0, tenuti = 0, pieni = 0, passi = 0;

    memset(&c, 0, sizeof(c));
    g_rx_dim = dim;
    c.rcv_nxt = isn;                     /* anche vicino a 2^32: i numeri girano */

    while (letto < FLUSSO && passi++ < 40000000u) {
        unsigned int cap = rx_cap(&c), fin = cap - c.rx_len;
        unsigned int base = c.rcv_nxt - isn;        /* fin dove e' confermato */
        unsigned int da, n, r;

        /* il mittente: di solito il pezzo dopo, ogni tanto uno piu' avanti
         * (quello prima si e' perso), uno gia' mandato, o uno a cavallo */
        r = caso() % 100;
        if (r < perdita)            da = base + (caso() % (fin + 1));          /* in anticipo */
        else if (r < perdita + 5)   da = base > 3000 ? base - caso() % 3000 : 0; /* vecchio o a cavallo */
        else                        da = base;
        (void)mandato;
        if (da >= FLUSSO) da = base;
        n = 1 + caso() % 1460;
        if (da + n > FLUSSO) n = FLUSSO - da;
        if (n && da + n <= base + fin + 3000) {
            switch (rx_dati(&c, isn + da, g_f + da, n)) {
            case RX_TENUTO: tenuti++; break;
            case RX_PIENO:  pieni++;  break;
            default: break;
            }
        }
        if (caso() % 100 < lettura) letto += leggi(&c, g_out + letto, 1 + caso() % 1500);
        if (c.rx_off + c.rx_len > cap) { printf("  ! oltre il buffer\n"); return 0; }
    }
    while (c.rx_len) letto += leggi(&c, g_out + letto, 4096);
    if (c.rx != c.rx_riserva) free(c.rx);
    if (letto != FLUSSO || memcmp(g_f, g_out, FLUSSO) != 0) {
        unsigned int i;
        for (i = 0; i < FLUSSO && g_f[i] == g_out[i]; i++) ;
        printf("  ! letti %u su %u, primo byte diverso a %u (tenuti %u, pieni %u)\n", letto, FLUSSO, i, tenuti, pieni);
        return 0;
    }
    printf("  buffer %5u, anticipi %2u%%, lettura %3u%%: uguale (%u pezzi tenuti da parte, %u volte pieno)\n",
           dim, perdita, lettura, tenuti, pieni);
    return 1;
}

int main(void)
{
    static const unsigned int DIM[] = { 4096, 16384, 64512 };
    static const unsigned int PERD[] = { 0, 3, 20, 60 };
    static const unsigned int LETT[] = { 100, 30, 5 };
    unsigned int i, j, k, no = 0, n = 0;

    for (i = 0; i < FLUSSO; i++) g_f[i] = (unsigned char)(caso() >> 4);
    for (i = 0; i < 3; i++)
        for (j = 0; j < 4; j++)
            for (k = 0; k < 3; k++) {
                n++;
                if (!giro(DIM[i], PERD[j], (n & 1) ? 0xFFFFF000u : 1000u * n, LETT[k])) no++;
            }
    printf("tcprxprova: %u NO su %u\n", no, n);
    return no ? 1 : 0;
}
