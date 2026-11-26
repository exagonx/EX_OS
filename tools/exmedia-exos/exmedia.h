/* =============================================================================
 * tools/exmedia-exos/exmedia.h — a media stream for EXBrowser, over FFmpeg
 *
 * Copyright (C) 2026 Graziano Falcone
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * EXBrowser (debug/implementazione_BROWSER002.txt, P13) opens libexmedia.so.1
 * with dlopen: FFmpeg's structures stay in here, and the browser sees bytes
 * going in and pictures and samples coming out.
 *
 * The bytes come in pieces (exm_aggiungi): a whole file (<video src>), or the
 * segments of Media Source Extensions as they are appended; exm_fine says no
 * more will come. exm_prepara opens the demuxer when there is enough.
 * exm_pompa reads and decodes ahead: up to EXM_FOTO_MAX pictures, already in
 * RGBA at the size asked, and up to a second of PCM, 16-bit interleaved at the
 * rate and channels asked.
 * ========================================================================== */
#ifndef EXMEDIA_H
#define EXMEDIA_H

typedef struct ExMedia ExMedia;

#define EXM_FOTO_MAX 4

ExMedia *exm_apri(void);
void     exm_chiudi(ExMedia *m);
/* more bytes; 0 if there is no room */
int      exm_aggiungi(ExMedia *m, const void *b, unsigned int n);
void     exm_fine(ExMedia *m);
/* 1 ready, 0 not yet (more bytes needed), -1 not a media we can read */
int      exm_prepara(ExMedia *m);
/* what it is: seconds (0 unknown), the picture's size, audio and video */
double   exm_durata(ExMedia *m);
int      exm_info(ExMedia *m, int *w, int *h, int *audio, int *video);
/* the size and format of what comes out: RGBA w x h, PCM hz x canali */
void     exm_uscita(ExMedia *m, int w, int h, int hz, int canali);
/* decode ahead; 1 something done, 0 waiting for bytes, -1 the end */
int      exm_pompa(ExMedia *m);
/* the picture to show at time t (seconds): copied into rgba (w*h*4), its
 * time in *pts; 1 a new one, 0 the same as before, -1 none yet */
int      exm_foto(ExMedia *m, double t, unsigned int *rgba, double *pts);
/* up to max frames of PCM (a frame = canali samples); how many; their
 * starting time in *pts */
int      exm_suono(ExMedia *m, short *pcm, int max, double *pts);
/* go to time t: the queues are emptied */
int      exm_cerca(ExMedia *m, double t);
/* the end of what has been appended, in seconds (buffered) */
double   exm_fin_qui(ExMedia *m);

#endif
