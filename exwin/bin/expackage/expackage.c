/* =============================================================================
 * exwin/bin/expackage/expackage.c — install and remove applications from the
 * repository, with the mouse (3 November 2026)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * What the user asked for: a window with the applications of the repository,
 * each one marked to be installed or removed; a menu entry where the
 * repository and its mode (HTTP, FTP, ...) are set and checked to hold a list
 * of applications; an application that needs others says so, and they are
 * installed with it.
 *
 * ! IT IS A FACE OF netupdate, NOT A SECOND netupdate. The catalogue, the
 * register, the checksums, the dependency chain and the removal of orphans
 * are all in bin/netupdate, and they stay there: this program runs
 *
 *     netupdate -install:list -tab        the list (one line per package)
 *     netupdate -install:<id> -yes        an install, with its chain
 *     netupdate -remove:<id> -yes         a removal, with its orphans
 *     netupdate -set:<TIPO> <indirizzo>   the repository
 *
 * and shows what they print. A second copy of the rules would drift from the
 * first; one program that writes /boot/netupdate.reg is the only way the
 * register stays true.
 *
 * ! THE DEPENDENCIES ARE SHOWN HERE, AND RESOLVED THERE. Marking an
 * application marks the ones it needs, in chain, as "[d]", so the user sees
 * before pressing Applica what will arrive; netupdate then computes the same
 * chain again from the same catalogue and installs it. On removal it is the
 * other way round: an application another installed one needs cannot be
 * marked, and the libraries nobody uses any more go away with it (netupdate
 * says which, in the log).
 *
 * ! netupdate RUNS IN THE BACKGROUND: the network and the disk can take
 * minutes, and a window that does not repaint for minutes looks dead. A
 * timer looks at the child (waitpid WNOHANG) and puts the last line it wrote
 * on the status line.
 *
 * ! WRITING NEEDS root (/boot/netupdate.reg, /boot/netupdate.cnf, /bin). Any
 * user sees the list; the buttons that change something say so instead of
 * running a netupdate that would fail halfway.
 * ============================================================================= */
#include "libc.h"
#include "exwin.h"
#include "exdlg.h"
#include "exinfo.h"
#include "kbd_proto.h"

#define VERSIONE_APP "0.001"
EX_VERSIONE("expackage", VERSIONE_APP);

#define FIN_W       660
#define FIN_H       450
#define ID_LEN       24
/* what is SHOWN of netupdate's output (Visualizza > Registro): the tail of
 * the session. A view, not a limit: netupdate's own work is not cut. */
#define LOG_MAX   32768
#define CNF       "/boot/netupdate.cnf"

enum {
    ID_LISTA = 10, ID_SEGNA, ID_APPLICA, ID_PULISCI, ID_AGGIORNA, ID_ESCI,
    ID_REPO, ID_LOG, ID_ISTRUZIONI, ID_INFO,
    ID_R_TIPO, ID_R_URL, ID_R_VERIFICA, ID_R_CHIUDI
};

/* what the user asked for a row */
#define SEGNO_NO      0
#define SEGNO_METTI   1       /* not installed: install it */
#define SEGNO_TOGLI   2       /* installed: remove it */

typedef struct {
    char id[ID_LEN];
    char nome[48];
    char dice[100];
    char **vuole;             /* n_vuole names, inside one allocated copy */
    int  n_vuole;
    long mb;
    int  installato;
    int  sempre;
    int  segno;
    int  dipendenza;          /* needed by a marked one, not installed */
} App;

/* ! AS MANY AS THE REPOSITORY HAS: the array grows with the list (the rule
 * of the system: no fixed ceiling; netupdate had one at 32 and lost two
 * packages when the catalogue reached 34). */
static App     *g_app;
static int      g_n, g_cap;
static ExWindow g_f, g_lista, g_info1, g_info2, g_msg, g_repo_lbl, g_bt_applica;
static ExWindow g_r = 0, g_r_tipo, g_r_url, g_r_esito;
static char     g_netupdate[64];
static char     g_log[LOG_MAX];          /* everything netupdate said, this session */
static int      g_root;

/* =============================================================================
 * netupdate in the background: a queue of jobs, one running at a time
 * ============================================================================= */
#define J_LISTA     1
#define J_INSTALLA  2
#define J_TOGLI     3
#define J_SET       4
#define J_VERIFICA  5     /* a list after J_SET: is there a catalogue? */
#define J_RIPRISTINA 6    /* -set back to the old repository */
typedef struct { int tipo; char a[200]; char b[16]; } Lavoro;

static Lavoro *g_coda;
static int    g_n_coda, g_cap_coda, g_prossimo;
static int    g_pid = 0, g_tipo_corrente;
static char   g_file_log[64], g_file_vuoto[64];
static int    g_errori;
static char   g_vecchio_tipo[16], g_vecchio_url[200];

static void messaggio(const char *t)
{
    ex_set_text(g_msg, t);
    if (!ex_redraw(g_msg)) ex_update(g_f);
}

