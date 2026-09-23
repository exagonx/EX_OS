/* =============================================================================
 * lib/exdlg/scarichi.c — downloads, and the window that shows them
 *
 * Asked on 23 September 2026 for EXBrowser: a download manager that shows
 * state, percentage and average timing, opens by itself when a download
 * starts, can be reopened from a menu, sits beside the program or is closed
 * while the downloads go on — and goes away with the program. And, asked
 * straight after: in the toolkit, so any program made with exide has it.
 *
 * ! EVERY DOWNLOAD IS A PROCESS: /bin/scarica -avanza <url> <file>. exhttp
 * keeps its state in globals (the reused connection, the verso, the TLS), so
 * two requests in one process would step on each other — and a download in
 * the program's own process would freeze it until the end, which is what
 * EXBrowser did until today. A child process leaves the program free, and
 * its stdout, a pipe, says how far it has got (AVANZA / FINE / ERRORE, see
 * bin/scarica/scarica.c).
 *
 * ! THE PIPE IS READ BY THE MESSAGE LOOP (ex_guarda_fd), NOT ON A TIMER: a
 * timer belongs to a window, and a download must go on when its window is
 * closed. A descriptor belongs to the process.
 *
 * ! THE WINDOW IS THE PROGRAM'S, NOT A PROCESS OF ITS OWN: when the program
 * exits it disappears with it. The downloads do not die by themselves with
 * the program — they are children, not threads — so whoever exits calls
 * ex_scarichi_ferma_tutti(), which stops them and removes the half files.
 * ============================================================================= */
#include "libc.h"
#include "exwin.h"
#include "exdlg.h"

#define SC_MAX      16
#define ID_LISTA    1
#define ID_FERMA    2
#define ID_CARTELLA 3
#define ID_TOGLI    4

#define W_FIN       640
#define H_FIN       250

typedef struct {
    int           usato;
    int           pid;
    int           fd;           /* the read end of the pipe */
    int           vivo;         /* the pipe is still open and watched: until
                                 * then the slot is NOT reused, even removed
                                 * from the list — its reader still points
                                 * here */
    int           stato;        /* EX_SC_* */
    unsigned long fatti, attesi;
    unsigned int  t0, t1;       /* start and end, uptime_ms */
    char          url[EXSC_URL_MAX];
    char          dove[EXSC_DOVE_MAX];
    char          motivo[96];
    char          riga[160];    /* a line from the pipe, not finished yet */
    unsigned int  riga_n;
} Scarico;

static Scarico       g_sc[SC_MAX];
static ExFinestra    g_win, g_lista, g_nota;
static ExFinestra    g_b_ferma, g_b_cartella, g_b_togli;
static int           g_righe[SC_MAX];      /* list row -> download id */
static int           g_n_righe;
static ExScaricoFine g_fine;
static void         *g_fine_dato;

/* -----------------------------------------------------------------------------
 * The numbers, as a person reads them
 * --------------------------------------------------------------------------- */
static void misura(unsigned long b, char *out)
{
    if (b >= 1024ul * 1024ul)
        sprintf(out, "%lu,%lu MB", b / (1024ul * 1024ul), (b % (1024ul * 1024ul)) * 10 / (1024ul * 1024ul));
    else if (b >= 1024ul)
        sprintf(out, "%lu KB", b / 1024ul);
    else
        sprintf(out, "%lu byte", b);
}

static void durata(unsigned int s, char *out)
{
    if (s >= 3600) sprintf(out, "%u:%02u:%02u", s / 3600, (s / 60) % 60, s % 60);
    else           sprintf(out, "%u:%02u", s / 60, s % 60);
}

