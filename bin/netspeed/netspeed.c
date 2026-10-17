/* =============================================================================
 * bin/netspeed/netspeed.c — how fast does the network go, both ways
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 *     netspeed                       the instructions
 *     netspeed -makeserver           puts the test point on a web server (FTP/FTPS)
 *     netspeed -test[:url] [-mb:N]   upload once, download N times (8)
 *
 * The user (16-17 October 2026): the network looks too slow; a tool made of
 * two parts, a PHP script that takes the upload and this program.
 *
 * -makeserver asks for the access (ftp or ftps), the FTP server, user and
 * password, the folder on the server and the web address of that folder;
 * it GENERATES netspeed/index.php - with that address and a description of
 * what it is for, shown to anyone who opens it in a browser - creates the
 * netspeed directory and uploads the file in binary mode. Only the web
 * address is kept ($HOME/.exwin/config/netspeed.cfg), never the FTP user or
 * password: they are needed once, to put the script there.
 *
 * -test makes a temporary file of 1 MB of random data, timing how long the
 * disk takes to write it (and to read it back), POSTs it once to the script,
 * asks the script where it stored it (?download answers with the file's
 * URL) and downloads that file N times (-mb:N, 8 by default: 16, 32, 64...
 * for asymmetric lines, where downloading is much faster than uploading).
 * Every download is compared byte by byte with what was sent.
 *
 * ! THE TIMES INCLUDE EVERYTHING one transfer costs: the name lookup, the TCP
 * connection, HTTP (and TLS on https) and the server's own time, one
 * connection at a time. It is what a program on EX-OS sees; a speed test
 * site opening many connections at once measures more.
 *
 * ! THE DATA ARE LETTERS, DIGITS, '-' AND '_': six random bits a byte, so
 * nothing on the way can compress them, and the POST body needs no
 * percent-encoding (which would triple its size and time the encoder).
 *
 * ! FTPS IS EXPLICIT (AUTH TLS on port 21), with PROT P: commands and data
 * are both encrypted, through exhttp's TLS (exhttp_tls_sopra). EX-OS speaks
 * TLS 1.3 only; a server that offers 1.2 alone gets a clear refusal.
 * ============================================================================= */
#include "libc.h"

/* ! WEAK: at 32 bit fsync is in the static libc only, not in the system's
 * libc.so this program links with. Where it exists, the disk time includes
 * getting the data onto the disk. */
extern int fsync(int fd) __attribute__((weak));
#include "exhttp.h"
#include "exuser.h"

/* +0.001 a ogni modifica: `netspeed -version` la stampa. Vedi EX_VERSIONE. */
EX_VERSIONE("netspeed", "0.002");

#define CHIAVE      "exos"          /* the same as in the generated PHP */
#define DATI_N      (1024u * 1024u) /* the test file: 1 MB */
#define VOLTE_PRED  8u
#define VOLTE_MAX   1024u
#define FINESTRA    (64u * 1024u)
#define RISP_MAX    2048u
#define URL_MAX     EXHTTP_URL_MAX
#define CFG_REL     "/.exwin/config/netspeed.cfg"

static char          g_corpo[DATI_N + 64u];
static unsigned char g_finestra[FINESTRA];

/* =============================================================================
 * Small things
 * ============================================================================= */

/* KB/s and Mbit/s of n bytes in ms milliseconds, as text.
 * ! NO 64-BIT DIVISION: 32-bit EX-OS programs carry no libgcc (__udivdi3).
 * n stays well inside 32 bits: KB/s = (n/1024)*1000/ms, tenths of Mbit/s =
 * n*8*10/(ms*1000) = 2n/(25ms). */
static void velocita(char *s, unsigned int max, unsigned long n, unsigned long ms)
{
    unsigned long kbs, mbit10;

    if (ms == 0) ms = 1;
    kbs    = (n / 1024ul) * 1000ul / ms;
    mbit10 = (2ul * n) / (25ul * ms);
    snprintf(s, max, "%6lu KB/s  (%lu.%lu Mbit/s)", kbs, mbit10 / 10ul, mbit10 % 10ul);
}

/* One line from the keyboard, without the newline. 0 at end of input. */
static int chiedi(const char *domanda, const char *pred, char *dst, unsigned int max)
{
    unsigned int n = 0;
    char         c;

    printf("%s", domanda);
    if (pred && pred[0]) printf(" [%s]", pred);
    printf(": ");
    fflush(stdout);
    while (read(0, &c, 1) == 1) {
        if (c == '\r') continue;
        if (c == '\n') break;
        if (n + 1 < max) dst[n++] = c;
    }
    dst[n] = 0;
    while (n > 0 && dst[n - 1] == ' ') dst[--n] = 0;
    if (n == 0 && pred) { strncpy(dst, pred, max - 1); dst[max - 1] = 0; }
    return 1;
}

