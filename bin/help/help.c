/* =============================================================================
 * bin/help/help.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 *     help              l'indice: che cosa c'e' e a che serve
 *     help ls           il blocco 'ls', e solo quello
 *     help -t           tutto il file di seguito, senza sfogliare
 *
 * -----------------------------------------------------------------------------
 * ! L'AIUTO E' UN FILE DI TESTO, NON DELLE printf
 *
 * Fino ad agosto 2026 `help` era una sessantina di println() dentro la shell.
 * Tre cose non andavano, e nessuna si risolveva aggiungendo righe:
 *
 *   - non ci stava. Lo schermo e' 80x25, l'elenco era piu' lungo, e le
 *     prime pagine scorrevano via — cioe' proprio quelle di chi non sa
 *     ancora cosa cercare;
 *   - non si poteva chiedere una cosa sola. `help ls` non esisteva perche'
 *     non c'era niente da indicizzare: era un testo unico;
 *   - descriveva i comandi della shell e basta. I programmi di /bin — che
 *     sono la maggior parte di cio' che si puo' dare al prompt — non
 *     comparivano, perche' la shell non ha modo di sapere cosa fanno.
 *
 * Ora il testo sta in /boot/help.txt, diviso in blocchi con
 * un'intestazione. Aggiungere un comando vuol dire aggiungere un blocco a
 * un file di testo sul floppy: non si ricompila niente, e chi installa un
 * programma nuovo puo' documentarlo.
 *
 * -----------------------------------------------------------------------------
 * IL FORMATO
 *
 *     [nome altro-nome ...]
 *     riga di sommario, quella che compare nell'indice
 *
 *       il resto del blocco, indentato o no, come si vuole
 *
 *     [prossimo]
 *     ...
 *
 * Un'intestazione e' una riga che comincia con '[' e finisce con ']'. I
 * nomi dentro le quadre sono sinonimi: `[poweroff shutdown]` risponde a
 * tutti e due. Tutto cio' che sta prima della prima intestazione e' un
 * preambolo e non appartiene a nessun blocco.
 *
 * ! IL SOMMARIO E' LA PRIMA RIGA NON VUOTA, e la si scrive corta apposta:
 * finisce in un indice largo 80 colonne accanto al nome, e se sfora viene
 * troncato. Non e' un limite del formato, e' la larghezza dello schermo.
 *
 * -----------------------------------------------------------------------------
 * ! SENZA TASTIERA IN RAW SI STAMPA E BASTA
 *
 * Sfogliare vuol dire ricevere i tasti uno per uno, e quella strada passa
 * dal servizio 'kbd' (vedi drivers/kbd/kbd_proto.h). Se il servizio non
 * c'e' — kernel.cfg senza [modules], driver morto — oppure se siamo stati
 * lanciati in background, il testo viene riversato di seguito come farebbe
 * `cat`. Un aiuto che si rifiuta di parlare perche' non puo' impaginare
 * sarebbe la scelta peggiore fra le due.
 * ============================================================================= */

#include "libc.h"
#include "kbd_proto.h"

/* +0.001 a ogni modifica: `help -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("help", "0.001");

#define FILE_AIUTO   "/boot/help.txt"

#define TESTO_MAX    32768
#define RIGHE_MAX    1600
#define BLOCCHI_MAX  128
#define NOMI_MAX     48
#define VIDEO_RIGHE  25
#define VIDEO_COL    80

/* Le righe di testo visibili: lo schermo meno il titolo e la barra in
 * fondo. Se il conto sbaglia di uno lo schermo scorre da solo a ogni
 * ridisegno, e il difetto si vede come uno sfarfallio che sale. */
#define FINESTRA     (VIDEO_RIGHE - 2)

static char   g_testo[TESTO_MAX];
static char  *g_riga[RIGHE_MAX];
static int    g_nrighe;