static void riga_di(const Scarico *s, char *out, unsigned int max)
{
    const char  *nome = strrchr(s->dove, '/');
    unsigned int ms = (s->stato == EX_SC_IN_CORSO ? uptime_ms() : s->t1) - s->t0;
    /* ! NO 64-BIT DIVISION: there is no libgcc here (__udivdi3). Split so
     * that nothing overflows 32 bits: bytes/ms times 1000, plus the rest. */
    unsigned long vel = ms ? (s->fatti / ms) * 1000ul + ((s->fatti % ms) * 1000ul) / ms : 0;
    char         fatti[24], attesi[24], v[24], resta[16], dur[16];

    nome = nome ? nome + 1 : s->dove;
    misura(s->fatti, fatti);
    misura(vel, v);
    durata(ms / 1000u, dur);

    switch (s->stato) {
    case EX_SC_IN_CORSO:
        if (s->attesi) {
            unsigned int pc = (unsigned int)(s->attesi >= 100 ? s->fatti / (s->attesi / 100)
                                                              : s->fatti * 100 / s->attesi);

            misura(s->attesi, attesi);
            if (vel && s->attesi > s->fatti)
                durata((unsigned int)((s->attesi - s->fatti) / vel), resta);
            else
                strcpy(resta, "?");
            /* ! UNDER 64 CHARACTERS: a list row holds LISTA_TESTO_MAX = 64
             * and cuts the rest — the first version lost the time left,
             * which is the number one looks for. */
            snprintf(out, max, "%-18.18s %3u%% %s/%s %s/s ancora %s",
                     nome, pc > 100 ? 100 : pc, fatti, attesi, v, resta);
        } else {
            /* ! SENZA LUNGHEZZA NON SI INVENTA UNA PERCENTUALE: il server
             * non l'ha detta (risposte a pezzi), e un numero finto sarebbe
             * peggio di nessun numero. Si dice quanto e' arrivato. */
            snprintf(out, max, "%-18.18s    ? %s %s/s da %s", nome, fatti, v, dur);
        }
        break;
    case EX_SC_FATTO:
        snprintf(out, max, "%-18.18s fatto %s in %s, media %s/s", nome, fatti, dur, v);
        break;
    case EX_SC_FERMATO:
        snprintf(out, max, "%-18.18s fermato dopo %s", nome, fatti);
        break;
    default:
        snprintf(out, max, "%-18.18s NON riuscito: %s", nome, s->motivo);
        break;
    }
}

/* -----------------------------------------------------------------------------
 * The window
 * --------------------------------------------------------------------------- */
static void finestra_rifai(void)
{
    int  i, scelta;
    char r[200];

    if (!g_win) return;

    scelta = (int)ex_lista_scelta(g_lista);
    ex_lista_svuota(g_lista);
    g_n_righe = 0;
    for (i = 0; i < SC_MAX; i++) {
        if (!g_sc[i].usato) continue;
        riga_di(&g_sc[i], r, sizeof(r));
        ex_lista_aggiungi(g_lista, r);
        g_righe[g_n_righe++] = i;
    }
    if (g_n_righe) ex_lista_scegli(g_lista, (unsigned int)(scelta < g_n_righe ? scelta : g_n_righe - 1));

    {
        int in_corso = ex_scarichi_in_corso();

        snprintf(r, sizeof(r), in_corso ? "%d in corso. Chiudere questa finestra non li ferma."
                                        : "Nessuno in corso.", in_corso);
        ex_testo_metti(g_nota, r);
    }
    ex_procedura_base(g_win, EXM_DISEGNA, 0, 0);
}

static int scelto(void)
{
    int k;

    if (!g_win || g_n_righe == 0) return -1;
    k = (int)ex_lista_scelta(g_lista);
    return (k >= 0 && k < g_n_righe) ? g_righe[k] : -1;
}

static long proc_finestra(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    int id;

    switch (msg) {
    /* ! CHIUDERE LA FINESTRA NON FERMA NIENTE: gli scaricamenti vanno avanti
     * e la finestra si riapre quando serve. La base, qui, chiuderebbe il
     * PROGRAMMA intero (ex_procedura_base su EXM_CHIUDI esce). */
    case EXM_CHIUDI:
        ex_distruggi(g_win);
        g_win = 0;
        return 0;

    /* ! THE KEYBOARD, AS IN THE OTHER DIALOGS: a button with the focus does
     * not take Enter by itself in this toolkit (see cf_proc in exdlg.c), so
     * Enter presses the focused button here. Canc stops the chosen download,
     * Esc closes the window — which stops nothing. The first test pressed
     * Tab and Enter on «Ferma», and the download went on to the end. */
    case EXM_TASTO: {
        unsigned int c = wp & 0xFFFF;
        ExFinestra   chi = ex_fuoco_chi(f);

        if (c == 27) return proc_finestra(f, EXM_CHIUDI, 0, 0);
        if (c == 0x0109u) return proc_finestra(f, EXM_COMANDO, ID_FERMA, 0);  /* Canc */
        if (c == '\n' || c == '\r') {
            if (chi == g_b_ferma)    return proc_finestra(f, EXM_COMANDO, ID_FERMA, 0);
            if (chi == g_b_cartella) return proc_finestra(f, EXM_COMANDO, ID_CARTELLA, 0);
            if (chi == g_b_togli)    return proc_finestra(f, EXM_COMANDO, ID_TOGLI, 0);
            return 0;
        }
        return ex_procedura_base(f, msg, wp, lp);
    }

    case EXM_COMANDO:
        id = scelto();
        if (wp == ID_FERMA && id >= 0) ex_scarico_ferma(id);
        if (wp == ID_CARTELLA && id >= 0) {
            char  cart[EXSC_DOVE_MAX], *barra;
            char *av[3];

            strcpy(cart, g_sc[id].dove);
            barra = strrchr(cart, '/');
            if (barra) *(barra == cart ? barra + 1 : barra) = '\0';
            av[0] = (char *)(access("/exwin/bin/filemgr", F_OK) == 0
                             ? "/exwin/bin/filemgr" : "/cdrom/exwin/bin/filemgr");
            av[1] = cart;
            av[2] = 0;
            spawn_ex(av[0], av, environ, 0, 0);
        }
        if (wp == ID_TOGLI) {
            int i;
            for (i = 0; i < SC_MAX; i++)
                if (g_sc[i].usato && g_sc[i].stato != EX_SC_IN_CORSO) g_sc[i].usato = 0;
        }
        finestra_rifai();
        return 0;

    default:
        (void)lp;
        return ex_procedura_base(f, msg, wp, lp);
    }
}

