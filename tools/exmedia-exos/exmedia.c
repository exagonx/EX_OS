/* =============================================================================
 * tools/exmedia-exos/exmedia.c — a media stream for EXBrowser, over FFmpeg
 *
 * Copyright (C) 2026 Graziano Falcone
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * See exmedia.h. FFmpeg 8 (avformat 63) as EX-OS has it (tools/ffmpeg-exos):
 * the demuxer reads from our own buffer (an AVIOContext with a read callback),
 * which grows as bytes are appended; the decoders are FFmpeg's (H.264, VP8,
 * VP9, AV1 through dav1d, AAC, Opus, Vorbis, MP3...), the pictures go through
 * swscale to BGRA (0xAARRGGBB words on a little-endian machine, what ExWin
 * draws), the sound through swresample to 16-bit interleaved PCM.
 *
 * ! "NOT YET" IS NOT "THE END": with Media Source Extensions the bytes come
 * while the media plays. The read callback answers AVERROR(EAGAIN) when it has
 * no more bytes and exm_fine has not been called; FFmpeg's AVIOContext then
 * marks itself at the end, and exm_pompa clears that mark before trying again.
 * ========================================================================== */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include "exmedia.h"

#define BUF_MAX     (256u * 1024u * 1024u)   /* what one stream may keep */
#define AVIO_BUF    65536
#define PCM_SECONDI 2
/* ! A DEMUXER THAT MEETS THE END OF THE BYTES THINKS THE MEDIA IS OVER, and
 * FFmpeg's (Matroska's first) do not take it back when more come. So it never
 * meets it before exm_fine: it is opened only with APRI_MIN bytes (or at the
 * end), and a packet is read only while MARGINE bytes are still unread - more
 * than any packet. What is appended last is read at the next append. */
#define APRI_MIN    (512u * 1024u)
#define MARGINE     (256u * 1024u)

typedef struct { unsigned int *px; double pts; int pieno; } Foto;
#define CODA_MAX 8192
typedef struct { AVPacket *p[CODA_MAX]; int testa, n; } Coda;

struct ExMedia {
    unsigned char   *b;
    unsigned int     n, cap, pos;
    int              fine;
    AVFormatContext *fmt;
    AVIOContext     *io;
    int              pronto, rotto, finito;
    int              sv, sa;                      /* stream indexes, -1 */
    AVCodecContext  *dv, *da;
    AVFrame         *fr;
    AVPacket        *pk;
    struct SwsContext *sws;
    SwrContext      *swr;
    int              sws_w, sws_h, sws_fmt, sws_ow, sws_oh;
    int              ow, oh, ohz, ocan;           /* what comes out */
    Foto             foto[EXM_FOTO_MAX];
    double           ultima_mostrata;
    short           *pcm;                         /* a ring of frames */
    int              pcm_cap, pcm_testa, pcm_n;
    double           pcm_pts;                     /* the time of pcm_testa */
    double           fin_qui, scarta_prima;
    Coda             cv, ca;                      /* packets read, not yet decoded */
    int              letto_tutto;
};

static int coda_metti(Coda *q, AVPacket *p)
{
    if (q->n >= CODA_MAX) return 0;
    q->p[(q->testa + q->n) % CODA_MAX] = p;
    q->n++;
    return 1;
}

static AVPacket *coda_prendi(Coda *q)
{
    AVPacket *p;

    if (!q->n) return 0;
    p = q->p[q->testa];
    q->testa = (q->testa + 1) % CODA_MAX;
    q->n--;
    return p;
}

static void coda_vuota(Coda *q)
{
    AVPacket *p;

    while ((p = coda_prendi(q)) != 0) av_packet_free(&p);
}

/* ------------------------------------------------------------- reading */
static int leggi(void *o, uint8_t *dst, int max)
{
    ExMedia     *m = (ExMedia *)o;
    unsigned int k = m->n - m->pos;

    if (k == 0) return m->fine ? AVERROR_EOF : AVERROR(EAGAIN);
    if (k > (unsigned int)max) k = (unsigned int)max;
    memcpy(dst, m->b + m->pos, k);
    m->pos += k;
    return (int)k;
}

