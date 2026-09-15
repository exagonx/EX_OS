/* =============================================================================
 * bin/ftpswap/ftpswap.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ftpswap — tenere allineata una directory con un server FTP
 *
 *     ftpswap /miadir            allinea
 *     ftpswap /miadir -n         dice che cosa farebbe, e non tocca niente
 *     ftpswap /miadir -v         fa vedere il dialogo FTP
 *
 * Dentro /miadir ci vuole ftpswap.cfg. Se non c'e', lo si chiede e lo si
 * scrive: server, directory remota, utente, password, binario o testo, passiva
 * o attiva.
 *
 * -----------------------------------------------------------------------------
 * ! IL DIARIO E' IL PEZZO CHE FA LA DIFFERENZA FRA «NUOVO» E «CANCELLATO»
 *
 * Senza memoria, un file che c'e' da una parte sola e' sempre un file nuovo da
 * copiare, e cancellare qualcosa diventa impossibile: al giro dopo torna. Con
 * la memoria di com'era alla fine dell'ultimo giro, lo stesso fatto si legge
 * nei due modi giusti:
 *
 *     c'e' qui, non c'e' li', e NEL DIARIO NON C'E'  ->  e' nato qui: si copia
 *     c'e' qui, non c'e' li', e NEL DIARIO C'E'      ->  e' morto li': si cancella
 *
 * ! E IL DIARIO TIENE LE DATE DI TUTT'E DUE LE PARTI, non una sola. Non e' un
 * di piu': su EX-OS `utime()` NON CAMBIA NIENTE — nessun filesystem sa
 * riscrivere le date, sta scritto in libc.h. Quindi un file appena scaricato
 * ha la data di ADESSO, cioe' e' piu' recente di quello sul server da cui
 * viene, e un confronto fra i due orologi lo rimanderebbe su al giro dopo, e
 * giu' al giro dopo ancora, per sempre. Confrontando invece ogni parte CON SE
 * STESSA com'era nel diario, la domanda diventa «e' cambiato da allora?», che
 * ha una risposta sola e giusta.
 *
 * ! SUL SERVER LA DATA SI PUO' SCRIVERE, e si scrive: dopo aver caricato un
 * file gli si mette la data del locale con MFMT. Non serve all'algoritmo — il
 * diario basta — ma rende leggibile un elenco remoto a chi lo guarda.
 *
 * -----------------------------------------------------------------------------
 * ! DUE FILE NON SI CARICANO MAI: ftpswap.cfg E ftpswap.diario
 *
 * Il primo contiene la PASSWORD in chiaro: mandarlo sul server vorrebbe dire
 * pubblicare la chiave della porta che si sta aprendo. Il secondo e' lo stato
 * di QUESTA macchina: due macchine che allineano la stessa directory hanno due
 * diari diversi, e scambiarseli vuol dire raccontarsi cancellazioni che non
 * sono mai successe.
 * ============================================================================= */
#include "libc.h"
#include "exftp.h"

/* +0.001 a ogni modifica: `ftpswap -version` la stampa. Vedi EX_VERSIONE. */
EX_VERSIONE("ftpswap", "0.001");

#define PERC_MAX     256
#define NOME_MAX      96
#define RIGA_MAX     512
#define VOCI_MAX     256     /* voci remote per directory                     */

/* ! SOTTO_MAX COSTA PILA, E IL CONTO E' QUESTO: 64 x 96 byte = 6 KB per ogni
 * livello di profondita', perche' i nomi delle sottodirectory devono
 * sopravvivere fino a dopo la ricorsione. La pila di un programma EX-OS cresce
 * su richiesta fino a 256 KB (USER_STACK_MAX), quindi ci stanno una quarantina
 * di livelli — piu' di quanti ne abbia qualunque albero vero. L'elenco remoto,
 * che e' il pezzo grosso, sta nel mucchio e si libera PRIMA di ricorrere. */
#define SOTTO_MAX     64     /* sottodirectory per directory                  */
#define TOLLERANZA     2     /* secondi: FAT tiene le date a passi di due     */

#define CFG_NOME     "ftpswap.cfg"
#define DIARIO_NOME  "ftpswap.diario"

typedef struct {
    char         server[128];
    unsigned int porta;
    char         remoto[PERC_MAX];
    char         utente[64];
    char         password[64];
    int          binario;       /* 1 = binario (TYPE I), 0 = testo (TYPE A)   */
    int          passiva;       /* 1 = PASV, 0 = PORT                         */
} Cfg;

