/* =============================================================================
 * bin/extaskbar/extaskbar.c — the text status bar, and the services' logs
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     extaskbar -show       shows the bar (and starts the service if needed)
 *     extaskbar -hide       hides it: the logs are still collected, the
 *                           console stays clean
 *     extaskbar -stop       stops the service: everything the services print
 *                           goes back to the console
 *     extaskbar -menu       which items the bar shows, and the settings
 *     extaskbar -details    each service's log, live, and whether to keep it
 *                           in /boot/<service>/log.txt
 *
 * Asked by the user on 16 October 2026: what the services print in the
 * background (WiFi, USB, automount...) made the command line unusable. While
 * this service runs, the kernel hands it what background processes write
 * (SYS_EXTASKBAR, lib/include/extaskbar_abi.h) instead of putting it on the
 * screen; the bar on the last line shows the items the services send
 * (lib/include/extaskbar.h). 80 columns only, as the user asked: the items
 * have fixed widths and `-menu` counts the columns left.
 *
 * Each service's log is kept in memory (64 KB each unless changed in -menu),
 * the oldest lines going first; with saving on, it is also appended to
 * /boot/<service>/log.txt, which never grows past 1024 KB - over that the
 * oldest lines are cut. The settings are in $HOME/.config/extaskbar.cfg.
 *
 * -menu and -details draw on the terminal's alternate screen: when they end
 * the console shows again what it showed before, untouched. They work at the
 * console and over telnet.
 * ============================================================================= */
#include "libc.h"
#include <stdarg.h>
#include "kbd_proto.h"
#include "extaskbar_abi.h"

#define VERSIONE        "0.002"
#define COLONNE         80
#define VOCI_MAX        24
#define LOG_MAX         32
#define FILE_MAX        (1024u * 1024u)
#define FILE_TAGLIO     (896u * 1024u)      /* what is left after a cut */
#define MEM_KB_BASE     64
#define ATTR_BARRA      0x1F                /* white on blue */
#define ATTR_ATTENZIONE 0x4E                /* yellow on red */
#define ATTR_SEP        0x17                /* grey on blue */

static int extb(unsigned int op, const void *a, unsigned int n)
{
    int r;

    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "a"(SYS_EXTASKBAR), "b"((uintptr_t)op), "c"((uintptr_t)a), "d"((uintptr_t)n)
        : "memory");
    return r;
}

/* =============================================================================
 * The configuration
 * ============================================================================= */
typedef struct {
    int  avvio;                         /* root's: started by /boot/avvio.sh */
    int  barra;                         /* shown at start */
    char voci[VOCI_MAX][EXTB_VOCE_NOME];
    int  n_voci;
    int  mem_kb;
    char salva[LOG_MAX][20];
    int  n_salva;
} Config;

static const char *VOCI_BASE[] = { "USB", "WIFI", "RX", "TX", "FAIL", "LOST" };

static void cfg_base(Config *c)
{
    int i;

    memset(c, 0, sizeof(*c));
    c->barra = 1;
    c->mem_kb = MEM_KB_BASE;
    for (i = 0; i < (int)(sizeof(VOCI_BASE) / sizeof(VOCI_BASE[0])); i++)
        strcpy(c->voci[c->n_voci++], VOCI_BASE[i]);
}

static void cfg_percorso(char *out, int n)
{
    const char *h = getenv("HOME");
    int l;

    if (!h || !h[0]) h = "/root";
    l = (int)strlen(h);
    while (l > 0 && h[l - 1] == '/') l--;               /* HOME=/ on the live CD */
    snprintf(out, (size_t)n, "%.*s/.config/extaskbar.cfg", l, h);
}

/* "a,b,c" into a table */
static int lista(const char *s, char (*t)[20], int max, int larg)
{
    int n = 0, k = 0;

    while (*s == ' ') s++;
    for (; ; s++) {
        if (*s == ',' || *s == 0 || *s == '\n' || *s == '\r') {
            while (k > 0 && t[n][k - 1] == ' ') k--;
            if (k > 0 && n < max) { t[n][k] = 0; n++; }
            k = 0;
            if (*s != ',') break;
            while (s[1] == ' ') s++;
            continue;
        }
        if (n < max && k < larg - 1) t[n][k++] = *s;
    }
    return n;
}

static void cfg_leggi(Config *c, const char *percorso)
{
    char  riga[512], *v;
    FILE *f;

    cfg_base(c);
    f = fopen(percorso, "r");
    if (!f) return;
    while (fgets(riga, sizeof(riga), f)) {
        if (riga[0] == '#' || !(v = strchr(riga, '='))) continue;
        *v++ = 0;
        while (*v == ' ') v++;
        if (strncmp(riga, "barra", 5) == 0) c->barra = strncmp(v, "no", 2) != 0;
        else if (strncmp(riga, "avvio", 5) == 0) c->avvio = strncmp(v, "si", 2) == 0;
        else if (strncmp(riga, "memoria_kb", 10) == 0) {
            c->mem_kb = atoi(v);
            if (c->mem_kb < 4) c->mem_kb = 4;
            if (c->mem_kb > 1024) c->mem_kb = 1024;
        } else if (strncmp(riga, "voci", 4) == 0) {
            static char t[VOCI_MAX][20];
            int i;

            memset(t, 0, sizeof(t));
            c->n_voci = lista(v, t, VOCI_MAX, EXTB_VOCE_NOME);
            for (i = 0; i < c->n_voci; i++) strcpy(c->voci[i], t[i]);
        } else if (strncmp(riga, "salva", 5) == 0) {
            memset(c->salva, 0, sizeof(c->salva));
            c->n_salva = lista(v, c->salva, LOG_MAX, 20);
        }
    }
    fclose(f);
}