static void percorso_cfg(char *dst, unsigned int max)
{
    const char *h = getenv("HOME");

    snprintf(dst, max, "%s%s", (h && h[0]) ? h : "", CFG_REL);
}

/* The web address saved by -makeserver, or "" */
static void cfg_leggi(char *url, unsigned int max)
{
    char  p[256], riga[URL_MAX + 32];
    FILE *f;

    url[0] = 0;
    percorso_cfg(p, sizeof(p));
    f = fopen(p, "r");
    if (!f) return;
    while (fgets(riga, sizeof(riga), f)) {
        char *v;
        size_t n;

        if (strncmp(riga, "server", 6) != 0) continue;
        v = strchr(riga, '=');
        if (!v) continue;
        v++;
        while (*v == ' ') v++;
        n = strlen(v);
        while (n > 0 && (v[n - 1] == '\n' || v[n - 1] == '\r' || v[n - 1] == ' ')) v[--n] = 0;
        strncpy(url, v, max - 1);
        url[max - 1] = 0;
    }
    fclose(f);
}

static int cfg_scrivi(const char *url)
{
    char        p[256], d[256];
    const char *h = getenv("HOME");
    FILE       *f;

    if (!h || !h[0]) return 0;
    snprintf(d, sizeof(d), "%s/.exwin", h);        mkdir(d, 0755);
    snprintf(d, sizeof(d), "%s/.exwin/config", h); mkdir(d, 0755);
    percorso_cfg(p, sizeof(p));
    f = fopen(p, "w");
    if (!f) return 0;
    fprintf(f, "# netspeed: the test point made by `netspeed -makeserver`.\n");
    fprintf(f, "# Only its web address: the FTP user and password are never kept.\n");
    fprintf(f, "server = %s\n", url);
    fclose(f);
    return 1;
}

/* =============================================================================
 * The PHP script, generated for the site it goes on
 * ============================================================================= */