/* Una riga del diario: com'era questo file alla fine dell'ultimo giro. */
typedef struct Voce {
    struct Voce *poi;
    char         percorso[PERC_MAX];    /* relativo alla radice               */
    int          directory;
    long         dim_loc, data_loc;
    long         dim_rem, data_rem;
} Voce;

/* Una voce dell'elenco remoto di UNA directory. */
typedef struct {
    char nome[NOME_MAX];
    int  directory;
    long dim;
    long quando;
    int  visto;
} Rem;

static Cfg   g_cfg;
static ExFtp g_f;
static Voce *g_diario     = 0;     /* quello letto: si consuma               */
static Voce *g_nuovo      = 0;     /* quello che si scrivera'                */
static char  g_radice[PERC_MAX];
static int   g_prova      = 0;     /* -n: non tocca niente                   */
static int   g_verboso    = 0;

static long  g_su = 0, g_giu = 0, g_canc_loc = 0, g_canc_rem = 0, g_conflitti = 0;

/* -----------------------------------------------------------------------------
 * Percorsi
 * --------------------------------------------------------------------------- */
static void unisci(char *out, unsigned int max, const char *a, const char *b)
{
    unsigned int n;

    if (b == 0 || b[0] == '\0') {
        strncpy(out, a, max - 1);
        out[max - 1] = '\0';
        return;
    }
    n = (unsigned int)strlen(a);
    if (n > 0 && a[n - 1] == '/') snprintf(out, max, "%s%s", a, b);
    else                          snprintf(out, max, "%s/%s", a, b);
}

/* Il percorso locale di una cosa che nel giro si chiama `rel`. */
static void locale_di(char *out, unsigned int max, const char *rel)
{
    unisci(out, max, g_radice, rel);
}

/* Il percorso remoto: la radice remota del .cfg piu' il relativo. */
static void remoto_di(char *out, unsigned int max, const char *rel)
{
    unisci(out, max, g_cfg.remoto, rel);
}

/* -----------------------------------------------------------------------------
 * Il diario
 * --------------------------------------------------------------------------- */
static Voce *diario_cerca(const char *rel)
{
    Voce *v;

    for (v = g_diario; v != 0; v = v->poi)
        if (strcmp(v->percorso, rel) == 0) return v;
    return 0;
}

static void diario_segna(const char *rel, int directory,
                         long dim_loc, long data_loc,
                         long dim_rem, long data_rem)
{
    Voce *v = (Voce *)malloc(sizeof(Voce));

    if (v == 0) return;

    memset(v, 0, sizeof(*v));
    strncpy(v->percorso, rel, sizeof(v->percorso) - 1);
    v->directory = directory;
    v->dim_loc   = dim_loc;
    v->data_loc  = data_loc;
    v->dim_rem   = dim_rem;
    v->data_rem  = data_rem;
    v->poi       = g_nuovo;
    g_nuovo      = v;
}

static void diario_leggi(void)
{
    char  perc[PERC_MAX], riga[RIGA_MAX];
    FILE *f;

    locale_di(perc, sizeof(perc), DIARIO_NOME);
    f = fopen(perc, "r");
    if (f == 0) return;

    while (fgets(riga, sizeof(riga), f) != 0) {
        Voce *v;
        char *p = riga, *campo[6];
        int   n = 0;

        if (riga[0] == '#' || riga[0] == '\n') continue;

        campo[n++] = p;
        while (*p && n < 6) {
            if (*p == '\t') { *p = '\0'; campo[n++] = p + 1; }
            p++;
        }
        while (*p && *p != '\n') p++;
        *p = '\0';

        if (n < 2) continue;

        v = (Voce *)malloc(sizeof(Voce));
        if (v == 0) break;
        memset(v, 0, sizeof(*v));

        v->directory = (campo[0][0] == 'D');
        strncpy(v->percorso, campo[1], sizeof(v->percorso) - 1);

        if (!v->directory && n >= 6) {
            v->dim_loc  = (long)strtoul(campo[2], 0, 10);
            v->data_loc = (long)strtoul(campo[3], 0, 10);
            v->dim_rem  = (long)strtoul(campo[4], 0, 10);
            v->data_rem = (long)strtoul(campo[5], 0, 10);
        }

        v->poi    = g_diario;
        g_diario  = v;
    }
    fclose(f);
}

