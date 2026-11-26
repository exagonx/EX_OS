/* =============================================================================
 * exwin/bin/exbrowser/browser_media.c — <video>, <audio> and MSE (P13)
 *
 * Copyright (C) 2026 Graziano Falcone
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The decoding is libexmedia.so.1 (tools/exmedia-exos, over FFmpeg), opened
 * with dlopen. Here: one player per media element, its clock, the pictures
 * drawn in the element's rectangle, the sound written into the ring of the
 * "audio" service (drivers/audio/audio_proto.h, as lib/exsuono does), and the
 * native __exos_media(op, ...) of browser_media.js:
 *   "nuovo", el                 -> the player id of that element
 *   "carica", id, url           -> 1: the whole file, fetched by the browser
 *   "sb"                        -> a SourceBuffer id (Media Source Extensions)
 *   "sb_aggiungi", sb, bytes    -> 1 or 0 (QuotaExceeded)
 *   "sb_fine", sb               -> endOfStream
 *   "sb_fin", sb                -> the end of what was appended, seconds
 *   "collega", id, sbv, sba     -> the element plays those SourceBuffers
 *   "suona", "pausa", "vai" (t), "volume" (v)
 *   "stato", id                 -> JSON {t, durata, pronto, w, h, finito, suona}
 *
 * ! THE CLOCK IS THE WALL CLOCK while playing (uptime_ms), and the sound is
 * written as fast as the ring takes it: half a second of ring keeps the two
 * close. ! A METRONOME: ExWin's timers wake every 200 ms; a thread writes a
 * byte into a pipe every 33 ms while something plays, and the message loop
 * watches the pipe (ex_watch_fd): 30 pictures a second without touching ExWin.
 * ========================================================================== */
#include "browser_priv.h"
#include "browser_media_js.h"
#include "audio_proto.h"
#include <pthread.h>

/* exbrowser.c: the whole file of a media, fetched (malloc'd) */
int  media_scarica(const char *url, unsigned char **dati, unsigned int *n);
void media_ridisegna(void);

/* --- libexmedia.so.1 (tools/exmedia-exos/exmedia.h) */
typedef struct ExMedia ExMedia;
static ExMedia *(*xm_apri)(void);
static void     (*xm_chiudi)(ExMedia *);
static int      (*xm_aggiungi)(ExMedia *, const void *, unsigned int);
static void     (*xm_fine)(ExMedia *);
static int      (*xm_prepara)(ExMedia *);
static double   (*xm_durata)(ExMedia *);
static int      (*xm_info)(ExMedia *, int *, int *, int *, int *);
static void     (*xm_uscita)(ExMedia *, int, int, int, int);
static int      (*xm_pompa)(ExMedia *);
static int      (*xm_leggi)(ExMedia *);
static int      (*xm_foto)(ExMedia *, double, unsigned int *, double *);
static int      (*xm_suono)(ExMedia *, short *, int, double *);
static int      (*xm_cerca)(ExMedia *, double);
static double   (*xm_fin_qui)(ExMedia *);

static int g_cercata, g_pronta;

static int exmedia_pronta(void)
{
    void *l;
    int   ok = 1;

    if (g_cercata) return g_pronta;
    g_cercata = 1;
    l = dlopen("libexmedia.so.1", RTLD_NOW);
    if (!l) { printf("exbrowser: media: libexmedia.so.1 non c'e'\n"); return 0; }
#define S(v, n) do { *(void **)&v = dlsym(l, n); if (!v) ok = 0; } while (0)
    S(xm_apri, "exm_apri"); S(xm_chiudi, "exm_chiudi"); S(xm_aggiungi, "exm_aggiungi");
    S(xm_fine, "exm_fine"); S(xm_prepara, "exm_prepara"); S(xm_durata, "exm_durata");
    S(xm_info, "exm_info"); S(xm_uscita, "exm_uscita"); S(xm_pompa, "exm_pompa"); S(xm_leggi, "exm_leggi");
    S(xm_foto, "exm_foto"); S(xm_suono, "exm_suono"); S(xm_cerca, "exm_cerca");
    S(xm_fin_qui, "exm_fin_qui");
#undef S
    g_pronta = ok;
    return ok;
}

