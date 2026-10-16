/* =============================================================================
 * drivers/ar5k/ar5k_conn.h — joining a network, for the wifi0 service
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * ar5k_conn.c keeps the chip on while connected: the service calls
 * ar5k_conn_lavora() at least every few tens of milliseconds, and takes the
 * Ethernet frames that arrived with ar5k_conn_prendi(). It sees nothing of
 * ath5k (wifi_proto.h types only).
 * ============================================================================= */
#ifndef AR5K_CONN_H
#define AR5K_CONN_H

#include "wifi_proto.h"

/* 0, or a WIFI_ERR_* with the reason in `perche` (no secret in it) */
int  ar5k_conn_collega(volatile void *registri, unsigned int sotto_id, const WifiConnetti *c,
                       char *perche, int perche_n);
void ar5k_conn_stacca(void);
int  ar5k_conn_attiva(void);
int  ar5k_conn_lavora(void);                   /* 1: the link has just been lost */
int  ar5k_conn_invia(const unsigned char *eth, int n);
int  ar5k_conn_prendi(unsigned char *out);     /* 1514 bytes; 0 = nothing */
void ar5k_conn_stato(WifiStato *s);            /* fills it when connected */
void ar5k_conn_conta(void);                    /* the counters, one line in the log */
void ar5k_conn_contatori(unsigned int *inviati, unsigned int *ricevuti, unsigned int *err_tx,
                         unsigned int *err_rx, unsigned int *persi);

#endif
