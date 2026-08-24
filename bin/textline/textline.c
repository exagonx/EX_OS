/* =============================================================================
 * bin/textline/textline.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Text Line — editor di testo lineare per EX-OS (/bin/textline)
 *
 * Modello edlin di MS-DOS: non c'è un cursore che si muove sullo schermo,
 * si opera per NUMERO DI RIGA. È la scelta giusta per questo sistema —
 * il TTY consegna testo una riga alla volta (vedi drivers/kbd/kbd.c) e
 * non esiste ancora una modalità raw, quindi un editor a schermo pieno
 * non avrebbe modo di leggere i tasti mentre vengono premuti.
 *
 * USO
 *   textline <file>              apre il file per l'editing
 *   textline <file> -v           visualizza il contenuto ed esce
 *   textline <file> -vp          visualizza a pagine ed esce
 *   textline <file> -c:<file2>   copia <file> in <file2> ed esce
 *
 * COMANDI DELL'EDITOR — vedi cmd_help()
 *
 * ---------------------------------------------------------------------------
 * SCELTE DI IMPLEMENTAZIONE, e perché
 *
 * 1. Righe in un array statico, non in memoria allocata.
 *    La free() della libc di EX-OS è un no-op dichiarato (allocatore a
 *    bump su sbrk, vedi lib/libc.c): ogni modifica a una riga
 *    perderebbe per sempre la memoria della versione precedente, e un
 *    editor è fatto apposta per modificare le righe molte volte. Con
 *    slot a dimensione fissa il consumo è noto e costante.
 *
 * 2. Lettura con read() diretta, non con gets()/getchar().
 *    Il TTY è orientato alla riga: una read() restituisce l'intera riga
 *    digitata. Leggere un carattere per volta significherebbe una
 *    syscall per carattere e — prima della correzione di luglio 2026 —
 *    perdere il resto della riga.
 *
 * 3. ESC si rileva DENTRO la riga, non come tasto immediato.
 *    Il servizio kbd consegna la riga solo su Invio, quindi un ESC
 *    premuto viene recapitato come byte 0x1B insieme al resto. Qui si
 *    considera annullata qualunque riga che contenga un ESC. Un ESC che
 *    interrompe immediatamente richiederebbe una modalità raw nel
 *    protocollo IPC della tastiera, che non esiste ancora.
 * ---------------------------------------------------------------------------
 * ============================================================================= */

#include "libc.h"

/* =============================================================================
 * Identità del programma
 *
 * ▲ INCREMENTARE TL_VERSION DI 0.001 A OGNI MODIFICA ▲
 * Stessa regola e stesso formato della versione del kernel
 * (kernel/include/version.h): stringa con tre decimali, non un numero —
 * EX-OS non usa la virgola mobile.
 * ============================================================================= */
#define TL_NAME     "Text Line"
#define TL_VERSION  "0.001"
#define TL_AUTHOR   "Graziano Falcone"
#define TL_EMAIL    "exagonx@hotmail.com"
#define TL_LICENSE  "GPL 2.0"

/* =============================================================================
 * Limiti del buffer di editing
 *
 * 512 righe da 128 caratteri = 64KB di BSS. Il loader ELF mappa p_memsz,
 * quindi lo spazio è allocato e azzerato al caricamento senza costare
 * nulla sul floppy (la BSS non occupa spazio nel file).
 * ============================================================================= */
#define MAX_LINES   512
#define MAX_LINE    128
#define PAGE_LINES  20      /* righe per pagina in -vp e lp */
#define INBUF       256     /* buffer di lettura da tastiera */

#define KEY_ESC     0x1B

static char g_text[MAX_LINES][MAX_LINE];
static int  g_count;                    /* righe usate */
static int  g_cur;                      /* riga corrente, 1-based (0 = nessuna) */
static char g_path[128];                /* file aperto */
static int  g_dirty;                    /* modifiche non salvate */

/* =============================================================================
 * Utilità
 * ============================================================================= */

/* Legge una riga da tastiera. Ritorna:
 *   >=0  lunghezza della riga (terminatore rimosso)
 *   -1   riga annullata con ESC
 *   -2   fine input / errore di lettura
 *
 * Il buffer è sempre terminato da '\0' anche in caso di annullamento. */
