/* =============================================================================
 * bin/ftp/ftp.c — the ftp shell, FTP and FTPS
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 *     ftp                                  the ftp> prompt, not connected yet
 *     ftp [-tls|-tlsi] SERVER [PORT]       connects, asks user and password
 *     ftp SERVER -u USER [-w PASS] [-p PORT] [-a] [-v] [command ...]
 *
 * With a command after the options, that command alone runs and ftp exits
 * (the 0.001 behaviour, kept for scripts): `ftp server get file.txt`.
 *
 * The user (17 October 2026): typed alone, ftp opens a SHELL with every
 * command of the classic ftp client, and FTPS. `help` lists them; the table
 * `g_comandi` below is the list, with each command's help line.
 *
 * Everything that talks to the server is lib/exftp (shared with ftpswap,
 * netspeed and the file manager); FTPS is exhttp's TLS (TLS 1.3), loaded at
 * 32 bit from exhttp.so only when FTPS is used.
 *
 * ! PLAIN FTP SENDS THE PASSWORD IN CLEAR. It is the protocol, not this
 * program: whoever is on the path reads user and password as they are. The
 * program says so once, when a password is about to go out unencrypted,
 * and offers `open -tls` (explicit FTPS, AUTH TLS) as the way out.
 *
 * ! A CERTIFICATE THAT CANNOT BE VERIFIED IS SHOWN, NOT SWALLOWED (22 October
 * 2026). A home server (vsftpd) has a self-signed certificate under a name
 * that is not its address. ftp shows its SHA-256 and asks; accepted, it goes
 * in $HOME/.app/exhttp/fidati.txt for that host, and from then on that
 * certificate - and only that one - passes. A different one is asked again:
 * that is the moment someone is in the middle.
 *
 * ! THE PASSWORD IS ASKED WITHOUT ECHO (exuser_leggi_password, console and
 * pseudo-terminal alike) and wiped from memory once sent; -w on the command
 * line exists for scripts, and a command line is visible to whoever lists
 * the processes.
 * ============================================================================= */
#include "libc.h"
#include "exftp.h"
#include "exhttp.h"
#include "exuser.h"

/* +0.001 at every change: `ftp -version` prints it. See EX_VERSIONE. */
EX_VERSIONE("ftp", "0.004");

#define RIGA_MAX   1024
#define ARGS_MAX   16

static ExFtp g_f;
static int   g_modo     = EXFTP_CHIARO;   /* how `open` connects by default */
static int   g_chiedi   = 1;              /* `prompt`: ask in mget/mput/mdelete */
static int   g_hash     = 0;              /* `hash`: a # every 8 KB */
static int   g_debug    = 0;              /* `debug`: show the commands sent */
static int   g_verboso  = 1;              /* `verbose`: show the replies */
static long  g_hash_fatti;
static unsigned long g_t0;

/* =============================================================================
 * The log hook: what the user sees of the dialogue
 * ============================================================================= */
static void registro(void *dato, int verso, const char *riga)
{
    (void)dato;
    if (verso == '<' && g_verboso) printf("%s\n", riga);
    else if (verso == '>' && g_debug) printf("---> %s\n", riga);
    else if (verso == '!' && (g_verboso || g_debug)) printf("(%s)\n", riga);
}

static int avanza(void *dato, long fatti, long totale)
{
    (void)dato; (void)totale;
    if (g_hash) {
        while (g_hash_fatti + 8192 <= fatti) {
            printf("#");
            g_hash_fatti += 8192;
        }
        fflush(stdout);
    }
    return 1;
}

/* =============================================================================
 * Small things
 * ============================================================================= */
/* ! ONE read() FOR THE WHOLE LINE, never a byte at a time: the console hands
 * over a line per read and drops what does not fit, so a 1-byte loop got the
 * first letter and never saw the newline (the first QEMU run, 17/10/2026). */
static void leggi_riga(const char *domanda, char *dst, unsigned int max)
{
    int n;

    printf("%s", domanda);
    fflush(stdout);
    n = (int)read(0, dst, max - 1);
    if (n < 0) n = 0;
    dst[n] = 0;
    while (n > 0 && (dst[n - 1] == '\n' || dst[n - 1] == '\r')) dst[--n] = 0;
}

static int si_no(const char *cosa, const char *nome)
{
    char r[16];

    printf("%s %s? ", cosa, nome);
    leggi_riga("", r, sizeof(r));
    return r[0] != 'n' && r[0] != 'N';
}

/* Splits a line in words; "quoted words" keep their spaces. */
static int pezzi(char *s, char **a, int max)
{
    int n = 0;

    while (*s && n < max) {
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        if (*s == '"') {
            a[n++] = ++s;
            while (*s && *s != '"') s++;
        } else {
            a[n++] = s;
            while (*s && *s != ' ' && *s != '\t') s++;
        }
        if (*s) *s++ = 0;
    }
    return n;
}

/* '*' and '?' wildcards, as mget/mput/mdelete take them. */
static int combacia(const char *m, const char *s)
{
    for (; *m; m++, s++) {
        if (*m == '*') {
            while (m[1] == '*') m++;
            if (!m[1]) return 1;
            for (; *s; s++) if (combacia(m + 1, s)) return 1;
            return 0;
        }
        if (!*s) return 0;
        if (*m != '?' && *m != *s) return 0;
    }
    return *s == 0;
}

static int ha_jolly(const char *s) { return strchr(s, '*') || strchr(s, '?'); }

static const char *base(const char *p)
{
    const char *b = strrchr(p, '/');

    return b ? b + 1 : p;
}