/* ------------------------------------------------------------ the tables */
#define LETT_MAX 8
#define SB_MAX   16
typedef struct {
    int          usato, nodo, sbv, sba;
    const void  *vista;
    ExMedia     *mv, *ma;              /* the same for a whole file */
    int          pronto, w, h, audio, video, finito, suona;
    double       durata, t_base;       /* the media time at t_ms */
    unsigned int t_ms;
    unsigned int *px;                  /* the picture at the piece's size */
    int          pw, ph, ha_foto;
    int          sx, sy, sw, sh, cx, cy, cw, ch, visto;
    int          volume;
} Lettore;
typedef struct { int usato; const void *vista; ExMedia *m; } Sb;
static Lettore g_lett[LETT_MAX];
static Sb      g_sb[SB_MAX];

static double ora_media(Lettore *l)
{
    if (!l->suona) return l->t_base;
    return l->t_base + (double)(uptime_ms() - l->t_ms) / 1000.0;
}

/* ------------------------------------------------------------- the sound */
static int           g_audio_pid = -1, g_audio_aperto, g_audio_partito, g_audio_hz, g_audio_can;
static AudioAnello  *g_anello;
static unsigned char *g_campioni;
static Lettore      *g_audio_di;           /* the player that has the card */

typedef struct { int pid; unsigned int tipo; } Aspetto;
static int filtro(const IpcMessage *m, void *dato)
{
    const Aspetto *a = (const Aspetto *)dato;

    return ((int)m->sender_pid == a->pid && m->tipo == a->tipo) ? IPC_MIO : IPC_ALTRUI;
}

static int audio_apri(void)
{
    static unsigned char buf[IPC_MSG_MAX_DATA];
    AudioFormato f;
    AudioEsito   e;
    ShmZona      z;
    IpcMessage   meta;
    Aspetto      a;

    if (g_audio_aperto) return 1;
    g_audio_pid = ipc_lookup(AUDIO_SERVIZIO);
    if (g_audio_pid <= 0) return 0;
    f.rate = 44100; f.canali = 2; f.bit = 16;
    if (ipc_send((unsigned int)g_audio_pid, AUDIO_MSG_APRI, &f, sizeof(f)) < 0) return 0;
    a.pid = g_audio_pid; a.tipo = AUDIO_MSG_ESITO;
    if (ipc_scegli(filtro, &a, &meta, buf, sizeof(buf), 2000) < 0 || meta.len < sizeof(e)) return 0;
    memcpy(&e, buf, sizeof(e));
    if (e.esito != 0 || e.formato.bit != 16 || e.formato.canali < 1 || e.formato.canali > 2) return 0;
    memset(&z, 0, sizeof(z));
    strcpy(z.nome, e.zona);
    z.byte = e.zona_byte;
    if (shm_apri(&z) < 0) { ipc_send((unsigned int)g_audio_pid, AUDIO_MSG_CHIUDI, 0, 0); return 0; }
    g_anello = (AudioAnello *)z.virt;
    g_campioni = (unsigned char *)z.virt + g_anello->dati_off;
    g_audio_hz = (int)e.formato.rate; g_audio_can = (int)e.formato.canali;
    g_audio_aperto = 1; g_audio_partito = 0;
    return 1;
}

static void audio_chiudi(void)
{
    if (!g_audio_aperto) return;
    ipc_send((unsigned int)g_audio_pid, AUDIO_MSG_CHIUDI, 0, 0);
    if (g_anello) shm_chiudi(g_anello);
    g_anello = 0; g_campioni = 0;
    g_audio_aperto = 0; g_audio_partito = 0; g_audio_di = 0;
}