static int read_line(char *buf, int max)
{
    int n = (int)read(0, buf, (size_t)(max - 1));
    int i;

    if (n <= 0) {
        buf[0] = '\0';
        return -2;
    }

    buf[n] = '\0';

    /* Togli il terminatore di riga */
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        buf[--n] = '\0';
    }

    /* ESC in qualunque posizione annulla la riga: vedi la nota 3 in
     * testa al file sul perché non può essere un tasto immediato. */
    for (i = 0; i < n; i++) {
        if (buf[i] == KEY_ESC) {
            buf[0] = '\0';
            return -1;
        }
    }

    return n;
}

/* Attende INVIO fra una pagina e l'altra.
 * Ritorna 0 per continuare, -1 se l'utente vuole interrompere.
 *
 * La richiesta è "premere un tasto", ma il TTY di EX-OS consegna il testo
 * solo su Invio: non esiste un modo per accorgersi di un tasto qualunque
 * prima che la riga sia completa. Servirebbe una modalità raw nel
 * protocollo IPC della tastiera (drivers/kbd/kbd_proto.h), oggi assente. */
static int wait_page(void)
{
    char buf[INBUF];
    int  n;

    printf("-- INVIO per continuare, q per interrompere --");
    n = read_line(buf, sizeof(buf));
    printf("\n");

    if (n == -1 || n == -2) return -1;          /* ESC o fine input */
    if (buf[0] == 'q' || buf[0] == 'Q') return -1;
    return 0;
}

/* Converte una stringa in intero. Ritorna il valore, e avanza *pp oltre
 * le cifre consumate. Se non c'è nessuna cifra ritorna -1. */
static int parse_num(const char **pp)
{
    const char *p = *pp;
    int val = 0;
    int any = 0;

    while (*p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
        any = 1;
    }
    *pp = p;
    return any ? val : -1;
}

/* =============================================================================
 * Buffer di testo
 * ============================================================================= */

static void buf_clear(void)
{
    g_count = 0;
    g_cur   = 0;
    g_dirty = 0;
}

/* Inserisce 'txt' come nuova riga in posizione 'pos' (1-based).
 * pos == g_count+1 significa "in coda". Ritorna 0, -1 se pieno. */
static int line_insert(int pos, const char *txt)
{
    int i;

    if (g_count >= MAX_LINES) {
        printf("textline: buffer pieno (%d righe massime)\n", MAX_LINES);
        return -1;
    }
    if (pos < 1) pos = 1;
    if (pos > g_count + 1) pos = g_count + 1;

    /* Sposta in giù le righe da 'pos' in poi */
    for (i = g_count; i >= pos; i--) {
        strncpy(g_text[i], g_text[i - 1], MAX_LINE);
        g_text[i][MAX_LINE - 1] = '\0';
    }

    strncpy(g_text[pos - 1], txt, MAX_LINE);
    g_text[pos - 1][MAX_LINE - 1] = '\0';
    g_count++;
    g_dirty = 1;
    return 0;
}

static int line_delete(int pos)
{
    int i;

    if (pos < 1 || pos > g_count) {
        printf("textline: riga %d inesistente (il file ha %d righe)\n",
               pos, g_count);
        return -1;
    }

    for (i = pos; i < g_count; i++) {
        strncpy(g_text[i - 1], g_text[i], MAX_LINE);
        g_text[i - 1][MAX_LINE - 1] = '\0';
    }
    g_count--;
    g_dirty = 1;

    if (g_cur > g_count) g_cur = g_count;
    return 0;
}

/* =============================================================================
 * Caricamento e salvataggio
 * ============================================================================= */

static int buf_load(const char *path)
{
    char rd[512];
    char cur[MAX_LINE];
    int  fd;
    int  n;
    int  cl = 0;

    buf_clear();

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        /* Non è un errore: aprire un file inesistente significa
         * cominciarne uno nuovo, come fa edlin. */
        printf("textline: '%s' non esiste, nuovo file\n", path);
        return 0;
    }

    while ((n = (int)read(fd, rd, sizeof(rd))) > 0) {
        int i;
        for (i = 0; i < n; i++) {
            char c = rd[i];

            if (c == '\r') continue;          /* tollera i fine riga CRLF */

            if (c == '\n') {
                cur[cl] = '\0';
                if (line_insert(g_count + 1, cur) != 0) { close(fd); goto fine; }
                cl = 0;
                continue;
            }

            if (cl < MAX_LINE - 1) {
                cur[cl++] = c;
            }
            /* Riga più lunga di MAX_LINE: i caratteri in eccesso sono
             * scartati. Meglio troncare che corrompere il buffer — e il
             * troncamento è visibile, l'utente lo vede sullo schermo. */
        }
    }

    /* Ultima riga senza '\n' finale */
    if (cl > 0) {
        cur[cl] = '\0';
        line_insert(g_count + 1, cur);
    }