static int cfg_scrivi(const Config *c, const char *percorso)
{
    char  dir[160], *p;
    FILE *f;
    int   i;

    strncpy(dir, percorso, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = 0;
    if ((p = strrchr(dir, '/')) != NULL) { *p = 0; mkdir(dir, 0755); }
    f = fopen(percorso, "w");
    if (!f) return -1;
    fprintf(f, "# extaskbar %s - written by `extaskbar -menu` and `-details`\n", VERSIONE);
    fprintf(f, "avvio = %s\n", c->avvio ? "si" : "no");
    fprintf(f, "barra = %s\n", c->barra ? "si" : "no");
    fprintf(f, "memoria_kb = %d\n", c->mem_kb);
    fprintf(f, "voci = ");
    for (i = 0; i < c->n_voci; i++) fprintf(f, "%s%s", i ? "," : "", c->voci[i]);
    fprintf(f, "\nsalva = ");
    for (i = 0; i < c->n_salva; i++) fprintf(f, "%s%s", i ? "," : "", c->salva[i]);
    fprintf(f, "\n");
    fclose(f);
    return 0;
}

/* =============================================================================
 * THE SERVICE
 * ============================================================================= */
typedef struct {
    char nome[EXTB_VOCE_NOME];
    int  larghezza;
    char testo[EXTB_VOCE_TESTO];
    int  attenzione;
} Voce;

typedef struct {
    char          nome[20];
    char         *buf;
    unsigned int  n, cap;
    unsigned int  righe;
    unsigned int  pid;
    int           su_file;
    unsigned int  file_byte;
    int           a_capo;           /* the last byte kept ended a line */
} Log;

static Config g_cfg;
static char   g_cfg_percorso[160];
static Voce   g_voci[VOCI_MAX];
static int    g_n_voci;
static Log    g_log[LOG_MAX];
static int    g_n_log;
static int    g_mostrata;
static int    g_cambiata = 1;

/* the items every bar knows, with the text they show until their service
 * says something: the user's widths (16 October 2026) */
static void voci_note(void)
{
    static const struct { const char *n; int l; const char *t; } NOTE[] = {
        { "USB",    15, "USB none" },        { "WIFI",  15, "WIFI none" },
        { "RX",     12, "RX 00000000" },     { "TX",    12, "TX 00000000" },
        { "FAIL",   11, "FAIL 000000" },     { "LOST",  11, "LOST 000000" },
        { "ETH",    15, "ETH none" },        { "ORA",    7, "--:--" },
        { "BAT",    10, "BAT --" },          { "TELNET", 12, "TELNET off" },
    };
    int i;

    for (i = 0; i < (int)(sizeof(NOTE) / sizeof(NOTE[0])); i++) {
        strcpy(g_voci[i].nome, NOTE[i].n);
        g_voci[i].larghezza = NOTE[i].l;
        strcpy(g_voci[i].testo, NOTE[i].t);
    }
    g_n_voci = i;
}

static Voce *voce(const char *nome)
{
    int i;

    for (i = 0; i < g_n_voci; i++) if (strcmp(g_voci[i].nome, nome) == 0) return &g_voci[i];
    return NULL;
}

static int mostrata(const char *nome)
{
    int i;

    for (i = 0; i < g_cfg.n_voci; i++) if (strcmp(g_cfg.voci[i], nome) == 0) return 1;
    return 0;
}

static void barra_disegna(void)
{
    unsigned short c[COLONNE];
    int      col = 0, i, k;

    for (k = 0; k < COLONNE; k++) c[k] = (unsigned short)(' ' | (ATTR_BARRA << 8));
    for (i = 0; i < g_cfg.n_voci; i++) {
        Voce *v = voce(g_cfg.voci[i]);
        int   a, l;

        if (!v || col + v->larghezza > COLONNE) continue;
        a = v->attenzione ? ATTR_ATTENZIONE : ATTR_BARRA;
        l = v->larghezza;
        /* the text may fill the whole width ("FAIL 000000" is 11 of 11); when
         * it leaves the last column free, that column is the separator */
        for (k = 0; k < l && v->testo[k]; k++) c[col + k] = (unsigned short)((unsigned char)v->testo[k] | (a << 8));
        for (; k < l; k++) c[col + k] = (unsigned short)(' ' | (a << 8));
        if ((int)strlen(v->testo) < l) c[col + l - 1] = (unsigned short)(0xB3 | (ATTR_SEP << 8));   /* CP437 thin bar */
        col += v->larghezza;
    }
    extb(EXTB_BARRA, c, COLONNE);
}

static void barra(int mostra)
{
    g_mostrata = mostra;
    if (mostra) barra_disegna();
    else extb(EXTB_BARRA, NULL, 0);
}

/* --- the logs --------------------------------------------------------------------- */
static int salvato(const char *nome)
{
    int i;

    for (i = 0; i < g_cfg.n_salva; i++) if (strcmp(g_cfg.salva[i], nome) == 0) return 1;
    return 0;
}

static void file_percorso(const char *nome, char *out, int n, int solo_dir)
{
    snprintf(out, (size_t)n, solo_dir ? "/boot/%s" : "/boot/%s/log.txt", nome);
}

static Log *log_di(const char *nome)
{
    Log *l;
    int  i;

    for (i = 0; i < g_n_log; i++) if (strcmp(g_log[i].nome, nome) == 0) return &g_log[i];
    if (g_n_log == LOG_MAX) return NULL;
    l = &g_log[g_n_log++];
    memset(l, 0, sizeof(*l));
    strncpy(l->nome, nome, sizeof(l->nome) - 1);
    l->cap = (unsigned int)g_cfg.mem_kb * 1024u;
    l->buf = malloc(l->cap);
    if (!l->buf) l->cap = 0;
    l->a_capo = 1;
    l->su_file = salvato(nome);
    return l;
}

/* the memory keeps the newest `cap` bytes, cut at a line */
static void log_stringi(Log *l, unsigned int serve)
{
    unsigned int via, k;

    if (l->n + serve <= l->cap) return;
    via = l->n + serve - l->cap;
    if (via < l->cap / 8) via = l->cap / 8;             /* a few lines at a time */
    if (via > l->n) via = l->n;
    while (via < l->n && l->buf[via - 1] != '\n') via++;
    for (k = 0; k < via; k++) if (l->buf[k] == '\n' && l->righe) l->righe--;
    memmove(l->buf, l->buf + via, l->n - via);
    l->n -= via;
}

/* /boot/<name>/log.txt past 1024 KB: the oldest lines go */
static void file_taglia(const char *percorso, unsigned int *byte)
{
    FILE *f = fopen(percorso, "r");
    char *b;
    long  n;
    unsigned int da;

    if (!f) return;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    if (n <= (long)FILE_MAX) { fclose(f); *byte = (unsigned int)n; return; }
    b = malloc((size_t)n);
    if (!b) { fclose(f); return; }
    fseek(f, 0, SEEK_SET);
    n = (long)fread(b, 1, (size_t)n, f);
    fclose(f);
    da = (unsigned int)n - FILE_TAGLIO;
    while (da < (unsigned int)n && b[da - 1] != '\n') da++;
    f = fopen(percorso, "w");
    if (f) { fwrite(b + da, 1, (size_t)((unsigned int)n - da), f); fclose(f); }
    *byte = (unsigned int)n - da;
    free(b);
}

static void file_aggiungi(Log *l, const char *t, unsigned int n)
{
    char  p[64];
    FILE *f;

    file_percorso(l->nome, p, sizeof(p), 1);
    mkdir(p, 0755);
    file_percorso(l->nome, p, sizeof(p), 0);
    f = fopen(p, "a");
    if (!f) return;
    fwrite(t, 1, n, f);
    fclose(f);
    l->file_byte += n;
    if (l->file_byte > FILE_MAX) file_taglia(p, &l->file_byte);
}

/* text from the kernel: escape sequences and carriage returns out, a time at
 * the start of every line */
static void log_aggiungi(const char *nome, unsigned int pid, const char *t, unsigned int n)
{
    static char pulito[EXTB_TESTO_MAX * 2];
    unsigned int i, m = 0;
    Log *l = log_di(nome);

    if (!l || !l->buf) return;
    l->pid = pid;
    for (i = 0; i < n && m < sizeof(pulito) - 16; i++) {
        unsigned char ch = (unsigned char)t[i];

        if (ch == 0x1B) {                               /* ESC [ ... letter */
            i++;
            if (i < n && t[i] == '[')
                while (i + 1 < n && !((t[i + 1] >= 'A' && t[i + 1] <= 'Z') || (t[i + 1] >= 'a' && t[i + 1] <= 'z'))) i++;
            i++;
            continue;
        }
        if (ch == '\r') continue;
        if (ch != '\n' && ch != '\t' && ch < 32) continue;
        if (l->a_capo) {
            time_t     o = time(NULL);
            struct tm  tm;

            localtime_r(&o, &tm);
            m += (unsigned int)snprintf(pulito + m, 16, "%02d:%02d:%02d ", tm.tm_hour, tm.tm_min, tm.tm_sec);
            l->a_capo = 0;
        }
        pulito[m++] = (char)ch;
        if (ch == '\n') { l->a_capo = 1; l->righe++; }
    }
    if (!m) return;
    log_stringi(l, m);
    if (m > l->cap) return;
    memcpy(l->buf + l->n, pulito, m);
    l->n += m;
    if (l->su_file) file_aggiungi(l, pulito, m);
}

static void kernel_svuota(void)
{
    static unsigned char b[16384];
    int n, k;

    while ((n = extb(EXTB_LEGGI, b, sizeof(b))) > 0) {
        for (k = 0; k + (int)sizeof(ExtbRecord) <= n; ) {
            ExtbRecord *r = (ExtbRecord *)(b + k);
            char nome[21];

            memcpy(nome, r->nome, 20);
            nome[20] = 0;
            if (!nome[0]) strcpy(nome, "?");
            if (r->persi) {
                char t[64];
                int  tn = snprintf(t, sizeof(t), "[extaskbar: %u messaggi persi, anello pieno]\n", r->persi);
                log_aggiungi(nome, r->pid, t, (unsigned)tn);
            }
            log_aggiungi(nome, r->pid, (const char *)(r + 1), r->len);
            k += (int)sizeof(ExtbRecord) + r->len;
        }
        if (n < (int)sizeof(b) / 2) break;
    }
}

static void applica_cfg(void)
{
    int i;

    for (i = 0; i < g_n_log; i++) {
        Log *l = &g_log[i];
        unsigned int cap = (unsigned int)g_cfg.mem_kb * 1024u;

        l->su_file = salvato(l->nome);
        if (cap != l->cap) {
            char *nb;

            if (l->n > cap) { l->cap = l->n; log_stringi(l, l->n - cap); }
            nb = realloc(l->buf, cap);
            if (nb) { l->buf = nb; l->cap = cap; }
        }
    }
    g_cambiata = 1;
}

static void rispondi(unsigned int pid, int codice)
{
    ipc_send(pid, EXTB_MSG_ESITO, &codice, sizeof(codice));
}

static int servi(void)
{
    static unsigned char buf[IPC_MSG_MAX_DATA];
    IpcMessage meta;
    unsigned int ultimo = 0;
    int i;

    if (getuid() != 0) { printf("extaskbar: il servizio parte solo da root\n"); return 1; }
    if (ipc_register(EXTB_SERVIZIO) < 0) { printf("extaskbar: c'e' gia'\n"); return 1; }
    if (extb(EXTB_REGISTRA, NULL, 0) != 0) { printf("extaskbar: il kernel non ha SYS_EXTASKBAR (serve 0.255)\n"); return 1; }
    cfg_percorso(g_cfg_percorso, sizeof(g_cfg_percorso));
    cfg_leggi(&g_cfg, g_cfg_percorso);
    voci_note();
    barra(g_cfg.barra);

    for (;;) {
        int r = ipc_recv_timeout(&meta, buf, sizeof(buf), 200);

        kernel_svuota();
        if (r >= 0) switch (meta.tipo) {
        case EXTB_MSG_VOCE: {
            ExtbVoce v;
            Voce    *x;

            memset(&v, 0, sizeof(v));
            memcpy(&v, buf, meta.len < sizeof(v) ? meta.len : sizeof(v));
            v.nome[EXTB_VOCE_NOME - 1] = 0;
            v.testo[EXTB_VOCE_TESTO - 1] = 0;
            if (!v.nome[0]) break;
            x = voce(v.nome);
            if (!x && g_n_voci < VOCI_MAX) { x = &g_voci[g_n_voci++]; strcpy(x->nome, v.nome); }
            if (!x) break;
            if (v.larghezza >= 4 && v.larghezza <= COLONNE) x->larghezza = (int)v.larghezza;
            strcpy(x->testo, v.testo);
            x->attenzione = v.attenzione != 0;
            if (mostrata(x->nome)) g_cambiata = 1;
            break;
        }
        case EXTB_MSG_COMANDO: {
            ExtbComando c;

            memset(&c, 0, sizeof(c));
            memcpy(&c, buf, meta.len < sizeof(c) ? meta.len : sizeof(c));
            c.nome[sizeof(c.nome) - 1] = 0;
            c.percorso[sizeof(c.percorso) - 1] = 0;
            switch (c.cmd) {
            case EXTB_CMD_MOSTRA:   barra(1); rispondi(meta.sender_pid, 0); break;
            case EXTB_CMD_NASCONDI: barra(0); rispondi(meta.sender_pid, 0); break;
            case EXTB_CMD_FERMA:
                barra(0);
                extb(EXTB_LASCIA, NULL, 0);
                rispondi(meta.sender_pid, 0);
                return 0;
            case EXTB_CMD_RILEGGI:
                if (c.percorso[0]) strcpy(g_cfg_percorso, c.percorso);
                cfg_leggi(&g_cfg, g_cfg_percorso);
                applica_cfg();
                rispondi(meta.sender_pid, 0);
                break;
            case EXTB_CMD_IMPOSTA: {
                static char t[VOCI_MAX][20];
                int k;

                memset(t, 0, sizeof(t));
                g_cfg.n_voci = lista(c.percorso, t, VOCI_MAX, EXTB_VOCE_NOME);
                for (k = 0; k < g_cfg.n_voci; k++) strcpy(g_cfg.voci[k], t[k]);
                if (c.valore >= 4 && c.valore <= 1024) g_cfg.mem_kb = (int)c.valore;
                if (strncmp(c.nome, "avvio=", 6) == 0) g_cfg.avvio = strcmp(c.nome + 6, "si") == 0;
                applica_cfg();
                rispondi(meta.sender_pid, cfg_scrivi(&g_cfg, g_cfg_percorso) == 0 ? 0 : -EIO);
                break;
            }
            case EXTB_CMD_SALVA: {
                int k, j;

                for (k = 0; k < g_cfg.n_salva && strcmp(g_cfg.salva[k], c.nome) != 0; k++) ;
                if (c.valore && k == g_cfg.n_salva && g_cfg.n_salva < LOG_MAX) strcpy(g_cfg.salva[g_cfg.n_salva++], c.nome);
                if (!c.valore && k < g_cfg.n_salva) {
                    for (j = k; j + 1 < g_cfg.n_salva; j++) strcpy(g_cfg.salva[j], g_cfg.salva[j + 1]);
                    g_cfg.n_salva--;
                }
                cfg_scrivi(&g_cfg, g_cfg_percorso);
                applica_cfg();
                /* switched on: what is in memory goes to the file first */
                for (k = 0; k < g_n_log; k++)
                    if (c.valore && strcmp(g_log[k].nome, c.nome) == 0 && g_log[k].n) {
                        char p[64];
                        struct stat st;

                        file_percorso(c.nome, p, sizeof(p), 0);
                        g_log[k].file_byte = stat(p, &st) == 0 ? (unsigned int)st.st_size : 0;
                        file_aggiungi(&g_log[k], g_log[k].buf, g_log[k].n);
                    }
                rispondi(meta.sender_pid, 0);
                break;
            }
            default: rispondi(meta.sender_pid, -EINVAL); break;
            }
            break;
        }
        case EXTB_MSG_ELENCO:
            for (i = 0; i < g_n_log; i++) {
                ExtbServizio s;

                memset(&s, 0, sizeof(s));
                strcpy(s.nome, g_log[i].nome);
                s.byte = g_log[i].n;
                s.righe = g_log[i].righe;
                s.su_file = (unsigned)g_log[i].su_file;
                s.pid = g_log[i].pid;
                ipc_send(meta.sender_pid, EXTB_MSG_SERVIZIO, &s, sizeof(s));
            }
            ipc_send(meta.sender_pid, EXTB_MSG_FINE, NULL, 0);
            break;
        case EXTB_MSG_LOG: {
            ExtbChiediLog q;
            Log *l = NULL;

            memset(&q, 0, sizeof(q));
            memcpy(&q, buf, meta.len < sizeof(q) ? meta.len : sizeof(q));
            q.nome[sizeof(q.nome) - 1] = 0;
            for (i = 0; i < g_n_log; i++) if (strcmp(g_log[i].nome, q.nome) == 0) l = &g_log[i];
            if (l) {
                unsigned int da = q.dalla_fine < l->n ? l->n - q.dalla_fine : 0, k;

                while (da > 0 && da < l->n && l->buf[da - 1] != '\n') da++;
                for (k = da; k < l->n; k += 1024)
                    ipc_send(meta.sender_pid, EXTB_MSG_TESTO, l->buf + k, l->n - k < 1024 ? l->n - k : 1024);
            }
            ipc_send(meta.sender_pid, EXTB_MSG_FINE, NULL, 0);
            break;
        }
        default:
            break;
        }
        /* the clock: the bar's own item */
        if (uptime_ms() - ultimo >= 1000) {
            Voce *o = voce("ORA");
            time_t t = time(NULL);
            struct tm tm;
            char x[8];

            ultimo = uptime_ms();
            localtime_r(&t, &tm);
            snprintf(x, sizeof(x), "%02d:%02d", tm.tm_hour, tm.tm_min);
            if (o && strcmp(o->testo, x) != 0) { strcpy(o->testo, x); if (mostrata("ORA")) g_cambiata = 1; }
        }
        if (g_cambiata && g_mostrata) { g_cambiata = 0; barra_disegna(); }
    }
}

/* =============================================================================
 * THE CLIENT SIDE
 * ============================================================================= */
static int g_srv;

static int chiedi(unsigned int cmd, unsigned int valore, const char *nome, const char *percorso)
{
    ExtbComando c;
    IpcMessage  meta;
    int         esito = -1, i;

    memset(&c, 0, sizeof(c));
    c.cmd = cmd;
    c.valore = valore;
    if (nome) strncpy(c.nome, nome, sizeof(c.nome) - 1);
    if (percorso) strncpy(c.percorso, percorso, sizeof(c.percorso) - 1);
    if (ipc_send((unsigned int)g_srv, EXTB_MSG_COMANDO, &c, sizeof(c)) < 0) return -1;
    for (i = 0; i < 10; i++) {
        if (ipc_recv_timeout(&meta, &esito, sizeof(esito), 2000) < 0) return -1;
        if ((int)meta.sender_pid == g_srv && meta.tipo == EXTB_MSG_ESITO) return esito;
    }
    return -1;
}

static int avvia_servizio(char *argv0)
{
    char *av[3];
    int   i, pid;

    av[0] = argv0;
    av[1] = "-servizio";
    av[2] = NULL;
    pid = spawn(strchr(argv0, '/') ? argv0 : "extaskbar", av);
    if (pid < 0) pid = spawn("/bin/extaskbar", av);
    if (pid < 0) { printf("extaskbar: non riesco ad avviare il servizio (%d)\n", pid); return -1; }
    for (i = 0; i < 30; i++) {
        usleep(100000);
        if ((g_srv = ipc_lookup(EXTB_SERVIZIO)) > 0) return 0;
    }
    printf("extaskbar: il servizio non risponde\n");
    return -1;
}

/* --- the keyboard and the screen, at the console or on a terminal ------------------- */
static int g_kbd, g_pty, g_console;
static int g_righe = 24, g_colonne = 80;

static void kbd_modo(unsigned int m)
{
    KbdSetMode s;

    s.modo = m;
    s.console = (unsigned int)g_console;
    ipc_send((unsigned int)g_kbd, KBD_MSG_SETMODE, &s, sizeof(s));
}

static int schermo_apri(void)
{
    ConsoleInfo ci;
    TtyWinSize  ws;

    if (isatty(0) && pty_ctl(0, PTY_CTL_MODO, 0) == 0) {   /* a pseudo-terminal: telnet */
        unsigned int m = (unsigned int)pty_ctl(0, PTY_CTL_LEGGI_MISURA, 0);

        g_pty = 1;
        if (m) { g_righe = (int)(m >> 16); g_colonne = (int)(m & 0xffff); }
    } else {
        if (console_info(&ci) != 0) return -1;
        if (ci.fg != 0 && ci.fg != (unsigned)getpid()) return -1;
        g_console = (int)ci.mia;
        g_kbd = ipc_lookup(KBD_SERVICE_NAME);
        if (g_kbd <= 0) return -1;
        if (tty_getsize(&ws) == 0 && ws.rows) { g_righe = ws.rows; g_colonne = ws.cols; }
        tty_raw(1);
        kbd_modo(KBD_MODE_RAW);
    }
    if (g_colonne > COLONNE) g_colonne = COLONNE;           /* 80 columns, always */
    if (g_righe < 10) g_righe = 10;
    printf("\x1B[?1049h\x1B[?25l");
    fflush(stdout);
    return 0;
}

static void schermo_chiudi(void)
{
    printf("\x1B[0m\x1B[?25h\x1B[?1049l");
    fflush(stdout);
    if (g_pty) pty_ctl(0, PTY_CTL_MODO, PTY_CANONICO | PTY_ECO);
    else { kbd_modo(KBD_MODE_COOKED); tty_raw(0); }
}

#define T_SU     1001
#define T_GIU    1002
#define T_SIN    1003
#define T_DES    1004
#define T_PGSU   1005
#define T_PGGIU  1006
#define T_NIENTE 0

/* the next key, or T_NIENTE after `ms` without one */
static int tasto(int ms)
{
    if (g_pty) {
        struct pollfd p;
        unsigned char c, s[4];

        p.fd = 0; p.events = POLLIN; p.revents = 0;
        if (poll(&p, 1, ms) <= 0) return T_NIENTE;
        if (read(0, &c, 1) != 1) return 'q';
        if (c != 0x1B) return c == '\r' ? '\n' : c;
        p.revents = 0;
        if (poll(&p, 1, 50) <= 0) return 0x1B;
        if (read(0, s, 1) != 1 || s[0] != '[') return 0x1B;
        if (read(0, s + 1, 1) != 1) return 0x1B;
        switch (s[1]) {
        case 'A': return T_SU;  case 'B': return T_GIU;
        case 'C': return T_DES; case 'D': return T_SIN;
        case '5': read(0, s + 2, 1); return T_PGSU;
        case '6': read(0, s + 2, 1); return T_PGGIU;
        default:  return T_NIENTE;
        }
    } else {
        IpcMessage meta;
        unsigned int k;
        unsigned int fine = uptime_ms() + (unsigned)ms;

        if (ipc_send((unsigned int)g_kbd, KBD_MSG_READKEY, &g_console, sizeof(g_console)) < 0) return 'q';
        for (;;) {
            int resta = (int)(fine - uptime_ms());

            if (resta <= 0) resta = 1;
            if (ipc_recv_timeout(&meta, &k, sizeof(k), (unsigned)resta) < 0) { kbd_modo(KBD_MODE_RAW); return T_NIENTE; }
            if ((int)meta.sender_pid != g_kbd || meta.tipo != KBD_MSG_KEY) continue;
            switch (k & KBD_KEY_MASK) {
            case KBD_K_UP: return T_SU;     case KBD_K_DOWN: return T_GIU;
            case KBD_K_LEFT: return T_SIN;  case KBD_K_RIGHT: return T_DES;
            case KBD_K_PGUP: return T_PGSU; case KBD_K_PGDN: return T_PGGIU;
            default: return (int)(k & 0xff) == '\r' ? '\n' : (int)(k & 0xff);
            }
        }
    }
}

/* ! A SCREEN GOES OUT IN ONE WRITE: drawn piece by piece, the second's
 * redraw of -details was seen half done (16 October 2026). The libc's
 * setvbuf does not buffer, so the frame is put together here. */
static char g_q[65536];
static int  g_qn;

static int qf(const char *fmt, ...)
{
    va_list a;
    int     r;

    va_start(a, fmt);
    r = vsnprintf(g_q + g_qn, sizeof(g_q) - (size_t)g_qn, fmt, a);
    va_end(a);
    if (r < 0) return 0;
    if (g_qn + r > (int)sizeof(g_q) - 1) r = (int)sizeof(g_q) - 1 - g_qn;
    g_qn += r;
    return r;
}

static void qflush(void)
{
    int k = 0, w;

    while (k < g_qn && (w = (int)write(1, g_q + k, (size_t)(g_qn - k))) > 0) k += w;
    g_qn = 0;
}

static void vai(int r, int c) { qf("\x1B[%d;%dH", r, c); }

static void riga_piena(int r, const char *colore, const char *testo)
{
    int n;

    vai(r, 1);
    qf("%s", colore);
    n = qf("%.*s", g_colonne, testo);
    while (n++ < g_colonne) qf(" ");
    qf("\x1B[0m");
}

/* =============================================================================
 * -menu: which items, and the memory per log
 * ============================================================================= */
static int voci_dal_servizio(Voce *out, int max)
{
    /* the service does not list its items over IPC: the known ones, plus
     * those named in the configuration, are what can be chosen */
    int i, n;
    Config c;

    voci_note();
    n = g_n_voci < max ? g_n_voci : max;
    for (i = 0; i < n; i++) out[i] = g_voci[i];
    cfg_leggi(&c, g_cfg_percorso);
    for (i = 0; i < c.n_voci && n < max; i++) {
        int j, c2 = 0;
        for (j = 0; j < n; j++) if (strcmp(out[j].nome, c.voci[i]) == 0) c2 = 1;
        if (!c2) { memset(&out[n], 0, sizeof(out[n])); strcpy(out[n].nome, c.voci[i]); out[n].larghezza = 12; n++; }
    }
    return n;
}

static int occupate(const Config *c, const Voce *v, int n)
{
    int i, j, tot = 0;

    for (i = 0; i < c->n_voci; i++)
        for (j = 0; j < n; j++) if (strcmp(c->voci[i], v[j].nome) == 0) tot += v[j].larghezza;
    return tot;
}

/* every change is applied at once: by the service, which also writes its
 * configuration, or - with no service running - straight to the file */
static void menu_applica(const Config *c, char *avviso, int n)
{
    char l[128];
    int  i, k = 0, r;

    for (i = 0; i < c->n_voci && k < (int)sizeof(l) - EXTB_VOCE_NOME - 2; i++)
        k += snprintf(l + k, sizeof(l) - (size_t)k, "%s%s", i ? "," : "", c->voci[i]);
    l[k] = 0;
    if (g_srv > 0) r = chiedi(EXTB_CMD_IMPOSTA, (unsigned)c->mem_kb, c->avvio ? "avvio=si" : "avvio=no", l);
    else           r = cfg_scrivi(c, g_cfg_percorso);
    if (r != 0) snprintf(avviso, (size_t)n, "applicato, ma non riesco a scrivere %s", g_cfg_percorso);
}

static int menu(void)
{
    Voce v[VOCI_MAX];
    Config c;
    int n, sel = 0, i, k, fine = 0, cambiato = 0;
    char t[128], avviso[80] = "";

    cfg_leggi(&c, g_cfg_percorso);
    n = voci_dal_servizio(v, VOCI_MAX);
    if (schermo_apri() != 0) { printf("extaskbar: -menu vuole la console in primo piano o un terminale\n"); return 1; }
    while (!fine) {
        int libere = COLONNE - occupate(&c, v, n);

        qf("\x1B[0m\x1B[2J");
        riga_piena(1, "\x1B[37;44m", " extaskbar - voci della barra e impostazioni");
        vai(3, 3);
        qf("Spazi liberi sulla barra: %d di %d colonne", libere, COLONNE);
        for (i = 0; i < n; i++) {
            int on = 0;
            for (k = 0; k < c.n_voci; k++) if (strcmp(c.voci[k], v[i].nome) == 0) on = 1;
            vai(5 + i, 3);
            qf("%s[%c] %-8s %2d colonne  %-24s\x1B[0m", i == sel ? "\x1B[30;47m" : "",
                   on ? 'x' : ' ', v[i].nome, v[i].larghezza, v[i].testo);
        }
        vai(6 + n, 3);
        qf("%sMemoria per ogni log: %d KB  (sinistra/destra)\x1B[0m", sel == n ? "\x1B[30;47m" : "", c.mem_kb);
        vai(7 + n, 3);
        qf("%s[%c] Parte da sola all'avvio%s\x1B[0m", sel == n + 1 ? "\x1B[30;47m" : "", c.avvio ? 'x' : ' ',
           getuid() == 0 ? "" : "  (solo root)");
        vai(9 + n, 3);
        qf("\x1B[33m%s\x1B[0m", avviso);
        snprintf(t, sizeof(t), " su/giu' sceglie  spazio [x]  sinistra/destra memoria  q esce");
        riga_piena(g_righe, "\x1B[30;47m", t);
        qflush();
        avviso[0] = 0;

        switch (k = tasto(60000)) {
        case T_SU:  if (sel > 0) sel--; break;
        case T_GIU: if (sel < n + 1) sel++; break;
        case T_SIN: if (sel == n && c.mem_kb > 8) { c.mem_kb = c.mem_kb > 64 ? c.mem_kb - 32 : c.mem_kb - 8; cambiato = 1; } break;
        case T_DES: if (sel == n && c.mem_kb < 1024) { c.mem_kb = c.mem_kb >= 64 ? c.mem_kb + 32 : c.mem_kb + 8; cambiato = 1; } break;
        case 's': case 'S': break;
        case ' ': case '\n':
            if (sel == n + 1) {
                if (getuid() != 0) { snprintf(avviso, sizeof(avviso), "l'avvio automatico lo decide root"); break; }
                c.avvio = !c.avvio;
                cambiato = 1;
                break;
            }
            if (sel < n) {
                for (k = 0; k < c.n_voci && strcmp(c.voci[k], v[sel].nome) != 0; k++) ;
                if (k < c.n_voci) {
                    for (; k + 1 < c.n_voci; k++) strcpy(c.voci[k], c.voci[k + 1]);
                    c.n_voci--;
                } else if (v[sel].larghezza > libere) {
                    snprintf(avviso, sizeof(avviso), "%s vuole %d colonne, ne restano %d: togline un'altra",
                             v[sel].nome, v[sel].larghezza, libere);
                    break;
                } else if (c.n_voci < VOCI_MAX) strcpy(c.voci[c.n_voci++], v[sel].nome);
                cambiato = 1;
            }
            break;
        case 'q': case 'Q': case 0x1B: fine = 1; break;
        default: break;
        }
        if (cambiato) { menu_applica(&c, avviso, sizeof(avviso)); cambiato = 0; }
    }
    schermo_chiudi();
    return 0;
}

/* =============================================================================
 * -details: the services on the left, the chosen one's log on the right
 * ============================================================================= */
static int elenco(ExtbServizio *s, int max)
{
    IpcMessage meta;
    unsigned char b[IPC_MSG_MAX_DATA];
    int n = 0, i;

    if (ipc_send((unsigned int)g_srv, EXTB_MSG_ELENCO, NULL, 0) < 0) return 0;
    for (i = 0; i < 200; i++) {
        if (ipc_recv_timeout(&meta, b, sizeof(b), 2000) < 0) break;
        if ((int)meta.sender_pid != g_srv) continue;
        if (meta.tipo == EXTB_MSG_FINE) break;
        if (meta.tipo == EXTB_MSG_SERVIZIO && n < max) memcpy(&s[n++], b, sizeof(s[0]));
    }
    return n;
}

static int coda_log(const char *nome, char *out, int max)
{
    ExtbChiediLog q;
    IpcMessage meta;
    unsigned char b[IPC_MSG_MAX_DATA];
    int n = 0, i;

    memset(&q, 0, sizeof(q));
    strncpy(q.nome, nome, sizeof(q.nome) - 1);
    q.dalla_fine = (unsigned int)max - 1;
    if (ipc_send((unsigned int)g_srv, EXTB_MSG_LOG, &q, sizeof(q)) < 0) return 0;
    for (i = 0; i < 400; i++) {
        if (ipc_recv_timeout(&meta, b, sizeof(b), 2000) < 0) break;
        if ((int)meta.sender_pid != g_srv) continue;
        if (meta.tipo == EXTB_MSG_FINE) break;
        if (meta.tipo == EXTB_MSG_TESTO && n + (int)meta.len < max) { memcpy(out + n, b, meta.len); n += (int)meta.len; }
    }
    out[n] = 0;
    return n;
}

#define LARGA_ELENCO 22

static int dettagli(void)
{
    static ExtbServizio s[LOG_MAX];
    static char testo[48 * 1024];
    static char *righe[2048];
    int n, sel = 0, scorri = 0, fine = 0, i, k, nr, alte;
    char t[128], avviso[80] = "";

    if (schermo_apri() != 0) { printf("extaskbar: -details vuole la console in primo piano o un terminale\n"); return 1; }
    alte = g_righe - 3;
    while (!fine) {
        int larga = g_colonne - LARGA_ELENCO - 1;

        n = elenco(s, LOG_MAX);
        if (sel >= n) sel = n ? n - 1 : 0;
        nr = 0;
        if (n) {
            char *p = testo;
            coda_log(s[sel].nome, testo, sizeof(testo));
            /* lines, long ones folded at the pane's width */
            while (*p && nr < 2048) {
                char *e = strchr(p, '\n');
                int   l = e ? (int)(e - p) : (int)strlen(p);

                righe[nr++] = p;
                if (l > larga) { p += larga; continue; }
                if (!e) break;
                *e = 0;
                p = e + 1;
            }
        }
        if (scorri > nr - alte) scorri = nr - alte;
        if (scorri < 0) scorri = 0;

        qf("\x1B[0m\x1B[2J");
        riga_piena(1, "\x1B[37;44m", " extaskbar - i log dei servizi");
        for (i = 0; i < alte; i++) {
            vai(2 + i, 1);
            if (i < n) {
                snprintf(t, sizeof(t), "%c%-14s %s %4uK", s[i].su_file ? '*' : ' ', s[i].nome,
                         "", (s[i].byte + 1023) / 1024);
                qf("%s%-*.*s\x1B[0m", i == sel ? "\x1B[30;47m" : "", LARGA_ELENCO, LARGA_ELENCO, t);
            } else qf("%-*s", LARGA_ELENCO, "");
            qf("\x1B[34m\xB3\x1B[0m");
            k = nr - alte - scorri + i;             /* the newest at the bottom */
            if (k >= 0 && k < nr) {
                char *r = righe[k];
                int   l = (int)strlen(r);
                if (l > larga) l = larga;
                qf("%.*s", l, r);
            }
        }
        if (!n) { vai(4, LARGA_ELENCO + 3); qf("Nessun servizio ha ancora scritto niente."); }
        vai(g_righe - 1, 1);
        qf("\x1B[33m%s\x1B[0m", avviso);
        snprintf(t, sizeof(t), " su/giu' servizio  PgSu/PgGiu scorre  f salva su file %s  q esce",
                 n && s[sel].su_file ? "[x]" : "[ ]");
        riga_piena(g_righe, "\x1B[30;47m", t);
        qflush();
        avviso[0] = 0;

        switch (tasto(1000)) {                      /* live: redrawn every second */
        case T_SU:    if (sel > 0) { sel--; scorri = 0; } break;
        case T_GIU:   if (sel + 1 < n) { sel++; scorri = 0; } break;
        case T_PGSU:  scorri += alte - 1; break;
        case T_PGGIU: scorri -= alte - 1; break;
        case 'f': case 'F':
            if (n) {
                int on = !s[sel].su_file;
                if (chiedi(EXTB_CMD_SALVA, (unsigned)on, s[sel].nome, NULL) == 0)
                    snprintf(avviso, sizeof(avviso), on ? "%s: salvato in /boot/%s/log.txt (max 1024 KB)"
                                                         : "%s: solo in memoria%.0s", s[sel].nome, s[sel].nome);
                else snprintf(avviso, sizeof(avviso), "il servizio non ha risposto");
            }
            break;
        case 'q': case 'Q': case 0x1B: fine = 1; break;
        default: break;
        }
    }
    schermo_chiudi();
    return 0;
}

static void uso(void)
{
    printf("extaskbar %s - la barra di stato e i log dei servizi\n\n", VERSIONE);
    printf("  extaskbar -show      mostra la barra (avvia il servizio se serve)\n");
    printf("  extaskbar -hide      la nasconde: i log si raccolgono, la console resta pulita\n");
    printf("  extaskbar -stop      ferma il servizio: i messaggi tornano sulla console\n");
    printf("  extaskbar -menu      le voci da mostrare (spazi liberi: 80 colonne) e la memoria\n");
    printf("  extaskbar -details   il log di ogni servizio in tempo reale, e il salvataggio\n");
    printf("                       in /boot/<servizio>/log.txt (al massimo 1024 KB)\n\n");
    printf("  extaskbar -boot:on   parte da sola a ogni avvio (root; -boot:off la spegne)\n\n");
    printf("Configurazione: $HOME/.config/extaskbar.cfg\n");
    printf("Vanno nei log i programmi lanciati con '&' (e i loro figli).\n");
}

int main(int argc, char **argv)
{
    const char *o = argc > 1 ? argv[1] : "-show";

    if (strcmp(o, "-servizio") == 0) return servi();
    if (strcmp(o, "-h") == 0 || strcmp(o, "--help") == 0) { uso(); return 0; }

    cfg_percorso(g_cfg_percorso, sizeof(g_cfg_percorso));
    g_srv = ipc_lookup(EXTB_SERVIZIO);
    if (g_srv <= 0) g_srv = 0;

    /* /boot/avvio.sh: starts only if root switched it on, else not a word */
    if (strcmp(o, "-auto") == 0) {
        Config c;

        if (g_srv) return 0;
        cfg_leggi(&c, g_cfg_percorso);
        if (!c.avvio) return 0;
        return avvia_servizio(argv[0]) == 0 ? 0 : 1;
    }
    if (strcmp(o, "-boot:on") == 0 || strcmp(o, "-boot:off") == 0) {
        Config c;

        if (getuid() != 0) { printf("extaskbar: l'avvio automatico lo decide root\n"); return 1; }
        cfg_leggi(&c, g_cfg_percorso);
        c.avvio = o[7] == 'n';
        if (cfg_scrivi(&c, g_cfg_percorso) != 0) { printf("extaskbar: non riesco a scrivere %s\n", g_cfg_percorso); return 1; }
        if (g_srv) chiedi(EXTB_CMD_RILEGGI, 0, NULL, g_cfg_percorso);
        printf("extaskbar: all'avvio %s (%s)\n", c.avvio ? "parte da sola" : "non parte", g_cfg_percorso);
        return 0;
    }
    if (strcmp(o, "-show") == 0) {
        if (!g_srv) return avvia_servizio(argv[0]) == 0 ? 0 : 1;
        return chiedi(EXTB_CMD_MOSTRA, 0, NULL, NULL) == 0 ? 0 : 1;
    }
    if (strcmp(o, "-hide") == 0) {
        if (!g_srv) { printf("extaskbar: il servizio non e' in esecuzione\n"); return 1; }
        return chiedi(EXTB_CMD_NASCONDI, 0, NULL, NULL) == 0 ? 0 : 1;
    }
    if (strcmp(o, "-stop") == 0) {
        if (!g_srv) { printf("extaskbar: il servizio non e' in esecuzione\n"); return 0; }
        return chiedi(EXTB_CMD_FERMA, 0, NULL, NULL) == 0 ? 0 : 1;
    }
    if (strcmp(o, "-menu") == 0) return menu();
    if (strcmp(o, "-details") == 0) {
        if (!g_srv) { printf("extaskbar: il servizio non e' in esecuzione (extaskbar -show)\n"); return 1; }
        return dettagli();
    }
    uso();
    return 2;
}