static const char PHP_MODELLO[] =
"<?php\n"
"/* =============================================================================\n"
" * netspeed/index.php - the server half of /bin/netspeed (EX-OS)\n"
" *\n"
" * Generated by `netspeed -makeserver` on @DATA@ for @SITO@\n"
" * EX-OS - Extensible Operating System, GPL-2.0-or-later\n"
" *\n"
" *     POST  chiave=exos&dati=<letters, digits, - and _>  -> stored\n"
" *     GET   ?download   -> the web address of the caller's stored file\n"
" *     GET               -> what this is, for a browser\n"
" *\n"
" * Anyone can write here, so the defences are limits: one file per address\n"
" * (named by a hash of it, never by anything sent), at most BYTE_MAX bytes,\n"
" * only the characters netspeed sends, at most FILE_MAX files, removed after\n"
" * a day.\n"
" * ========================================================================== */\n"
"define('CHIAVE',   '" CHIAVE "');\n"
"define('BYTE_MAX', 4 * 1024 * 1024);\n"
"define('FILE_MAX', 100);\n"
"define('VITA',     24 * 3600);\n"
"define('SITO',     '@SITO@');\n"
"define('DOVE',     __DIR__ . '/file');\n"
"\n"
"function basta($codice, $messaggio) {\n"
"    http_response_code($codice);\n"
"    header('Content-Type: text/plain; charset=utf-8');\n"
"    echo $messaggio . \"\\n\";\n"
"    exit;\n"
"}\n"
"\n"
"$ip   = isset($_SERVER['REMOTE_ADDR']) ? $_SERVER['REMOTE_ADDR'] : 'nessuno';\n"
"$nome = substr(hash('sha256', $ip), 0, 24) . '.bin';\n"
"$file = DOVE . '/' . $nome;\n"
"if (!is_dir(DOVE)) {\n"
"    @mkdir(DOVE, 0755);\n"
"    @file_put_contents(DOVE . '/index.html', '');   /* no listing */\n"
"}\n"
"\n"
"/* --- where the caller's file is ------------------------------------------- */\n"
"if ($_SERVER['REQUEST_METHOD'] === 'GET' && isset($_GET['download'])) {\n"
"    if (!is_file($file)) basta(404, 'niente da scaricare: prima si carica.');\n"
"    header('Content-Type: text/plain; charset=utf-8');\n"
"    header('Cache-Control: no-store');\n"
"    echo SITO . 'file/' . $nome . \"\\n\";\n"
"    exit;\n"
"}\n"
"\n"
"/* --- a browser: what this is ---------------------------------------------- */\n"
"if ($_SERVER['REQUEST_METHOD'] !== 'POST') {\n"
"    header('Content-Type: text/html; charset=utf-8');\n"
"    echo '<!doctype html><html lang=\"it\"><head><meta charset=\"utf-8\">';\n"
"    echo '<meta name=\"viewport\" content=\"width=device-width\"><title>EX-OS netspeed</title>';\n"
"    echo '<style>body{font-family:sans-serif;max-width:46em;margin:2em auto;padding:0 1em;line-height:1.5}code{background:#eee;padding:0 .3em}</style>';\n"
"    echo '</head><body>';\n"
"    echo '<h1>EX-OS netspeed</h1>';\n"
"    echo '<p>Questo e\\' un <b>punto di misura della velocita\\' di rete</b> per il programma ';\n"
"    echo '<code>netspeed</code> del sistema operativo EX-OS. Non e\\' una pagina da usare ';\n"
"    echo 'con il browser: serve alle macchine EX-OS per sapere quanto va veloce la loro ';\n"
"    echo 'connessione, in invio e in ricezione.</p>';\n"
"    echo '<h2>Come funziona</h2><ol>';\n"
"    echo '<li>La macchina EX-OS crea un file temporaneo da 1 MB di dati casuali (e misura ';\n"
"    echo 'quanto ci mette il suo disco a scriverlo).</li>';\n"
"    echo '<li>Lo invia qui con una POST: questo script lo conserva.</li>';\n"
"    echo '<li>Chiede <code>?download</code> e riceve l\\'indirizzo del file conservato.</li>';\n"
"    echo '<li>Lo riscarica 8 volte (o 16, 32, 64...) e misura ogni volta il tempo: le ';\n"
"    echo 'linee asimmetriche ricevono molto piu\\' in fretta di quanto inviano.</li></ol>';\n"
"    echo '<p>Dalla macchina EX-OS:</p><p><code>netspeed -test:' . htmlspecialchars(SITO) . ' -mb:8</code></p>';\n"
"    echo '<h2>Cosa resta su questo server</h2>';\n"
"    echo '<p>Un file di dati casuali per indirizzo IP, al massimo 4 MB, chiamato con ';\n"
"    echo 'un\\'impronta dell\\'indirizzo e non con l\\'indirizzo stesso; si cancella da ';\n"
"    echo 'solo dopo un giorno. Al massimo ' . FILE_MAX . ' file in tutto.</p>';\n"
"    echo '<p>Installato su <b>' . htmlspecialchars(SITO) . '</b> il @DATA@ con ';\n"
"    echo '<code>netspeed -makeserver</code>.</p></body></html>';\n"
"    exit;\n"
"}\n"
"\n"
"/* --- the upload ------------------------------------------------------------ */\n"
"$chiave = isset($_POST['chiave']) ? $_POST['chiave'] : '';\n"
"if (!hash_equals(CHIAVE, $chiave)) basta(403, \"chiave sbagliata: questo non e' un posto dove scrivere.\");\n"
"$dati = isset($_POST['dati']) ? $_POST['dati'] : '';\n"
"if ($dati === '') basta(400, 'nessun dato: non salvo niente.');\n"
"if (strlen($dati) > BYTE_MAX) basta(413, 'troppi dati: rifiutato.');\n"
"if (!preg_match('/^[A-Za-z0-9_-]+$/', $dati)) basta(400, 'solo lettere, cifre, - e _: rifiutato.');\n"
"$tutti = glob(DOVE . '/*.bin');\n"
"if ($tutti === false) $tutti = array();\n"
"foreach ($tutti as $f) if (filemtime($f) < time() - VITA) @unlink($f);\n"
"$tutti = glob(DOVE . '/*.bin');\n"
"if ($tutti !== false && count($tutti) >= FILE_MAX && !is_file($file))\n"
"    basta(507, 'troppi file sul server: riprova domani.');\n"
"if (file_put_contents($file, $dati, LOCK_EX) === false) basta(500, 'il server non riesce a scrivere.');\n"
"header('Content-Type: text/plain; charset=utf-8');\n"
"echo 'ricevuti ' . strlen($dati) . \" byte\\n\";\n";

/* The template with @SITO@ and @DATA@ filled in. `sito` has been checked:
 * it holds no quote and no backslash (it ends up inside a PHP string). */
static unsigned int php_genera(char *dst, unsigned int max, const char *sito, const char *data)
{
    const char  *s = PHP_MODELLO;
    unsigned int n = 0;

    while (*s && n + 1 < max) {
        const char *v = 0;

        if (strncmp(s, "@SITO@", 6) == 0)      { v = sito; s += 6; }
        else if (strncmp(s, "@DATA@", 6) == 0) { v = data; s += 6; }
        if (v) {
            while (*v && n + 1 < max) dst[n++] = *v++;
        } else {
            dst[n++] = *s++;
        }
    }
    dst[n] = 0;
    return n;
}

/* =============================================================================
 * A small FTP client: commands on one connection, data on another (PASV)
 * ============================================================================= */