fine:
    close(fd);
    g_dirty = 0;
    g_cur   = (g_count > 0) ? 1 : 0;
    printf("textline: '%s' caricato, %d righe\n", path, g_count);
    return 0;
}

static int buf_save(const char *path)
{
    int fd;
    int i;

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        printf("textline: impossibile scrivere '%s' (errore %d)\n", path, fd);
        return -1;
    }

    for (i = 0; i < g_count; i++) {
        size_t len = strlen(g_text[i]);
        if (len > 0 && write(fd, g_text[i], len) < 0) {
            printf("textline: errore di scrittura alla riga %d\n", i + 1);
            close(fd);
            return -1;
        }
        if (write(fd, "\n", 1) < 0) {
            printf("textline: errore di scrittura alla riga %d\n", i + 1);
            close(fd);
            return -1;
        }
    }

    close(fd);
    g_dirty = 0;
    printf("textline: '%s' salvato, %d righe\n", path, g_count);
    return 0;
}

/* =============================================================================
 * Visualizzazione
 * ============================================================================= */

/* Mostra le righe da 'da' a 'a' (1-based, inclusi).
 * paginate != 0 → si ferma ogni PAGE_LINES righe. */
static void show_range(int da, int a, int paginate)
{
    int i;
    int mostrate = 0;

    if (g_count == 0) {
        printf("textline: documento vuoto\n");
        return;
    }

    if (da < 1)       da = 1;
    if (a  > g_count) a  = g_count;
    if (da > a) {
        printf("textline: intervallo vuoto (%d,%d)\n", da, a);
        return;
    }

    for (i = da; i <= a; i++) {
        /* Il ':' dopo il numero marca la riga corrente, come in edlin */
        printf("%5d%c %s\n", i, (i == g_cur) ? ':' : ' ', g_text[i - 1]);

        if (paginate && ++mostrate >= PAGE_LINES && i < a) {
            mostrate = 0;
            if (wait_page() != 0) return;
        }
    }
}

/* =============================================================================
 * Comandi
 * ============================================================================= */

static void cmd_help(void)
{
    printf("\n");
    printf("  %s %s - editor di testo lineare per EX-OS\n", TL_NAME, TL_VERSION);
    printf("  Autore  : %s <%s>\n", TL_AUTHOR, TL_EMAIL);
    printf("  Licenza : %s (GNU General Public License)\n", TL_LICENSE);
    printf("\n");
    printf("  COMANDI\n");
    printf("    h, help        questo elenco\n");
    printf("    l              elenca tutto il documento\n");
    printf("    lNN            elenca dalla riga NN\n");
    printf("    lNN,MM         elenca dalla riga NN alla MM\n");
    printf("    lp[NN[,MM]]    come l, ma a pagine (%d righe)\n", PAGE_LINES);
    printf("    m              modifica la riga corrente\n");
    printf("    mNN            modifica la riga NN\n");
    printf("    n              aggiunge nuove righe in coda\n");
    printf("    dNN            cancella la riga NN\n");
    printf("    cNN,MM         copia la riga NN inserendola alla riga MM\n");
    printf("    w              salva\n");
    printf("    e              salva ed esce\n");
    printf("    q              esce senza salvare\n");
    printf("\n");
    printf("  Durante l'inserimento di una riga, ESC annulla la riga e\n");
    printf("  riporta al prompt dei comandi.\n");
    printf("\n");
}

/* Modifica la riga 'n': mostra il testo attuale e ne chiede uno nuovo. */
static void cmd_modify(int n)
{
    char buf[INBUF];
    int  r;

    if (g_count == 0) {
        printf("textline: documento vuoto, usa 'n' per inserire righe\n");
        return;
    }
    if (n < 1 || n > g_count) {
        printf("textline: riga %d inesistente (il file ha %d righe)\n",
               n, g_count);
        return;
    }

    printf("%5d: %s\n", n, g_text[n - 1]);
    printf("%5d> ", n);

    r = read_line(buf, sizeof(buf));
    if (r == -1) {
        printf("(annullato, riga invariata)\n");
        return;
    }
    if (r == -2) return;

    strncpy(g_text[n - 1], buf, MAX_LINE);
    g_text[n - 1][MAX_LINE - 1] = '\0';
    g_cur   = n;
    g_dirty = 1;
}