static int collegato(void)
{
    if (g_f.aperto && g_f.dentro) return 1;
    printf(g_f.aperto ? "Non sei entrato: `user NOME`.\n" : "Non collegato: `open SERVER`.\n");
    return 0;
}

static void esito(int rc, const char *cosa)
{
    if (rc >= 0) return;
    if (g_f.errore[0] && (rc == -EPROTO || rc == -EPROTONOSUPPORT || rc == -ENETDOWN || rc == -EHOSTUNREACH))
        printf("%s: %s\n", cosa, g_f.errore);
    else if (!g_verboso && g_f.risposta[0])
        printf("%s: %s\n", cosa, g_f.risposta);
    else if (!g_f.risposta[0] || rc == -ECONNRESET || rc == -ETIMEDOUT)
        printf("%s: %s\n", cosa, exftp_perche(rc));
}

static void tempo_via(void) { g_t0 = uptime_ms(); g_hash_fatti = 0; }

static void tempo_fine(const char *verbo, long byte)
{
    unsigned long ms = uptime_ms() - g_t0;

    if (g_hash && g_hash_fatti > 0) printf("\n");
    if (byte < 0) return;
    if (ms == 0) ms = 1;
    printf("%ld byte %s in %lu.%02lu s (%lu KB/s)\n", byte, verbo,
           ms / 1000ul, (ms % 1000ul) / 10ul,
           (unsigned long)byte / 1024ul * 1000ul / ms);
}

static long dim_locale(const char *p)
{
    struct stat st;

    if (stat(p, &st) != 0) return -1;
    return (long)st.st_size;
}

/* =============================================================================
 * Connecting
 * ============================================================================= */
static int apri(const char *host, const char *porta_s, int modo,
                const char *utente, const char *password);

/* Does the server's reply speak of encryption? vsftpd: «530 Non-anonymous
 * sessions must use encryption.»; others say SSL or TLS. */
static int parla_di_cifra(const char *r)
{
    static const char *const parole[] = { "encrypt", "ssl", "tls", "cifr" };
    unsigned int k;

    for (; *r; r++)
        for (k = 0; k < sizeof(parole) / sizeof(parole[0]); k++)
            if (strncasecmp(r, parole[k], strlen(parole[k])) == 0) return 1;
    return 0;
}

static int accedi(const char *utente, const char *password)
{
    char u[128], p[128];
    int  rc;

    if (!g_f.aperto) { printf("Non collegato.\n"); return 1; }
    if (utente && utente[0]) {
        strncpy(u, utente, sizeof(u) - 1);
        u[sizeof(u) - 1] = 0;
    } else {
        char d[180];

        snprintf(d, sizeof(d), "Utente (%s): ", g_f.host);
        leggi_riga(d, u, sizeof(u));
        if (!u[0]) strcpy(u, "anonymous");
    }

    /* USER first: a server that wants no password says 230 already. */
    rc = exftp_comando(&g_f, "USER", u);

    /* ! THE SERVER THAT WANTS FTPS SAYS SO AT USER, before the password:
     * vsftpd with force_local_logins_ssl answers 530 there. The user asked
     * (22 October 2026) that ftp then goes over to -tls by itself — and
     * it can, without a password having gone out in clear. Once only: on
     * the new connection g_f.cifrato is set. */
    if (rc == 530 && !g_f.cifrato && parla_di_cifra(g_f.risposta)) {
        char h[sizeof(g_f.host)], ps[12];

        printf("Il server accetta questo utente solo su una connessione cifrata:\n"
               "ricollego in FTPS (come `open -tls`).\n");
        strcpy(h, g_f.host);
        snprintf(ps, sizeof(ps), "%u", g_f.porta);
        return apri(h, ps, EXFTP_TLS, u, password);
    }
    if (rc == 530 && g_f.cifrato && parla_di_cifra(g_f.risposta))
        printf("! Il server non accetta questo utente su una connessione cifrata:\n"
               "  provate `open` senza -tls (la password viaggerebbe in chiaro).\n");
    if (rc == 331 || rc == 332) {
        if (password) {
            strncpy(p, password, sizeof(p) - 1);
            p[sizeof(p) - 1] = 0;
        } else if (strcmp(u, "anonymous") == 0 || strcmp(u, "ftp") == 0) {
            strcpy(p, "exos@");
        } else {
            if (!g_f.cifrato)
                printf("! FTP senza TLS: la password viaggia IN CHIARO (`open -tls` per FTPS).\n");
            printf("Password: ");
            fflush(stdout);
            if (exuser_leggi_password(p, (int)sizeof(p)) < 0) {
                leggi_riga("", p, sizeof(p));          /* no hidden input here */
            }
        }
        rc = exftp_comando(&g_f, "PASS", p);
        memset(p, 0, sizeof(p));
    }
    if (rc < 200 || rc >= 300) {
        printf("Accesso non riuscito.\n");
        return 1;
    }
    g_f.dentro = 1;
    if (g_f.cifrato) {
        exftp_comando(&g_f, "PBSZ", "0");
        if (exftp_protezione(&g_f, 1) == 0) printf("Comandi e dati cifrati (TLS).\n");
        else printf("! Il server rifiuta PROT P: i comandi sono cifrati, i dati no.\n");
    }
    exftp_binario(&g_f, 1);
    {
        char tipo[96];

        if (exftp_comando(&g_f, "SYST", 0) == 215 && !g_verboso) {
            strncpy(tipo, g_f.risposta + 4, sizeof(tipo) - 1);
            tipo[sizeof(tipo) - 1] = 0;
            printf("Sistema remoto: %s\n", tipo);
        }
    }
    printf("Modo binario.\n");
    return 0;
}