static void diario_scrivi(void)
{
    char  perc[PERC_MAX];
    FILE *f;
    Voce *v;

    if (g_prova) return;

    locale_di(perc, sizeof(perc), DIARIO_NOME);
    f = fopen(perc, "w");
    if (f == 0) {
        printf("! non riesco a scrivere %s: il prossimo giro non sapra' che\n",
               perc);
        printf("  cosa e' stato cancellato, e rimettera' tutto a posto come\n");
        printf("  se fosse nuovo. Non si perde niente, si perde la memoria.\n");
        return;
    }

    fprintf(f, "# ftpswap — com'erano le cose alla fine dell'ultimo giro.\n");
    fprintf(f, "# Non si scrive a mano: serve a distinguere un file NUOVO da\n");
    fprintf(f, "# uno CANCELLATO. Cancellando questo file non si perde niente,\n");
    fprintf(f, "# si perde la memoria: al giro dopo torna tutto quel che c'e'\n");
    fprintf(f, "# da una parte sola.\n");
    fprintf(f, "# D<tab>percorso     oppure\n");
    fprintf(f, "# F<tab>percorso<tab>dim_locale<tab>data_locale<tab>dim_remota<tab>data_remota\n");

    for (v = g_nuovo; v != 0; v = v->poi) {
        if (v->directory) fprintf(f, "D\t%s\n", v->percorso);
        else fprintf(f, "F\t%s\t%ld\t%ld\t%ld\t%ld\n", v->percorso,
                     v->dim_loc, v->data_loc, v->dim_rem, v->data_rem);
    }
    fclose(f);
}

/* -----------------------------------------------------------------------------
 * La configurazione
 * --------------------------------------------------------------------------- */
static void taglia(char *s)
{
    unsigned int n = (unsigned int)strlen(s);

    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' ||
                     s[n-1] == ' '  || s[n-1] == '\t')) s[--n] = '\0';
}

static int cfg_leggi(const char *perc)
{
    char  riga[RIGA_MAX];
    FILE *f = fopen(perc, "r");

    if (f == 0) return -1;

    g_cfg.porta   = 21;
    g_cfg.binario = 1;
    g_cfg.passiva = 1;

    while (fgets(riga, sizeof(riga), f) != 0) {
        char *uguale, *chiave = riga, *valore;

        if (riga[0] == '#') continue;
        uguale = strchr(riga, '=');
        if (uguale == 0) continue;

        *uguale = '\0';
        valore  = uguale + 1;
        taglia(chiave);
        while (*valore == ' ' || *valore == '\t') valore++;
        taglia(valore);

        if      (strcmp(chiave, "server")   == 0) strncpy(g_cfg.server, valore, sizeof(g_cfg.server) - 1);
        else if (strcmp(chiave, "porta")    == 0) g_cfg.porta = (unsigned int)atoi(valore);
        else if (strcmp(chiave, "remoto")   == 0) strncpy(g_cfg.remoto, valore, sizeof(g_cfg.remoto) - 1);
        else if (strcmp(chiave, "utente")   == 0) strncpy(g_cfg.utente, valore, sizeof(g_cfg.utente) - 1);
        else if (strcmp(chiave, "password") == 0) strncpy(g_cfg.password, valore, sizeof(g_cfg.password) - 1);
        else if (strcmp(chiave, "modo")     == 0) g_cfg.binario = (valore[0] != 'a' && valore[0] != 'A');
        else if (strcmp(chiave, "connessione") == 0) g_cfg.passiva = (valore[0] != 'a' && valore[0] != 'A');
    }
    fclose(f);

    return g_cfg.server[0] ? 0 : -1;
}

static void chiedi(const char *domanda, const char *pred, char *out,
                   unsigned int max)
{
    char riga[RIGA_MAX];
    int  n;

    if (pred && pred[0]) printf("  %s [%s]: ", domanda, pred);
    else                 printf("  %s: ", domanda);
    fflush(stdout);

    n = (int)read(0, riga, sizeof(riga) - 1);
    if (n <= 0) { riga[0] = '\0'; n = 0; }
    riga[n] = '\0';
    taglia(riga);

    if (riga[0] == '\0' && pred) strncpy(out, pred, max - 1);
    else                         strncpy(out, riga, max - 1);
    out[max - 1] = '\0';
}