static int64_t cerca_b(void *o, int64_t dove, int come)
{
    ExMedia *m = (ExMedia *)o;
    int64_t  p;

    if (come == AVSEEK_SIZE) return m->fine ? (int64_t)m->n : -1;
    come &= ~AVSEEK_FORCE;
    if (come == SEEK_SET) p = dove;
    else if (come == SEEK_CUR) p = (int64_t)m->pos + dove;
    else if (come == SEEK_END) { if (!m->fine) return -1; p = (int64_t)m->n + dove; }
    else return -1;
    if (p < 0 || p > (int64_t)m->n) return -1;
    m->pos = (unsigned int)p;
    return p;
}

ExMedia *exm_apri(void)
{
    ExMedia *m = (ExMedia *)calloc(1, sizeof(ExMedia));

    if (!m) return 0;
    m->sv = m->sa = -1;
    m->ultima_mostrata = -1;
    return m;
}

int exm_aggiungi(ExMedia *m, const void *b, unsigned int n)
{
    if (!m || m->fine) return 0;
    if (m->n + n > BUF_MAX) return 0;
    if (m->n + n > m->cap) {
        unsigned int c = m->cap ? m->cap : 1u << 20;
        unsigned char *nb;

        while (c < m->n + n) c *= 2;
        nb = (unsigned char *)realloc(m->b, c);
        if (!nb) return 0;
        m->b = nb; m->cap = c;
    }
    memcpy(m->b + m->n, b, n);
    m->n += n;
    return 1;
}

void exm_fine(ExMedia *m) { if (m) m->fine = 1; }

static AVCodecContext *decodificatore(AVStream *s)
{
    const AVCodec  *c = avcodec_find_decoder(s->codecpar->codec_id);
    AVCodecContext *x;

    if (!c) return 0;
    x = avcodec_alloc_context3(c);
    if (!x) return 0;
    if (avcodec_parameters_to_context(x, s->codecpar) < 0) { avcodec_free_context(&x); return 0; }
    x->thread_count = 1;
    x->pkt_timebase = s->time_base;
    if (avcodec_open2(x, c, 0) < 0) { avcodec_free_context(&x); return 0; }
    return x;
}

static void chiudi_formato(ExMedia *m)
{
    if (m->fmt) avformat_close_input(&m->fmt);
    if (m->io) { av_freep(&m->io->buffer); avio_context_free(&m->io); }
}

int exm_prepara(ExMedia *m)
{
    unsigned char *buf;

    if (!m || m->rotto) return -1;
    if (m->pronto) return 1;
    if (m->n < APRI_MIN && !m->fine) return 0;
    m->pos = 0;
    buf = (unsigned char *)av_malloc(AVIO_BUF);
    if (!buf) return -1;
    m->io = avio_alloc_context(buf, AVIO_BUF, 0, m, leggi, 0, cerca_b);
    m->fmt = avformat_alloc_context();
    if (!m->io || !m->fmt) { chiudi_formato(m); return -1; }
    m->fmt->pb = m->io;
    m->fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    /* a small look at the start: the headers say the codecs */
    m->fmt->probesize = 64 * 1024;
    m->fmt->max_analyze_duration = AV_TIME_BASE / 2;
    if (avformat_open_input(&m->fmt, 0, 0, 0) < 0) {
        m->fmt = 0;
        chiudi_formato(m);
        if (m->fine) { m->rotto = 1; return -1; }
        return 0;                          /* more bytes, then again */
    }
    if (avformat_find_stream_info(m->fmt, 0) < 0 && !m->fine) {
        /* not enough to know the codecs: wait, if what we have says so */
        if (m->fmt->nb_streams == 0) { chiudi_formato(m); return 0; }
    }
    m->sv = av_find_best_stream(m->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, 0, 0);
    m->sa = av_find_best_stream(m->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, 0, 0);
    if (m->sv >= 0 && !(m->dv = decodificatore(m->fmt->streams[m->sv]))) m->sv = -1;
    if (m->sa >= 0 && !(m->da = decodificatore(m->fmt->streams[m->sa]))) m->sa = -1;
    if (m->sv < 0 && m->sa < 0) { chiudi_formato(m); m->rotto = 1; return -1; }
    m->fr = av_frame_alloc();
    m->pk = av_packet_alloc();
    if (!m->fr || !m->pk) { m->rotto = 1; return -1; }
    m->pronto = 1;
    return 1;
}