static void audio_riempi(Lettore *l)
{
    static short tmp[4096 * 2];
    unsigned int libero, quadro = (unsigned int)g_audio_can * 2;
    int          n, k;

    if (!g_audio_aperto || g_audio_di != l || !l->ma) return;
    libero = AUDIO_LIBERO(g_anello) / quadro;
    while (libero > 0) {
        n = xm_suono(l->ma, tmp, (int)(libero > 4096 ? 4096 : libero), 0);
        if (n <= 0) break;
        for (k = 0; k < n; k++) {
            if (l->volume < 100) { tmp[k * g_audio_can] = (short)(tmp[k * g_audio_can] * l->volume / 100);
                                   if (g_audio_can > 1) tmp[k * 2 + 1] = (short)(tmp[k * 2 + 1] * l->volume / 100); }
        }
        {
            unsigned int byte = (unsigned int)n * quadro, da = g_anello->scritto & (g_anello->byte - 1);
            unsigned int primo = g_anello->byte - da;

            if (primo > byte) primo = byte;
            memcpy(g_campioni + da, tmp, primo);
            memcpy(g_campioni, (unsigned char *)tmp + primo, byte - primo);
            g_anello->scritto += byte;
        }
        libero -= (unsigned int)n;
    }
    if (!g_audio_partito && AUDIO_PIENO(g_anello) > 0) {
        ipc_send((unsigned int)g_audio_pid, AUDIO_MSG_VIA, 0, 0);
        g_audio_partito = 1;
    }
}

/* ------------------------------------------------------------ the metronome */
static int             g_tubo[2] = { -1, -1 };
static volatile int    g_batti;            /* 1 while something plays or loads */
static pthread_t       g_filo;
static int             g_filo_va;

static void *metronomo(void *x)
{
    (void)x;
    for (;;) {
        usleep(33000);
        if (g_batti && g_tubo[1] >= 0) {
            char c = 1;

            if (write(g_tubo[1], &c, 1) < 0) {}
        }
    }
    return 0;
}

static void giro(void);

static void battito(void *dato, int fd)
{
    char b[64];

    (void)dato;
    while (read(fd, b, sizeof(b)) == (int)sizeof(b)) {}
    giro();
}

static void metronomo_avvia(void)
{
    if (g_filo_va) { g_batti = 1; return; }
    if (pipe(g_tubo) != 0) return;
    fcntl(g_tubo[0], F_SETFL, O_NONBLOCK);
    fcntl(g_tubo[1], F_SETFL, O_NONBLOCK);
    if (pthread_create(&g_filo, 0, metronomo, 0) != 0) { close(g_tubo[0]); close(g_tubo[1]); g_tubo[0] = g_tubo[1] = -1; return; }
    ex_watch_fd(g_tubo[0], battito, 0);
    g_filo_va = 1;
    g_batti = 1;
}

/* -------------------------------------------------------------- one turn */
static void prepara(Lettore *l)
{
    int w = 0, h = 0, a = 0, v = 0, r;

    if (l->pronto || !l->mv) return;
    r = xm_prepara(l->mv);
    if (r < 0) { l->finito = 1; return; }
    if (r == 0) return;
    if (l->ma && l->ma != l->mv && xm_prepara(l->ma) <= 0) return;
    xm_info(l->mv, &w, &h, &a, &v);
    l->w = w; l->h = h; l->video = v;
    l->audio = a || (l->ma && l->ma != l->mv);
    if (!l->ma && a) l->ma = l->mv;
    l->durata = xm_durata(l->mv);
    l->pronto = 1;
    media_ridisegna();                 /* the size may have changed: relayout */
}

static void disegna_foto(Lettore *l)
{
    int x = l->sx, y = l->sy, w = l->sw, h = l->sh, ox = 0, oy = 0;

    if (!l->visto || !l->ha_foto || !l->px || l->vista != g_vi) return;
    if (x < l->cx) { ox = l->cx - x; w -= ox; x = l->cx; }
    if (y < l->cy) { oy = l->cy - y; h -= oy; y = l->cy; }
    if (x + w > l->cx + l->cw) w = l->cx + l->cw - x;
    if (y + h > l->cy + l->ch) h = l->cy + l->ch - y;
    if (w <= 0 || h <= 0) return;
    ex_pixmap(g_f, x, y, w, h, l->px + oy * l->pw + ox, (unsigned int)l->pw);
    ex_update_rect(g_f, x, y, w, h);
}

