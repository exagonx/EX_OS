/* =============================================================================
 * bin/login/login.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Chiede chi sei, e se la risposta torna avvia la shell.
 *
 * -----------------------------------------------------------------------------
 * ! E' UN CICLO, ED E' TUTTO IL PUNTO
 *
 * Fino ad agosto 2026 il kernel lanciava /bin/sh direttamente su ogni
 * console. `exit` faceva quello che deve fare — terminava la shell — e la
 * console restava morta: nessuno la riapriva, perche' nessuno stava
 * guardando. Su una macchina con quattro console bastavano quattro `exit`
 * per non avere piu' modo di dare un comando.
 *
 * Qui la shell e' un FIGLIO dentro un ciclo. Quando esce, il ciclo
 * ricomincia dalla richiesta di accesso. Non c'e' nessun caso speciale per
 * `exit`, nessun messaggio da intercettare, nessuna scorciatoia: uscire
 * dalla shell E' tornare al login, per come sono fatte le cose.
 *
 * ! QUESTO PROGRAMMA NON DEVE USCIRE MAI. Se uscisse, la console tornerebbe
 * morta esattamente come prima — e stavolta senza nemmeno la shell. Ogni
 * errore qui dentro si riferisce e si ricomincia; nessuno rende da main().
 *
 * -----------------------------------------------------------------------------
 * IL FILE DEGLI UTENTI
 *
 *     /boot/utenti   nome:uid:gid            0644, lo legge chiunque
 *     /boot/ombra    nome:sale:impronta      0600, solo root
 *
 * ! DUE FILE E NON UNO, ed e' il motivo per cui Unix ha /etc/passwd e
 * /etc/shadow separati. Con tutto in un file solo bisogna scegliere fra due
 * mali: leggibile da tutti — e allora chiunque si porta via le impronte e
 * prova candidati con comodo — oppure leggibile solo da root, e allora
 * NESSUNO PUO' PIU' TRADURRE UN uid IN UN NOME. Si e' visto subito: `id`
 * rispondeva «uid=1000(uid1000)» a un utente che si chiamava mario.
 *
 * I nomi sono pubblici perche' servono a chiunque; le impronte sono di root
 * perche' non servono a nessun altro.
 *
 * ! L'uid E' NEL FILE, dal 17 agosto 2026. Prima non c'era e non serviva:
 * login autenticava e basta, e la shell girava comunque da root. Adesso login
 * SCENDE con setuid() prima di lanciarla, e per scendere deve sapere a chi.
 *
 * ! root E' uid 0 PER DEFINIZIONE, e non e' una convenzione di questo file: e'
 * il numero che vfs_permesso() lascia passare dappertutto. Un utente normale
 * comincia da 1000, come su ogni Unix, cosi' i numeri bassi restano liberi per
 * i conti di servizio che un giorno serviranno.
 *
 * `impronta` e' lo SHA-256 di "sale:password" in esadecimale. Il sale rende
 * diverse due impronte di password uguali e impedisce le tabelle
 * precalcolate. La password non tocca mai il disco.
 *
 * ! SHA-256 NON E' UNA FUNZIONE DI DERIVAZIONE DI CHIAVI, ed e' bene che
 * stia scritto anche qui: e' veloce per costruzione, quindi chi si porta via
 * questo file puo' provare molti candidati al secondo. Il sale non lo
 * rallenta. Un vero irrobustimento (molte iterazioni, o memoria dura) e' il
 * passo successivo, e cambiera' il formato di questo file.
 *
 * ! E IL FILE NON E' PROTETTO. EX-OS non ha ancora i permessi sui file:
 * chiunque puo' leggerlo, e chiunque puo' riscriverlo. L'autenticazione qui
 * serve a separare gli utenti e a non lasciare una macchina aperta a chi ci
 * si siede davanti, non a resistere a chi ha gia' un programma in esecuzione
 * sul sistema. Dirlo e' meglio che lasciarlo credere.
 * ============================================================================= */

#include "libc.h"
#include "kbd_proto.h"

#define UTENTI     "/boot/utenti"    /* nome:uid:gid — pubblico */
#define OMBRA      "/boot/ombra"     /* nome:sale:impronta — di root */
#define NOME_MAX   32
#define PASS_MAX   64
#define RIGA_MAX   192
#define FILE_MAX   8192