static void log_aggiungi(const char *t)
{
    size_t l = strlen(g_log), k = strlen(t);

    if (k >= LOG_MAX / 2) t += k - LOG_MAX / 2, k = LOG_MAX / 2;
    if (l + k + 1 >= LOG_MAX) {          /* keep the tail */
        size_t via = l + k + 1 - LOG_MAX + LOG_MAX / 4;

        memmove(g_log, g_log + via, l - via + 1);
        l -= via;
    }
    memcpy(g_log + l, t, k + 1);
}

static void accoda(int tipo, const char *a, const char *b)
{
    if (g_n_coda >= g_cap_coda) {
        int   cap = g_cap_coda ? g_cap_coda * 2 : 16;
        void *n = realloc(g_coda, (size_t)cap * sizeof(Lavoro));

        if (!n) { messaggio("Memoria finita."); return; }
        g_coda = (Lavoro *)n;
        g_cap_coda = cap;
    }
    g_coda[g_n_coda].tipo = tipo;
    snprintf(g_coda[g_n_coda].a, sizeof(g_coda[0].a), "%s", a ? a : "");
    snprintf(g_coda[g_n_coda].b, sizeof(g_coda[0].b), "%s", b ? b : "");
    g_n_coda++;
}

static int occupato(void) { return g_pid > 0 || g_prossimo < g_n_coda; }

static void bottoni(void)
{
    ex_enable(g_bt_applica, !occupato());
}

static int avvia(const Lavoro *l)
{
    char       *av[6];
    char        arg1[ID_LEN + 16], titolo[120];
    SpawnRedir  r[3];
    int         fd, pid, n = 0;

    av[n++] = g_netupdate;
    switch (l->tipo) {
    case J_LISTA: case J_VERIFICA:
        av[n++] = (char *)"-install:list"; av[n++] = (char *)"-tab";
        snprintf(titolo, sizeof(titolo), "Leggo l'elenco del repository...");
        break;
    case J_INSTALLA:
        snprintf(arg1, sizeof(arg1), "-install:%s", l->a);
        av[n++] = arg1; av[n++] = (char *)"-yes";
        snprintf(titolo, sizeof(titolo), "Installo %s...", l->a);
        break;
    case J_TOGLI:
        snprintf(arg1, sizeof(arg1), "-remove:%s", l->a);
        av[n++] = arg1; av[n++] = (char *)"-yes";
        snprintf(titolo, sizeof(titolo), "Tolgo %s...", l->a);
        break;
    default:                                    /* J_SET, J_RIPRISTINA */
        snprintf(arg1, sizeof(arg1), "-set:%s", l->b);
        av[n++] = arg1; av[n++] = (char *)l->a;
        snprintf(titolo, sizeof(titolo), "Scrivo il repository...");
        break;
    }
    av[n] = 0;

    fd = open(g_file_log, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) { messaggio("Non riesco a scrivere in /tmp."); return -1; }
    /* stdin from an empty file: any question netupdate still asks gets its
     * default at once instead of waiting for a keyboard nobody is at */
    r[0].fd = 0; r[0].flags = O_RDONLY; r[0].percorso = g_file_vuoto; r[0].fd_padre = 0;
    r[1].fd = 1; r[1].flags = 0; r[1].percorso = NULL; r[1].fd_padre = fd;
    r[2].fd = 2; r[2].flags = 0; r[2].percorso = NULL; r[2].fd_padre = fd;
    pid = spawn_ex(g_netupdate, av, environ, r, 3);
    close(fd);
    if (pid < 0) { messaggio("netupdate non parte."); return -1; }
    {
        char riga[300];
        int  k;

        snprintf(riga, sizeof(riga), "\n$");
        for (k = 0; k < n; k++) {
            size_t l2 = strlen(riga);
            snprintf(riga + l2, sizeof(riga) - l2, " %s", av[k]);
        }
        strcat(riga, "\n");
        log_aggiungi(riga);
    }
    g_pid = pid;
    g_tipo_corrente = l->tipo;
    messaggio(titolo);
    return 0;
}

/* the whole output of the job just finished */
static int leggi_log(char *buf, int max)
{
    int fd = open(g_file_log, O_RDONLY), n, t = 0;

    if (fd < 0) { buf[0] = 0; return 0; }
    while (t < max - 1 && (n = (int)read(fd, buf + t, (unsigned int)(max - 1 - t))) > 0) t += n;
    close(fd);
    buf[t] = 0;
    return t;
}