static void giro(void)
{
    int i, attivi = 0;

    for (i = 0; i < LETT_MAX; i++) {
        Lettore *l = &g_lett[i];
        double   t, pts;

        if (!l->usato) continue;
        if (!l->pronto) { prepara(l); if (!l->pronto && !l->finito) attivi++; continue; }
        if (l->vista != g_vi) continue;
        /* ! ONE CALL PER STREAM: exm_uscita with other numbers empties the
         * queues (a whole file is audio and video in one stream) */
        if (l->mv == l->ma && l->ma) xm_uscita(l->mv, l->video ? l->pw : 0, l->video ? l->ph : 0, 44100, 2);
        else {
            if (l->mv && l->video) xm_uscita(l->mv, l->pw, l->ph, 0, 0);
            if (l->ma) xm_uscita(l->ma, 0, 0, 44100, 2);
        }
        if (l->mv != l->ma) {
            if (l->video && l->pw > 0 && (!l->ha_foto || l->suona)) xm_pompa(l->mv);
            if (l->ma && l->suona) xm_pompa(l->ma);
        }
        if (l->mv == l->ma || !l->ma) {
            int r = xm_pompa(l->mv);

            if (r < 0 && l->suona && l->durata > 0 && ora_media(l) >= l->durata - 0.05) { l->finito = 1; l->suona = 0; l->t_base = l->durata; }
        }
        t = ora_media(l);
        if (l->video && l->px && xm_foto(l->mv, t, l->px, &pts) == 1) { l->ha_foto = 1; disegna_foto(l); }
        if (l->suona) {
            if (l->audio && (g_audio_di == l || (!g_audio_di && audio_apri()))) { g_audio_di = l; audio_riempi(l); }
            if (l->durata > 0 && t >= l->durata) { l->suona = 0; l->finito = 1; l->t_base = l->durata; }
            attivi++;
        }
    }
    if (!attivi) {
        g_batti = 0;
        if (g_audio_di && !g_audio_di->suona) audio_chiudi();
    }
}

/* --------------------------------------------------------------- the piece */
int media_misura(int nodo, int *w, int *h)
{
    int i;

    for (i = 0; i < LETT_MAX; i++)
        if (g_lett[i].usato && g_lett[i].nodo == nodo && g_lett[i].vista == g_vi && g_lett[i].pronto && g_lett[i].w > 0) {
            *w = g_lett[i].w; *h = g_lett[i].h;
            return 1;
        }
    return 0;
}

void media_disegna(int nodo, int x, int y, int w, int h, int cx, int cy, int cw, int ch)
{
    int i;

    for (i = 0; i < LETT_MAX; i++) {
        Lettore *l = &g_lett[i];

        if (!l->usato || l->nodo != nodo || l->vista != g_vi) continue;
        l->sx = x; l->sy = y; l->sw = w; l->sh = h;
        l->cx = cx; l->cy = cy; l->cw = cw; l->ch = ch; l->visto = 1;
        if (l->pw != w || l->ph != h) {
            free(l->px);
            l->px = (unsigned int *)calloc((size_t)w * (size_t)h, 4);
            l->pw = w; l->ph = h; l->ha_foto = 0;
            if (l->pronto && l->video) metronomo_avvia();
        }
        if (l->ha_foto) { disegna_foto(l); return; }
        break;
    }
    /* nothing to show yet: black, as the browsers */
    {
        int xx = x, yy = y, ww = w, hh = h;

        if (xx < cx) { ww -= cx - xx; xx = cx; }
        if (yy < cy) { hh -= cy - yy; yy = cy; }
        if (xx + ww > cx + cw) ww = cx + cw - xx;
        if (yy + hh > cy + ch) hh = cy + ch - yy;
        if (ww > 0 && hh > 0) ex_fill_rect(g_f, xx, yy, ww, hh, 0x000000);
    }
}