typedef struct {
    ExHttpTrasporto t;
    char            host[128];
    int             cifrato;        /* 1 after AUTH TLS */
    unsigned char   buf[1024];
    unsigned int    n;
    char            ultima[256];    /* the last reply, for messages */
} Ftp;

/* One line of the reply, without CR LF. 0 if the connection gave nothing. */
static int ftp_riga(Ftp *f, char *riga, unsigned int max)
{
    for (;;) {
        unsigned int i;
        int          r;

        for (i = 0; i < f->n; i++) {
            if (f->buf[i] == '\n') {
                unsigned int k = i;

                if (k > 0 && f->buf[k - 1] == '\r') k--;
                if (k >= max) k = max - 1;
                memcpy(riga, f->buf, k);
                riga[k] = 0;
                memmove(f->buf, f->buf + i + 1, f->n - i - 1);
                f->n -= i + 1;
                return 1;
            }
        }
        if (f->n == sizeof(f->buf)) f->n = 0;     /* a line too long: dropped */
        r = f->t.leggi(f->t.stato, f->buf + f->n, sizeof(f->buf) - f->n, 20000);
        if (r <= 0) return 0;
        f->n += (unsigned int)r;
    }
}

/* The reply code (multi-line replies read to their end), 0 on silence. */
static int ftp_risposta(Ftp *f)
{
    char riga[512];
    int  codice;

    if (!ftp_riga(f, riga, sizeof(riga))) return 0;
    strncpy(f->ultima, riga, sizeof(f->ultima) - 1);
    f->ultima[sizeof(f->ultima) - 1] = 0;
    codice = atoi(riga);
    if (strlen(riga) >= 4 && riga[3] == '-') {
        char fine[5];

        memcpy(fine, riga, 3);
        fine[3] = ' ';
        fine[4] = 0;
        while (ftp_riga(f, riga, sizeof(riga))) {
            if (strncmp(riga, fine, 4) == 0) {
                strncpy(f->ultima, riga, sizeof(f->ultima) - 1);
                break;
            }
        }
    }
    return codice;
}

static int scrivi_tutto(ExHttpTrasporto *t, const void *dati, unsigned int n)
{
    const unsigned char *p = (const unsigned char *)dati;

    while (n > 0) {
        int r = t->scrivi(t->stato, p, n);

        if (r <= 0) return 0;
        p += r;
        n -= (unsigned int)r;
    }
    return 1;
}

/* Sends one command; the reply code. The line is wiped from the stack after
 * sending: for PASS it holds the password. */
static int ftp_comando(Ftp *f, const char *cmd, const char *arg)
{
    char riga[512];

    snprintf(riga, sizeof(riga), "%s%s%s\r\n", cmd, arg ? " " : "", arg ? arg : "");
    if (!scrivi_tutto(&f->t, riga, (unsigned int)strlen(riga))) return 0;
    memset(riga, 0, sizeof(riga));
    return ftp_risposta(f);
}

/* PASV, then a data connection to the port it names - on the SAME host name:
 * the address in the reply is often a private one behind the server's NAT,
 * and FTPS checks the certificate against the name. */
static int ftp_dati(Ftp *f, ExHttpTrasporto *d)
{
    const char  *p;
    unsigned int v[6], k = 0;

    if (ftp_comando(f, "PASV", 0) != 227) return 0;
    p = strchr(f->ultima, '(');
    if (!p) return 0;
    p++;
    while (k < 6 && *p) {
        if (*p >= '0' && *p <= '9') {
            v[k++] = (unsigned int)atoi(p);
            while (*p >= '0' && *p <= '9') p++;
        } else {
            p++;
        }
    }
    if (k < 6) return 0;
    return exhttp_tcp(d, f->host, v[4] * 256u + v[5]);
}

/* Upload `dati` as `nome` in the current directory, in binary mode. */
static int ftp_stor(Ftp *f, const char *nome, const char *dati, unsigned int n, char *errore, unsigned int emax)
{
    ExHttpTrasporto tcp, d;
    int             r;

    if (!ftp_dati(f, &tcp)) { snprintf(errore, emax, "PASV: %s", f->ultima); return 0; }
    r = ftp_comando(f, "STOR", nome);
    if (r != 150 && r != 125) {
        tcp.chiudi(tcp.stato);
        snprintf(errore, emax, "STOR: %s", f->ultima);
        return 0;
    }
    if (f->cifrato) {
        if (!exhttp_tls_sopra(&d, &tcp, f->host, errore, emax)) {
            tcp.chiudi(tcp.stato);
            return 0;
        }
    } else {
        d = tcp;
    }
    if (!scrivi_tutto(&d, dati, n)) {
        d.chiudi(d.stato);
        snprintf(errore, emax, "la connessione dei dati si e' chiusa durante l'invio");
        return 0;
    }
    d.chiudi(d.stato);
    r = ftp_risposta(f);
    if (r != 226 && r != 250) { snprintf(errore, emax, "dopo l'invio: %s", f->ultima); return 0; }
    return 1;
}