/* Inserisce righe in coda finché l'utente non annulla con ESC. */
static void cmd_new(void)
{
    char buf[INBUF];

    printf("Inserimento righe in coda. ESC per terminare.\n");

    for (;;) {
        int r;

        printf("%5d> ", g_count + 1);
        r = read_line(buf, sizeof(buf));

        if (r == -1) {                 /* ESC: fine inserimento */
            printf("(fine inserimento)\n");
            return;
        }
        if (r == -2) return;

        if (line_insert(g_count + 1, buf) != 0) return;
        g_cur = g_count;
    }
}

static void cmd_copy(int src, int dst)
{
    char tmp[MAX_LINE];

    if (src < 1 || src > g_count) {
        printf("textline: riga sorgente %d inesistente (il file ha %d righe)\n",
               src, g_count);
        return;
    }
    if (dst < 1 || dst > g_count + 1) {
        printf("textline: destinazione %d fuori intervallo (1-%d)\n",
               dst, g_count + 1);
        return;
    }

    /* Copia in un temporaneo PRIMA di inserire: l'inserimento sposta le
     * righe successive, quindi g_text[src-1] potrebbe non essere più la
     * riga che l'utente ha chiesto di copiare. */
    strncpy(tmp, g_text[src - 1], MAX_LINE);
    tmp[MAX_LINE - 1] = '\0';

    if (line_insert(dst, tmp) == 0) {
        g_cur = dst;
        printf("textline: riga %d copiata alla riga %d\n", src, dst);
    }
}

/* =============================================================================
 * Interprete dei comandi
 *
 * Ritorna 0 per continuare, 1 per uscire.
 * ============================================================================= */
static int exec_command(const char *cmd)
{
    const char *p = cmd;
    char        c;

    /* Salta spazi iniziali */
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') return 0;      /* riga vuota: nessuna azione */

    c = *p++;

    /* --- help ------------------------------------------------------------ */
    /* Copre sia 'h' sia 'help': quello che segue la lettera è ignorato,
     * come per tutti gli altri comandi a lettera singola. */
    if (c == 'h' || c == 'H') { cmd_help(); return 0; }

    /* --- list ------------------------------------------------------------ */
    if (c == 'l' || c == 'L') {
        int paginate = 0;
        int da, a;

        if (*p == 'p' || *p == 'P') { paginate = 1; p++; }

        da = parse_num(&p);
        if (da < 0) {                       /* 'l' / 'lp' senza numeri */
            show_range(1, g_count, paginate);
            return 0;
        }

        if (*p == ',') {
            p++;
            a = parse_num(&p);
            if (a < 0) a = g_count;
        } else {
            a = g_count;                    /* 'lNN' → da NN alla fine */
        }

        show_range(da, a, paginate);
        return 0;
    }

    /* --- modify ---------------------------------------------------------- */
    if (c == 'm' || c == 'M') {
        int n = parse_num(&p);
        cmd_modify((n < 0) ? g_cur : n);    /* 'm' senza numero: riga corrente */
        return 0;
    }

    /* --- new ------------------------------------------------------------- */
    if (c == 'n' || c == 'N') { cmd_new(); return 0; }

    /* --- delete ---------------------------------------------------------- */
    if (c == 'd' || c == 'D') {
        int n = parse_num(&p);
        if (n < 0) n = g_cur;               /* 'd' senza numero: riga corrente */
        if (n < 1) {
            printf("textline: nessuna riga da cancellare\n");
            return 0;
        }
        line_delete(n);
        return 0;
    }

    /* --- copy ------------------------------------------------------------ */
    if (c == 'c' || c == 'C') {
        int src = parse_num(&p);
        int dst;

        if (src < 0 || *p != ',') {
            printf("textline: sintassi 'cNN,MM' (copia la riga NN alla riga MM)\n");
            return 0;
        }
        p++;
        dst = parse_num(&p);
        if (dst < 0) {
            printf("textline: sintassi 'cNN,MM' (copia la riga NN alla riga MM)\n");
            return 0;
        }
        cmd_copy(src, dst);
        return 0;
    }

    /* --- salvataggio e uscita -------------------------------------------- */
    /* NOTA: 'w', 'e' e 'q' non erano nella specifica iniziale, ma senza un
     * modo di salvare e uscire l'editor non sarebbe utilizzabile. Nomi
     * presi da edlin (E = end, Q = quit) con l'aggiunta di W = write. */
    if (c == 'w' || c == 'W') { buf_save(g_path); return 0; }

    if (c == 'e' || c == 'E') {
        if (buf_save(g_path) != 0) {
            printf("textline: salvataggio fallito, uscita annullata\n");
            return 0;
        }
        return 1;
    }

    if (c == 'q' || c == 'Q') {
        if (g_dirty) {
            char buf[INBUF];
            printf("Ci sono modifiche non salvate. Uscire comunque? (s/n) ");
            if (read_line(buf, sizeof(buf)) < 0) return 0;
            if (buf[0] != 's' && buf[0] != 'S') {
                printf("(uscita annullata)\n");
                return 0;
            }
        }
        return 1;
    }

    printf("textline: comando sconosciuto '%c' - 'h' per l'elenco\n", c);
    return 0;
}