/* =============================================================================
 * Il file di configurazione, chiesto e scritto
 *
 * ! LA PASSWORD FINISCE IN CHIARO DENTRO UN FILE, e va detto a chi lo sta
 * creando, non scritto nella documentazione che nessuno apre. FTP la manda in
 * chiaro sul cavo comunque — e' un protocollo nato prima che qualcuno
 * ascoltasse — quindi il file non aggiunge un pericolo nuovo; ma un file si
 * copia, si mette in un archivio, si pubblica per sbaglio. Per questo si
 * scrive a 0600 e per questo ftpswap non lo carica MAI sul server.
 * ========================================================================== */
static int cfg_crea(const char *perc)
{
    char  risposta[RIGA_MAX];
    FILE *f;

    printf("\n  %s non c'e': lo creo adesso.\n\n", perc);

    chiedi("server FTP (ftp.esempio.it)", "", g_cfg.server, sizeof(g_cfg.server));
    if (g_cfg.server[0] == '\0') {
        printf("  Senza server non si va da nessuna parte. Lascio stare.\n");
        return -1;
    }

    chiedi("porta", "21", risposta, sizeof(risposta));
    g_cfg.porta = (unsigned int)atoi(risposta);
    if (g_cfg.porta == 0) g_cfg.porta = 21;

    chiedi("directory sul server (/miadir)", "/", g_cfg.remoto, sizeof(g_cfg.remoto));
    chiedi("utente", "anonymous", g_cfg.utente, sizeof(g_cfg.utente));

    printf("  ! la password finisce IN CHIARO in questo file (e FTP la manda\n");
    printf("    in chiaro sul cavo comunque). Il file si scrive a 0600.\n");
    chiedi("password", "", g_cfg.password, sizeof(g_cfg.password));

    chiedi("modo: binario o ascii", "binario", risposta, sizeof(risposta));
    g_cfg.binario = (risposta[0] != 'a' && risposta[0] != 'A');

    chiedi("connessione: passiva o attiva", "passiva", risposta, sizeof(risposta));
    g_cfg.passiva = (risposta[0] != 'a' && risposta[0] != 'A');

    f = fopen(perc, "w");
    if (f == 0) {
        printf("  Non riesco a scrivere %s (%s).\n", perc, strerror(errno));
        return -1;
    }

    fprintf(f, "# ftpswap — con chi si allinea questa directory.\n");
    fprintf(f, "#\n");
    fprintf(f, "# ! LA PASSWORD E' IN CHIARO QUI DENTRO. Il file e' a 0600 e\n");
    fprintf(f, "# ftpswap non lo carica mai sul server: e' la chiave della\n");
    fprintf(f, "# porta che sta aprendo.\n");
    fprintf(f, "server      = %s\n", g_cfg.server);
    fprintf(f, "porta       = %u\n", g_cfg.porta);
    fprintf(f, "remoto      = %s\n", g_cfg.remoto);
    fprintf(f, "utente      = %s\n", g_cfg.utente);
    fprintf(f, "password    = %s\n", g_cfg.password);
    fprintf(f, "modo        = %s\n", g_cfg.binario ? "binario" : "ascii");
    fprintf(f, "connessione = %s\n", g_cfg.passiva ? "passiva" : "attiva");
    fclose(f);

    if (chmod(perc, 0600) != 0)
        printf("  ! non sono riuscito a metterlo a 0600: guardalo tu.\n");

    printf("\n  Scritto %s.\n\n", perc);
    return 0;
}

/* -----------------------------------------------------------------------------
 * L'elenco remoto di una directory
 * --------------------------------------------------------------------------- */
typedef struct {
    Rem *v;
    int  n;
} Raccolta;

static int raccogli(void *dato, const ExFtpVoce *e)
{
    Raccolta *r = (Raccolta *)dato;

    if (r->n >= VOCI_MAX) return 0;             /* pieno: si smette */

    memset(&r->v[r->n], 0, sizeof(Rem));
    strncpy(r->v[r->n].nome, e->nome, NOME_MAX - 1);
    r->v[r->n].directory = e->directory ? 1 : 0;
    r->v[r->n].dim       = e->dimensione;
    r->v[r->n].quando    = e->quando;
    r->n++;
    return 1;
}

static Rem *trova(Rem *v, int n, const char *nome)
{
    int i;

    for (i = 0; i < n; i++)
        if (strcmp(v[i].nome, nome) == 0) return &v[i];
    return 0;
}