void ex_scarichi_finestra(void)
{
    if (g_win) { ex_procedura_base(g_win, EXM_DISEGNA, 0, 0); return; }

    g_win = ex_crea("finestra", "Download", EX_TITOLO | EX_BORDO | EX_CHIUDI,
                    EX_AUTO, EX_AUTO, W_FIN, H_FIN, 0, 0, proc_finestra);
    if (!g_win) return;

    g_lista = ex_crea("lista", "", EX_FIGLIO, 8, 8, W_FIN - 16, H_FIN - 80,
                      g_win, ID_LISTA, 0);
    g_b_ferma = ex_crea("pulsante", "Ferma", EX_FIGLIO, 8, H_FIN - 66, 90, 26,
                        g_win, ID_FERMA, 0);
    g_b_cartella = ex_crea("pulsante", "Apri la cartella", EX_FIGLIO, 106, H_FIN - 66,
                           150, 26, g_win, ID_CARTELLA, 0);
    g_b_togli = ex_crea("pulsante", "Togli i finiti", EX_FIGLIO, 264, H_FIN - 66,
                        130, 26, g_win, ID_TOGLI, 0);
    /* The list has the keys: arrows choose, Canc stops. */
    ex_fuoco(g_lista);
    g_nota = ex_crea("etichetta", "", EX_FIGLIO, 8, H_FIN - 30, W_FIN - 16, 16,
                     g_win, 0, 0);
    finestra_rifai();
}

/* -----------------------------------------------------------------------------
 * The pipe
 * --------------------------------------------------------------------------- */
static void finito(int id, int stato, const char *motivo)
{
    Scarico *s = &g_sc[id];

    if (s->stato == EX_SC_IN_CORSO) {
        s->stato = stato;
        s->t1 = uptime_ms();
        if (motivo) { strncpy(s->motivo, motivo, sizeof(s->motivo) - 1); s->motivo[sizeof(s->motivo) - 1] = '\0'; }
        if (g_fine) g_fine(g_fine_dato, id, stato);
    }
}

static void riga_letta(int id, const char *r)
{
    Scarico *s = &g_sc[id];

    if (strncmp(r, "AVANZA ", 7) == 0) {
        char *dopo;
        s->fatti  = strtoul(r + 7, &dopo, 10);
        s->attesi = strtoul(dopo, 0, 10);
    } else if (strncmp(r, "FINE ", 5) == 0) {
        char *dopo;
        (void)strtoul(r + 5, &dopo, 10);
        s->fatti = strtoul(dopo, 0, 10);
        if (!s->attesi) s->attesi = s->fatti;
        finito(id, EX_SC_FATTO, 0);
    } else if (strncmp(r, "ERRORE ", 7) == 0) {
        finito(id, EX_SC_FALLITO, r + 7);
    }
}

static void leggi(void *dato, int fd)
{
    int      id = (int)(long)dato;
    Scarico *s = &g_sc[id];
    char     buf[256];
    int      n, i;

    n = (int)read(fd, buf, sizeof(buf));
    if (n <= 0) {
        int st;

        ex_guarda_fd(fd, 0, 0);
        close(fd);
        s->fd = -1;
        s->vivo = 0;
        waitpid(s->pid, &st, WNOHANG);
        /* The process went away without saying FINE: killed, or dead. */
        finito(id, EX_SC_FALLITO, "il programma che scaricava si e' fermato");
        finestra_rifai();
        return;
    }
    for (i = 0; i < n; i++) {
        if (buf[i] == '\n') {
            s->riga[s->riga_n] = '\0';
            riga_letta(id, s->riga);
            s->riga_n = 0;
        } else if (s->riga_n + 1 < sizeof(s->riga)) {
            s->riga[s->riga_n++] = buf[i];
        }
    }
    finestra_rifai();
}

