/* =============================================================================
 * lib/exhttp/h2.h
 * EX-OS — Extensible Operating System
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 * H2: one HTTP/2 connection, several requests on it (streams), over a
 * transport that is not ours to know (debug/implementazione_BROWSER002.txt,
 * P4). The frames, HPACK and the flow control are nghttp2's (libnghttp2.so.14,
 * tools/nghttp2-exos), opened with dlopen the first time: when it is missing
 * h2_disponibile() says 0 and nobody offers "h2" in ALPN.
 *
 * ! BYTES IN, BYTES OUT, NO SOCKET: h2_dai takes what the transport read,
 * h2 writes through the `scrivi` it was given. Who reads and when stays
 * exhttp's (exhttp_avanza: only what the stack already holds).
 *
 * ! ONLY exhttp.so HAS THIS FILE, as srotola.c: exhttp.c names it with weak
 * symbols, and the static exhttp of netupdate never speaks HTTP/2.
 * ============================================================================= */
#ifndef H2_H
#define H2_H

typedef struct H2 H2;

typedef struct {
    /* one header of the answer of stream `flusso` (":status" included) */
    void (*testa)(void *flusso, const char *nome, unsigned int nl,
                  const char *valore, unsigned int vl);
    /* bytes of the body, as they come */
    void (*dati)(void *flusso, const unsigned char *d, unsigned int n);
    /* the stream is over: errore 0 = well (END_STREAM), else the H2 code */
    void (*chiuso)(void *flusso, unsigned int errore);
} H2Ganci;

typedef int (*H2Scrivi)(void *tubo, const unsigned char *d, unsigned int n);

int  h2_disponibile(void);

/* A client session on a connection that has just chosen "h2"; it sends the
 * preface and our SETTINGS at once. 0 if it cannot. */
H2  *h2_apri(H2Scrivi scrivi, void *tubo, const H2Ganci *g);

/* A GET: `n` headers besides the four pseudo-headers. Returns the stream id
 * (> 0) or <= 0. `flusso` comes back in the hooks. */
int  h2_richiesta(H2 *h, const char *schema, const char *autorita,
                  const char *percorso, const char *const *nomi,
                  const char *const *valori, int n, void *flusso);

/* Bytes read from the transport. < 0: the connection is broken (protocol
 * error, GOAWAY with nothing more to do, or the write failed). */
int  h2_dai(H2 *h, const unsigned char *d, unsigned int n);

/* Sends what nghttp2 has queued (window updates, acks, requests). < 0 if
 * the transport refused. */
int  h2_manda(H2 *h);

/* The caller has given up on a stream: no more hooks for it, and the server
 * is told to stop (RST_STREAM CANCEL). */
void h2_lascia(H2 *h, int id);

/* 1 while new streams may be opened on it (no GOAWAY received). */
int  h2_aperta(const H2 *h);

void h2_chiudi(H2 *h);

#endif /* H2_H */