/* =============================================================================
 * -makeserver
 * ============================================================================= */
static char g_php[16384];

static int fai_server(void)
{
    char   tipo[16], server[160], utente[96], password[128], cartella[256], sito[URL_MAX];
    char   data[32], errore[160], url[URL_MAX];
    Ftp   *f;
    char  *dp;
    unsigned int porta = 21, n;
    int    r, cifrato, i;
    time_t ora = time(0);
    struct tm *tm = localtime(&ora);

    printf("netspeed -makeserver: mette il punto di misura su un sito web, via FTP.\n");
    printf("Serve: l'accesso FTP del sito e l'indirizzo web della cartella scelta.\n");
    printf("! Utente e password FTP servono solo adesso: non vengono salvati.\n\n");

    chiedi("Tipo di accesso, ftp o ftps", "ftps", tipo, sizeof(tipo));
    for (i = 0; tipo[i]; i++) if (tipo[i] >= 'A' && tipo[i] <= 'Z') tipo[i] = (char)(tipo[i] + 32);
    if (strcmp(tipo, "ftp") && strcmp(tipo, "ftps")) { printf("netspeed: ftp oppure ftps.\n"); return 1; }
    cifrato = strcmp(tipo, "ftps") == 0;
    chiedi("Server FTP (nome o indirizzo, :porta se non e' 21)", 0, server, sizeof(server));
    if (!server[0]) { printf("netspeed: serve il server.\n"); return 1; }
    if ((dp = strrchr(server, ':')) != 0) { porta = (unsigned int)atoi(dp + 1); *dp = 0; }
    if (porta == 0 || porta > 65535) { printf("netspeed: porta non valida.\n"); return 1; }
    chiedi("Utente FTP", 0, utente, sizeof(utente));
    printf("Password FTP: ");
    fflush(stdout);
    if (exuser_leggi_password(password, (int)sizeof(password)) < 0) {
        printf("\nnetspeed: non riesco a chiedere la password senza mostrarla. Mi fermo.\n");
        return 1;
    }
    printf("\n");
    chiedi("Cartella sul server FTP in cui creare netspeed/", "/", cartella, sizeof(cartella));
    chiedi("Indirizzo web di quella cartella (http://... o https://...)", 0, sito, sizeof(sito));
    if (strncmp(sito, "http://", 7) && strncmp(sito, "https://", 8)) {
        printf("netspeed: l'indirizzo comincia con http:// o https://\n");
        memset(password, 0, sizeof(password));
        return 1;
    }
    if (strchr(sito, '\'') || strchr(sito, '\\') || strchr(sito, '"') || strchr(sito, ' ')) {
        printf("netspeed: l'indirizzo non puo' contenere apici, spazi o barre rovesce.\n");
        memset(password, 0, sizeof(password));
        return 1;
    }
    n = (unsigned int)strlen(sito);
    if (n > 0 && sito[n - 1] == '/') sito[--n] = 0;
    snprintf(url, sizeof(url), "%s/netspeed/", sito);

    snprintf(data, sizeof(data), "%04d-%02d-%02d", tm ? tm->tm_year + 1900 : 2026,
             tm ? tm->tm_mon + 1 : 1, tm ? tm->tm_mday : 1);
    n = php_genera(g_php, sizeof(g_php), url, data);

    printf("\n%s su %s:%u, cartella %s, utente %s\n", cifrato ? "FTPS" : "FTP", server, porta,
           cartella, utente);
    printf("Il punto di misura sara' %s (index.php, %u byte)\n\n", url, n);

    f = (Ftp *)calloc(1, sizeof(*f));
    if (!f) { memset(password, 0, sizeof(password)); return 1; }
    strncpy(f->host, server, sizeof(f->host) - 1);
    if (!exhttp_tcp(&f->t, server, porta)) {
        printf("netspeed: non riesco a collegarmi a %s:%u\n", server, porta);
        memset(password, 0, sizeof(password));
        return 1;
    }
    r = ftp_risposta(f);
    if (r != 220) { printf("netspeed: il server non si presenta come FTP: %s\n", f->ultima); goto fine; }
    if (cifrato) {
        ExHttpTrasporto tcp = f->t;

        r = ftp_comando(f, "AUTH", "TLS");
        if (r != 234) { printf("netspeed: il server non accetta FTPS (AUTH TLS): %s\n", f->ultima); goto fine; }
        if (!exhttp_tls_sopra(&f->t, &tcp, server, errore, sizeof(errore))) {
            printf("netspeed: TLS non riuscito: %s\n", errore);
            tcp.chiudi(tcp.stato);
            memset(password, 0, sizeof(password));
            return 1;
        }
        f->cifrato = 1;
        printf("  connessione cifrata (TLS 1.3)\n");
    }
    r = ftp_comando(f, "USER", utente);
    if (r == 331) r = ftp_comando(f, "PASS", password);
    memset(password, 0, sizeof(password));
    if (r != 230 && r != 202) { printf("netspeed: accesso rifiutato: %s\n", f->ultima); goto fine; }
    printf("  accesso come %s\n", utente);
    if (ftp_comando(f, "TYPE", "I") != 200) { printf("netspeed: modo binario rifiutato: %s\n", f->ultima); goto fine; }
    if (cifrato) {
        ftp_comando(f, "PBSZ", "0");
        if (ftp_comando(f, "PROT", "P") != 200) { printf("netspeed: PROT P rifiutato: %s\n", f->ultima); goto fine; }
    }
    if (cartella[0] && strcmp(cartella, "/") != 0 && ftp_comando(f, "CWD", cartella) != 250) {
        printf("netspeed: la cartella %s non c'e': %s\n", cartella, f->ultima);
        goto fine;
    }
    r = ftp_comando(f, "MKD", "netspeed");
    if (r == 257) printf("  cartella netspeed creata\n");
    else          printf("  cartella netspeed: %s\n", f->ultima);    /* most often: it exists */
    if (ftp_comando(f, "CWD", "netspeed") != 250) { printf("netspeed: non entro in netspeed: %s\n", f->ultima); goto fine; }
    if (!ftp_stor(f, "index.php", g_php, n, errore, sizeof(errore))) {
        printf("netspeed: index.php non caricato: %s\n", errore);
        goto fine;
    }
    printf("  index.php caricato (%u byte)\n", n);
    ftp_comando(f, "QUIT", 0);
    f->t.chiudi(f->t.stato);

    /* The proof: the page answers as the script it is. */
    {
        ExHttpEsito e;
        static unsigned char pagina[8192];

        memset(&e, 0, sizeof(e));

        if (exhttp_prendi(url, pagina, sizeof(pagina) - 1, &e) && e.codice == 200) {
            pagina[e.byte < sizeof(pagina) - 1 ? e.byte : sizeof(pagina) - 1] = 0;
            if (strstr((char *)pagina, "EX-OS netspeed"))
                printf("  %s risponde: il punto di misura funziona\n", url);
            else
                printf("  ! %s risponde, ma non con lo script: PHP e' attivo su quel sito?\n", url);
        } else {
            printf("  ! %s non risponde (%d): l'indirizzo web corrisponde alla cartella FTP?\n",
                   url, e.codice);
        }
    }
    if (cfg_scrivi(url)) printf("\nSalvato: `netspeed -test` usera' %s\n", url);
    return 0;

fine:
    memset(password, 0, sizeof(password));
    ftp_comando(f, "QUIT", 0);
    f->t.chiudi(f->t.stato);
    return 1;
}