/* -----------------------------------------------------------------------------
 * Le azioni
 * --------------------------------------------------------------------------- */
static void dillo(const char *verso, const char *rel, const char *perche)
{
    printf("  %-9s %s%s%s\n", verso, rel, perche ? "   " : "", perche ? perche : "");
}

static int carica(const char *rel, long *dim_rem, long *data_rem, long data_loc)
{
    char loc[PERC_MAX], rem[PERC_MAX];

    locale_di(loc, sizeof(loc), rel);
    remoto_di(rem, sizeof(rem), rel);

    if (g_prova) { *dim_rem = 0; *data_rem = 0; return 0; }

    if (exftp_carica(&g_f, loc, rem) != 0) {
        printf("  ! %s non si carica: %s\n", rel, g_f.risposta);
        return -1;
    }

    /* La data del locale anche sul server: non serve all'algoritmo — il diario
     * basta — ma rende leggibile l'elenco remoto a chi lo guarda. Se il server
     * non conosce MFMT non e' un guaio, si tiene la sua. */
    exftp_mfmt(&g_f, rem, data_loc);

    *dim_rem  = exftp_size(&g_f, rem);
    *data_rem = exftp_mdtm(&g_f, rem);
    g_su++;
    return 0;
}

static int scarica(const char *rel, long *dim_loc, long *data_loc)
{
    char        loc[PERC_MAX], rem[PERC_MAX];
    struct stat st;

    locale_di(loc, sizeof(loc), rel);
    remoto_di(rem, sizeof(rem), rel);

    if (g_prova) { *dim_loc = 0; *data_loc = 0; return 0; }

    if (exftp_scarica(&g_f, rem, loc) != 0) {
        printf("  ! %s non si scarica: %s\n", rel, g_f.risposta);
        return -1;
    }

    /* ! LA DATA DEL FILE APPENA SCRITTO E' ADESSO, e non c'e' modo di
     * cambiarla: utime() su EX-OS non fa niente. E' esattamente il motivo per
     * cui il diario tiene le date delle due parti separate. */
    if (stat(loc, &st) == 0) { *dim_loc = (long)st.st_size; *data_loc = (long)st.st_mtime; }
    else                     { *dim_loc = 0; *data_loc = 0; }
    g_giu++;
    return 0;
}

/* Mette da parte il perdente di un conflitto, in locale, come <nome>.prima. */
static int salva_prima(const char *rel)
{
    char loc[PERC_MAX], prima[PERC_MAX];

    locale_di(loc, sizeof(loc), rel);
    snprintf(prima, sizeof(prima), "%s.prima", loc);

    if (g_prova) return 0;

    remove(prima);
    if (rename(loc, prima) != 0) {
        printf("  ! non riesco a mettere da parte %s\n", rel);
        return -1;
    }
    return 0;
}

/* =============================================================================
 * Il confronto di un file che c'e' da tutt'e due le parti
 *
 * La domanda non e' «quale dei due e' piu' nuovo» — due orologi diversi non si
 * confrontano — ma «chi e' cambiato da quando li ho visti l'ultima volta».
 * ========================================================================== */