void media_tutti_via(void)
{
    int i;

    for (i = 0; i < LETT_MAX; i++)
        if (g_lett[i].usato && g_lett[i].vista == g_vi) {
            if (g_audio_di == &g_lett[i]) audio_chiudi();
            if (g_lett[i].mv && g_lett[i].sbv < 0) xm_chiudi(g_lett[i].mv);
            free(g_lett[i].px);
            memset(&g_lett[i], 0, sizeof(g_lett[i]));
        }
    for (i = 0; i < SB_MAX; i++)
        if (g_sb[i].usato && g_sb[i].vista == g_vi) { xm_chiudi(g_sb[i].m); memset(&g_sb[i], 0, sizeof(g_sb[i])); }
}

/* --------------------------------------------------------------- natives */
static Lettore *lett_di(ExJsCtx *c, ExJsVal v)
{
    int i = (int)exjs_a_numero(c, v);

    return (i >= 0 && i < LETT_MAX && g_lett[i].usato && g_lett[i].vista == g_vi) ? &g_lett[i] : 0;
}

static Sb *sb_di(ExJsCtx *c, ExJsVal v)
{
    int i = (int)exjs_a_numero(c, v);

    return (i >= 0 && i < SB_MAX && g_sb[i].usato && g_sb[i].vista == g_vi) ? &g_sb[i] : 0;
}

/* the node of an element: through exbrowser.c's bridge */
int media_nodo(ExJsVal v);