/* ! LA CONSOLE SI RIVENDICA, NON SI EREDITA. Il driver di tastiera
 * consegna i tasti al processo dichiarato in primo piano su quella
 * console, e chi non si dichiara non riceve niente: il prompt compariva e
 * restava li' per sempre, senza un errore, perche' nessuno stava
 * ascoltando. La shell lo fa da sempre (sh_setfg in bin/sh/shell.c);
 * login, che il kernel lancia al suo posto, deve farlo per se'.
 *
 * Va rifatto anche DOPO ogni figlio: mentre gira la shell il primo piano
 * e' suo, e quando finisce va ripreso o la richiesta di accesso
 * successiva resterebbe muta. */
static void prendi_console(void)
{
    console_setfg((unsigned)getpid());
}

static char  g_shell[128] = "/bin/sh";
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

/* Rende la lunghezza, o -1 se la modalita' raw non e' disponibile. */
static int leggi_password(char *dst, int max)
{
    int len = 0;

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
static int trova_utente(const char *nome, char *sale, char *imp,
                        unsigned int *uid, unsigned int *gid)
{
    char su[16], sg[16];

    if (cerca_riga(UTENTI, nome, 2, su, sizeof(su), sg, sizeof(sg)) != 0)
        return -1;

    if (uid) *uid = (unsigned int)atoi(su);
    if (gid) *gid = (unsigned int)atoi(sg);

    /* ! LE IMPRONTE STANNO NELL'ALTRO FILE, e chi non e' root non le legge:
     * cerca_riga rende -1, e chi ha chiamato non riesce ad autenticare. E'
     * giusto — solo login, che gira da root, deve poterlo fare. */
    if (sale && imp)
        return cerca_riga(OMBRA, nome, 2, sale, 17, imp, 65);

    return 0;
}

static int c_e_qualche_utente(void)
{
    struct stat st;
    return (stat(UTENTI, &st) == 0 && st.st_size > 0);
}

/* Il primo uid libero da 1000 in su. Legge il file e prende il massimo + 1:
 * riusare un numero gia' speso vorrebbe dire che i file del vecchio
 * proprietario diventano di quello nuovo, in silenzio. */
static unsigned int prossimo_uid(void)
{
    static char testo[FILE_MAX];
    unsigned int massimo = 999;
    int fd, n, i = 0;

    fd = open(UTENTI, O_RDONLY);
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
        printf("login: %s: %s\n", file, strerror(errno));
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

static int aggiungi_utente(const char *nome, const char *pass, unsigned int uid)
{
    char sale[17], imp[65], riga[RIGA_MAX];

    sale_nuovo(sale);
    impronta_di(sale, pass, imp);

    /* ! PRIMA L'OMBRA, POI I NOMI, e l'ordine conta: se la seconda scrittura
     * fallisce resta un'impronta senza nome, che non fa entrare nessuno.
     * All'incontrario resterebbe un NOME SENZA IMPRONTA — un conto che
     * esiste e non ha password, cioe' il difetto peggiore possibile. */
    snprintf(riga, sizeof(riga), "%s:%s:%s\n", nome, sale, imp);
    if (scrivi_riga(OMBRA, riga, 0600) != 0) return -1;

    snprintf(riga, sizeof(riga), "%s:%u:%u\n", nome, uid, uid);
    return scrivi_riga(UTENTI, riga, 0644);
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
static void primo_utente(void)
{
    char nome[NOME_MAX], p1[PASS_MAX], p2[PASS_MAX];

    printf("\n  Sistema nuovo: non c'e' ancora nessun utente.\n");
    printf("  Creane uno adesso — da qui in poi servira' per entrare.\n\n");

    for (;;) {
        printf("  nome utente: ");
        if (leggi_riga(nome, sizeof(nome)) <= 0) continue;
        if (!nome_valido(nome)) {
            printf("  Solo lettere, cifre e '_', al massimo %d caratteri.\n",
                   NOME_MAX - 1);
            continue;
        }

        printf("  password: ");
        if (leggi_password(p1, sizeof(p1)) < 0) {
            printf("\n  La tastiera non risponde in modalita' raw: non posso\n");
            printf("  chiedere una password senza mostrarla a schermo.\n");
            printf("  Serve /dev/kbd.drv — vedi [modules] in kernel.cfg.\n");
            sleep(3);
            continue;
        }
        if (p1[0] == '\0') {
            printf("  Una password vuota lascia la macchina aperta. Riprova.\n");
            continue;
        }

        printf("  ripetila:  ");
        if (leggi_password(p2, sizeof(p2)) < 0) continue;
        if (strcmp(p1, p2) != 0) {
            printf("  Le due non coincidono. Riprova.\n");
            continue;
        }

        /* ! IL PRIMO UTENTE E' root, uid 0, E GLI ALTRI PARTONO DA 1000. Non
         * e' una convenzione di comodo: 0 e' il numero che vfs_permesso()
         * lascia passare dappertutto, e chi crea il primo conto su una
         * macchina appena installata deve poterci fare tutto. I numeri fra 1 e
         * 999 restano liberi per i conti di servizio. */
        if (aggiungi_utente(nome, p1,
                            c_e_qualche_utente() ? prossimo_uid() : 0u) == 0) {
            printf("\n  Utente '%s' creato.\n\n", nome);
            return;
        }
        printf("  Non sono riuscito a scriverlo. Riprovo.\n");
        sleep(2);
    }
}


/* ─────────────────────────────────────────────────────────────────────────────
 * main
 * ───────────────────────────────────────────────────────────────────────────── */

static char g_casa[128];

static void avvia_shell(const char *utente, unsigned int uid, unsigned int gid)
{
    char *argv[2];
    int   pid, stato;

    argv[0] = g_shell;
    argv[1] = NULL;

    /* L'utente entra nell'ambiente della shell: da li' lo legge `whoami`,
     * e ci si appoggera' il giorno che i file avranno un proprietario. */
    setenv("USER", utente, 1);

    /* =====================================================================
     * ! SI SCENDE PRIMA DI LANCIARE, e questo e' il punto di tutto il
     * meccanismo. EX-OS non ha il bit setuid sui file, quindi un programma non
     * puo' alzarsi di privilegio: l'unica direzione possibile e' giu'. init
     * resta root, avvia login che resta root, login autentica e SCENDE, e solo
     * allora lancia la shell — che eredita l'identita' come eredita la console
     * e la directory corrente.
     *
     * ! E DA QUI NON SI TORNA SU. Quando la shell finisce, questo processo e'
     * ormai l'utente e non puo' piu' chiedere una password per conto di
     * nessun altro: e' per questo che il ciclo del login rifa' `exec` di se
     * stesso invece di rimettersi ad aspettare.
     * ===================================================================== */
    /* =====================================================================
     * ! LA CASA SI CREA DA root E POI SI CONSEGNA, ed e' l'unico ordine che
     * funziona. /home appartiene a root con 0755: un utente normale non ci
     * puo' creare dentro, quindi non puo' farsi la propria directory. E se la
     * crea root senza consegnarla, il padrone ha una casa in cui non puo'
     * scrivere.
     *
     * Creare-e-consegnare e' esattamente quello che fa `adduser` su ogni Unix,
     * e qui si puo' fare perche' in questo istante siamo ancora root: dopo
     * setuid() non si potrebbe piu'.
     *
     * ! SE ESISTE GIA' NON E' UN ERRORE — e' il caso normale dal secondo
     * accesso in poi — e nemmeno se il filesystem non ha i proprietari:
     * chown rende ENOSYS su FAT, e li' non c'e' niente da consegnare.
     * ===================================================================== */
    {
        char casa[128];

        /* ! /home E' IL CONTENITORE, LA CASA E' /home/<utente>. E root fa
         * eccezione con /root, come su ogni Unix: la sua casa deve stare fuori
         * da /home perche' /home puo' essere un volume a parte, montato dopo —
         * e l'amministratore deve poter entrare anche quando non c'e'. Le crea
         * tutt'e due `install`. */
        if (uid == 0) snprintf(casa, sizeof(casa), "/root");
        else          snprintf(casa, sizeof(casa), "/home/%s", utente);

        if (mkdir(casa, 0755) == 0) {
            if (uid != 0 && chown(casa, uid, gid) != 0 && errno != ENOSYS)
                printf("login: %s creata ma non consegnata (%s)\n",
                       casa, strerror(errno));
        }
        strncpy(g_casa, casa, sizeof(g_casa) - 1);
        g_casa[sizeof(g_casa) - 1] = '\0';
    }

    if (uid != 0 && setuid(uid) != 0) {
        printf("login: non riesco a scendere all'utente %s (%s)\n",
               utente, strerror(errno));
        printf("       la shell NON viene avviata: meglio nessun accesso che\n");
        printf("       un accesso con i privilegi sbagliati.\n");
        sleep(3);
        return;
    }
    (void)gid;

    (void)gid;

    /* ! E CI SI ENTRA DOPO ESSERE SCESI, non prima: entrare in una directory
     * vuole il permesso di attraversarla, e vogliamo che sia l'UTENTE a
     * poterlo fare — se ci riuscisse solo root, la casa non sarebbe sua. */
    if (g_casa[0]) {
        if (chdir(g_casa) == 0) setenv("HOME", g_casa, 1);
        else printf("login: non riesco a entrare in %s (%s)\n",
                    g_casa, strerror(errno));
    }

    pid = spawn(g_shell, argv);
    if (pid < 0) {
        printf("login: %s non si avvia (%s)\n", g_shell, strerror(errno));
        sleep(3);
        return;
    }
    waitpid(pid, &stato, 0);
    prendi_console();     /* la shell l'aveva presa: se la riprende login */
}

int main(int argc, char **argv)
{
    char nome[NOME_MAX], pass[PASS_MAX];
    char sale[17], imp[65], prova[65];
    unsigned int uid = 0, gid = 0;
    int  i;

    for (i = 1; i < argc; i++) {
        /* =================================================================
         * ! «login -a» AGGIUNGE UN UTENTE E BASTA, e sta qui invece che in un
         * /bin/adduser a parte per una ragione sola: tutto cio' che serve —
         * leggere una password senza mostrarla, il sale, SHA-256, il formato
         * del file — e' gia' in questo binario. Un programma separato ne
         * sarebbe una seconda copia, e due copie della stessa cosa divergono
         * al primo cambiamento di formato.
         *
         * ! LO PUO' FARE SOLO root, e il controllo e' qui e non nel file:
         * /boot/utenti non e' protetto da permessi finche' non lo si crea con
         * i permessi giusti, e chiedere il privilegio a chi chiama e' il
         * controllo che non si puo' aggirare rinominando un file.
         * ================================================================= */
        if (strcmp(argv[i], "-a") == 0) {
            if (getuid() != 0) {
                printf("login -a: solo root puo' aggiungere un utente.\n");
                return 1;
            }
            prendi_console();
            primo_utente();
            return 0;
        }
        if (argv[i][0] != '-') {
            strncpy(g_shell, argv[i], sizeof(g_shell) - 1);
            g_shell[sizeof(g_shell) - 1] = '\0';
        }
    }

    prendi_console();

    for (;;) {
        if (!c_e_qualche_utente()) {
            primo_utente();
            continue;                 /* ora c'e': si passa dal login vero */
        }

        printf("\n");
        printf("  EX-OS — accesso\n\n");
        printf("  utente: ");
        if (leggi_riga(nome, sizeof(nome)) <= 0) continue;

        printf("  password: ");
        if (leggi_password(pass, sizeof(pass)) < 0) {
            printf("\n  Tastiera non disponibile in raw: non posso chiedere\n");
            printf("  una password senza mostrarla. Serve /dev/kbd.drv.\n");
            sleep(3);
            continue;
        }

        if (trova_utente(nome, sale, imp, &uid, &gid) == 0) {
            impronta_di(sale, pass, prova);
            if (strcmp(prova, imp) == 0) {
                printf("\n");
                avvia_shell(nome, uid, gid);
                /* La shell e' finita: si ricomincia. E' `exit` che torna
                 * al login, senza che nessuno lo tratti come un caso a
                 * parte. Vedi il commento in testa al file. */
                continue;
            }
        }

        /* ! UN SOLO MESSAGGIO PER I DUE CASI. Dire "utente inesistente"
         * separatamente da "password sbagliata" regala a chi prova
         * l'elenco dei nomi validi: si scoprono i conti esistenti senza
         * indovinare una sola password.
         *
         * E la pausa non e' cortesia: senza, si possono provare migliaia
         * di password al minuto battendole da uno script. */
        printf("\n  Accesso non riuscito.\n");
        sleep(2);
    }

    /* Non raggiunto: vedi "questo programma non deve uscire mai". */
}