static void confronta(const char *rel, long dim_loc, long data_loc, Rem *r)
{
    Voce *d = diario_cerca(rel);
    int   l_cambiato, r_cambiato;
    long  ndim_loc = dim_loc, ndata_loc = data_loc;
    long  ndim_rem = r->dim, ndata_rem = r->quando;

    if (d == 0) {
        /* ! MAI VISTO PRIMA, E C'E' DA TUTT'E DUE LE PARTI: e' il primo giro
         * su una directory che esisteva gia' di la'. Non si sa chi e' piu'
         * nuovo davvero, quindi si guarda quel che si ha: dimensione uguale
         * vuol dire quasi sempre lo stesso file, e non si tocca niente. */
        if (dim_loc == r->dim) {
            diario_segna(rel, 0, dim_loc, data_loc, r->dim, r->quando);
            return;
        }
        /* Diversi e senza storia: vince il piu' recente, e l'altro si salva. */
        g_conflitti++;
        if (r->quando > data_loc) {
            dillo("PRIMO-CONFLITTO", rel, "(prendo il remoto, salvo il locale)");
            if (salva_prima(rel) == 0 && scarica(rel, &ndim_loc, &ndata_loc) != 0) return;
        } else {
            dillo("PRIMO-CONFLITTO", rel, "(tengo il locale)");
            if (carica(rel, &ndim_rem, &ndata_rem, data_loc) != 0) return;
        }
        diario_segna(rel, 0, ndim_loc, ndata_loc, ndim_rem, ndata_rem);
        return;
    }

    l_cambiato = (dim_loc != d->dim_loc) || (data_loc != d->data_loc);
    r_cambiato = (r->dim != d->dim_rem) ||
                 (r->quando >= 0 && d->data_rem >= 0 &&
                  (r->quando - d->data_rem >  TOLLERANZA ||
                   d->data_rem - r->quando >  TOLLERANZA));

    if (!l_cambiato && !r_cambiato) {
        diario_segna(rel, 0, dim_loc, data_loc, r->dim, r->quando);
        return;
    }

    if (l_cambiato && !r_cambiato) {
        dillo("SU", rel, 0);
        if (carica(rel, &ndim_rem, &ndata_rem, data_loc) != 0) return;
        diario_segna(rel, 0, dim_loc, data_loc, ndim_rem, ndata_rem);
        return;
    }

    if (!l_cambiato && r_cambiato) {
        dillo("GIU", rel, 0);
        if (scarica(rel, &ndim_loc, &ndata_loc) != 0) return;
        diario_segna(rel, 0, ndim_loc, ndata_loc, r->dim, r->quando);
        return;
    }

    /* =====================================================================
     * ! CAMBIATI TUTT'E DUE: VINCE IL PIU' RECENTE, MA L'ALTRO NON SI PERDE.
     * Qui i due orologi vanno confrontati per forza, ed e' l'unico posto dove
     * si fa: e' una scelta, non una misura. Percio' il perdente si mette da
     * parte come <nome>.prima invece di finire sotto il vincitore — se la
     * scelta era sbagliata, il file e' ancora li'.
     * ===================================================================== */
    g_conflitti++;
    if (r->quando > data_loc) {
        dillo("CONFLITTO", rel, "(vince il remoto, il locale in .prima)");
        if (salva_prima(rel) != 0) return;
        if (scarica(rel, &ndim_loc, &ndata_loc) != 0) return;
        diario_segna(rel, 0, ndim_loc, ndata_loc, r->dim, r->quando);
    } else {
        dillo("CONFLITTO", rel, "(vince il locale, il remoto in .prima)");
        {
            char rem[PERC_MAX], loc_prima[PERC_MAX], rel_prima[PERC_MAX];

            snprintf(rel_prima, sizeof(rel_prima), "%s.prima", rel);
            remoto_di(rem, sizeof(rem), rel);
            locale_di(loc_prima, sizeof(loc_prima), rel_prima);
            if (!g_prova) exftp_scarica(&g_f, rem, loc_prima);
        }
        if (carica(rel, &ndim_rem, &ndata_rem, data_loc) != 0) return;
        diario_segna(rel, 0, dim_loc, data_loc, ndim_rem, ndata_rem);
    }
}

/* -----------------------------------------------------------------------------
 * Il giro, una directory per volta
 * --------------------------------------------------------------------------- */
static void allinea(const char *rel);

static void nome_rel(char *out, unsigned int max, const char *rel, const char *nome)
{
    if (rel[0] == '\0') { strncpy(out, nome, max - 1); out[max - 1] = '\0'; }
    else                snprintf(out, max, "%s/%s", rel, nome);
}

/* Da saltare: i due file di ftpswap, e le copie messe da parte. */
static int da_saltare(const char *rel, const char *nome)
{
    unsigned int n;

    if (strcmp(nome, ".") == 0 || strcmp(nome, "..") == 0) return 1;

    if (rel[0] == '\0' &&
        (strcmp(nome, CFG_NOME) == 0 || strcmp(nome, DIARIO_NOME) == 0)) return 1;

    /* ! LE COPIE .prima NON SI CARICANO. Sono la rete di sicurezza di un
     * conflitto, e mandarle sul server vorrebbe dire creare un file nuovo la'
     * per ogni conflitto qui — che al giro dopo tornerebbe giu' da tutte le
     * altre macchine. Restano dove servono: sotto gli occhi di chi deve
     * guardarle. */
    n = (unsigned int)strlen(nome);
    if (n > 6 && strcmp(nome + n - 6, ".prima") == 0) return 1;

    return 0;
}

