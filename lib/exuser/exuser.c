/* =============================================================================
 * lib/exuser/exuser.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il perche' sta in exuser.h. Questo codice viene da bin/login/login.c, dove ha
 * autenticato da solo per settimane: e' stato SPOSTATO, non riscritto, perche'
 * riscrivere un archivio di password vuol dire rifare gli stessi errori con
 * numeri diversi. Le uniche modifiche sono la `radice` — il prefisso dei due
 * file, che serve a `install` per scrivere sul disco che sta preparando — e i
 * nomi pubblici.
 * ============================================================================= */

#include "libc.h"
#include "kbd_proto.h"
#include "exuser.h"

#define NOME_MAX   EXUSER_NOME_MAX
#define PASS_MAX   EXUSER_PASS_MAX
#define RIGA_MAX   192
#define FILE_MAX   8192

/* ! LA RADICE E' UN PREFISSO, NON UN PUNTO DI MONTAGGIO: passando 0 o "" si
 * ottengono i percorsi assoluti di sempre, cioe' il sistema in esecuzione.
 * `install` passa il punto in cui ha montato il disco. */
static const char *perc(const char *radice, const char *nome,
                        char *out, unsigned int max)
{
    unsigned int i = 0, j;

    if (radice && radice[0]) {
        for (j = 0; radice[j] && i < max - 1; j++) out[i++] = radice[j];
        while (i > 1 && out[i - 1] == '/') i--;
    }
    for (j = 0; "/boot/"[j] && i < max - 1; j++) out[i++] = "/boot/"[j];
    for (j = 0; nome[j] && i < max - 1; j++) out[i++] = nome[j];
    out[i] = '\0';
    return out;
}

static void prendi_console(void)
{
    console_setfg((unsigned)getpid());
}

static int   g_kbd_pid = -1;


/* ─────────────────────────────────────────────────────────────────────────────
 * Lettura della password: modalità raw, eco a nostro carico
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * ! IN COOKED IL DRIVER FA L'ECO LUI, e una password battuta comparirebbe
 * a schermo mentre la si scrive. In raw non fa eco: i tasti arrivano uno
 * per uno e siamo noi a decidere cosa mostrare — un asterisco per
 * carattere, che dice quanto si e' scritto senza dire cosa.
 * ───────────────────────────────────────────────────────────────────────────── */

static int kbd_trova(void)
{
    if (g_kbd_pid < 0) {
        int p = ipc_lookup(KBD_SERVICE_NAME);
        g_kbd_pid = (p > 0) ? p : 0;
    }
    return g_kbd_pid;
}

static void kbd_modo(unsigned int modo)
{
    KbdSetMode  m;
    ConsoleInfo ci;

    if (kbd_trova() <= 0) return;
    m.modo    = modo;
    m.console = (console_info(&ci) == 0) ? ci.mia : 0;
    ipc_send(g_kbd_pid, KBD_MSG_SETMODE, &m, sizeof(m));
}

/* Il prossimo tasto, 0 se il servizio non risponde piu'. Riafferma la
 * modalita' a ogni scadenza: un programma appena terminato puo' aver
 * riportato il driver in cooked, e senza questo giro si resterebbe fermi
 * per sempre. Stessa ragione scritta in bin/sh/shell.c. */
static unsigned int kbd_tasto(void)
{
    IpcMessage    meta;
    unsigned char buf[64];
    ConsoleInfo   ci;
    unsigned int  console = 0;
    int           i;

    if (kbd_trova() <= 0) return 0;
    if (console_info(&ci) == 0) console = ci.mia;

    for (;;) {
        if (ipc_send(g_kbd_pid, KBD_MSG_READKEY, &console, sizeof(console)) < 0)
            return 0;
        for (i = 0; i < 8; i++) {
            if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) break;
            if ((int)meta.sender_pid != g_kbd_pid) continue;
            if (meta.tipo == KBD_MSG_KEY && meta.len >= sizeof(unsigned int)) {
                unsigned int k;
                memcpy(&k, buf, sizeof(k));
                return k;
            }
        }
        kbd_modo(KBD_MODE_RAW);       /* riafferma e riprova */
    }
}