typedef struct {
    char  nomi[NOMI_MAX];   /* "poweroff shutdown", separati da spazi */
    char  primo[NOMI_MAX];  /* il primo dei nomi: quello mostrato nell'indice */
    int   prima;            /* prima riga del corpo */
    int   dopo;             /* una oltre l'ultima */
    const char *sommario;
} Blocco;

static Blocco g_blocco[BLOCCHI_MAX];
static int    g_nblocchi;

/* La vista corrente: puntatori a righe da mostrare. Per un blocco puntano
 * dentro g_testo; per l'indice puntano a g_indice, composto sul momento. */
static char  *g_vista[RIGHE_MAX];
static int    g_nvista;
static char   g_indice[BLOCCHI_MAX + 8][VIDEO_COL + 1];


/* ─────────────────────────────────────────────────────────────────────────────
 * Lettura e analisi del file
 * ───────────────────────────────────────────────────────────────────────────── */

static int riga_vuota(const char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
    return (*s == '\0');
}

/* L'intestazione di un blocco: '[' in prima colonna, ']' a chiudere, e
 * NIENT'ALTRO sulla riga. I nomi finiscono in b->nomi separati da spazi
 * singoli, cosi' il confronto puo' scorrerli senza rianalizzare le quadre.
 *
 * ! LA QUADRA DEVE CHIUDERE LA RIGA, e la regola larga non funzionava.
 * Bastava che la riga COMINCIASSE con '[' e avesse un ']' da qualche
 * parte: dentro il blocco 'export' la riga
 *
 *     [env] di /boot/kernel.cfg — vedi /boot/kernel.txt.
 *
 * diventava un blocco chiamato 'env', il secondo con quel nome, e
 * spezzava in due il blocco che la conteneva. Il sintomo era un nome
 * ripetuto nell'indice: cioe' il testo che si scrive puo' cambiare la
 * struttura del file, che e' esattamente cio' che un formato non deve
 * permettere. Citare una sezione a inizio riga e' normale in un testo che
 * parla di file INI. */
static int intestazione(const char *s, Blocco *b)
{
    int i = 0, j = 0, spazio = 1;

    if (s[0] != '[') return 0;

    /* Il ']' dev'essere l'ultima cosa non bianca della riga. */
    for (i = 1; s[i] && s[i] != ']'; i++) ;
    if (s[i] != ']') return 0;
    for (i++; s[i]; i++)
        if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r') return 0;

    for (i = 1; s[i] && s[i] != ']'; i++) {
        char c = s[i];

        if (c == ' ' || c == '\t' || c == ',') {
            if (!spazio && j < NOMI_MAX - 1) { b->nomi[j++] = ' '; spazio = 1; }
            continue;
        }
        if (j < NOMI_MAX - 1) { b->nomi[j++] = c; spazio = 0; }
    }

    while (j > 0 && b->nomi[j-1] == ' ') j--;
    b->nomi[j] = '\0';
    if (b->nomi[0] == '\0') return 0;

    for (i = 0; b->nomi[i] && b->nomi[i] != ' ' && i < NOMI_MAX - 1; i++)
        b->primo[i] = b->nomi[i];
    b->primo[i] = '\0';
    return 1;
}

/* Il nome cercato e' uno dei sinonimi di questo blocco? Confronto senza
 * distinzione fra maiuscole e minuscole: chi scrive `help LS` cerca la
 * stessa cosa di chi scrive `help ls`. */