double exm_durata(ExMedia *m)
{
    if (!m || !m->fmt || m->fmt->duration == AV_NOPTS_VALUE || m->fmt->duration <= 0) return 0;
    return (double)m->fmt->duration / AV_TIME_BASE;
}

int exm_info(ExMedia *m, int *w, int *h, int *audio, int *video)
{
    if (!m || !m->pronto) return 0;
    if (w) *w = m->dv ? m->dv->width : 0;
    if (h) *h = m->dv ? m->dv->height : 0;
    if (audio) *audio = m->sa >= 0;
    if (video) *video = m->sv >= 0;
    return 1;
}

void exm_uscita(ExMedia *m, int w, int h, int hz, int canali)
{
    int i;

    if (!m) return;
    if (w != m->ow || h != m->oh) {
        for (i = 0; i < EXM_FOTO_MAX; i++) { free(m->foto[i].px); m->foto[i].px = 0; m->foto[i].pieno = 0; }
        m->ow = w; m->oh = h;
    }
    if (hz != m->ohz || canali != m->ocan) {
        free(m->pcm); m->pcm = 0;
        if (m->swr) swr_free(&m->swr);
        m->ohz = hz; m->ocan = canali;
        m->pcm_cap = hz * PCM_SECONDI;
        m->pcm = (short *)malloc((size_t)m->pcm_cap * (size_t)canali * sizeof(short));
        m->pcm_testa = m->pcm_n = 0;
    }
}

static double secondi(AVFrame *f, AVStream *s)
{
    int64_t p = f->best_effort_timestamp;

    if (p == AV_NOPTS_VALUE) p = f->pts;
    if (p == AV_NOPTS_VALUE) return -1;
    return p * av_q2d(s->time_base);
}

/* a decoded picture into a free slot: 0 if there is none */
static int foto_metti(ExMedia *m, AVFrame *f, double t)
{
    int      i, libera = -1;
    uint8_t *dst[4];
    int      linee[4];

    if (m->ow <= 0 || m->oh <= 0) return 1;            /* nobody looks: dropped */
    if (t >= 0 && t < m->scarta_prima) return 1;
    for (i = 0; i < EXM_FOTO_MAX; i++) if (!m->foto[i].pieno) { libera = i; break; }
    if (libera < 0) return 0;
    if (!m->foto[libera].px) m->foto[libera].px = (unsigned int *)malloc((size_t)m->ow * (size_t)m->oh * 4);
    if (!m->foto[libera].px) return 1;
    if (!m->sws || m->sws_w != f->width || m->sws_h != f->height || m->sws_fmt != f->format ||
        m->sws_ow != m->ow || m->sws_oh != m->oh) {
        sws_freeContext(m->sws);
        m->sws = sws_getContext(f->width, f->height, (enum AVPixelFormat)f->format, m->ow, m->oh,
                                AV_PIX_FMT_BGRA, SWS_BILINEAR, 0, 0, 0);
        m->sws_w = f->width; m->sws_h = f->height; m->sws_fmt = f->format;
        m->sws_ow = m->ow; m->sws_oh = m->oh;
        if (!m->sws) return 1;
    }
    dst[0] = (uint8_t *)m->foto[libera].px; dst[1] = dst[2] = dst[3] = 0;
    linee[0] = m->ow * 4; linee[1] = linee[2] = linee[3] = 0;
    sws_scale(m->sws, (const uint8_t *const *)f->data, f->linesize, 0, f->height, dst, linee);
    m->foto[libera].pts = t;
    m->foto[libera].pieno = 1;
    return 1;
}