/* -----------------------------------------------------------------------------
 * ! DENTRO UNO PSEUDO-TERMINALE LA TASTIERA NON C'ENTRA, e questa e' la strada
 * che permette a login di funzionare su una sessione remota. Il driver kbd
 * serve la tastiera FISICA di una console: chiedergli il modo raw da dentro un
 * telnet vorrebbe dire spegnere l'eco a chi sta seduto davanti alla macchina,
 * e non a chi sta battendo la password dall'altra parte del cavo.
 *
 * Su un pty l'eco la fa la disciplina di linea, e si spegne dicendolo a lei.
 * Rende 1 se stdin e' un pty (e allora il modo e' stato impostato), 0 se no.
 * --------------------------------------------------------------------------- */
static int pty_modo(unsigned int modo)
{
    return pty_ctl(0, PTY_CTL_MODO, modo) == 0;
}

static int pty_c_e(void)
{
    return pty_ctl(0, PTY_CTL_LEGGI_MISURA, 0) >= 0;
}

/* La password su un pty: modo grezzo e senza eco, un byte per volta, e
 * l'asterisco lo stampiamo noi — esattamente come si fa con la tastiera. */
static int leggi_password_pty(char *dst, int max)
{
    int len = 0;

    if (!pty_modo(0)) return -1;        /* grezzo, senza eco */

    for (;;) {
        unsigned char c;
        int n = (int)read(0, &c, 1);

        if (n <= 0) { pty_modo(PTY_CANONICO | PTY_ECO); return -1; }

        if (c == '\n' || c == '\r') {
            printf("\n");
            dst[len] = '\0';
            pty_modo(PTY_CANONICO | PTY_ECO);
            return len;
        }
        if (c == '\b' || c == 127u) {
            if (len > 0) { len--; printf("\b \b"); }
            continue;
        }
        if (c >= 32u && c < 127u && len < max - 1) {
            dst[len++] = (char)c;
            printf("*");
        }
    }
}

/* Rende la lunghezza, o -1 se non c'e' modo di leggerla senza mostrarla. */
static int leggi_password(char *dst, int max)
{
    int len = 0;

    if (pty_c_e()) return leggi_password_pty(dst, max);

    kbd_modo(KBD_MODE_RAW);
    if (g_kbd_pid <= 0) return -1;

    for (;;) {
        unsigned int ev = kbd_tasto();
        unsigned int k  = ev & KBD_KEY_MASK;

        if (ev == 0) { kbd_modo(KBD_MODE_COOKED); return -1; }

        if (k == '\n' || k == '\r') {
            printf("\n");
            dst[len] = '\0';
            kbd_modo(KBD_MODE_COOKED);
            return len;
        }
        if (k == '\b' || k == 127u) {
            /* Si cancella anche l'asterisco: lasciarlo direbbe una
             * lunghezza che non e' quella della password. */
            if (len > 0) { len--; printf("\b \b"); }
            continue;
        }
        if (k >= 32u && k < 127u && len < max - 1) {
            dst[len++] = (char)k;
            printf("*");
        }
    }
}