/* -----------------------------------------------------------------------------
 * The interface
 * --------------------------------------------------------------------------- */
int ex_scarico_avvia(const char *url, const char *dove)
{
    const char *bin = access("/bin/scarica", F_OK) == 0 ? "/bin/scarica" :
                      access("/cdrom/bin/scarica", F_OK) == 0 ? "/cdrom/bin/scarica" : 0;
    SpawnRedir  r;
    char       *av[5];
    int         p[2], id, pid;

    if (!bin) return EX_SC_ERR_PROGRAMMA;
    if (!url || !dove || strlen(url) >= EXSC_URL_MAX || strlen(dove) >= EXSC_DOVE_MAX)
        return EX_SC_ERR_USO;
    for (id = 0; id < SC_MAX; id++) if (!g_sc[id].usato && !g_sc[id].vivo) break;
    if (id == SC_MAX) {
        /* The finished ones make room: only the running ones count. */
        for (id = 0; id < SC_MAX; id++)
            if (g_sc[id].stato != EX_SC_IN_CORSO && !g_sc[id].vivo) break;
        if (id == SC_MAX) return EX_SC_ERR_PIENO;
    }
    if (pipe(p) != 0) return EX_SC_ERR_PROGRAMMA;

    r.fd = 1; r.flags = 0; r.percorso = 0; r.fd_padre = p[1];
    av[0] = (char *)bin; av[1] = "-avanza"; av[2] = (char *)url; av[3] = (char *)dove; av[4] = 0;
    pid = spawn_ex(bin, av, environ, &r, 1);
    close(p[1]);                        /* ! or EOF never comes: see pipe() */
    if (pid < 0) { close(p[0]); return EX_SC_ERR_PROGRAMMA; }

    if (ex_guarda_fd(p[0], leggi, (void *)(long)id) != 0) {
        /* An exwin.so older than 23 September 2026: nobody would read. */
        interrompi(pid);
        close(p[0]);
        return EX_SC_ERR_TOOLKIT;
    }

    memset(&g_sc[id], 0, sizeof(Scarico));
    g_sc[id].usato = 1;
    g_sc[id].pid = pid;
    g_sc[id].fd = p[0];
    g_sc[id].vivo = 1;
    g_sc[id].stato = EX_SC_IN_CORSO;
    g_sc[id].t0 = uptime_ms();
    strcpy(g_sc[id].url, url);
    strcpy(g_sc[id].dove, dove);

    ex_scarichi_finestra();         /* ! it opens by itself, as asked */
    finestra_rifai();
    return id;
}

int ex_scarico_info(int id, ExScarico *out)
{
    const Scarico *s;

    if (id < 0 || id >= SC_MAX || !g_sc[id].usato || !out) return 0;
    s = &g_sc[id];
    memset(out, 0, sizeof(*out));
    out->stato  = s->stato;
    out->fatti  = s->fatti;
    out->attesi = s->attesi;
    out->ms     = (s->stato == EX_SC_IN_CORSO ? uptime_ms() : s->t1) - s->t0;
    strcpy(out->url, s->url);
    strcpy(out->dove, s->dove);
    strcpy(out->motivo, s->motivo);
    return 1;
}

void ex_scarico_ferma(int id)
{
    if (id < 0 || id >= SC_MAX || !g_sc[id].usato || g_sc[id].stato != EX_SC_IN_CORSO) return;
    interrompi(g_sc[id].pid);
    /* ! IL FILE A META' SI TOGLIE QUI: il processo fermato non puo' farlo,
     * interrompi() lo fa uscire senza che possa pulire (vedi libc.h). */
    remove(g_sc[id].dove);
    finito(id, EX_SC_FERMATO, 0);
    finestra_rifai();
}

int ex_scarichi_in_corso(void)
{
    int i, n = 0;

    for (i = 0; i < SC_MAX; i++)
        if (g_sc[i].usato && g_sc[i].stato == EX_SC_IN_CORSO) n++;
    return n;
}

void ex_scarichi_ferma_tutti(void)
{
    int i;

    for (i = 0; i < SC_MAX; i++) ex_scarico_ferma(i);
}

void ex_scarichi_alla_fine(ExScaricoFine fn, void *dato)
{
    g_fine = fn;
    g_fine_dato = dato;
}