static void suono_metti(ExMedia *m, AVFrame *f, double t)
{
    int      quanti, coda, k;
    short    tmp[8192];
    uint8_t *out[1];

    if (!m->pcm || m->ohz <= 0) return;
    if (!m->swr) {
        AVChannelLayout ol;

        av_channel_layout_default(&ol, m->ocan);
        if (swr_alloc_set_opts2(&m->swr, &ol, AV_SAMPLE_FMT_S16, m->ohz, &f->ch_layout,
                                (enum AVSampleFormat)f->format, f->sample_rate, 0, 0) < 0 || swr_init(m->swr) < 0) {
            swr_free(&m->swr);
            return;
        }
    }
    out[0] = (uint8_t *)tmp;
    quanti = swr_convert(m->swr, out, (int)(sizeof(tmp) / sizeof(short)) / m->ocan,
                         (const uint8_t **)f->extended_data, f->nb_samples);
    if (quanti <= 0) return;
    if (m->pcm_n == 0 && t >= 0) m->pcm_pts = t;
    for (k = 0; k < quanti && m->pcm_n < m->pcm_cap; k++) {
        coda = (m->pcm_testa + m->pcm_n) % m->pcm_cap;
        memcpy(m->pcm + coda * m->ocan, tmp + k * m->ocan, (size_t)m->ocan * sizeof(short));
        m->pcm_n++;
    }
}

/* frames out of a decoder; 0 if a picture had no room (try again later) */
static int ricevi(ExMedia *m, AVCodecContext *d, int idx)
{
    for (;;) {
        int r = avcodec_receive_frame(d, m->fr);

        if (r < 0) return 1;
        if (idx == m->sv) {
            if (!foto_metti(m, m->fr, secondi(m->fr, m->fmt->streams[idx]))) {
                /* no room: keep it, the next pump puts it */
                av_frame_unref(m->fr);
                return 0;
            }
        } else suono_metti(m, m->fr, secondi(m->fr, m->fmt->streams[idx]));
        av_frame_unref(m->fr);
    }
}

int exm_leggi(ExMedia *m)
{
    int r, giri = 0;

    if (!m) return -1;
    if (!m->pronto) { r = exm_prepara(m); if (r <= 0) return r; }
    while (!m->letto_tutto && giri++ < 4096) {
        AVPacket *p;

        if (!m->fine && m->n - m->pos < MARGINE) break;

        /* ! the end of the bytes for now is not the end of the media */
        m->io->eof_reached = 0;
        m->io->error = 0;
        r = av_read_frame(m->fmt, m->pk);
        if (r == AVERROR(EAGAIN)) break;
        if (r < 0) { if (m->fine) m->letto_tutto = 1; break; }
        if (m->pk->pts != AV_NOPTS_VALUE) {
            double e = (m->pk->pts + (m->pk->duration > 0 ? m->pk->duration : 0)) *
                       av_q2d(m->fmt->streams[m->pk->stream_index]->time_base);

            if (e > m->fin_qui) m->fin_qui = e;
        }
        if ((m->pk->stream_index == m->sv && m->dv) || (m->pk->stream_index == m->sa && m->da)) {
            p = av_packet_clone(m->pk);
            if (p && !coda_metti(m->pk->stream_index == m->sv ? &m->cv : &m->ca, p)) { av_packet_free(&p); av_packet_unref(m->pk); break; }
        }
        av_packet_unref(m->pk);
    }
    return 1;
}

/* one packet of a stream into its decoder; 0 if none is queued */
static int decodifica(ExMedia *m, int video)
{
    Coda           *q = video ? &m->cv : &m->ca;
    AVCodecContext *d = video ? m->dv : m->da;
    int             idx = video ? m->sv : m->sa;
    AVPacket       *p;

    if (!d) return 0;
    /* what the decoder holds first: a picture that had no room */
    if (!ricevi(m, d, idx)) return 0;
    p = coda_prendi(q);
    if (!p) {
        if (m->letto_tutto && !m->finito) { avcodec_send_packet(d, 0); ricevi(m, d, idx); }
        return 0;
    }
    if (avcodec_send_packet(d, p) == AVERROR(EAGAIN)) { ricevi(m, d, idx); avcodec_send_packet(d, p); }
    av_packet_free(&p);
    ricevi(m, d, idx);
    return 1;
}

static int foto_piene(ExMedia *m)
{
    int i, n = 0;

    for (i = 0; i < EXM_FOTO_MAX; i++) n += m->foto[i].pieno;
    return n >= EXM_FOTO_MAX;
}