static void allinea(const char *rel)
{
    char      loc[PERC_MAX];
    char      sotto[SOTTO_MAX][NOME_MAX];
    int       n_sotto = 0;
    Rem      *rem;
    Raccolta  rc;
    DIR      *d;
    struct dirent *e;
    int       i;

    rem = (Rem *)malloc(sizeof(Rem) * VOCI_MAX);
    if (rem == 0) { printf("! memoria finita\n"); return; }

    rc.v = rem;
    rc.n = 0;

    {
        char r[PERC_MAX];

        remoto_di(r, sizeof(r), rel);
        if (exftp_elenco(&g_f, r, raccogli, &rc) < 0) {
            /* La directory remota non c'e': la si crea e si va avanti con
             * l'elenco vuoto. */
            if (!g_prova) exftp_mkd(&g_f, r);
            rc.n = 0;
        }
    }

    /* --- 1. quel che c'e' qui ------------------------------------------- */
    locale_di(loc, sizeof(loc), rel);
    d = opendir(loc);
    if (d != 0) {
        while ((e = readdir(d)) != 0) {
            char        figlio[PERC_MAX], perc[PERC_MAX];
            struct stat st;
            Rem        *r;

            if (da_saltare(rel, e->d_name)) continue;

            nome_rel(figlio, sizeof(figlio), rel, e->d_name);
            locale_di(perc, sizeof(perc), figlio);

            if (stat(perc, &st) != 0) continue;

            r = trova(rem, rc.n, e->d_name);
            if (r) r->visto = 1;

            if (S_ISDIR(st.st_mode)) {
                if (r == 0) {
                    char rr[PERC_MAX];

                    remoto_di(rr, sizeof(rr), figlio);
                    dillo("MKDIR-SU", figlio, 0);
                    if (!g_prova) exftp_mkd(&g_f, rr);
                }
                diario_segna(figlio, 1, 0, 0, 0, 0);
                if (n_sotto < SOTTO_MAX) {
                    strncpy(sotto[n_sotto], e->d_name, NOME_MAX - 1);
                    sotto[n_sotto][NOME_MAX - 1] = '\0';
                    n_sotto++;
                }
                continue;
            }

            if (r == 0) {
                Voce *dd = diario_cerca(figlio);

                if (dd != 0) {
                    /* C'era, e sul server non c'e' piu': l'hanno cancellato la'. */
                    dillo("CANCELLO-QUI", figlio, 0);
                    if (!g_prova) remove(perc);
                    g_canc_loc++;
                } else {
                    long dr = 0, qr = 0;

                    dillo("SU", figlio, "(nuovo)");
                    if (carica(figlio, &dr, &qr, (long)st.st_mtime) == 0)
                        diario_segna(figlio, 0, (long)st.st_size,
                                     (long)st.st_mtime, dr, qr);
                }
                continue;
            }

            confronta(figlio, (long)st.st_size, (long)st.st_mtime, r);
        }
        closedir(d);
    } else if (rel[0] != '\0') {
        printf("  ! non riesco ad aprire %s\n", loc);
    }

    /* --- 2. quel che c'e' solo di la' ----------------------------------- */
    for (i = 0; i < rc.n; i++) {
        char  figlio[PERC_MAX], perc[PERC_MAX];
        Voce *dd;

        if (rem[i].visto) continue;
        if (da_saltare(rel, rem[i].nome)) continue;

        nome_rel(figlio, sizeof(figlio), rel, rem[i].nome);
        locale_di(perc, sizeof(perc), figlio);
        dd = diario_cerca(figlio);

        if (rem[i].directory) {
            if (dd != 0) {
                /* Era nel diario e qui non c'e' piu': cancellata qui, si
                 * cancella la'. Se dentro c'e' ancora roba il server dice di
                 * no, ed e' giusto: si vedra' al giro dopo, a directory
                 * svuotata. */
                char rr[PERC_MAX];

                remoto_di(rr, sizeof(rr), figlio);
                dillo("RMDIR-LI", figlio, 0);
                if (!g_prova) exftp_rmd(&g_f, rr);
                g_canc_rem++;
                continue;
            }
            dillo("MKDIR-QUI", figlio, 0);
            if (!g_prova) mkdir(perc, 0755);
            diario_segna(figlio, 1, 0, 0, 0, 0);
            if (n_sotto < SOTTO_MAX) {
                strncpy(sotto[n_sotto], rem[i].nome, NOME_MAX - 1);
                sotto[n_sotto][NOME_MAX - 1] = '\0';
                n_sotto++;
            }
            continue;
        }

        if (dd != 0) {
            char rr[PERC_MAX];

            remoto_di(rr, sizeof(rr), figlio);
            dillo("CANCELLO-LI", figlio, 0);
            if (!g_prova) exftp_dele(&g_f, rr);
            g_canc_rem++;
            continue;
        }

        {
            long dl = 0, ql = 0;

            dillo("GIU", figlio, "(nuovo)");
            if (scarica(figlio, &dl, &ql) == 0)
                diario_segna(figlio, 0, dl, ql, rem[i].dim, rem[i].quando);
        }
    }

    free(rem);

    /* --- 3. dentro le sottodirectory ------------------------------------ */
    for (i = 0; i < n_sotto; i++) {
        char figlio[PERC_MAX];

        nome_rel(figlio, sizeof(figlio), rel, sotto[i]);
        allinea(figlio);
    }
}