static ExJsVal nat_media(ExJsCtx *c, ExJsVal questo, const ExJsVal *a, int n_arg, void *dato)
{
    const char *op;
    char        o[16];
    Lettore    *l;

    (void)questo; (void)dato;
    if (n_arg < 1 || !(op = exjs_a_stringa(c, a[0]))) return exjs_indefinito();
    snprintf(o, sizeof(o), "%s", op);
    if (!exmedia_pronta()) return exjs_numero(c, -1);
    if (!strcmp(o, "nuovo")) {
        int nodo = n_arg > 1 ? media_nodo(a[1]) : -1, i, libero = -1;

        if (nodo < 0) return exjs_numero(c, -1);
        for (i = 0; i < LETT_MAX; i++) {
            if (g_lett[i].usato && g_lett[i].nodo == nodo && g_lett[i].vista == g_vi) return exjs_numero(c, i);
            if (!g_lett[i].usato && libero < 0) libero = i;
        }
        if (libero < 0) return exjs_numero(c, -1);
        memset(&g_lett[libero], 0, sizeof(Lettore));
        g_lett[libero].usato = 1; g_lett[libero].nodo = nodo; g_lett[libero].vista = g_vi;
        g_lett[libero].sbv = g_lett[libero].sba = -1; g_lett[libero].volume = 100;
        return exjs_numero(c, libero);
    }
    if (!strcmp(o, "sb")) {
        int i;

        for (i = 0; i < SB_MAX && g_sb[i].usato; i++) {}
        if (i == SB_MAX || !(g_sb[i].m = xm_apri())) return exjs_numero(c, -1);
        g_sb[i].usato = 1; g_sb[i].vista = g_vi;
        return exjs_numero(c, i);
    }
    if (!strcmp(o, "sb_aggiungi") || !strcmp(o, "sb_fine") || !strcmp(o, "sb_fin")) {
        Sb *s = n_arg > 1 ? sb_di(c, a[1]) : 0;

        if (!s) return exjs_numero(c, 0);
        if (!strcmp(o, "sb_aggiungi")) {
            unsigned int n;
            void        *b = n_arg > 2 ? exjs_buffer_dati(c, a[2], &n) : 0;

            if (!b) return exjs_numero(c, 0);
            metronomo_avvia();
            if (!xm_aggiungi(s->m, b, n)) return exjs_numero(c, 0);
            /* ! read what came, now: `buffered` says it at updateend */
            xm_leggi(s->m);
            return exjs_numero(c, 1);
        }
        if (!strcmp(o, "sb_fine")) { xm_fine(s->m); xm_leggi(s->m); return exjs_numero(c, 1); }
        return exjs_numero(c, xm_fin_qui(s->m));
    }
    l = n_arg > 1 ? lett_di(c, a[1]) : 0;
    if (!l) return exjs_indefinito();
    if (!strcmp(o, "carica")) {
        const char    *u = n_arg > 2 ? exjs_a_stringa(c, a[2]) : 0;
        char           url[1024];
        unsigned char *d = 0;
        unsigned int   n = 0;

        if (!u || !u[0]) return exjs_numero(c, 0);
        snprintf(url, sizeof(url), "%s", u);
        if (l->mv && l->sbv < 0) xm_chiudi(l->mv);
        l->mv = l->ma = 0; l->pronto = l->finito = l->ha_foto = 0; l->t_base = 0; l->suona = 0;
        if (!media_scarica(url, &d, &n) || !(l->mv = xm_apri())) { free(d); l->finito = 1; return exjs_numero(c, 0); }
        xm_aggiungi(l->mv, d, n);
        xm_fine(l->mv);
        free(d);
        metronomo_avvia();
        return exjs_numero(c, 1);
    }
    if (!strcmp(o, "collega")) {
        Sb *v = n_arg > 2 ? sb_di(c, a[2]) : 0, *s = n_arg > 3 ? sb_di(c, a[3]) : 0;

        l->mv = v ? v->m : (s ? s->m : 0);
        l->ma = s ? s->m : 0;
        l->sbv = v ? (int)(v - g_sb) : -2; l->sba = s ? (int)(s - g_sb) : -1;
        l->pronto = l->finito = l->ha_foto = 0;
        metronomo_avvia();
        return exjs_numero(c, l->mv != 0);
    }
    if (!strcmp(o, "suona")) {
        if (!l->suona) { l->t_ms = uptime_ms(); l->suona = 1; l->finito = 0; }
        metronomo_avvia();
        return exjs_numero(c, 1);
    }
    if (!strcmp(o, "pausa")) {
        if (l->suona) { l->t_base = ora_media(l); l->suona = 0; }
        if (g_audio_di == l) audio_chiudi();
        return exjs_numero(c, 1);
    }
    if (!strcmp(o, "vai")) {
        double t = n_arg > 2 ? exjs_a_numero(c, a[2]) : 0;

        if (t < 0) t = 0;
        if (l->mv) xm_cerca(l->mv, t);
        if (l->ma && l->ma != l->mv) xm_cerca(l->ma, t);
        l->t_base = t; l->t_ms = uptime_ms(); l->ha_foto = 0; l->finito = 0;
        if (g_audio_di == l) audio_chiudi();
        metronomo_avvia();
        return exjs_numero(c, 1);
    }
    if (!strcmp(o, "volume")) {
        double v = n_arg > 2 ? exjs_a_numero(c, a[2]) : 1;

        l->volume = (int)(v * 100 + 0.5);
        if (l->volume < 0) l->volume = 0;
        if (l->volume > 100) l->volume = 100;
        return exjs_numero(c, 1);
    }
    if (!strcmp(o, "stato")) {
        char j[256];

        snprintf(j, sizeof(j), "{\"t\":%.3f,\"durata\":%.3f,\"pronto\":%d,\"w\":%d,\"h\":%d,\"finito\":%d,\"suona\":%d,\"audio\":%d,\"video\":%d,\"errore\":%d}",
                 ora_media(l), l->durata, l->pronto, l->w, l->h, l->finito, l->suona, l->audio, l->video,
                 l->finito && !l->pronto);
        return exjs_stringa(c, j, -1);
    }
    return exjs_indefinito();
}

void media_registra(ExJsCtx *js)
{
    ExJsErrore err;
    ExJsVal    r;

    /* ! libexmedia and FFmpeg are opened at the first <video>, not here */
    exjs_metti(js, exjs_globale(js), "__exos_lettore", exjs_nativa(js, nat_media, 0, "__exos_lettore"));
    memset(&err, 0, sizeof(err));
    if (!exjs_esegui(js, MEDIA_JS, (unsigned int)(sizeof(MEDIA_JS) - 1), &r, &err))
        printf("exbrowser: media, riga %d: %s\n", err.riga, err.messaggio);
}