static int apri(const char *host, const char *porta_s, int modo,
                const char *utente, const char *password)
{
    unsigned int porta = porta_s ? (unsigned int)atoi(porta_s) : 0;
    int          rc;

    if (g_f.aperto) exftp_chiudi(&g_f);
    printf("Collegamento a %s%s...\n", host,
           modo == EXFTP_TLS ? " (FTPS, AUTH TLS)" : modo == EXFTP_TLS_IMPLICITO ? " (FTPS implicito)" : "");
    rc = exftp_connetti(&g_f, host, porta, modo);
    if (rc != 0 && modo != EXFTP_CHIARO && strstr(g_f.errore, "certificat")) {
        char hex[2 * 32 + 1], r[16];
        int  k;

        if (exhttp_tls_impronta(hex, sizeof(hex))) {
            printf("ftp: %s: %s\n", host, g_f.errore);
            printf("Il certificato non e' firmato da un'autorita' conosciuta, o porta un\n"
                   "altro nome. Controllate l'impronta SHA-256 sul server\n"
                   "(openssl x509 -in CERT -noout -fingerprint -sha256):\n  ");
            for (k = 0; hex[k]; k += 2)
                printf("%c%c%s", toupper((unsigned char)hex[k]),
                       toupper((unsigned char)hex[k + 1]), hex[k + 2] ? ":" : "\n");
            leggi_riga("Accettarlo e ricordarlo per questo server? (si/no): ", r, sizeof(r));
            if (r[0] != 's' && r[0] != 'S') return 1;
            if (!exhttp_tls_fidati(host, hex)) {
                printf("ftp: non riesco a scrivere $HOME/.app/exhttp/fidati.txt\n");
                return 1;
            }
            rc = exftp_connetti(&g_f, host, porta, modo);
        }
    }
    if (rc != 0) {
        printf("ftp: %s: %s\n", host, g_f.errore[0] ? g_f.errore : exftp_perche(rc));
        return 1;
    }
    printf("Collegato a %s:%u%s.\n", g_f.host, g_f.porta, g_f.cifrato ? ", cifrato" : "");
    return accedi(utente, password);
}

/* =============================================================================
 * Listings
 * ============================================================================= */
static int stampa_riga(void *dato, const char *r)
{
    FILE *out = (FILE *)dato;

    if (out) fprintf(out, "%s\n", r);
    else     printf("%s\n", r);
    return 1;
}

static int elenca(const char *verbo, const char *dir, const char *file)
{
    FILE *out = 0;
    int   rc;

    if (!collegato()) return 1;
    if (file && file[0] && strcmp(file, "-") != 0) {
        out = fopen(file, "w");
        if (!out) { printf("ftp: non riesco a scrivere %s\n", file); return 1; }
    }
    rc = exftp_righe(&g_f, verbo, dir, stampa_riga, out);
    if (out) fclose(out);
    esito(rc, verbo);
    return rc < 0;
}

/* NLST matched against a pattern: the names, for mget and mdelete. */
typedef struct {
    const char *maschera;
    char      (*nomi)[EXFTP_NOME_MAX];
    int         n, max;
} Raccolta;

static int raccogli(void *dato, const char *r)
{
    Raccolta   *c = (Raccolta *)dato;
    const char *b = base(r);

    if (c->n >= c->max) return 0;
    if (!combacia(c->maschera, b)) return 1;
    strncpy(c->nomi[c->n], r, EXFTP_NOME_MAX - 1);
    c->nomi[c->n][EXFTP_NOME_MAX - 1] = 0;
    c->n++;
    return 1;
}

/* The remote names a pattern covers; a name without wildcards is itself. */
static int remoti(const char *m, char (*nomi)[EXFTP_NOME_MAX], int max)
{
    Raccolta    c;
    const char *b;
    char        dir[EXFTP_NOME_MAX];
    int         rc;

    if (!ha_jolly(m)) {
        strncpy(nomi[0], m, EXFTP_NOME_MAX - 1);
        nomi[0][EXFTP_NOME_MAX - 1] = 0;
        return 1;
    }
    b = base(m);
    dir[0] = 0;
    if (b != m) {
        unsigned int k = (unsigned int)(b - m);

        if (k >= sizeof(dir)) k = sizeof(dir) - 1;
        memcpy(dir, m, k);
        dir[k] = 0;
    }
    c.maschera = b; c.nomi = nomi; c.n = 0; c.max = max;
    rc = exftp_righe(&g_f, "NLST", dir, raccogli, &c);
    if (rc < 0) { esito(rc, "NLST"); return 0; }
    /* NLST answers bare names for the current directory: the dir goes back on. */
    if (dir[0]) {
        int i;

        for (i = 0; i < c.n; i++) {
            char t[EXFTP_NOME_MAX];

            if (strchr(nomi[i], '/')) continue;
            snprintf(t, sizeof(t), "%s%s", dir, nomi[i]);
            strcpy(nomi[i], t);
        }
    }
    return c.n;
}

/* =============================================================================
 * Transfers
 * ============================================================================= */
static int prendi(const char *remoto, const char *locale, int riprendi)
{
    long dim;
    int  rc;

    if (!collegato()) return 1;
    if (!locale || !locale[0]) locale = base(remoto);
    g_f.riparti = 0;
    if (riprendi) {
        dim = dim_locale(locale);
        if (dim > 0) {
            long r = exftp_size(&g_f, remoto);

            if (r >= 0 && r <= dim) { printf("%s e' gia' tutto qui.\n", locale); return 0; }
            g_f.riparti = dim;
            printf("Riprendo da %ld byte.\n", dim);
        }
    }
    tempo_via();
    rc = exftp_scarica(&g_f, remoto, locale);
    if (rc != 0) {
        if (g_hash && g_hash_fatti) printf("\n");
        esito(rc, remoto);
        return 1;
    }
    tempo_fine("ricevuti", dim_locale(locale));
    return 0;
}

