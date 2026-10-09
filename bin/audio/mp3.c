/* =============================================================================
 * bin/audio/mp3.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il decodificatore MP3 di `audio` (9 ottobre 2026)
 *
 * ! IL LAVORO LO FA minimp3, CHE NON E' NOSTRO: e' in lib/terze/minimp3, di
 * dominio pubblico (CC0). Questo file lo compila — una volta sola, qui — e
 * gli mette davanti tre funzioni col nome di casa: apri, prossimo pezzo,
 * chiudi. Scrivere un decodificatore MP3 da capo sono migliaia di righe di
 * aritmetica gia' scritte bene da altri.
 *
 * Senza SIMD: il bersaglio e' un Pentium MMX, e minimp3 userebbe SSE.
 * ============================================================================= */

#include "libc.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#define MINIMP3_ONLY_MP3
#include "minimp3.h"

#include "mp3.h"

static mp3dec_t       g_dec;
static unsigned char *g_file = 0;
static unsigned int   g_len = 0, g_pos = 0;

/* Apre il file e decodifica il primo pezzo, per sapere frequenza e canali.
 * 0, o -1 (e dice perche'). */
int mp3_apri(const char *percorso, unsigned int *hz, unsigned int *canali)
{
    static short prova[MINIMP3_MAX_SAMPLES_PER_FRAME];
    mp3dec_frame_info_t info;
    struct stat st;
    unsigned int p;
    int fd, n, tot = 0;

    if (stat(percorso, &st) != 0 || st.st_size <= 0) {
        printf("audio: %s non si apre\n", percorso);
        return -1;
    }
    g_len  = (unsigned int)st.st_size;
    g_file = (unsigned char *)malloc(g_len);
    if (!g_file) {
        printf("audio: %s e' troppo grande per la memoria (%u KB)\n", percorso, g_len / 1024);
        return -1;
    }
    fd = open(percorso, O_RDONLY);
    if (fd < 0) { printf("audio: %s non si apre\n", percorso); return -1; }
    while (tot < (int)g_len && (n = (int)read(fd, g_file + tot, g_len - (unsigned int)tot)) > 0)
        tot += n;
    close(fd);
    g_len = (unsigned int)tot;

    /* Si cerca il primo pezzo vero: prima ci possono essere le etichette
     * (ID3) e altra roba, che il decodificatore salta dicendo quanti byte. */
    mp3dec_init(&g_dec);
    for (p = 0; p < g_len; ) {
        int campioni = mp3dec_decode_frame(&g_dec, g_file + p, (int)(g_len - p), prova, &info);

        if (campioni > 0) {
            *hz     = (unsigned int)info.hz;
            *canali = (unsigned int)info.channels;
            mp3dec_init(&g_dec);        /* si riparte da capo, da qui */
            g_pos = p;
            return 0;
        }
        if (info.frame_bytes <= 0) break;
        p += (unsigned int)info.frame_bytes;
    }
    printf("audio: in %s non trovo audio MP3\n", percorso);
    return -1;
}

/* Il prossimo pezzo, in campioni a 16 bit (alternati se stereo). Rende quanti
 * BYTE ha scritto in `pcm`, 0 alla fine del file. `pcm` deve tenere
 * MP3_PEZZO_BYTE. */
unsigned int mp3_prossimo(short *pcm)
{
    mp3dec_frame_info_t info;

    while (g_pos < g_len) {
        int campioni = mp3dec_decode_frame(&g_dec, g_file + g_pos, (int)(g_len - g_pos),
                                           pcm, &info);

        if (info.frame_bytes <= 0) break;
        g_pos += (unsigned int)info.frame_bytes;
        if (campioni > 0)
            return (unsigned int)campioni * (unsigned int)info.channels * 2u;
    }
    return 0;
}

void mp3_chiudi(void)
{
    if (g_file) free(g_file);
    g_file = 0;
    g_len = g_pos = 0;
}