/* =============================================================================
 * -test
 * ============================================================================= */
typedef struct {
    const char   *atteso;
    unsigned long n_atteso, arrivati, diversi;
} Confronto;

static int verso_confronta(void *dato, const unsigned char *d, unsigned int n)
{
    Confronto   *c = (Confronto *)dato;
    unsigned int i;

    for (i = 0; i < n; i++, c->arrivati++)
        if (c->arrivati >= c->n_atteso || (unsigned char)c->atteso[c->arrivati] != d[i])
            c->diversi++;
    return 1;
}

static unsigned int g_seme;

static unsigned int a_caso(void)            /* xorshift32 */
{
    g_seme ^= g_seme << 13;
    g_seme ^= g_seme >> 17;
    g_seme ^= g_seme << 5;
    return g_seme;
}

static int fai_test(const char *url_dato, unsigned int volte)
{
    static const char alfabeto[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    char          url[URL_MAX], url_file[URL_MAX], tmp[256], riga[64];
    const char   *h = getenv("HOME");
    char         *dati;
    unsigned int  testa, i, k;
    unsigned long t0, ms_scrivi, ms_leggi, ms_su, ms_tot = 0, ms_min = 0, ms_max = 0, errati = 0, riusciti = 0;
    int           fd;
    ExHttpEsito   e;
    static char   blocco[FINESTRA];

    if (url_dato && url_dato[0]) strncpy(url, url_dato, sizeof(url) - 1);
    else                         cfg_leggi(url, sizeof(url));
    url[sizeof(url) - 1] = 0;
    if (!url[0]) {
        printf("netspeed: nessun server. `netspeed -test:<url>` o, la prima volta,\n");
        printf("          `netspeed -makeserver` per crearne uno.\n");
        return 1;
    }
    k = (unsigned int)strlen(url);
    if (k + 16 >= sizeof(url)) { printf("netspeed: url troppo lungo.\n"); return 1; }
    if (url[k - 1] != '/') { url[k] = '/'; url[k + 1] = 0; }

    printf("netspeed: %s, 1 MB su, %u volte giu'\n", url, volte);

    /* --- the temporary file, timed on the disk ------------------------------- */
    snprintf(tmp, sizeof(tmp), "%s/netspeed-%d.tmp", (h && h[0] && strcmp(h, "/")) ? h : "", getpid());
    g_seme = (unsigned int)uptime_ms() * 2654435761u + 0x9E3779B9u;
    if (g_seme == 0) g_seme = 1;
    testa = (unsigned int)snprintf(g_corpo, 64, "chiave=%s&dati=", CHIAVE);
    dati  = g_corpo + testa;

    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) { printf("netspeed: non posso creare %s\n", tmp); return 1; }
    t0 = uptime_ms();
    for (i = 0; i < DATI_N; i += FINESTRA) {
        for (k = 0; k < FINESTRA; k++) blocco[k] = alfabeto[a_caso() & 63u];
        if (write(fd, blocco, FINESTRA) != (long)FINESTRA) {
            close(fd); unlink(tmp);
            printf("netspeed: scrittura di %s non riuscita (disco pieno?)\n", tmp);
            return 1;
        }
    }
    if (fsync) fsync(fd);
    close(fd);
    ms_scrivi = uptime_ms() - t0;

    fd = open(tmp, O_RDONLY);
    if (fd < 0) { unlink(tmp); printf("netspeed: non rileggo %s\n", tmp); return 1; }
    t0 = uptime_ms();
    for (i = 0; i < DATI_N; ) {
        long r = read(fd, dati + i, DATI_N - i);

        if (r <= 0) break;
        i += (unsigned int)r;
    }
    close(fd);
    ms_leggi = uptime_ms() - t0;
    unlink(tmp);
    if (i != DATI_N) { printf("netspeed: riletti %u byte su %u\n", i, DATI_N); return 1; }
    dati[DATI_N] = 0;
    velocita(riga, sizeof(riga), DATI_N, ms_scrivi);
    printf("  disco, scrittura   1024 KB in %5lu ms  %s  (%s)\n", ms_scrivi, riga, tmp);
    velocita(riga, sizeof(riga), DATI_N, ms_leggi);
    printf("  disco, lettura     1024 KB in %5lu ms  %s\n", ms_leggi, riga);

    /* --- upload, once --------------------------------------------------------- */
    t0 = uptime_ms();
    if (!exhttp_posta(url, g_corpo, g_finestra, RISP_MAX, &e)) {
        printf("netspeed: invio non riuscito: %s\n", e.errore[0] ? e.errore : "?");
        return 1;
    }
    ms_su = uptime_ms() - t0;
    if (e.codice != 200) {
        k = e.byte < RISP_MAX - 1 ? e.byte : RISP_MAX - 1;
        g_finestra[k] = 0;
        printf("netspeed: il server risponde %d: %s\n", e.codice, (char *)g_finestra);
        return 1;
    }
    velocita(riga, sizeof(riga), DATI_N, ms_su);
    printf("  invio              1024 KB in %5lu ms  %s\n", ms_su, riga);

    /* --- where the server put it --------------------------------------------- */
    snprintf(url_file, sizeof(url_file), "%s?download", url);
    if (!exhttp_prendi(url_file, g_finestra, RISP_MAX, &e) || e.codice != 200) {
        printf("netspeed: ?download non risponde (%d)\n", e.codice);
        return 1;
    }
    k = e.byte < RISP_MAX - 1 ? e.byte : RISP_MAX - 1;
    g_finestra[k] = 0;
    while (k > 0 && (g_finestra[k - 1] == '\n' || g_finestra[k - 1] == '\r' || g_finestra[k - 1] == ' '))
        g_finestra[--k] = 0;
    if (strncmp((char *)g_finestra, "http", 4) != 0 || k + 16 >= sizeof(url_file)) {
        printf("netspeed: ?download non da' un indirizzo: %s\n", (char *)g_finestra);
        return 1;
    }
    strcpy(url_file, (char *)g_finestra);

    /* --- downloads -------------------------------------------------------------- */
    for (i = 0; i < volte; i++) {
        Confronto     c;
        unsigned long ms;
        char          u[URL_MAX];
        int           ok;

        /* ?n=... so that no cache on the way answers in place of the server */
        snprintf(u, sizeof(u), "%s?n=%u%lu", url_file, i, (unsigned long)uptime_ms());
        c.atteso = dati; c.n_atteso = DATI_N; c.arrivati = 0; c.diversi = 0;
        exhttp_verso(verso_confronta, &c);
        t0 = uptime_ms();
        ok = exhttp_prendi(u, g_finestra, FINESTRA, &e);
        ms = uptime_ms() - t0;
        exhttp_verso(0, 0);
        if (ok && e.byte > 0) verso_confronta(&c, g_finestra, e.byte);

        if (!ok || e.codice != 200) {
            printf("  ricezione %3u: non riuscita (%s)\n", i + 1,
                   !ok ? (e.errore[0] ? e.errore : "?") : "codice HTTP diverso da 200");
            errati++;
            continue;
        }
        if (c.arrivati != DATI_N || c.diversi) {
            printf("  ricezione %3u: %lu byte su %u, %lu diversi: DATI SBAGLIATI\n",
                   i + 1, c.arrivati, DATI_N, c.diversi);
            errati++;
            continue;
        }
        velocita(riga, sizeof(riga), DATI_N, ms);
        printf("  ricezione %3u      1024 KB in %5lu ms  %s\n", i + 1, ms, riga);
        riusciti++;
        ms_tot += ms;
        if (ms_min == 0 || ms < ms_min) ms_min = ms;
        if (ms > ms_max) ms_max = ms;
    }

    /* --- summary ---------------------------------------------------------------- */
    printf("\n");
    velocita(riga, sizeof(riga), DATI_N, ms_scrivi); printf("  disco, scrittura:    %s\n", riga);
    velocita(riga, sizeof(riga), DATI_N, ms_su);     printf("  invio:               %s\n", riga);
    if (riusciti) {
        unsigned long rapp10 = ms_su * riusciti * 10ul / (ms_tot ? ms_tot : 1);

        velocita(riga, sizeof(riga), DATI_N * riusciti, ms_tot); printf("  ricezione, media:    %s\n", riga);
        velocita(riga, sizeof(riga), DATI_N, ms_min);            printf("  ricezione, migliore: %s\n", riga);
        velocita(riga, sizeof(riga), DATI_N, ms_max);            printf("  ricezione, peggiore: %s\n", riga);
        printf("  ricezione/invio:     %lu.%lu volte\n", rapp10 / 10ul, rapp10 % 10ul);
    }
    if (errati) printf("  %lu ricezioni su %u non riuscite o sbagliate\n", errati, volte);
    return errati ? 1 : 0;
}