/* -----------------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------------- */
static void uso(void)
{
    printf("ftpswap - tiene allineata una directory con un server FTP\n\n");
    printf("  ftpswap /miadir        allinea\n");
    printf("  ftpswap /miadir -n     dice che cosa farebbe, senza toccare\n");
    printf("  ftpswap /miadir -v     fa vedere il dialogo FTP\n\n");
    printf("Dentro /miadir ci vuole %s: se non c'e' lo chiedo e lo scrivo.\n",
           CFG_NOME);
    printf("Tiene un %s con com'erano le cose l'ultima volta: e' quello\n",
           DIARIO_NOME);
    printf("che distingue un file NUOVO da uno CANCELLATO.\n\n");
    printf("Conflitto (cambiato di qua e di la'): vince il piu' recente e\n");
    printf("l'altro si mette da parte come <nome>.prima, qui in locale.\n");
}

int main(int argc, char **argv)
{
    char perc[PERC_MAX];
    int  i, rc;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0) { g_prova = 1; continue; }
        if (strcmp(argv[i], "-v") == 0) { g_verboso = 1; continue; }
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            uso();
            return 0;
        }
        if (g_radice[0] == '\0') {
            strncpy(g_radice, argv[i], sizeof(g_radice) - 1);
            continue;
        }
    }

    if (g_radice[0] == '\0') { uso(); return 1; }

    {
        struct stat st;

        if (stat(g_radice, &st) != 0 || !S_ISDIR(st.st_mode)) {
            printf("ftpswap: %s non e' una directory.\n", g_radice);
            return 1;
        }
    }

    locale_di(perc, sizeof(perc), CFG_NOME);
    if (cfg_leggi(perc) != 0 && cfg_crea(perc) != 0) return 1;

    printf("ftpswap: %s  <->  %s@%s%s\n", g_radice,
           g_cfg.utente[0] ? g_cfg.utente : "anonymous",
           g_cfg.server, g_cfg.remoto);
    if (g_prova) printf("         (prova: non tocco niente)\n");

    g_f.verboso = g_verboso;

    rc = exftp_apri(&g_f, g_cfg.server, g_cfg.porta, g_cfg.utente, g_cfg.password);
    if (rc != 0) {
        printf("ftpswap: non entro nel server (%d)%s%s\n", rc,
               g_f.risposta[0] ? ": " : "", g_f.risposta);
        return 1;
    }

    exftp_binario(&g_f, g_cfg.binario);
    exftp_passiva(&g_f, g_cfg.passiva);

    /* ! LA DIRECTORY REMOTA DEVE ESISTERE, e se non c'e' la si crea: chi
     * allinea una directory nuova non deve andare a farsela a mano con un
     * altro programma. */
    if (!g_prova && g_cfg.remoto[0] && strcmp(g_cfg.remoto, "/") != 0)
        exftp_mkd(&g_f, g_cfg.remoto);

    diario_leggi();
    allinea("");
    diario_scrivi();

    exftp_chiudi(&g_f);

    printf("\n  su %ld, giu' %ld, cancellati qui %ld, cancellati la' %ld",
           g_su, g_giu, g_canc_loc, g_canc_rem);
    if (g_conflitti) printf(", conflitti %ld (guarda i .prima)", g_conflitti);
    printf(".\n");

    return 0;
}