static int manda(const char *locale, const char *remoto, int accoda)
{
    long dim = dim_locale(locale);
    int  rc;

    if (!collegato()) return 1;
    if (dim < 0) { printf("ftp: %s non c'e'.\n", locale); return 1; }
    if (!remoto || !remoto[0]) remoto = base(locale);
    g_f.riparti = 0;
    if (accoda) {
        long r = exftp_size(&g_f, remoto);

        if (r >= dim) { printf("%s e' gia' tutto sul server.\n", remoto); return 0; }
        if (r > 0) { g_f.riparti = r; printf("Riprendo da %ld byte.\n", r); }
    }
    tempo_via();
    rc = exftp_carica(&g_f, locale, remoto);
    if (rc != 0) {
        if (g_hash && g_hash_fatti) printf("\n");
        esito(rc, locale);
        return 1;
    }
    tempo_fine("mandati", dim);
    return 0;
}

#define TANTI 256
static char g_nomi[TANTI][EXFTP_NOME_MAX];

static int mget(int n, char **a)
{
    int i, k, quanti;

    if (!collegato()) return 1;
    for (i = 1; i < n; i++) {
        quanti = remoti(a[i], g_nomi, TANTI);
        if (quanti == 0) printf("%s: niente.\n", a[i]);
        for (k = 0; k < quanti; k++) {
            if (g_chiedi && !si_no("mget", g_nomi[k])) continue;
            prendi(g_nomi[k], base(g_nomi[k]), 0);
        }
    }
    return 0;
}

static int mdelete(int n, char **a)
{
    int i, k, quanti;

    if (!collegato()) return 1;
    for (i = 1; i < n; i++) {
        quanti = remoti(a[i], g_nomi, TANTI);
        for (k = 0; k < quanti; k++) {
            if (g_chiedi && !si_no("mdelete", g_nomi[k])) continue;
            esito(exftp_dele(&g_f, g_nomi[k]), g_nomi[k]);
        }
    }
    return 0;
}

static int mput(int n, char **a)
{
    int i;

    if (!collegato()) return 1;
    for (i = 1; i < n; i++) {
        char        dir[256];
        const char *b = base(a[i]);
        DIR        *d;
        struct dirent *e;
        int         trovati = 0;

        if (!ha_jolly(a[i])) {
            if (!g_chiedi || si_no("mput", a[i])) manda(a[i], base(a[i]), 0);
            continue;
        }
        if (b == a[i]) strcpy(dir, ".");
        else {
            unsigned int k = (unsigned int)(b - a[i]);

            if (k >= sizeof(dir)) k = sizeof(dir) - 1;
            memcpy(dir, a[i], k);
            dir[k] = 0;
        }
        d = opendir(dir);
        if (!d) { printf("%s: cartella locale illeggibile.\n", dir); continue; }
        while ((e = readdir(d)) != 0) {
            char p[512];
            struct stat st;

            if (e->d_name[0] == '.' && (e->d_name[1] == 0 || (e->d_name[1] == '.' && e->d_name[2] == 0))) continue;
            if (!combacia(b, e->d_name)) continue;
            if (strcmp(dir, ".") == 0) snprintf(p, sizeof(p), "%s", e->d_name);
            else                      snprintf(p, sizeof(p), "%s%s", dir, e->d_name);
            if (stat(p, &st) != 0 || S_ISDIR(st.st_mode)) continue;
            trovati++;
            if (g_chiedi && !si_no("mput", p)) continue;
            manda(p, e->d_name, 0);
        }
        closedir(d);
        if (!trovati) printf("%s: niente.\n", a[i]);
    }
    return 0;
}

/* =============================================================================
 * The commands
 * ============================================================================= */
static int stato(void)
{
    if (g_f.aperto)
        printf("Collegato a %s:%u%s%s.\n", g_f.host, g_f.porta,
               g_f.cifrato ? ", comandi cifrati" : ", in chiaro",
               g_f.prot_p ? " e dati cifrati" : "");
    else
        printf("Non collegato.\n");
    printf("Modo: %s; canale dati: %s; open: %s\n",
           g_f.binario ? "binario" : "ascii", g_f.passiva ? "passivo" : "attivo",
           g_modo == EXFTP_TLS ? "FTPS (AUTH TLS)" : g_modo == EXFTP_TLS_IMPLICITO ? "FTPS implicito" : "FTP");
    printf("verbose %s, debug %s, hash %s, prompt %s\n",
           g_verboso ? "si" : "no", g_debug ? "si" : "no",
           g_hash ? "si" : "no", g_chiedi ? "si" : "no");
    return 0;
}

static int aiuto(int n, char **a);

typedef struct {
    const char *nomi;          /* "get|recv": the first is the name shown */
    const char *uso;
    const char *cosa;
    int (*fai)(int n, char **a);
} Comando;