static int leggi_riga(char *dst, int max)
{
    int n = (int)read(0, dst, (unsigned int)(max - 1));

    if (n <= 0) return -1;
    dst[n] = '\0';
    while (n > 0 && (dst[n-1] == '\n' || dst[n-1] == '\r' || dst[n-1] == ' '))
        dst[--n] = '\0';
    return n;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Il file degli utenti
 * ───────────────────────────────────────────────────────────────────────────── */

static void impronta_di(const char *sale, const char *pass, char out[65])
{
    char misto[NOME_MAX + PASS_MAX + 2];
    int  i = 0, j;

    for (j = 0; sale[j] && i < (int)sizeof(misto) - 2; j++) misto[i++] = sale[j];
    misto[i++] = ':';
    for (j = 0; pass[j] && i < (int)sizeof(misto) - 1; j++) misto[i++] = pass[j];
    misto[i] = '\0';

    sha256_esa(misto, (size_t)i, out);
}

/* Un sale nuovo.
 *
 * ! NON E' CASUALE PER DAVVERO, e va detto. EX-OS non espone ancora una
 * sorgente di entropia allo spazio utente (il kernel ne ha una, vedi
 * arch/x86/entropia.c). Qui si mescolano l'orologio, i millisecondi
 * dall'avvio e il PID: basta a rendere diversi due sali generati sulla
 * stessa macchina, che e' cio' che serve contro le tabelle precalcolate,
 * ma un sale prevedibile non e' un sale forte. Il giorno che una syscall
 * di entropia ci sara', questa funzione e' l'unico posto da cambiare. */
static void sale_nuovo(char out[17])
{
    char semi[64];
    char imp[65];
    int  i;

    snprintf(semi, sizeof(semi), "%ld:%u:%u",
             (long)time(NULL), (unsigned)uptime_ms(), (unsigned)getpid());
    sha256_esa(semi, strlen(semi), imp);
    for (i = 0; i < 16; i++) out[i] = imp[i];
    out[16] = '\0';
}

/* Cerca `nome` e ne rende sale e impronta. 0 se trovato. */
/* Cerca `nome` in un file a due punti e rende i pezzi che seguono. `campi` e'
 * quanti due punti aspettarsi. Rende 0 se trovato.
 *
 * ! UNA FUNZIONE SOLA PER TUTT'E DUE I FILE. Hanno la stessa forma — nome,
 * poi campi separati da due punti — e scriverne due copie vorrebbe dire che
 * al primo cambiamento di formato una delle due resta indietro. E' successo
 * gia' una volta oggi, con SpawnExtra. */
static int cerca_riga(const char *file, const char *nome, int campi,
                      char *a, unsigned int amax, char *b, unsigned int bmax)
{
    static char testo[FILE_MAX];
    int  fd, n, i = 0;

    fd = open(file, O_RDONLY);
    if (fd < 0) return -1;
    n = (int)read(fd, testo, sizeof(testo) - 1);
    close(fd);
    if (n < 0) n = 0;
    testo[n] = '\0';

    while (i < n) {
        char riga[RIGA_MAX];
        int  l = 0, k, c[4], nc = 0;

        while (i < n && testo[i] != '\n' && l < RIGA_MAX - 1) riga[l++] = testo[i++];
        riga[l] = '\0';
        if (i < n && testo[i] == '\n') i++;
        if (riga[0] == '\0' || riga[0] == '#') continue;

        for (k = 0; riga[k]; k++)
            if (riga[k] == ':' && nc < 4) c[nc++] = k;
        if (nc < campi) continue;       /* riga malformata: si salta */

        riga[c[0]] = '\0';
        if (strcmp(riga, nome) != 0) continue;

        riga[c[1]] = '\0';
        if (a) { strncpy(a, riga + c[0] + 1, amax - 1); a[amax - 1] = '\0'; }
        if (b) { strncpy(b, riga + c[1] + 1, bmax - 1); b[bmax - 1] = '\0'; }
        return 0;
    }
    return -1;
}

/* L'identita' pubblica: uid e gid. La legge chiunque, e serve a `id`. */
static int trova_utente(const char *radice, const char *nome, char *sale,
                        char *imp, unsigned int *uid, unsigned int *gid)
{
    char su[16], sg[16], pa[96], pb[96];

    if (cerca_riga(perc(radice, "utenti", pa, sizeof(pa)), nome, 2, su, sizeof(su), sg, sizeof(sg)) != 0)
        return -1;

    if (uid) *uid = (unsigned int)atoi(su);
    if (gid) *gid = (unsigned int)atoi(sg);

    /* ! LE IMPRONTE STANNO NELL'ALTRO FILE, e chi non e' root non le legge:
     * cerca_riga rende -1, e chi ha chiamato non riesce ad autenticare. E'
     * giusto — solo login, che gira da root, deve poterlo fare. */
    if (sale && imp)
        return cerca_riga(perc(radice, "ombra",  pb, sizeof(pb)), nome, 2, sale, 17, imp, 65);

    return 0;
}

static int c_e_qualche_utente(const char *radice)
{
    struct stat st;
    char pa[96];
    return (stat(perc(radice, "utenti", pa, sizeof(pa)), &st) == 0 && st.st_size > 0);
}

/* Il primo uid libero da 1000 in su. Legge il file e prende il massimo + 1:
 * riusare un numero gia' speso vorrebbe dire che i file del vecchio
 * proprietario diventano di quello nuovo, in silenzio. */
static unsigned int prossimo_uid(const char *radice)
{
    static char testo[FILE_MAX];
    char pa[96];
    unsigned int massimo = 999;
    int fd, n, i = 0;

    fd = open(perc(radice, "utenti", pa, sizeof(pa)), O_RDONLY);
    if (fd < 0) return 1000;
    n = (int)read(fd, testo, sizeof(testo) - 1);
    close(fd);
    if (n < 0) n = 0;
    testo[n] = '\0';

    while (i < n) {
        int p = i, due = -1;

        while (i < n && testo[i] != '\n') {
            if (testo[i] == ':' && due < 0) due = i;
            i++;
        }
        if (i < n) i++;
        if (due > p) {
            unsigned int u = (unsigned int)atoi(testo + due + 1);
            if (u > massimo && u < 60000u) massimo = u;
        }
    }
    return massimo + 1;
}

/* Aggiunge una riga al file. Rende 0 se ci e' riuscito. */
/* Aggiunge l'utente ai DUE file. Rende 0 se ci e' riuscito.
 *
 * ! IN CODA E NON RISCRIVENDO: i file possono gia' avere altri utenti, e
 * riscriverli per intero vorrebbe dire poterli perdere tutti per un errore di
 * scrittura su uno solo. */
static int scrivi_riga(const char *file, const char *riga, unsigned int modo)
{
    int fd, l = (int)strlen(riga), scritti = 0, w;

    fd = open(file, O_WRONLY | O_CREAT | O_APPEND);
    if (fd < 0) {
        printf("utenti: %s: %s\n", file, strerror(errno));
        return -1;
    }
    chmod(file, modo);

    while (scritti < l) {
        w = (int)write(fd, riga + scritti, (unsigned int)(l - scritti));
        if (w <= 0) { close(fd); return -1; }
        scritti += w;
    }
    close(fd);
    return 0;
}

static int aggiungi_utente(const char *radice, const char *nome,
                           const char *pass, unsigned int uid,
                           unsigned int gid)
{
    char sale[17], imp[65], riga[RIGA_MAX], pa[96], pb[96];

    /* =========================================================================
     * ! UN NOME GIA' PRESO SI RIFIUTA, E IL CONTROLLO STA QUI DENTRO
     *
     * Le due scritture qui sotto AGGIUNGONO una riga in fondo: senza questo
     * controllo, `login -a` su un nome che esiste gia' ne scriveva una seconda
     * in tutt'e due i file, e diceva «Utente 'mario' creato».
     *
     * ! E LA RICERCA PRENDE LA PRIMA RIGA CHE COMBACIA (cerca_riga, qui
     * sopra), quindi vinceva quella VECCHIA: la password nuova non funzionava,
     * la vecchia si', e l'uid restava quello di prima. Cioe' il programma
     * diceva di aver fatto una cosa e ne aveva fatta un'altra — che e' peggio
     * di un rifiuto, perche' chi legge non ha motivo di controllare.
     *
     * ! IL CONTROLLO E' QUI E NON IN CHI CHIAMA perche' i chiamanti sono tre —
     * login al primo avvio, `login -a`, l'installatore — e un controllo
     * ripetuto tre volte e' un controllo che prima o poi ne ha due. Il file lo
     * protegge il codice che lo scrive.
     *
     * ! CAMBIARE UNA PASSWORD NON E' QUESTO: vuol dire riscrivere una riga in
     * mezzo a un file, cioe' rifarlo per intero. Il giorno che serve, e'
     * un'altra funzione — e questa le stara' accanto senza contraddirla.
     * ========================================================================= */
    if (trova_utente(radice, nome, 0, 0, 0, 0) == 0) return -2;

    sale_nuovo(sale);
    impronta_di(sale, pass, imp);

    /* ! PRIMA L'OMBRA, POI I NOMI, e l'ordine conta: se la seconda scrittura
     * fallisce resta un'impronta senza nome, che non fa entrare nessuno.
     * All'incontrario resterebbe un NOME SENZA IMPRONTA — un conto che
     * esiste e non ha password, cioe' il difetto peggiore possibile. */
    snprintf(riga, sizeof(riga), "%s:%s:%s\n", nome, sale, imp);
    if (scrivi_riga(perc(radice, "ombra",  pb, sizeof(pb)), riga, 0600) != 0) return -1;

    snprintf(riga, sizeof(riga), "%s:%u:%u\n", nome, uid, gid);
    return scrivi_riga(perc(radice, "utenti", pa, sizeof(pa)), riga, 0644);
}


/* Nomi accettabili: lettere, cifre, '_'. Niente ':' — separa i campi — e
 * niente spazi, che renderebbero ambigua la riga. */
static int nome_valido(const char *n)
{
    int i;

    if (n[0] == '\0') return 0;
    for (i = 0; n[i]; i++) {
        char c = n[i];
        if (c >= 'a' && c <= 'z') continue;
        if (c >= 'A' && c <= 'Z') continue;
        if (c >= '0' && c <= '9') continue;
        if (c == '_') continue;
        return 0;
    }
    return (i < NOME_MAX);
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Primo avvio
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * ! NON SI PUO' RESTARE CHIUSI FUORI, e non si puo' nemmeno restare senza
 * password per distrazione. Senza file utenti il sistema e' nuovo: si entra
 * senza chiedere niente — chiedere una password che nessuno ha ancora
 * scelto renderebbe la macchina inutilizzabile — e la PRIMA cosa che si fa
 * e' crearne uno. Finche' non e' creato non si va avanti.
 * ───────────────────────────────────────────────────────────────────────────── */

/* -----------------------------------------------------------------------------
 * Cio' che si vede da fuori
 * --------------------------------------------------------------------------- */
void exuser_prendi_console(void) { prendi_console(); }

int exuser_leggi_password(char *dst, int max) { return leggi_password(dst, max); }

int exuser_leggi_riga(char *dst, int max) { return leggi_riga(dst, max); }

int exuser_nome_valido(const char *n) { return nome_valido(n); }

int exuser_c_e_qualcuno(const char *radice) { return c_e_qualche_utente(radice); }

unsigned int exuser_prossimo_uid(const char *radice)
{
    return prossimo_uid(radice);
}

int exuser_aggiungi(const char *radice, const char *nome, const char *pass,
                    unsigned int uid, unsigned int gid)
{
    return aggiungi_utente(radice, nome, pass, uid, gid);
}

int exuser_e_amministratore(const char *radice, const char *nome)
{
    static char testo[2048];
    char pa[96];
    int  fd, n, i = 0;

    fd = open(perc(radice, "amministratori", pa, sizeof(pa)), O_RDONLY);
    if (fd < 0) return 0;
    n = (int)read(fd, testo, sizeof(testo) - 1);
    close(fd);
    if (n < 0) n = 0;
    testo[n] = '\0';

    while (i < n) {
        int p = i, l, k;

        while (i < n && testo[i] != '\n') i++;
        l = i - p;
        if (i < n) i++;
        while (l > 0 && (testo[p+l-1] == '\r' || testo[p+l-1] == ' ')) l--;
        if (l <= 0 || testo[p] == '#') continue;

        for (k = 0; k < l && nome[k]; k++)
            if (testo[p+k] != nome[k]) break;
        if (k == l && nome[k] == '\0') return 1;
    }
    return 0;
}

int exuser_amministratore_aggiungi(const char *radice, const char *nome)
{
    char pa[96], riga[EXUSER_NOME_MAX + 2];

    if (!exuser_nome_valido(nome)) return -1;
    if (exuser_e_amministratore(radice, nome)) return 0;   /* c'e' gia' */

    snprintf(riga, sizeof(riga), "%s\n", nome);
    return scrivi_riga(perc(radice, "amministratori", pa, sizeof(pa)),
                       riga, 0644);
}

int exuser_verifica(const char *radice, const char *nome, const char *pass,
                    unsigned int *uid, unsigned int *gid)
{
    char sale[17], imp[65], prova[65];

    if (!exuser_nome_valido(nome)) return 0;
    if (trova_utente(radice, nome, sale, imp, uid, gid) != 0) return 0;

    impronta_di(sale, pass, prova);
    return strcmp(prova, imp) == 0;
}