static char giu(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int combacia(const Blocco *b, const char *nome)
{
    int i = 0;

    while (b->nomi[i]) {
        int k = 0;

        while (b->nomi[i + k] && b->nomi[i + k] != ' ' &&
               nome[k] && giu(b->nomi[i + k]) == giu(nome[k])) k++;

        if (nome[k] == '\0' &&
            (b->nomi[i + k] == '\0' || b->nomi[i + k] == ' ')) return 1;

        while (b->nomi[i] && b->nomi[i] != ' ') i++;
        while (b->nomi[i] == ' ') i++;
    }
    return 0;
}

/* Rende 0 se il file c'e' ed e' stato analizzato. */
static int carica(void)
{
    int fd, n, i;

    fd = open(FILE_AIUTO, O_RDONLY);
    if (fd < 0) return -1;

    n = (int)read(fd, g_testo, sizeof(g_testo) - 1);
    close(fd);
    if (n <= 0) return -1;

    /* ! UN FILE PIU' LUNGO DEL BUFFER SI DICE. E' lo stesso difetto che
     * kernel.cfg ha avuto per due volte: si legge l'inizio, i blocchi in
     * fondo spariscono, e sembra che qualcuno non li abbia mai scritti. */
    if (n >= (int)sizeof(g_testo) - 1)
        printf("help: %s supera %d byte: letto solo l'inizio\n",
               FILE_AIUTO, (int)sizeof(g_testo) - 1);

    g_testo[n] = '\0';

    /* Spezzetta in righe, sul posto. */
    g_nrighe = 0;
    i = 0;
    while (i < n && g_nrighe < RIGHE_MAX) {
        g_riga[g_nrighe++] = &g_testo[i];
        while (i < n && g_testo[i] != '\n') i++;
        if (i < n) g_testo[i++] = '\0';
    }
    /* Il '\r' di un file scritto altrove finirebbe a video come un
     * ritorno a capo in mezzo alla riga: si toglie qui, una volta sola. */
    for (i = 0; i < g_nrighe; i++) {
        char *r = g_riga[i];
        int   l = (int)strlen(r);
        if (l > 0 && r[l-1] == '\r') r[l-1] = '\0';
    }

    /* I blocchi. */
    g_nblocchi = 0;
    for (i = 0; i < g_nrighe; i++) {
        Blocco b;
        int    j;

        b.nomi[0] = '\0';
        b.primo[0] = '\0';
        if (!intestazione(g_riga[i], &b)) continue;
        if (g_nblocchi >= BLOCCHI_MAX) {
            printf("help: piu' di %d blocchi: gli ultimi sono ignorati\n",
                   BLOCCHI_MAX);
            break;
        }

        b.prima = i + 1;
        for (j = i + 1; j < g_nrighe; j++) {
            Blocco prova;
            prova.nomi[0] = '\0';
            if (intestazione(g_riga[j], &prova)) break;
        }
        b.dopo = j;

        b.sommario = "";
        for (j = b.prima; j < b.dopo; j++) {
            if (!riga_vuota(g_riga[j])) { b.sommario = g_riga[j]; break; }
        }

        g_blocco[g_nblocchi++] = b;
    }

    return 0;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Le due viste
 * ───────────────────────────────────────────────────────────────────────────── */

static void vista_blocco(const Blocco *b)
{
    int i;

    g_nvista = 0;
    for (i = b->prima; i < b->dopo && g_nvista < RIGHE_MAX; i++)
        g_vista[g_nvista++] = g_riga[i];

    /* Le righe vuote in coda al blocco non sono contenuto: farebbero
     * sembrare il testo piu' lungo di quanto sia, e lo scorrimento
     * arriverebbe in fondo a una pagina bianca. */
    while (g_nvista > 0 && riga_vuota(g_vista[g_nvista - 1])) g_nvista--;
}

static void vista_indice(void)
{
    int i, n = 0;

    snprintf(g_indice[n], VIDEO_COL, "  %-16s %s", "COMANDO", "A CHE SERVE");
    g_vista[n] = g_indice[n]; n++;
    g_indice[n][0] = '\0';
    g_vista[n] = g_indice[n]; n++;

    for (i = 0; i < g_nblocchi && n < BLOCCHI_MAX + 6; i++) {
        snprintf(g_indice[n], VIDEO_COL, "  %-16s %s",
                 g_blocco[i].primo, g_blocco[i].sommario);
        g_vista[n] = g_indice[n];
        n++;
    }

    g_indice[n][0] = '\0';
    g_vista[n] = g_indice[n]; n++;
    snprintf(g_indice[n], VIDEO_COL,
             "  `help <comando>` per il dettaglio.  Sinonimi e opzioni stanno li'.");
    g_vista[n] = g_indice[n]; n++;

    g_nvista = n;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Lo sfogliatore
 *
 * ! UNA WRITE PER SCHERMATA. Una write per riga sono venticinque syscall
 * a ogni tasto, e su hardware vero si vedono passare: il testo arriva a
 * scaglioni invece che tutto insieme. Stesso motivo per cui gf_term.c
 * tiene un frame ombra — qui basta molto meno, perche' si ridisegna solo
 * quando si preme qualcosa.
 * ───────────────────────────────────────────────────────────────────────────── */

#define USCITA_MAX  (VIDEO_RIGHE * (VIDEO_COL + 16) + 256)
static char g_out[USCITA_MAX];
static int  g_outlen;

static void o_s(const char *s)
{
    while (*s && g_outlen < USCITA_MAX - 1) g_out[g_outlen++] = *s++;
}

static void o_riempi(int quante)
{
    while (quante-- > 0 && g_outlen < USCITA_MAX - 1) g_out[g_outlen++] = ' ';
}

/* Una barra: nero su grigio, riempita fino al bordo. Senza il riempimento
 * sarebbe lunga quanto il testo e sembrerebbe un errore di disegno.
 *
 * ! SI FERMA A 79 COLONNE, E L'ULTIMA VA LASCIATA STARE. Scrivere
 * nell'ottantesima colonna dell'ultima riga fa avanzare il cursore oltre
 * il bordo: il VGA va a capo e SCORRE LO SCHERMO DI UNA RIGA. Il sintomo
 * era che la barra del titolo spariva in cima a ogni ridisegno — sembrava
 * che non venisse disegnata, mentre veniva disegnata e subito buttata
 * fuori dallo schermo dalla riga scritta per ultima.
 *
 * ! E I COLORI SONO ESPLICITI, non ESC[7m. Il video inverso non e' fra
 * i codici che vga.c riconosce (0, 1, 30-37, 39, 40-47, 49, 90-97): la
 * sequenza passava senza fare niente e le barre uscivano identiche al
 * testo. Non e' un difetto del parser — il reverse non ha un attributo
 * VGA suo, si ottiene scambiando i colori, ed e' quello che si fa qui. */
#define BARRA_COL   (VIDEO_COL - 1)

static void o_barra(const char *s)
{
    int l = (int)strlen(s);

    if (l > BARRA_COL) l = BARRA_COL;

    o_s("\x1B[30;47m");
    {
        int i;
        for (i = 0; i < l && g_outlen < USCITA_MAX - 1; i++) g_out[g_outlen++] = s[i];
    }
    o_riempi(BARRA_COL - l);
    o_s("\x1B[0m");
}

static int g_kbd = -1;
static unsigned g_console = 0;

static void kbd_modo(unsigned modo)
{
    KbdSetMode m;

    if (g_kbd <= 0) return;
    m.modo    = modo;
    m.console = g_console;
    ipc_send((unsigned)g_kbd, KBD_MSG_SETMODE, &m, sizeof(m));
}

/* Il prossimo tasto, 0 se il servizio non risponde piu'.
 *
 * ! SI RIAFFERMA LA MODALITA' A OGNI SCADENZA, non si rinuncia. Chi si
 * ferma qualche secondo a leggere — che qui e' la cosa normale, non
 * l'eccezione — perderebbe la modalita' raw e i tasti successivi
 * finirebbero in una riga che nessuno legge. Stessa struttura di
 * bin/sh/shell.c e bin/login/login.c. */
static unsigned tasto(void)
{
    IpcMessage meta;
    unsigned   k = 0;
    int        i;

    if (g_kbd <= 0) return 0;

    for (;;) {
        if (ipc_send((unsigned)g_kbd, KBD_MSG_READKEY,
                     &g_console, sizeof(g_console)) < 0) return 0;

        for (i = 0; i < 8; i++) {
            if (ipc_recv_timeout(&meta, &k, sizeof(k), 2000) < 0) break;
            if ((int)meta.sender_pid != g_kbd) continue;
            if (meta.tipo == KBD_MSG_KEY && meta.len >= sizeof(k)) return k;
        }
        kbd_modo(KBD_MODE_RAW);
    }
}

static void disegna(const char *titolo, int alto)
{
    char barra[VIDEO_COL + 1];
    char intest[VIDEO_COL + 1];
    int  i;
    int  ultima = alto + FINESTRA;

    if (ultima > g_nvista) ultima = g_nvista;

    g_outlen = 0;
    o_s("\x1B[2J\x1B[H");

    /* ! SOLO ASCII IN CIO' CHE VA A VIDEO. Lo schermo e' in code page
     * 437 e non conosce UTF-8: un trattino lungo diventa due glifi
     * casuali, e siccome sono due BYTE il conto delle colonne salta —
     * quindi non e' solo brutto, sposta anche il riempimento della barra.
     * Vale per questo file e per /boot/help.txt. */
    snprintf(intest, sizeof(intest), " EX-OS - aiuto:  %s", titolo);
    o_barra(intest);
    o_s("\r\n");

    /* ! SI TRONCA A 79 COLONNE. Una riga larga esattamente quanto lo
     * schermo manda il cursore a capo da sola, e il "\r\n" che segue ne
     * aggiunge un secondo: una riga vuota in mezzo al testo e tutto il
     * resto della pagina spostato in giu' di uno. Stessa ragione per cui
     * la barra si ferma prima del bordo. */
    for (i = alto; i < ultima; i++) {
        const char *r = g_vista[i];
        int         j;

        for (j = 0; j < BARRA_COL && r[j] && g_outlen < USCITA_MAX - 1; j++)
            g_out[g_outlen++] = r[j];
        o_s("\r\n");
    }
    /* Le righe che mancano si emettono vuote: senza, la barra di stato
     * risalirebbe verso l'alto sull'ultima pagina e cambierebbe posto a
     * ogni scorrimento. */
    for (i = ultima - alto; i < FINESTRA; i++) o_s("\r\n");

    if (g_nvista <= FINESTRA) {
        snprintf(barra, sizeof(barra),
                 " %d righe   q per uscire", g_nvista);
    } else {
        snprintf(barra, sizeof(barra),
                 " righe %d-%d di %d   su/giu scorri  PgSu/PgGiu pagina  "
                 "Inizio/Fine  q esce",
                 alto + 1, ultima, g_nvista);
    }
    o_barra(barra);

    write(1, g_out, (size_t)g_outlen);
}

static void sfoglia(const char *titolo)
{
    int alto = 0;
    int max  = g_nvista - FINESTRA;

    if (max < 0) max = 0;

    for (;;) {
        unsigned ev, k;

        disegna(titolo, alto);

        ev = tasto();
        if (ev == 0) break;             /* il servizio e' sparito */
        k = ev & KBD_KEY_MASK;

        if (k == 'q' || k == 'Q' || k == 27u) break;

        if (k == KBD_K_DOWN)        alto++;
        else if (k == KBD_K_UP)     alto--;
        else if (k == KBD_K_PGDN || k == ' ') alto += FINESTRA;
        else if (k == KBD_K_PGUP)   alto -= FINESTRA;
        else if (k == KBD_K_HOME)   alto = 0;
        else if (k == KBD_K_END)    alto = max;
        else if (k == '\n' || k == '\r') alto++;

        if (alto > max) alto = max;
        if (alto < 0)   alto = 0;
    }

    /* La console si restituisce com'era: cooked, colori di sistema,
     * schermo pulito. Il prompt della shell non deve ereditare il video
     * inverso della barra. */
    write(1, "\x1B[0m\x1B[2J\x1B[H", 11);
}

/* 0 se la tastiera in raw e' utilizzabile su questa console. */
static int raw_possibile(void)
{
    ConsoleInfo ci;
    int         pid;

    if (console_info(&ci) == 0) {
        g_console = ci.mia;

        /* ! IN BACKGROUND NON SI PRENDONO I TASTI. La strada dell'IPC non
         * passa da sys_read, quindi la guardia che il kernel mette sullo
         * stdin dei processi in background qui non arriva: `help &`
         * diventerebbe l'ultimo ad aver chiesto un tasto — e il driver
         * serve l'ultimo — lasciando la shell bloccata su una riga che
         * non arriverebbe mai. Stessa verifica di bin/gfedit/gf_term.c. */
        if (ci.fg != 0 && ci.fg != (unsigned)getpid()) return -1;
        if (ci.mia != ci.visibile) return -1;
    }

    pid = ipc_lookup(KBD_SERVICE_NAME);
    if (pid <= 0) return -1;
    g_kbd = pid;
    return 0;
}

/* Il ripiego: tutto di seguito, come farebbe `cat`. */
static void riversa(void)
{
    int i;

    for (i = 0; i < g_nvista; i++) printf("%s\n", g_vista[i]);
}

static void mostra(const char *titolo)
{
    if (raw_possibile() != 0) { riversa(); return; }

    tty_raw(1);
    kbd_modo(KBD_MODE_RAW);
    sfoglia(titolo);
    kbd_modo(KBD_MODE_COOKED);
    tty_raw(0);
}


/* ─────────────────────────────────────────────────────────────────────────────
 * main
 * ───────────────────────────────────────────────────────────────────────────── */

static void non_trovato(const char *nome)
{
    int i, col = 0;

    printf("help: non c'e' un blocco '%s'.\n\n", nome);
    printf("Quelli che ci sono:\n\n  ");
    for (i = 0; i < g_nblocchi; i++) {
        int l = (int)strlen(g_blocco[i].primo);

        if (col + l + 2 > 76) { printf("\n  "); col = 0; }
        printf("%s  ", g_blocco[i].primo);
        col += l + 2;
    }
    printf("\n\n`help` da solo mostra l'indice con le descrizioni.\n");
}

int main(int argc, char **argv)
{
    int tutto = 0;
    int i, primo = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0)      tutto = 1;
        else if (argv[i][0] != '-' && !primo) primo = i;
    }

    if (carica() != 0) {
        printf("help: non riesco a leggere %s\n\n", FILE_AIUTO);
        printf("Il testo dell'aiuto e' un file, non e' dentro i programmi.\n");
        printf("Sul floppy sta in /boot; se manca, l'immagine e' incompleta.\n");
        return 1;
    }

    if (g_nblocchi == 0) {
        printf("help: %s non contiene nessun blocco.\n", FILE_AIUTO);
        printf("Un blocco comincia con una riga tipo  [ls]  in prima colonna.\n");
        return 1;
    }

    /* -t: tutto il file di seguito. Serve a leggerlo da un'altra macchina
     * (`help -t > /disco/aiuto.txt`) e a guardarlo quando lo sfogliatore
     * non e' quello che si vuole. */
    if (tutto) {
        for (i = 0; i < g_nrighe && i < RIGHE_MAX; i++) printf("%s\n", g_riga[i]);
        return 0;
    }

    if (primo == 0) {
        vista_indice();
        mostra("indice");
        return 0;
    }

    for (i = 0; i < g_nblocchi; i++) {
        if (combacia(&g_blocco[i], argv[primo])) {
            vista_blocco(&g_blocco[i]);
            mostra(g_blocco[i].primo);
            return 0;
        }
    }

    non_trovato(argv[primo]);
    return 1;
}