/* =============================================================================
 * Modalità editing
 * ============================================================================= */
static int edit_loop(void)
{
    char buf[INBUF];

    printf("\n%s %s - 'h' per l'elenco dei comandi\n", TL_NAME, TL_VERSION);

    for (;;) {
        int r;

        printf("*");
        r = read_line(buf, sizeof(buf));

        if (r == -2) {
            /* Fine input: non lasciare il lavoro non salvato in sospeso
             * senza dirlo. */
            if (g_dirty) {
                printf("\ntextline: input terminato con modifiche non "
                       "salvate in '%s'\n", g_path);
            }
            return 1;
        }
        if (r == -1) continue;      /* ESC al prompt: semplicemente ignora */

        if (exec_command(buf) != 0) return 0;
    }
}

/* =============================================================================
 * Opzioni da riga di comando
 * ============================================================================= */

static int opzione_copia(const char *src, const char *dst)
{
    char buf[512];
    int  fin, fout, n;
    int  totale = 0;

    fin = open(src, O_RDONLY);
    if (fin < 0) {
        printf("textline: impossibile aprire '%s' (errore %d)\n", src, fin);
        return 1;
    }

    fout = open(dst, O_WRONLY | O_CREAT | O_TRUNC);
    if (fout < 0) {
        printf("textline: impossibile creare '%s' (errore %d)\n", dst, fout);
        close(fin);
        return 1;
    }

    while ((n = (int)read(fin, buf, sizeof(buf))) > 0) {
        if (write(fout, buf, (size_t)n) < 0) {
            printf("textline: errore di scrittura su '%s'\n", dst);
            close(fin);
            close(fout);
            return 1;
        }
        totale += n;
    }

    close(fin);
    close(fout);
    printf("textline: '%s' copiato in '%s' (%d byte)\n", src, dst, totale);
    return 0;
}

static void uso(void)
{
    printf("%s %s - editor di testo lineare\n", TL_NAME, TL_VERSION);
    printf("Uso:\n");
    printf("  textline <file>              apre il file per l'editing\n");
    printf("  textline <file> -v           visualizza il contenuto\n");
    printf("  textline <file> -vp          visualizza a pagine\n");
    printf("  textline <file> -c:<file2>   copia <file> in <file2>\n");
}

int main(int argc, char **argv)
{
    const char *opt;

    if (argc < 2) {
        uso();
        return 1;
    }

    strncpy(g_path, argv[1], sizeof(g_path));
    g_path[sizeof(g_path) - 1] = '\0';

    /* --- senza opzioni: editing ------------------------------------------ */
    if (argc == 2) {
        buf_load(g_path);
        return edit_loop();
    }

    opt = argv[2];

    /* --- -c:<file2> ------------------------------------------------------- */
    if (opt[0] == '-' && opt[1] == 'c' && opt[2] == ':') {
        if (opt[3] == '\0') {
            printf("textline: -c: richiede un nome file (es. -c:copia.txt)\n");
            return 1;
        }
        return opzione_copia(g_path, opt + 3);
    }

    /* --- -vp (va controllato PRIMA di -v: -v ne è un prefisso) ------------ */
    if (strcmp(opt, "-vp") == 0) {
        buf_load(g_path);
        show_range(1, g_count, 1);
        return 0;
    }

    if (strcmp(opt, "-v") == 0) {
        buf_load(g_path);
        show_range(1, g_count, 0);
        return 0;
    }

    printf("textline: opzione sconosciuta '%s'\n", opt);
    uso();
    return 1;
}