/* =============================================================================
 * main
 * ============================================================================= */
static void istruzioni(void)
{
    char url[URL_MAX];

    cfg_leggi(url, sizeof(url));
    printf("netspeed - quanto va veloce la rete, in invio e in ricezione\n\n");
    printf("  netspeed -makeserver\n");
    printf("      Mette il punto di misura su un sito web: chiede l'accesso FTP o FTPS\n");
    printf("      (server, utente, password), la cartella e il suo indirizzo web; crea\n");
    printf("      netspeed/ con dentro un index.php generato al momento, che spiega a\n");
    printf("      chi lo apre col browser a cosa serve. Utente e password non si salvano.\n\n");
    printf("  netspeed -test[:url] [-mb:N]\n");
    printf("      Crea un file temporaneo da 1 MB di dati casuali (misurando il disco),\n");
    printf("      lo invia una volta al punto di misura, chiede dove l'ha messo e lo\n");
    printf("      riscarica N volte (8 se -mb manca; 16, 32, 64... per le linee\n");
    printf("      asimmetriche), controllando ogni byte. Senza url usa quello salvato\n");
    printf("      da -makeserver.\n\n");
    printf("  Esempio: netspeed -test:https://esempio.org/netspeed/ -mb:16\n\n");
    printf("  Server salvato: %s\n", url[0] ? url : "nessuno (netspeed -makeserver)");
    printf("\nI tempi comprendono nome, connessione e HTTP, una connessione alla\n");
    printf("volta: e' la velocita' che vede un programma di EX-OS.\n");
}

int main(int argc, char **argv)
{
    const char  *url = 0;
    unsigned int volte = VOLTE_PRED;
    int          test = 0, i;

    if (argc < 2) { istruzioni(); return 0; }
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-makeserver") == 0) return fai_server();
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) { istruzioni(); return 0; }
        if (strcmp(argv[i], "-test") == 0) { test = 1; continue; }
        if (strncmp(argv[i], "-test:", 6) == 0) { test = 1; url = argv[i] + 6; continue; }
        if (strncmp(argv[i], "-mb:", 4) == 0) {
            volte = (unsigned int)atoi(argv[i] + 4);
            if (volte < 1 || volte > VOLTE_MAX) {
                printf("netspeed: -mb:N con N da 1 a %u.\n", VOLTE_MAX);
                return 1;
            }
            continue;
        }
        printf("netspeed: opzione '%s' sconosciuta. `netspeed` da solo spiega come si usa.\n", argv[i]);
        return 1;
    }
    if (!test) { istruzioni(); return 0; }
    return fai_test(url, volte);
}