/* the last line with something on it, for the status line */
static void ultima_riga(char *out, int max)
{
    static char buf[4096];
    int         fd = open(g_file_log, O_RDONLY), n, t = 0;
    char       *p, *e;
    struct stat st;

    out[0] = 0;
    if (fd < 0) return;
    if (fstat(fd, &st) == 0 && (long)st.st_size > (long)sizeof(buf) - 1)
        lseek(fd, (long)st.st_size - (long)sizeof(buf) + 1, SEEK_SET);
    while (t < (int)sizeof(buf) - 1 && (n = (int)read(fd, buf + t, sizeof(buf) - 1 - t)) > 0) t += n;
    close(fd);
    buf[t] = 0;
    e = buf + t;
    while (e > buf && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ')) *--e = 0;
    p = e;
    while (p > buf && p[-1] != '\n') p--;
    while (*p == ' ') p++;
    snprintf(out, (unsigned int)max, "%s", p);
}

/* =============================================================================
 * The list
 * ============================================================================= */
static App *cerca(const char *id)
{
    int i;

    for (i = 0; i < g_n; i++) if (strcmp(g_app[i].id, id) == 0) return &g_app[i];
    return 0;
}

/* split one "PKG\t..." line (see riga_tab in bin/netupdate/netupdate.c) */
static void leggi_pkg(char *riga)
{
    char *campo[8];
    int   n = 0, k;
    App  *a;
    char *p = riga, *v;

    while (n < 8) {
        campo[n++] = p;
        p = strchr(p, '\t');
        if (!p) break;
        *p++ = 0;
    }
    if (n < 8) return;
    if (g_n >= g_cap) {
        int   cap = g_cap ? g_cap * 2 : 32;
        void *nuovo = realloc(g_app, (size_t)cap * sizeof(App));

        if (!nuovo) return;
        g_app = (App *)nuovo;
        g_cap = cap;
    }
    a = &g_app[g_n];
    memset(a, 0, sizeof(*a));
    snprintf(a->id, sizeof(a->id), "%s", campo[1]);
    a->installato = atoi(campo[2]);
    a->mb = atol(campo[3]);
    a->sempre = atoi(campo[4]);
    if (strcmp(campo[5], "-") != 0) {
        /* one block: the pointers, then the names they point into */
        int    quanti = 1;
        size_t l = strlen(campo[5]) + 1;
        char  *testo;

        for (v = campo[5]; *v; v++) if (*v == ',') quanti++;
        a->vuole = (char **)malloc((size_t)quanti * sizeof(char *) + l);
        if (a->vuole) {
            testo = (char *)(a->vuole + quanti);
            memcpy(testo, campo[5], l);
            for (v = testo; v && *v; ) {
                char *c = strchr(v, ',');

                if (c) *c = 0;
                a->vuole[a->n_vuole++] = v;
                v = c ? c + 1 : 0;
            }
        }
    }
    snprintf(a->nome, sizeof(a->nome), "%s", campo[6]);
    k = (int)strlen(campo[7]);
    while (k > 0 && (campo[7][k - 1] == '\n' || campo[7][k - 1] == '\r')) campo[7][--k] = 0;
    snprintf(a->dice, sizeof(a->dice), "%s", strcmp(campo[7], "-") ? campo[7] : "");
    g_n++;
}

/* rebuilds g_app from the output of -install:list -tab; returns how many,
 * -1 if the list did not reach its FINE line */
static int elenco_da(char *testo)
{
    char *r = testo, *fine;
    int   finito = 0, ns = 0, i;
    int  *segnati = g_n ? (int *)malloc((size_t)g_n * sizeof(int)) : 0;
    char (*vecchi)[ID_LEN] = g_n ? malloc((size_t)g_n * ID_LEN) : 0;

    /* the marks survive a reload */
    for (i = 0; i < g_n; i++) {
        if (g_app[i].segno && segnati && vecchi) {
            snprintf(vecchi[ns], ID_LEN, "%s", g_app[i].id);
            segnati[ns++] = g_app[i].segno;
        }
        free(g_app[i].vuole);
    }
    g_n = 0;
    while (r && *r) {
        fine = strchr(r, '\n');
        if (fine) *fine = 0;
        if (strncmp(r, "PKG\t", 4) == 0) leggi_pkg(r);
        else if (strncmp(r, "FINE\t", 5) == 0) finito = 1;
        r = fine ? fine + 1 : 0;
    }
    for (i = 0; i < ns; i++) {
        App *a = cerca(vecchi[i]);

        if (a && ((segnati[i] == SEGNO_METTI && !a->installato) ||
                  (segnati[i] == SEGNO_TOGLI && a->installato)))
            a->segno = segnati[i];
    }
    free(segnati);
    free(vecchi);
    return finito ? g_n : -1;
}

/* the [d] marks: the chain of everything marked to be installed */
static void dipendenze_calcola(void)
{
    int i, k, giro, cambiato;

    for (i = 0; i < g_n; i++) g_app[i].dipendenza = 0;
    for (giro = 0; giro <= g_n; giro++) {
        cambiato = 0;
        for (i = 0; i < g_n; i++) {
            App *a = &g_app[i];

            if (a->segno != SEGNO_METTI && !a->dipendenza) continue;
            for (k = 0; k < a->n_vuole; k++) {
                App *d = cerca(a->vuole[k]);

                if (!d || d->installato || d->segno == SEGNO_METTI || d->dipendenza) continue;
                d->dipendenza = 1;
                cambiato = 1;
            }
        }
        if (!cambiato) break;
    }
}

/* who, installed and staying, or about to be installed, needs `id`: a list
 * of names in out, "" if nobody */
static void chi_lo_usa(const char *id, char *out, int max)
{
    int i, k;

    out[0] = 0;
    for (i = 0; i < g_n; i++) {
        App *a = &g_app[i];
        int  resta = (a->installato && a->segno != SEGNO_TOGLI) ||
                     a->segno == SEGNO_METTI || a->dipendenza;

        if (!resta) continue;
        for (k = 0; k < a->n_vuole; k++)
            if (strcmp(a->vuole[k], id) == 0) {
                size_t l = strlen(out);

                snprintf(out + l, (unsigned int)(max - (int)l), "%s%s", l ? ", " : "", a->id);
                break;
            }
    }
}

static void lista_riempi(void)
{
    unsigned int scelta = ex_list_get_selected(g_lista);
    int          i;

    dipendenze_calcola();
    ex_list_clear(g_lista);
    for (i = 0; i < g_n; i++) {
        App        *a = &g_app[i];
        const char *s, *stato;
        char        r[160];

        if (a->segno == SEGNO_METTI)      s = "[+]";
        else if (a->segno == SEGNO_TOGLI) s = "[-]";
        else if (a->dipendenza)           s = "[d]";
        else if (a->installato)           s = "[x]";
        else                              s = "[ ]";
        if (a->installato) stato = a->sempre ? "del sistema" : "installato";
        else stato = "";
        snprintf(r, sizeof(r), "%s %-12s %-11s %5ld  %s", s, a->id, stato, a->mb, a->nome);
        ex_list_add(g_lista, r);
    }
    if (scelta < (unsigned int)g_n) ex_list_select(g_lista, scelta);
}

static void info_mostra(void)
{
    unsigned int i = ex_list_get_selected(g_lista);
    char         t[240], usa[160];
    App         *a;
    int          k;

    if (i >= (unsigned int)g_n) { ex_set_text(g_info1, ""); ex_set_text(g_info2, ""); return; }
    a = &g_app[i];
    snprintf(t, sizeof(t), "%s - %s", a->nome, a->dice[0] ? a->dice : a->id);
    ex_set_text(g_info1, t);
    t[0] = 0;
    if (a->n_vuole) {
        snprintf(t, sizeof(t), "Vuole:");
        for (k = 0; k < a->n_vuole; k++) {
            App   *d = cerca(a->vuole[k]);
            size_t l = strlen(t);

            snprintf(t + l, sizeof(t) - l, " %s%s", a->vuole[k],
                     d && d->installato ? "" : " (da installare)");
        }
    }
    chi_lo_usa(a->id, usa, sizeof(usa));
    if (usa[0]) {
        size_t l = strlen(t);

        snprintf(t + l, sizeof(t) - l, "%sLo usano: %s", l ? "   " : "", usa);
    }
    if (!t[0]) snprintf(t, sizeof(t), "Non vuole altri pacchetti.");
    ex_set_text(g_info2, t);
}

/* "Segna": flips the mark of the chosen row */
static void segna(void)
{
    unsigned int i = ex_list_get_selected(g_lista);
    App         *a;
    char         usa[160], t[240];

    if (i >= (unsigned int)g_n) { messaggio("Scegli un'applicazione dall'elenco."); return; }
    a = &g_app[i];
    if (a->installato) {
        if (a->segno == SEGNO_TOGLI) { a->segno = SEGNO_NO; messaggio("Resta installato."); }
        else if (a->sempre) {
            messaggio("E' del sistema: non si toglie.");
            return;
        } else {
            chi_lo_usa(a->id, usa, sizeof(usa));
            if (usa[0]) {
                snprintf(t, sizeof(t), "Non si puo' togliere %s: lo usano %s.", a->id, usa);
                messaggio(t);
                return;
            }
            a->segno = SEGNO_TOGLI;
            snprintf(t, sizeof(t), "%s sara' tolto, con le librerie che non usa piu' nessuno.", a->id);
            messaggio(t);
        }
    } else if (a->segno == SEGNO_METTI) {
        a->segno = SEGNO_NO;
        messaggio("Non sara' installato.");
    } else {
        int k, n = 0;

        a->segno = SEGNO_METTI;
        dipendenze_calcola();
        /* a [d] whose installed user is marked for removal: keep it */
        for (k = 0; k < g_n; k++)
            if (g_app[k].segno == SEGNO_TOGLI) {
                char u[160];

                chi_lo_usa(g_app[k].id, u, sizeof(u));
                if (u[0]) g_app[k].segno = SEGNO_NO;
            }
        snprintf(t, sizeof(t), "%s sara' installato", a->id);
        for (k = 0; k < g_n; k++)
            if (g_app[k].dipendenza) {
                size_t l = strlen(t);

                snprintf(t + l, sizeof(t) - l, "%s%s", n++ ? ", " : "; servono anche: ", g_app[k].id);
            }
        strncat(t, ".", sizeof(t) - strlen(t) - 1);
        messaggio(t);
    }
    lista_riempi();
    info_mostra();
    ex_update(g_f);
}

static void pulisci(void)
{
    int i;

    for (i = 0; i < g_n; i++) g_app[i].segno = SEGNO_NO;
    lista_riempi();
    info_mostra();
    messaggio("Nessun segno.");
    ex_update(g_f);
}

static int serve_root(const char *cosa)
{
    char t[300];

    if (g_root) return 0;
    snprintf(t, sizeof(t),
             "%s scrive in /boot e nelle directory del sistema: serve root.\n\n"
             "Entra come root, oppure da un terminale:  sudo expackage", cosa);
    ex_dlg_avviso("ExPackage", t);
    return 1;
}

static void applica(void)
{
    char t[1200];
    int  i, metti = 0, togli = 0;
    size_t l;

    if (occupato()) { messaggio("netupdate sta ancora lavorando."); return; }
    for (i = 0; i < g_n; i++) {
        if (g_app[i].segno == SEGNO_METTI) metti++;
        if (g_app[i].segno == SEGNO_TOGLI) togli++;
    }
    if (!metti && !togli) { messaggio("Niente da fare: segna prima qualcosa (Invio o doppio clic)."); return; }
    if (serve_root("Installare e togliere")) return;

    t[0] = 0;
    if (metti) {
        long mb = 0;

        strcat(t, "Installo:");
        for (i = 0; i < g_n; i++)
            if (g_app[i].segno == SEGNO_METTI || g_app[i].dipendenza) {
                l = strlen(t);
                snprintf(t + l, sizeof(t) - l, " %s%s", g_app[i].id,
                         g_app[i].dipendenza ? " (dipendenza)" : "");
                mb += g_app[i].mb;
            }
        l = strlen(t);
        snprintf(t + l, sizeof(t) - l, "\ncirca %ld MB.\n", mb);
    }
    if (togli) {
        strncat(t, "\nTolgo:", sizeof(t) - strlen(t) - 1);
        for (i = 0; i < g_n; i++)
            if (g_app[i].segno == SEGNO_TOGLI) {
                l = strlen(t);
                snprintf(t + l, sizeof(t) - l, " %s", g_app[i].id);
            }
        strncat(t, "\ne le librerie che non usa piu' nessuno.\n", sizeof(t) - strlen(t) - 1);
    }
    if (!ex_dlg_conferma("Applica", t, "Procedi", "Annulla")) return;

    g_n_coda = g_prossimo = 0;
    g_errori = 0;
    for (i = 0; i < g_n; i++) if (g_app[i].segno == SEGNO_TOGLI) accoda(J_TOGLI, g_app[i].id, 0);
    for (i = 0; i < g_n; i++) if (g_app[i].segno == SEGNO_METTI) accoda(J_INSTALLA, g_app[i].id, 0);
    accoda(J_LISTA, 0, 0);
    bottoni();
}

/* =============================================================================
 * The repository window
 * ============================================================================= */
static const char *g_tipi[4] = { "HTTP", "FTP", "HTTPS", "FTPS" };

static void cnf_leggi(char *tipo, int tmax, char *url, int umax)
{
    char  riga[300];
    FILE *f = fopen(CNF, "r");

    snprintf(tipo, (unsigned int)tmax, "HTTP");
    url[0] = 0;
    if (!f) return;
    while (fgets(riga, sizeof(riga), f)) {
        char *p = riga, *v, *e;

        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#') continue;
        v = strchr(p, '=');
        if (!v) continue;
        *v++ = 0;
        while (*v == ' ' || *v == '\t') v++;
        e = v + strlen(v);
        while (e > v && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ')) *--e = 0;
        e = p + strlen(p);
        while (e > p && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
        if (strcmp(p, "tipo") == 0) snprintf(tipo, (unsigned int)tmax, "%s", v);
        if (strcmp(p, "url") == 0)  snprintf(url, (unsigned int)umax, "%s", v);
    }
    fclose(f);
}

static void repo_etichetta(void)
{
    char tipo[16], url[200], t[260];

    cnf_leggi(tipo, sizeof(tipo), url, sizeof(url));
    if (url[0]) snprintf(t, sizeof(t), "Repository: %s://%s", tipo, url);
    else snprintf(t, sizeof(t), "Repository: non impostato (menu Repository > Imposta)");
    ex_set_text(g_repo_lbl, t);
}

static long proc_repo(ExWindow f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_CLOSE:
        ex_destroy(g_r);
        g_r = 0;
        return 0;
    case EXM_KEY:
        if ((wp & KBD_KEY_MASK) == 27) { ex_destroy(g_r); g_r = 0; return 0; }
        return ex_default_proc(f, msg, wp, lp);
    case EXM_COMMAND:
        if (wp == ID_R_CHIUDI) { ex_destroy(g_r); g_r = 0; return 0; }
        if (wp == ID_R_VERIFICA) {
            unsigned int k = ex_item_get_selected(g_r_tipo);
            const char  *u = ex_get_text(g_r_url);

            if (occupato()) { ex_set_text(g_r_esito, "netupdate sta ancora lavorando."); ex_update(g_r); return 0; }
            if (!u || !u[0]) { ex_set_text(g_r_esito, "Scrivi l'indirizzo del repository."); ex_update(g_r); return 0; }
            if (serve_root("Cambiare il repository")) return 0;
            cnf_leggi(g_vecchio_tipo, sizeof(g_vecchio_tipo), g_vecchio_url, sizeof(g_vecchio_url));
            g_n_coda = g_prossimo = 0;
            g_errori = 0;
            accoda(J_SET, u, g_tipi[k < 4 ? k : 0]);
            accoda(J_VERIFICA, 0, 0);
            bottoni();
            ex_set_text(g_r_esito, "Scrivo e controllo che ci sia l'elenco delle applicazioni...");
            ex_update(g_r);
        }
        return 0;
    }
    return ex_default_proc(f, msg, wp, lp);
}

static void repo_apri(void)
{
    char         tipo[16], url[200];
    unsigned int sw = 0, sh = 0, k;

    if (g_r) { ex_activate(g_r); return; }
    ex_screen_size(&sw, &sh);
    g_r = ex_create("window", "Repository", EX_CAPTION | EX_BORDER | EX_CLOSEBOX,
                    ((int)sw - 460) / 2, ((int)sh - 210) / 2, 460, 210, 0, 0, proc_repo);
    if (!g_r) return;
    cnf_leggi(tipo, sizeof(tipo), url, sizeof(url));
    ex_create("label", "Da dove si installano le applicazioni (lo stesso di netupdate -set).",
              EX_CHILD, 10, 28, 440, 18, g_r, 0, 0);
    ex_create("label", "Modalita'", EX_CHILD, 10, 58, 80, 18, g_r, 0, 0);
    g_r_tipo = ex_create("combo", "", EX_CHILD, 100, 56, 120, 22, g_r, ID_R_TIPO, 0);
    for (k = 0; k < 4; k++) {
        ex_item_add(g_r_tipo, g_tipi[k]);
        if (strcasecmp(tipo, g_tipi[k]) == 0) ex_item_select(g_r_tipo, k);
    }
    ex_create("label", "Indirizzo", EX_CHILD, 10, 90, 80, 18, g_r, 0, 0);
    g_r_url = ex_create("testo", url, EX_CHILD, 100, 88, 350, 22, g_r, ID_R_URL, 0);
    ex_create("label", "esempio: esempio.org/exos (la radice con versione.txt e catalogo.txt)",
              EX_CHILD, 10, 116, 440, 18, g_r, 0, 0);
    ex_create("button", "Salva e verifica", EX_CHILD, 10, 142, 140, 24, g_r, ID_R_VERIFICA, 0);
    ex_create("button", "Chiudi", EX_CHILD, 160, 142, 90, 24, g_r, ID_R_CHIUDI, 0);
    g_r_esito = ex_create("label", "", EX_CHILD, 10, 176, 440, 18, g_r, 0, 0);
    ex_default_proc(g_r, EXM_PAINT, 0, 0);
    ex_update(g_r);
}

static void repo_esito(const char *t)
{
    if (g_r) { ex_set_text(g_r_esito, t); ex_update(g_r); }
}

/* =============================================================================
 * A job ended: what it means
 * ============================================================================= */
static char g_uscita[LOG_MAX];

static void lavoro_finito(int stato)
{
    int  codice = WIFEXITED(stato) ? WEXITSTATUS(stato) : 1;
    char t[200];
    int  n;

    leggi_log(g_uscita, sizeof(g_uscita));
    if (g_tipo_corrente != J_LISTA && g_tipo_corrente != J_VERIFICA) log_aggiungi(g_uscita);
    else {
        char r[96];

        snprintf(r, sizeof(r), "(elenco: uscita %d)\n", codice);
        log_aggiungi(r);
    }

    switch (g_tipo_corrente) {
    case J_LISTA:
        n = elenco_da(g_uscita);
        lista_riempi();
        info_mostra();
        if (n < 0) {
            g_n = 0;
            ex_list_clear(g_lista);
            messaggio("L'elenco non si legge: Visualizza > Registro di netupdate dice perche'.");
        } else if (g_errori) {
            snprintf(t, sizeof(t), "Fatto, ma %d operazion%s non riuscit%s: vedi il registro.",
                     g_errori, g_errori == 1 ? "e" : "i", g_errori == 1 ? "a" : "e");
            messaggio(t);
        } else {
            snprintf(t, sizeof(t), "%d applicazioni nel repository.", n);
            messaggio(t);
        }
        break;
    case J_VERIFICA:
        n = elenco_da(g_uscita);
        lista_riempi();
        info_mostra();
        repo_etichetta();
        if (n > 0) {
            snprintf(t, sizeof(t), "Va bene: il repository ha %d applicazioni.", n);
            repo_esito(t);
            messaggio(t);
        } else {
            /* ! THE OLD ONE GOES BACK. A repository without a catalogue left
             * in netupdate.cnf would also break `netupdate -check`. */
            if (g_vecchio_url[0]) {
                accoda(J_RIPRISTINA, g_vecchio_url, g_vecchio_tipo);
                repo_esito("Nessun elenco di applicazioni li': rimetto il repository di prima.");
            } else
                repo_esito("Nessun elenco di applicazioni a quell'indirizzo.");
            messaggio("Il repository nuovo non ha un elenco: vedi il registro di netupdate.");
        }
        break;
    case J_SET:
        if (codice != 0) {
            g_prossimo = g_n_coda;          /* no check of what was not written */
            repo_esito("Non sono riuscito a scrivere /boot/netupdate.cnf.");
        }
        repo_etichetta();
        break;
    case J_RIPRISTINA:
        repo_etichetta();
        repo_esito("Nessun elenco li': il repository e' tornato quello di prima.");
        accoda(J_LISTA, 0, 0);
        break;
    default:                                    /* J_INSTALLA, J_TOGLI */
        if (codice != 0) g_errori++;
        break;
    }
}

static void controlla_lavoro(void)
{
    int  stato = 0, r;
    char t[200];

    if (g_pid > 0) {
        r = waitpid(g_pid, &stato, WNOHANG);
        if (r == 0) {
            if (g_tipo_corrente == J_INSTALLA || g_tipo_corrente == J_TOGLI) {
                char riga[160];

                ultima_riga(riga, sizeof(riga));
                snprintf(t, sizeof(t), "%s %s: %s", g_tipo_corrente == J_TOGLI ? "Tolgo" : "Installo",
                         g_coda[g_prossimo - 1].a, riga);
                messaggio(t);
            }
            return;
        }
        g_pid = 0;
        lavoro_finito(r > 0 ? stato : 1 << 8);
    }
    if (g_prossimo < g_n_coda) {
        if (avvia(&g_coda[g_prossimo++]) != 0) g_prossimo = g_n_coda;
    }
    bottoni();
    ex_update(g_f);
}

/* =============================================================================
 * The window
 * ============================================================================= */
static void mostra_log(void)
{
    ex_dlg_testo("Registro di netupdate", g_log[0] ? g_log : "netupdate non ha ancora detto niente.");
}

static void informazioni(void)
{
    char t[640];

    exinfo_testo(t, sizeof(t), "ExPackage", VERSIONE_APP,
                 "Installa e toglie le applicazioni del repository di "
                 "netupdate, con le loro dipendenze.");
    ex_dlg_avviso("Informazioni su", t);
}

static void istruzioni(void)
{
    ex_dlg_testo("Istruzioni di ExPackage",
        "L'ELENCO\n"
        "Le applicazioni del repository. Davanti a ognuna:\n"
        "  [ ] non installata        [x] installata\n"
        "  [+] da installare         [-] da togliere\n"
        "  [d] da installare perche' serve a una segnata [+]\n"
        "\n"
        "SEGNARE\n"
        "Invio, doppio clic o il pulsante Segna cambiano il segno della riga "
        "scelta. Una non installata diventa [+], e quelle che vuole diventano "
        "[d]; una installata diventa [-], se nessun'altra la usa. Sotto "
        "l'elenco: cosa vuole e chi la usa.\n"
        "\n"
        "APPLICA\n"
        "Toglie le [-], poi installa le [+] con le [d]. Lo fa netupdate, in "
        "disparte: la riga in fondo dice dove e' arrivato, e Visualizza > "
        "Registro di netupdate mostra tutto quello che ha scritto. Serve root.\n"
        "\n"
        "IL REPOSITORY\n"
        "Repository > Imposta: la modalita' (HTTP, FTP, HTTPS, FTPS) e "
        "l'indirizzo. Salva e verifica lo scrive in /boot/netupdate.cnf e "
        "controlla che li' ci sia l'elenco delle applicazioni; se non c'e', "
        "rimette quello di prima.\n"
        "\n"
        "DA RIGA DI COMANDO\n"
        "  netupdate -install:list -p    l'elenco, una pagina per volta\n"
        "  netupdate -install:<app>      installa\n"
        "  netupdate -remove:<app>       toglie\n");
}

static long proc(ExWindow f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_CLOSE:
        if (occupato() && !ex_dlg_conferma("ExPackage",
                "netupdate sta ancora lavorando: se chiudi, continua da solo\n"
                "ma qui non vedrai come finisce.", "Chiudi lo stesso", "Resta"))
            return 0;
        ex_quit(0);
        return 0;
    case EXM_TIMER:
        controlla_lavoro();
        return 0;
    case EXM_COMMAND:
        switch (wp) {
        case ID_LISTA:      info_mostra(); if (EX_IS_OPEN(lp)) segna(); break;
        case ID_SEGNA:      segna(); break;
        case ID_PULISCI:    pulisci(); break;
        case ID_APPLICA:    applica(); break;
        case ID_AGGIORNA:
            if (occupato()) { messaggio("netupdate sta ancora lavorando."); break; }
            g_n_coda = g_prossimo = 0;
            accoda(J_LISTA, 0, 0);
            bottoni();
            break;
        case ID_REPO:       repo_apri(); break;
        case ID_LOG:        mostra_log(); break;
        case ID_ISTRUZIONI: istruzioni(); break;
        case ID_INFO:       informazioni(); break;
        case ID_ESCI:       proc(f, EXM_CLOSE, 0, 0); return 0;
        default: break;
        }
        ex_update(g_f);
        return 0;
    }
    return ex_default_proc(f, msg, wp, lp);
}

static int trova_netupdate(void)
{
    static const char *dove[] = { "/bin/netupdate", "/cdrom/bin/netupdate", 0 };
    int i;

    for (i = 0; dove[i]; i++)
        if (access(dove[i], F_OK) == 0) { snprintf(g_netupdate, sizeof(g_netupdate), "%s", dove[i]); return 0; }
    return -1;
}

int main(void)
{
    ExMsg        m;
    ExWindow     mb;
    unsigned int sw = 0, sh = 0;
    int          y, fd;

    ex_screen_size(&sw, &sh);
    if (sw == 0) {
        printf("expackage: il server a finestre non risponde. Avvialo con:  exwin\n");
        printf("           Da riga di comando:  netupdate -install:list -p\n");
        return 1;
    }
    g_root = getuid() == 0;
    mkdir("/tmp", 0777);
    snprintf(g_file_log, sizeof(g_file_log), "/tmp/expackage-%d.log", getpid());
    snprintf(g_file_vuoto, sizeof(g_file_vuoto), "/tmp/expackage-%d.in", getpid());
    fd = open(g_file_vuoto, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) close(fd);

    g_f = ex_create("window", "ExPackage - applicazioni", EX_CAPTION | EX_BORDER | EX_CLOSEBOX,
                    ((int)sw - FIN_W) / 2, ((int)sh - FIN_H) / 2 > 0 ? ((int)sh - FIN_H) / 2 : 0,
                    FIN_W, FIN_H, 0, 0, proc);
    if (!g_f) return 1;
    mb = ex_menu_bar(g_f);
    ex_menu_add_item(mb, "File", "Aggiorna l'elenco", ID_AGGIORNA);
    ex_menu_add_item(mb, "File", "Applica", ID_APPLICA);
    ex_menu_add_item(mb, "File", "-", 0);
    ex_menu_add_item(mb, "File", "Esci", ID_ESCI);
    ex_menu_add_item(mb, "Repository", "Imposta e verifica...", ID_REPO);
    ex_menu_add_item(mb, "Visualizza", "Registro di netupdate", ID_LOG);
    ex_menu_add_item(mb, "Aiuto", "Istruzioni", ID_ISTRUZIONI);
    ex_menu_add_item(mb, "Aiuto", "Informazioni su ExPackage", ID_INFO);

    y = 28;
    g_repo_lbl = ex_create("label", "", EX_CHILD, 10, y, FIN_W - 20, 18, g_f, 0, 0);
    y += 22;
    ex_create("label", "    PACCHETTO    STATO          MB  COS'E'", EX_CHILD, 10, y, FIN_W - 20, 18, g_f, 0, 0);
    y += 20;
    g_lista = ex_create("lista", "", EX_CHILD, 10, y, FIN_W - 20, 250, g_f, ID_LISTA, 0);
    y += 256;
    g_info1 = ex_create("label", "", EX_CHILD, 10, y, FIN_W - 20, 18, g_f, 0, 0);
    y += 18;
    g_info2 = ex_create("label", "", EX_CHILD, 10, y, FIN_W - 20, 18, g_f, 0, 0);
    y += 24;
    ex_create("button", "Segna", EX_CHILD, 10, y, 100, 24, g_f, ID_SEGNA, 0);
    ex_create("button", "Togli i segni", EX_CHILD, 116, y, 120, 24, g_f, ID_PULISCI, 0);
    ex_create("button", "Aggiorna", EX_CHILD, 242, y, 100, 24, g_f, ID_AGGIORNA, 0);
    g_bt_applica = ex_create("button", "Applica", EX_CHILD, FIN_W - 110, y, 100, 24, g_f, ID_APPLICA, 0);
    y += 32;
    g_msg = ex_create("label", "", EX_CHILD, 10, y, FIN_W - 20, 18, g_f, 0, 0);

    repo_etichetta();
    ex_default_proc(g_f, EXM_PAINT, 0, 0);
    ex_update(g_f);

    if (trova_netupdate() != 0) {
        messaggio("netupdate non c'e' (/bin/netupdate): senza, qui non si puo' fare niente.");
        ex_enable(g_bt_applica, 0);
    } else {
        if (!g_root) messaggio("Si puo' guardare; per installare o togliere serve root.");
        accoda(J_LISTA, 0, 0);
        controlla_lavoro();
    }
    ex_set_timer(g_f, 400);

    while (ex_get_message(&m)) ex_dispatch(&m);
    if (g_pid <= 0) remove(g_file_log);
    remove(g_file_vuoto);
    return 0;
}