static int c_open(int n, char **a)
{
    int         modo = g_modo, i = 1;
    const char *host, *porta = 0;

    for (; i < n && a[i][0] == '-'; i++) {
        if (strcmp(a[i], "-tls") == 0)       modo = EXFTP_TLS;
        else if (strcmp(a[i], "-tlsi") == 0) modo = EXFTP_TLS_IMPLICITO;
        else if (strcmp(a[i], "-plain") == 0) modo = EXFTP_CHIARO;
    }
    if (i >= n) {
        static char h[128];

        leggi_riga("(a) ", h, sizeof(h));
        if (!h[0]) return 1;
        host = h;
    } else {
        host = a[i++];
        if (i < n) porta = a[i];
    }
    return apri(host, porta, modo, 0, 0);
}
static int c_ftps(int n, char **a)
{
    char *b[ARGS_MAX + 1];
    int   i;

    b[0] = a[0];
    b[1] = (char *)"-tls";
    for (i = 1; i < n && i < ARGS_MAX; i++) b[i + 1] = a[i];
    return c_open(i + 1, b);
}
static int c_user(int n, char **a)
{
    if (n < 2) { printf("uso: user NOME [PASSWORD]\n"); return 1; }
    return accedi(a[1], n > 2 ? a[2] : 0);
}
static int c_close(int n, char **a) { (void)n; (void)a; exftp_chiudi(&g_f); return 0; }
static int c_bye(int n, char **a)   { (void)n; (void)a; return -1; }
static int c_pwd(int n, char **a)
{
    char d[EXFTP_NOME_MAX];
    (void)n; (void)a;
    if (!collegato()) return 1;
    if (exftp_pwd(&g_f, d, sizeof(d)) == 0 && !g_verboso) printf("\"%s\"\n", d);
    return 0;
}
static int c_cd(int n, char **a)
{
    if (!collegato()) return 1;
    if (n > 1 && strcmp(a[1], "..") == 0) return exftp_cdup(&g_f) != 0;
    return exftp_cd(&g_f, n > 1 ? a[1] : "/") != 0;
}
static int c_cdup(int n, char **a)  { (void)n; (void)a; if (!collegato()) return 1; return exftp_cdup(&g_f) != 0; }
static int c_lcd(int n, char **a)
{
    char d[256];
    const char *h = getenv("HOME");

    if (chdir(n > 1 ? a[1] : (h && h[0] ? h : "/")) != 0) { printf("lcd: %s non c'e'.\n", a[1]); return 1; }
    if (getcwd(d, sizeof(d))) printf("Cartella locale: %s\n", d);
    return 0;
}
static int c_lpwd(int n, char **a)
{
    char d[256];
    (void)n; (void)a;
    if (getcwd(d, sizeof(d))) printf("Cartella locale: %s\n", d);
    return 0;
}
static int c_ls(int n, char **a)    { return elenca("LIST", n > 1 ? a[1] : "", n > 2 ? a[2] : 0); }
static int c_nlist(int n, char **a) { return elenca("NLST", n > 1 ? a[1] : "", n > 2 ? a[2] : 0); }
static int c_mlsd(int n, char **a)  { return elenca("MLSD", n > 1 ? a[1] : "", n > 2 ? a[2] : 0); }
static int c_get(int n, char **a)
{
    if (n < 2) { printf("uso: get REMOTO [LOCALE]\n"); return 1; }
    return prendi(a[1], n > 2 ? a[2] : 0, 0);
}
static int c_reget(int n, char **a)
{
    if (n < 2) { printf("uso: reget REMOTO [LOCALE]\n"); return 1; }
    return prendi(a[1], n > 2 ? a[2] : 0, 1);
}
static int c_put(int n, char **a)
{
    if (n < 2) { printf("uso: put LOCALE [REMOTO]\n"); return 1; }
    return manda(a[1], n > 2 ? a[2] : 0, 0);
}
static int c_reput(int n, char **a)
{
    if (n < 2) { printf("uso: reput LOCALE [REMOTO]\n"); return 1; }
    return manda(a[1], n > 2 ? a[2] : 0, 1);
}
static int c_append(int n, char **a)
{
    long dim;
    int  rc;

    if (n < 2) { printf("uso: append LOCALE [REMOTO]\n"); return 1; }
    if (!collegato()) return 1;
    dim = dim_locale(a[1]);
    if (dim < 0) { printf("ftp: %s non c'e'.\n", a[1]); return 1; }
    tempo_via();
    rc = exftp_accoda(&g_f, a[1], n > 2 ? a[2] : base(a[1]));
    esito(rc, a[1]);
    if (rc == 0) tempo_fine("mandati", dim);
    return rc != 0;
}
static int c_mkdir(int n, char **a)
{
    if (n < 2) { printf("uso: mkdir CARTELLA\n"); return 1; }
    if (!collegato()) return 1;
    return exftp_mkd(&g_f, a[1]) != 0;
}
static int c_rmdir(int n, char **a)
{
    if (n < 2) { printf("uso: rmdir CARTELLA\n"); return 1; }
    if (!collegato()) return 1;
    return exftp_rmd(&g_f, a[1]) != 0;
}
static int c_delete(int n, char **a)
{
    if (n < 2) { printf("uso: delete FILE\n"); return 1; }
    if (!collegato()) return 1;
    return exftp_dele(&g_f, a[1]) != 0;
}
static int c_rename(int n, char **a)
{
    if (n < 3) { printf("uso: rename VECCHIO NUOVO\n"); return 1; }
    if (!collegato()) return 1;
    return exftp_rinomina(&g_f, a[1], a[2]) != 0;
}
static int c_size(int n, char **a)
{
    long d;

    if (n < 2) { printf("uso: size FILE\n"); return 1; }
    if (!collegato()) return 1;
    d = exftp_size(&g_f, a[1]);
    if (d >= 0 && !g_verboso) printf("%s: %ld byte\n", a[1], d);
    return d < 0;
}
static int c_modtime(int n, char **a)
{
    long       q;
    time_t     t;
    struct tm  tm;

    if (n < 2) { printf("uso: modtime FILE\n"); return 1; }
    if (!collegato()) return 1;
    q = exftp_mdtm(&g_f, a[1]);
    if (q < 0) return 1;
    t = (time_t)q;
    gmtime_r(&t, &tm);
    printf("%s: %04d-%02d-%02d %02d:%02d:%02d UTC\n", a[1], tm.tm_year + 1900, tm.tm_mon + 1,
           tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return 0;
}
static int c_chmod(int n, char **a)
{
    char arg[EXFTP_NOME_MAX + 16];

    if (n < 3) { printf("uso: chmod MODO FILE\n"); return 1; }
    if (!collegato()) return 1;
    snprintf(arg, sizeof(arg), "CHMOD %s %s", a[1], a[2]);
    return exftp_comando(&g_f, "SITE", arg) / 100 != 2;
}
static int c_binary(int n, char **a) { (void)n; (void)a; if (!collegato()) return 1; return exftp_binario(&g_f, 1) != 0; }
static int c_ascii(int n, char **a)  { (void)n; (void)a; if (!collegato()) return 1; return exftp_binario(&g_f, 0) != 0; }
static int c_type(int n, char **a)
{
    if (n < 2) { printf("Modo: %s\n", g_f.binario ? "binary" : "ascii"); return 0; }
    if (strcmp(a[1], "binary") == 0 || strcmp(a[1], "image") == 0 || strcmp(a[1], "i") == 0) return c_binary(1, a);
    if (strcmp(a[1], "ascii") == 0 || strcmp(a[1], "a") == 0) return c_ascii(1, a);
    printf("type: binary oppure ascii\n");
    return 1;
}
static int interruttore(int *v, int n, char **a, const char *nome)
{
    if (n > 1) *v = strcmp(a[1], "on") == 0 || strcmp(a[1], "si") == 0 || strcmp(a[1], "1") == 0;
    else       *v = !*v;
    printf("%s: %s\n", nome, *v ? "si" : "no");
    return 0;
}
static int c_passive(int n, char **a)
{
    int v = g_f.passiva;

    interruttore(&v, n, a, "Modo passivo");
    exftp_passiva(&g_f, v);
    return 0;
}
static int c_active(int n, char **a) { (void)n; (void)a; exftp_passiva(&g_f, 0); printf("Modo attivo (PORT).\n"); return 0; }
static int c_prompt(int n, char **a)  { return interruttore(&g_chiedi, n, a, "Domande in mget/mput/mdelete"); }
static int c_hash(int n, char **a)    { return interruttore(&g_hash, n, a, "Un # ogni 8 KB"); }
static int c_verbose(int n, char **a) { return interruttore(&g_verboso, n, a, "Risposte del server"); }
static int c_debug(int n, char **a)   { return interruttore(&g_debug, n, a, "Comandi mandati"); }
static int c_status(int n, char **a)  { (void)n; (void)a; return stato(); }
static int c_system(int n, char **a)  { (void)n; (void)a; if (!collegato()) return 1; return exftp_comando(&g_f, "SYST", 0) != 215; }
static int c_noop(int n, char **a)    { (void)n; (void)a; if (!collegato()) return 1; return exftp_comando(&g_f, "NOOP", 0) != 200; }
static int c_quote(int n, char **a)
{
    char arg[RIGA_MAX];
    int  i, k = 0;

    if (n < 2) { printf("uso: quote COMANDO [ARGOMENTI]\n"); return 1; }
    if (!g_f.aperto) { printf("Non collegato.\n"); return 1; }
    arg[0] = 0;
    for (i = 2; i < n; i++) k += snprintf(arg + k, sizeof(arg) - (unsigned)k, "%s%s", i > 2 ? " " : "", a[i]);
    if (strcasecmp(a[1], "PASS") == 0) { printf("quote: per la password c'e' `user`.\n"); return 1; }
    return exftp_comando(&g_f, a[1], arg[0] ? arg : 0) < 0;
}
static int c_site(int n, char **a)
{
    char arg[RIGA_MAX];
    int  i, k = 0;

    if (n < 2) { printf("uso: site COMANDO\n"); return 1; }
    if (!collegato()) return 1;
    arg[0] = 0;
    for (i = 1; i < n; i++) k += snprintf(arg + k, sizeof(arg) - (unsigned)k, "%s%s", i > 1 ? " " : "", a[i]);
    return exftp_comando(&g_f, "SITE", arg) < 0;
}
static int c_rhelp(int n, char **a)
{
    int vecchio = g_verboso, rc;

    if (!g_f.aperto) { printf("Non collegato.\n"); return 1; }
    g_verboso = 1;
    rc = exftp_comando(&g_f, "HELP", n > 1 ? a[1] : 0);
    g_verboso = vecchio;
    return rc < 0;
}
static int c_rstatus(int n, char **a)
{
    int vecchio = g_verboso, rc;

    if (!g_f.aperto) { printf("Non collegato.\n"); return 1; }
    g_verboso = 1;
    rc = exftp_comando(&g_f, "STAT", n > 1 ? a[1] : 0);
    g_verboso = vecchio;
    return rc < 0;
}
static int c_prot(int n, char **a)
{
    if (!g_f.cifrato) { printf("prot: serve una connessione FTPS (`open -tls`).\n"); return 1; }
    if (n < 2) { printf("Dati %s.\n", g_f.prot_p ? "cifrati (P)" : "in chiaro (C)"); return 0; }
    return exftp_protezione(&g_f, a[1][0] == 'p' || a[1][0] == 'P') != 0;
}
static int c_tls(int n, char **a)
{
    if (n > 1) {
        if (strcmp(a[1], "on") == 0 || strcmp(a[1], "explicit") == 0) g_modo = EXFTP_TLS;
        else if (strcmp(a[1], "implicit") == 0) g_modo = EXFTP_TLS_IMPLICITO;
        else g_modo = EXFTP_CHIARO;
    } else {
        g_modo = g_modo == EXFTP_CHIARO ? EXFTP_TLS : EXFTP_CHIARO;
    }
    printf("`open` usera': %s\n", g_modo == EXFTP_TLS ? "FTPS (AUTH TLS)" :
           g_modo == EXFTP_TLS_IMPLICITO ? "FTPS implicito (porta 990)" : "FTP in chiaro");
    return 0;
}
static int c_shell(int n, char **a)
{
    char riga[RIGA_MAX];
    int  i, k = 0;

    riga[0] = 0;
    for (i = 1; i < n; i++) k += snprintf(riga + k, sizeof(riga) - (unsigned)k, "%s%s", i > 1 ? " " : "", a[i]);
    return system(riga[0] ? riga : "/bin/sh");
}

static const Comando g_comandi[] = {
    { "open|o",          "open [-tls|-tlsi] SERVER [PORTA]", "si collega (e chiede utente e password)", c_open },
    { "ftps",            "ftps SERVER [PORTA]",       "come open -tls: FTPS esplicito",           c_ftps },
    { "user",            "user NOME [PASSWORD]",      "entra con un altro utente",                c_user },
    { "close|disconnect","close",                     "chiude la connessione, resta in ftp",       c_close },
    { "bye|quit|exit",   "bye",                       "chiude ed esce",                            c_bye },
    { "pwd",             "pwd",                       "la cartella sul server",                    c_pwd },
    { "cd|cwd",          "cd CARTELLA",               "cambia cartella sul server",                c_cd },
    { "cdup",            "cdup",                      "sale di una cartella sul server",           c_cdup },
    { "lcd",             "lcd [CARTELLA]",            "cambia la cartella locale",                 c_lcd },
    { "lpwd",            "lpwd",                      "la cartella locale",                        c_lpwd },
    { "ls|dir",          "ls [CARTELLA] [FILE]",      "elenco (LIST); FILE per salvarlo",          c_ls },
    { "nlist|nls",       "nlist [CARTELLA] [FILE]",   "solo i nomi (NLST)",                        c_nlist },
    { "mlsd",            "mlsd [CARTELLA]",           "l'elenco per le macchine (MLSD)",           c_mlsd },
    { "get|recv",        "get REMOTO [LOCALE]",       "scarica un file",                           c_get },
    { "reget",           "reget REMOTO [LOCALE]",     "riprende uno scaricamento interrotto",      c_reget },
    { "mget",            "mget MASCHERA ...",         "scarica piu' file (* e ?)",                 mget },
    { "put|send",        "put LOCALE [REMOTO]",       "manda un file",                             c_put },
    { "reput",           "reput LOCALE [REMOTO]",     "riprende un invio interrotto",              c_reput },
    { "append",          "append LOCALE [REMOTO]",    "aggiunge in coda a un file remoto",         c_append },
    { "mput",            "mput MASCHERA ...",         "manda piu' file (* e ?)",                   mput },
    { "mkdir|mkd",       "mkdir CARTELLA",            "crea una cartella sul server",              c_mkdir },
    { "rmdir|rmd",       "rmdir CARTELLA",            "toglie una cartella vuota",                 c_rmdir },
    { "delete|del|rm",   "delete FILE",               "cancella un file sul server",               c_delete },
    { "mdelete",         "mdelete MASCHERA ...",      "cancella piu' file",                        mdelete },
    { "rename|ren",      "rename VECCHIO NUOVO",      "rinomina sul server",                       c_rename },
    { "size",            "size FILE",                 "quanto e' grande",                          c_size },
    { "modtime",         "modtime FILE",              "quando e' stato cambiato (UTC)",            c_modtime },
    { "chmod",           "chmod MODO FILE",           "permessi (SITE CHMOD)",                     c_chmod },
    { "binary|bin|image","binary",                    "modo binario (TYPE I)",                     c_binary },
    { "ascii",           "ascii",                     "modo testo, converte i fine riga",          c_ascii },
    { "type",            "type [binary|ascii]",       "mostra o cambia il modo",                   c_type },
    { "passive|pasv",    "passive [on|off]",          "canale dati passivo (EPSV/PASV)",           c_passive },
    { "active",          "active",                    "canale dati attivo (PORT)",                 c_active },
    { "tls",             "tls [on|implicit|off]",     "come si collega `open`",                    c_tls },
    { "prot",            "prot [P|C]",                "dati cifrati (P) o in chiaro (C)",          c_prot },
    { "prompt",          "prompt [on|off]",           "domande in mget/mput/mdelete",              c_prompt },
    { "hash",            "hash [on|off]",             "un # ogni 8 KB trasferiti",                 c_hash },
    { "verbose",         "verbose [on|off]",          "mostra le risposte del server",             c_verbose },
    { "debug",           "debug [on|off]",            "mostra i comandi mandati",                  c_debug },
    { "status",          "status",                    "come stanno le cose",                       c_status },
    { "rstatus",         "rstatus [FILE]",            "lo stato secondo il server (STAT)",         c_rstatus },
    { "system|syst",     "system",                    "che sistema e' il server",                  c_system },
    { "noop",            "noop",                      "tiene viva la connessione",                 c_noop },
    { "quote|literal",   "quote COMANDO [ARG]",       "manda un comando FTP cosi' com'e'",         c_quote },
    { "site",            "site COMANDO",              "un comando SITE del server",                c_site },
    { "rhelp|remotehelp","rhelp [COMANDO]",           "l'aiuto del server",                        c_rhelp },
    { "!",               "! [COMANDO]",               "un comando di EX-OS (senza: una shell)",    c_shell },
    { "help|?",          "help [COMANDO]",            "questo elenco",                             aiuto },
};
#define N_COMANDI (int)(sizeof(g_comandi) / sizeof(g_comandi[0]))

/* Is `nome` one of the names in "a|b|c"? */
static int e_nome(const char *nomi, const char *nome)
{
    unsigned int l = (unsigned int)strlen(nome);

    while (*nomi) {
        const char *fine = strchr(nomi, '|');
        unsigned int k = fine ? (unsigned int)(fine - nomi) : (unsigned int)strlen(nomi);

        if (k == l && strncmp(nomi, nome, l) == 0) return 1;
        if (!fine) break;
        nomi = fine + 1;
    }
    return 0;
}

static int aiuto(int n, char **a)
{
    int i;

    if (n > 1) {
        for (i = 0; i < N_COMANDI; i++) {
            if (e_nome(g_comandi[i].nomi, a[1])) {
                printf("%-28s %s\n", g_comandi[i].uso, g_comandi[i].cosa);
                if (strchr(g_comandi[i].nomi, '|')) printf("  (anche: %s)\n", g_comandi[i].nomi);
                return 0;
            }
        }
        printf("%s: comando sconosciuto.\n", a[1]);
        return 1;
    }
    printf("Comandi (help COMANDO per uno solo):\n");
    for (i = 0; i < N_COMANDI; i++)
        printf("  %-28s %s\n", g_comandi[i].uso, g_comandi[i].cosa);
    return 0;
}

/* -1 = leave ftp */
static int esegui(int n, char **a)
{
    int i;

    if (n == 0) return 0;
    if (a[0][0] == '!' && a[0][1]) {          /* "!ls" as well as "! ls" */
        char *b[ARGS_MAX + 1];

        b[0] = (char *)"!";
        b[1] = a[0] + 1;
        for (i = 1; i < n && i < ARGS_MAX; i++) b[i + 1] = a[i];
        return c_shell(i + 1, b);
    }
    for (i = 0; i < N_COMANDI; i++)
        if (e_nome(g_comandi[i].nomi, a[0])) return g_comandi[i].fai(n, a);
    printf("?Comando sconosciuto: %s (`help` li elenca)\n", a[0]);
    return 1;
}

static void uso(void)
{
    printf("uso: ftp                          apre la riga di comando ftp>\n");
    printf("     ftp [-tls|-tlsi] SERVER [PORTA]\n");
    printf("     ftp SERVER [-u UTENTE] [-w PASSWORD] [-p PORTA] [-a] [-v] [COMANDO ...]\n\n");
    printf("  -tls   FTPS esplicito (AUTH TLS sulla porta 21): comandi e dati cifrati\n");
    printf("  -tlsi  FTPS implicito (TLS subito, porta 990)\n");
    printf("  -a     canale dati attivo (PORT) invece che passivo\n");
    printf("  -v     mostra anche i comandi mandati\n");
    printf("Con un COMANDO dopo le opzioni esegue solo quello ed esce.\n");
}

int main(int argc, char **argv)
{
    char        riga[RIGA_MAX];
    char       *a[ARGS_MAX];
    const char *host = 0, *porta = 0, *utente = 0, *chiave = 0;
    int         i, primo_cmd = 0, rc = 0, modo = EXFTP_CHIARO, attivo = 0;

    exftp_prepara(&g_f);
    g_f.tls           = exhttp_tls_sopra;
    g_f.registro      = registro;
    g_f.avanza        = avanza;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) { uso(); return 0; }
        else if (strcmp(argv[i], "-tls") == 0)  modo = EXFTP_TLS;
        else if (strcmp(argv[i], "-tlsi") == 0) modo = EXFTP_TLS_IMPLICITO;
        else if (strcmp(argv[i], "-a") == 0)    attivo = 1;
        else if (strcmp(argv[i], "-v") == 0)    g_debug = 1;
        else if (strcmp(argv[i], "-u") == 0 && i + 1 < argc) utente = argv[++i];
        else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) chiave = argv[++i];
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) porta  = argv[++i];
        else if (!host) host = argv[i];
        else if (!porta && argv[i][0] >= '0' && argv[i][0] <= '9' && !primo_cmd) porta = argv[i];
        else { primo_cmd = i; break; }
    }
    g_modo = modo;
    if (attivo) exftp_passiva(&g_f, 0);

    if (host) {
        rc = apri(host, porta, modo, utente, chiave);
        if (primo_cmd > 0) {
            int n = 0;

            if (rc == 0) {
                for (i = primo_cmd; i < argc && n < ARGS_MAX; i++) a[n++] = argv[i];
                rc = esegui(n, a);
            }
            exftp_chiudi(&g_f);
            return rc < 0 ? 0 : rc;
        }
    } else {
        printf("ftp di EX-OS: `open SERVER` (FTP) o `ftps SERVER` (FTPS), `help` per i comandi.\n");
    }

    for (;;) {
        int n;

        printf("ftp> ");
        fflush(stdout);
        n = (int)read(0, riga, sizeof(riga) - 1);
        if (n <= 0) break;
        riga[n] = '\0';
        while (n > 0 && (riga[n - 1] == '\n' || riga[n - 1] == '\r')) riga[--n] = '\0';
        if (esegui(pezzi(riga, a, ARGS_MAX), a) < 0) break;
    }
    exftp_chiudi(&g_f);
    return 0;
}