int exm_pompa(ExMedia *m)
{
    int giri = 0, fatto = 0;

    if (!m || !m->pronto) return m && m->rotto ? -1 : 0;
    if (m->finito) return -1;
    exm_leggi(m);
    while (giri++ < 64) {
        int x = 0;

        if (m->sv >= 0 && m->ow > 0 && !foto_piene(m)) x |= decodifica(m, 1);
        if (m->sa >= 0 && m->pcm && m->pcm_n < m->pcm_cap / 2) x |= decodifica(m, 0);
        if (!x) break;
        fatto = 1;
    }
    if (m->letto_tutto && m->cv.n == 0 && m->ca.n == 0 && !fatto) { m->finito = 1; return -1; }
    return fatto;
}

int exm_foto(ExMedia *m, double t, unsigned int *rgba, double *pts)
{
    int i, scelta = -1;

    if (!m) return -1;
    /* the latest picture not after t; the older ones are gone */
    for (i = 0; i < EXM_FOTO_MAX; i++)
        if (m->foto[i].pieno && m->foto[i].pts <= t + 0.001 &&
            (scelta < 0 || m->foto[i].pts > m->foto[scelta].pts)) scelta = i;
    if (scelta < 0) {
        /* none yet due: at the start, the first one shows (a poster) */
        if (m->ultima_mostrata >= 0) return 0;
        for (i = 0; i < EXM_FOTO_MAX; i++)
            if (m->foto[i].pieno && (scelta < 0 || m->foto[i].pts < m->foto[scelta].pts)) scelta = i;
        if (scelta < 0) return -1;
        memcpy(rgba, m->foto[scelta].px, (size_t)m->ow * (size_t)m->oh * 4);
        if (pts) *pts = m->foto[scelta].pts;
        m->ultima_mostrata = m->foto[scelta].pts;
        return 1;
    }
    memcpy(rgba, m->foto[scelta].px, (size_t)m->ow * (size_t)m->oh * 4);
    if (pts) *pts = m->foto[scelta].pts;
    m->ultima_mostrata = m->foto[scelta].pts;
    for (i = 0; i < EXM_FOTO_MAX; i++)
        if (m->foto[i].pieno && m->foto[i].pts <= m->foto[scelta].pts) m->foto[i].pieno = 0;
    return 1;
}

int exm_suono(ExMedia *m, short *pcm, int max, double *pts)
{
    int k;

    if (!m || !m->pcm) return 0;
    if (pts) *pts = m->pcm_pts;
    for (k = 0; k < max && m->pcm_n > 0; k++) {
        memcpy(pcm + k * m->ocan, m->pcm + m->pcm_testa * m->ocan, (size_t)m->ocan * sizeof(short));
        m->pcm_testa = (m->pcm_testa + 1) % m->pcm_cap;
        m->pcm_n--;
    }
    if (m->ohz > 0) m->pcm_pts += (double)k / m->ohz;
    return k;
}

int exm_cerca(ExMedia *m, double t)
{
    int i;

    if (!m || !m->pronto) return 0;
    for (i = 0; i < EXM_FOTO_MAX; i++) m->foto[i].pieno = 0;
    m->pcm_testa = m->pcm_n = 0;
    m->ultima_mostrata = -1;
    m->finito = 0;
    m->letto_tutto = 0;
    coda_vuota(&m->cv); coda_vuota(&m->ca);
    m->io->eof_reached = 0; m->io->error = 0;
    if (av_seek_frame(m->fmt, -1, (int64_t)(t * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD) < 0) return 0;
    if (m->dv) avcodec_flush_buffers(m->dv);
    if (m->da) avcodec_flush_buffers(m->da);
    if (m->swr) swr_free(&m->swr);
    m->scarta_prima = t - 0.05;
    m->pcm_pts = t;
    return 1;
}

double exm_fin_qui(ExMedia *m) { return m ? m->fin_qui : 0; }

void exm_chiudi(ExMedia *m)
{
    int i;

    if (!m) return;
    if (m->dv) avcodec_free_context(&m->dv);
    if (m->da) avcodec_free_context(&m->da);
    chiudi_formato(m);
    coda_vuota(&m->cv); coda_vuota(&m->ca);
    av_frame_free(&m->fr);
    av_packet_free(&m->pk);
    sws_freeContext(m->sws);
    swr_free(&m->swr);
    for (i = 0; i < EXM_FOTO_MAX; i++) free(m->foto[i].px);
    free(m->pcm);
    free(m->b);
    free(m);
}
