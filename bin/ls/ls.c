/* =============================================================================
 * bin/ls/ls.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Elenca il contenuto di una directory.
 *
 *   ls              la directory corrente
 *   ls /bin         una directory qualunque
 *   ls -mc /bin     a colonne
 *   ls -md -p /bin  dettagliato, una pagina per volta
 *
 * -----------------------------------------------------------------------------
 * ! `-d` QUI NON E' IL `-d` DI POSIX
 *
 * Su Unix `ls -d` significa «mostra la directory, non il suo contenuto».
 * Qui significa «mostra i dettagli»: dimensione, data e ora. E' la scelta
 * di questo progetto, e viene detta nell'aiuto perche' chi arriva da Unix
 * si aspetta l'altra cosa e non deve scoprirlo per tentativi.
 *
 * -----------------------------------------------------------------------------
 * ! COSA VUOL DIRE "NASCOSTO" QUI
 *
 * Senza `-a` si nascondono i nomi che cominciano con un punto — compresi
 * `.` e `..`, come fa `ls` ovunque. Prima di agosto 2026 questo comando li
 * mostrava sempre: e' un cambiamento di comportamento, ed e' voluto.
 *
 * ! SU UN CD `.` E `..` NON CI SONO NEMMENO CON `-a`, e non e' un difetto
 * di questo programma: il driver ISO 9660 non li consegna affatto (in ISO
 * non sono nemmeno chiamati cosi' — sono due record con nome 0x00 e 0x01,
 * e kernel/fs/iso9660.c li salta). Su FAT e ext2 ci sono e `-a` li mostra.
 *
 * NON si guarda il bit «nascosto» di FAT (0x02). Farlo vorrebbe dire una
 * statraw() per OGNI voce, anche quando si stampano solo i nomi, e su un
 * floppy quello si sente. Il bit c'e' e si VEDE con `-md`, dove il file
 * viene comunque interrogato; semplicemente non nasconde niente. Una
 * regola sola, valida su FAT, ext2 e ISO 9660 — che di quel bit non hanno
 * nemmeno l'equivalente — e' meglio di due regole diverse secondo il
 * filesystem e secondo l'opzione.
 * ============================================================================= */

#include "libc.h"

/* Il blocco per chiamata e' LISTDIR_MAX_BATCH e non un numero scelto qui:
 * il kernel non ne consegna di piu' comunque, e chiederne 32 per poi
 * fermarsi appena ne tornano meno di 32 vorrebbe dire fermarsi alla prima
 * pagina. Con i nomi lunghi ogni voce pesa 264 byte, quindi il blocco e'
 * anche cio' che decide quanto stack usa questo programma. */
#define BLOCCO LISTDIR_MAX_BATCH

/* =============================================================================
 * ! LA CONSOLE E' 80x25, MA UN TERMINALE NO — E DAL 18 AGOSTO 2026 SI CHIEDE.
 *
 * Qui c'erano due #define, con accanto scritto che «non c'e' una syscall per
 * chiederlo». Adesso c'e', e serve davvero: dentro uno pseudo-terminale — una
 * finestra che si ridimensiona, una sessione SSH o telnet — la misura la sa il
 * pty. Chi non gliela chiede impagina per 80 colonne dentro una finestra larga
 * 120: le colonne restano strette a sinistra e mezzo schermo resta vuoto.
 *
 * ! E SI RILEGGE A OGNI AVVIO, non una volta per sempre: fra un `ls` e il
 * successivo la finestra puo' essere cambiata di misura.
 *
 * ! FUORI DA UN pty RESTA 80x25, e non e' un ripiego: la console vera di EX-OS
 * e' quella, e PTY_CTL_LEGGI_MISURA rende -ENOTTY proprio per dire «non sono
 * un pty». Un valore inventato sarebbe peggio del valore noto.
 * ============================================================================= */
static int g_col_schermo = 80;
static int g_righe_schermo = 25;

#define COLONNE_SCHERMO  g_col_schermo
#define RIGHE_SCHERMO    g_righe_schermo

static void misura_del_terminale(void)
{
    int m = pty_ctl(0, PTY_CTL_LEGGI_MISURA, 0);

    if (m < 0) return;                  /* non e' un pty: la console e' 80x25 */

    /* ! UNA MISURA ASSURDA SI IGNORA. Un terminale di due colonne non e' un
     * terminale, ed e' il genere di numero che arriva da un client che non lo
     * sa davvero: dividere per un valore cosi' darebbe una riga per voce e
     * nessuna colonna. */
    if ((m & 0xFFFF) >= 20)  g_col_schermo   = m & 0xFFFF;
    if ((m >> 16)   >= 4)    g_righe_schermo = (int)((unsigned)m >> 16);
}

/* Modi di visualizzazione. Si escludono a vicenda: vince l'ultimo
 * indicato, che e' il comportamento che ci si aspetta quando si ripete
 * un'opzione per correggersi. */
#define MODO_SEMPLICE   0   /* nome e dimensione, una voce per riga */
#define MODO_COLONNE    1   /* -mc */
#define MODO_DETTAGLI   2   /* -d  */
#define MODO_DIR        3   /* -md */

/* Attributi in convenzione FAT (vedi Stat in libc.h: valgono su tutti i
 * filesystem, perche' sono quelli che i programmi gia' interpretano). */
#define ATTR_SOLA_LETT  0x01
#define ATTR_NASCOSTO   0x02
#define ATTR_SISTEMA    0x04
#define ATTR_ETICHETTA  0x08
#define ATTR_DIRECTORY  0x10
#define ATTR_ARCHIVIO   0x20

static int g_modo       = MODO_SEMPLICE;
static int g_tutti      = 0;    /* -a */
static int g_pagine     = 0;    /* -p */
static int g_righe      = 0;    /* righe stampate da inizio pagina */
static int g_interrotto = 0;    /* l'utente ha chiesto di smettere */

/* =============================================================================
 * Aiuto
 * ============================================================================= */
static void aiuto(void)
{
    printf("uso: ls [opzioni] [percorso]\n\n");
    printf("Elenca il contenuto di una directory. Senza percorso, quella corrente.\n\n");
    printf("Modi di visualizzazione (l'ultimo indicato vince):\n");
    printf("  -mc     a colonne: solo i nomi, il piu' compatto\n");
    printf("  -d      dettagli: dimensione, data e ora\n");
    printf("  -md     dettagliato stile dir: aggiunge gli attributi\n");
    printf("          (senza nessuno di questi: nome e dimensione)\n\n");
    printf("Altre opzioni:\n");
    printf("  -a      mostra anche i nomi che cominciano con un punto\n");
    printf("          (su un CD `.` e `..` non ci sono comunque: ISO 9660\n");
    printf("           non li consegna)\n");
    printf("  -p      una pagina per volta (Invio avanza, q smette)\n");
    printf("  -h      questo aiuto (anche -help e --help)\n\n");
    printf("Attributi mostrati da -md:\n");
    printf("  D directory   A archivio   S sistema   N nascosto   R sola lettura\n\n");
    printf("ATTENZIONE: -d QUI SIGNIFICA \"DETTAGLI\", non quello che significa\n");
    printf("su Unix (dove `ls -d` mostra la directory invece del contenuto).\n\n");
    printf("Il bit \"nascosto\" di FAT si VEDE con -md ma non nasconde niente:\n");
    printf("a nascondere e' il punto iniziale, che e' la convenzione di tutti\n");
    printf("i filesystem che EX-OS monta.\n\n");
    printf("Esempi:\n");
    printf("  ls -mc /bin        i nomi di /bin, a colonne\n");
    printf("  ls -md -p /        tutto su /, dettagliato, una pagina per volta\n");
    printf("  ls -a -d           la directory corrente, dettagli, nascosti compresi\n");
}

/* =============================================================================
 * Impaginazione
 *
 * ! SI LEGGE UNA RIGA, NON UN TASTO. Leggere un singolo tasto vorrebbe
 * dire parlare al servizio 'kbd' in modalita' raw — come fa /bin/gfedit —
 * e quella strada porta con se' l'obbligo di rimettere a posto la console
 * anche quando il programma muore male. Per una pausa fra due pagine non
 * vale il prezzo: Invio va benissimo.
 *
 * ! LA PAUSA STA PRIMA DELLA RIGA, NON DOPO, e la differenza si vede
 * sull'ultima pagina. Contando le righe stampate ci si ferma appena il
 * conto arriva a schermo pieno — anche quando quella era l'ultima voce, e
 * allora si chiede «Invio per continuare» per continuare con niente.
 * Chiedendo invece PRIMA di stampare la riga successiva, la domanda si fa
 * solo se una riga successiva c'e' davvero.
 * ============================================================================= */
static void prima_di_stampare(void)
{
    char r[16];
    int  n;

    if (!g_pagine || g_interrotto) return;

    if (g_righe < RIGHE_SCHERMO - 2) { g_righe++; return; }

    printf("-- Invio per continuare, q per smettere --");
    n = (int)read(0, r, sizeof(r) - 1);
    if (n < 0) n = 0;
    r[n] = '\0';
    if (r[0] == 'q' || r[0] == 'Q') g_interrotto = 1;

    g_righe = 1;
}

/* =============================================================================
 * Percorsi e dati del file
 * ============================================================================= */
static void componi(char *dst, unsigned int max, const char *dir, const char *nome)
{
    unsigned int i = 0, j;

    for (j = 0; dir[j] && i + 1 < max; j++) dst[i++] = dir[j];
    /* Una barra sola: se la directory e' "/" ce l'ha gia'. */
    if (i > 0 && dst[i - 1] != '/' && i + 1 < max) dst[i++] = '/';
    for (j = 0; nome[j] && i + 1 < max; j++) dst[i++] = nome[j];
    dst[i] = '\0';
}

/* Scrive "AAAA-MM-GG  HH:MM" in `buf`, oppure dei trattini se la data non
 * c'e'. Un filesystem che non tiene le date darebbe 1970-01-01 00:00, che
 * sembra una data vera e non lo e'. */
static void data_ora(char *buf, unsigned int max, time_t t)
{
    struct tm *tm;

    if (t != 0) {
        tm = localtime(&t);
        if (tm != NULL && strftime(buf, max, "%Y-%m-%d  %H:%M", tm) != 0) return;
    }
    strncpy(buf, "----------  --:--", max - 1);
    buf[max - 1] = '\0';
}

static void attributi(char *buf, unsigned short attr)
{
    buf[0] = (attr & ATTR_DIRECTORY) ? 'D' : '-';
    buf[1] = (attr & ATTR_ARCHIVIO)  ? 'A' : '-';
    buf[2] = (attr & ATTR_SISTEMA)   ? 'S' : '-';
    buf[3] = (attr & ATTR_NASCOSTO)  ? 'N' : '-';
    buf[4] = (attr & ATTR_SOLA_LETT) ? 'R' : '-';
    buf[5] = '\0';
}

/* Vero se la voce va mostrata. */
static int visibile(const DirEntry *e)
{
    if (g_tutti) return 1;
    return e->name[0] != '.';
}

/* =============================================================================
 * Le forme di stampa
 * ============================================================================= */
static void stampa_semplice(const DirEntry *e)
{
    prima_di_stampare();
    if (g_interrotto) return;

    if (e->is_dir) printf("%s/\n", e->name);
    else           printf("%-12s %u\n", e->name, e->size);
}

static void stampa_dettagli(const char *dir, const DirEntry *e, int con_attributi)
{
    char        percorso[320];
    char        quando[24];
    char        attr[8];
    struct stat st;
    Stat        raw;

    prima_di_stampare();
    if (g_interrotto) return;

    componi(percorso, sizeof(percorso), dir, e->name);

    /* ! SE stat() FALLISCE SI STAMPA LO STESSO. La voce esiste — ce l'ha
     * appena data la directory — e farla sparire perche' non se ne
     * conoscono i dettagli sarebbe il modo peggiore di reagire: chi guarda
     * penserebbe che il file non c'e'. Si stampano i trattini. */
    if (stat(percorso, &st) != 0) {
        memset(&st, 0, sizeof(st));
        st.st_size = (off_t)e->size;
    }
    data_ora(quando, sizeof(quando), st.st_mtime);

    if (con_attributi) {
        unsigned short a = e->is_dir ? ATTR_DIRECTORY : 0;

        if (statraw(percorso, &raw) == 0) a = raw.st_attr;
        attributi(attr, a);
        printf("%s  %s  ", quando, attr);
    } else {
        printf("%s  ", quando);
    }

    if (e->is_dir) printf("%10s  %s\n", "<DIR>", e->name);
    else           printf("%10ld  %s\n", (long)st.st_size, e->name);
}

/* =============================================================================
 * Percorso della directory
 *
 * ! SERVE DAVVERO PERCORRERLA DUE VOLTE, e solo per il modo a colonne: la
 * larghezza delle colonne dipende dal nome piu' lungo, che si conosce solo
 * dopo averli visti tutti. L'alternativa sarebbe tenere tutti i nomi in
 * memoria — con nomi da 255 byte e una directory grande sono centinaia di
 * kilobyte — oppure indovinare una larghezza fissa e sbagliarla.
 *
 * Nelle altre modalita' la prima passata si salta.
 * ============================================================================= */
typedef struct {
    unsigned int voci;
    unsigned int file;
    unsigned int dir;
    unsigned int byte;      /* somma delle dimensioni dei file */
    unsigned int max_nome;
} Riepilogo;

static int percorri(const char *dir, Riepilogo *r, int solo_conta, int larghezza)
{
    DirEntry voci[BLOCCO];
    int      inizio = 0, n, i;
    int      in_riga = 0;

    for (;;) {
        n = listdir_from(dir, voci, BLOCCO, inizio);
        if (n < 0) return -1;
        if (n == 0) break;

        for (i = 0; i < n && !g_interrotto; i++) {
            unsigned int len;

            if (!visibile(&voci[i])) continue;

            r->voci++;
            if (voci[i].is_dir) r->dir++;
            else { r->file++; r->byte += voci[i].size; }

            len = (unsigned int)strlen(voci[i].name) + (voci[i].is_dir ? 1u : 0u);
            if (len > r->max_nome) r->max_nome = len;

            if (solo_conta) continue;

            switch (g_modo) {
            case MODO_COLONNE: {
                char nome[300];

                strncpy(nome, voci[i].name, sizeof(nome) - 2);
                nome[sizeof(nome) - 2] = '\0';
                if (voci[i].is_dir) strcat(nome, "/");

                if (in_riga == 0) {
                    prima_di_stampare();
                    if (g_interrotto) break;
                }
                printf("%-*s", larghezza, nome);
                in_riga++;
                if ((in_riga + 1) * larghezza > COLONNE_SCHERMO) {
                    printf("\n");
                    in_riga = 0;
                }
                break;
            }
            case MODO_DETTAGLI: stampa_dettagli(dir, &voci[i], 0); break;
            case MODO_DIR:      stampa_dettagli(dir, &voci[i], 1); break;
            default:            stampa_semplice(&voci[i]);         break;
            }
        }

        inizio += n;
        if (n < BLOCCO) break;      /* ultima pagina */
        if (g_interrotto) break;
    }

    if (!solo_conta && g_modo == MODO_COLONNE && in_riga > 0) printf("\n");
    return 0;
}

/* =============================================================================
 * main
 * ============================================================================= */
int main(int argc, char **argv)
{
    char        cwd[256];
    const char *bersaglio = NULL;
    Riepilogo   r;
    int         i, larghezza = 0;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (a[0] != '-' || a[1] == '\0') {
            if (bersaglio == NULL) bersaglio = a;
            continue;
        }

        if (strcmp(a, "-h") == 0 || strcmp(a, "-help") == 0 ||
            strcmp(a, "--help") == 0 || strcmp(a, "-?") == 0) {
            aiuto();
            return 0;
        }
        if (strcmp(a, "-a")  == 0) { g_tutti  = 1;            continue; }
        if (strcmp(a, "-p")  == 0) { g_pagine = 1;            continue; }
        if (strcmp(a, "-mc") == 0) { g_modo = MODO_COLONNE;   continue; }
        if (strcmp(a, "-md") == 0) { g_modo = MODO_DIR;       continue; }
        if (strcmp(a, "-d")  == 0) { g_modo = MODO_DETTAGLI;  continue; }

        /* ! SI RIFIUTA UN'OPZIONE SCONOSCIUTA invece di ignorarla. Un
         * `ls -l` ignorato in silenzio stampa qualcosa di plausibile e fa
         * credere che l'opzione esista. */
        printf("ls: opzione sconosciuta '%s'\n", a);
        printf("    `ls -h` le elenca tutte.\n");
        return 1;
    }

    if (bersaglio == NULL) {
        if (!getcwd(cwd, sizeof(cwd))) { cwd[0] = '/'; cwd[1] = '\0'; }
        bersaglio = cwd;
    }

    misura_del_terminale();
    memset(&r, 0, sizeof(r));

    /* Prima passata: serve solo alle colonne. Vedi il commento sopra. */
    if (g_modo == MODO_COLONNE) {
        if (percorri(bersaglio, &r, 1, 0) != 0) {
            printf("ls: impossibile leggere '%s'\n", bersaglio);
            return 1;
        }
        larghezza = (int)r.max_nome + 2;
        if (larghezza > COLONNE_SCHERMO) larghezza = COLONNE_SCHERMO;
        memset(&r, 0, sizeof(r));
    }

    if (g_modo == MODO_DIR) {
        printf("data        ora    attr   dimensione  nome\n");
        if (g_pagine) g_righe++;    /* l'intestazione occupa una riga */
    }

    if (percorri(bersaglio, &r, 0, larghezza) != 0) {
        /* ! UN FILE NON E' UNA DIRECTORY, E NON E' UN ERRORE. listdir()
         * fallisce su qualunque percorso che non sia una directory, e
         * fino ad agosto 2026 `ls nome.txt` rispondeva «impossibile
         * leggere» — cioe' diceva che il file non si poteva leggere
         * mentre il file c'era benissimo. Ci sono cascato io stesso
         * verificando l'assemblatore nativo: `ls -d /prova.o` diceva di
         * no su un oggetto appena creato, e per un momento ho creduto che
         * fosse `as` a non aver scritto niente. */
        struct stat st;

        if (stat(bersaglio, &st) == 0 && !S_ISDIR(st.st_mode)) {
            DirEntry uno;
            const char *nome = bersaglio, *p;

            for (p = bersaglio; *p; p++) if (*p == '/') nome = p + 1;

            memset(&uno, 0, sizeof(uno));
            strncpy(uno.name, nome, sizeof(uno.name) - 1);
            uno.size   = (unsigned int)st.st_size;
            uno.is_dir = 0;

            /* Il percorso della DIRECTORY che lo contiene: serve a
             * stampa_dettagli per ricomporlo. */
            {
                char dir[320];
                unsigned int k = (unsigned int)(nome - bersaglio);

                if (k == 0) { dir[0] = '.'; dir[1] = '\0'; }
                else {
                    if (k > sizeof(dir) - 1) k = sizeof(dir) - 1;
                    memcpy(dir, bersaglio, k);
                    /* la barra finale la rimette componi() */
                    while (k > 1 && dir[k-1] == '/') k--;
                    dir[k] = '\0';
                }

                /* L'intestazione l'ha gia' stampata il chiamante prima di
                 * percorri(): rifarla qui la mostrerebbe due volte. */
                switch (g_modo) {
                case MODO_COLONNE:  printf("%s\n", uno.name);              break;
                case MODO_DETTAGLI: stampa_dettagli(dir, &uno, 0);         break;
                case MODO_DIR:      stampa_dettagli(dir, &uno, 1);         break;
                default:            stampa_semplice(&uno);                 break;
                }
            }
            return 0;
        }

        printf("ls: impossibile leggere '%s'\n", bersaglio);
        return 1;
    }

    if (g_interrotto) return 0;

    if (r.voci == 0) {
        /* Due situazioni diverse, e vanno distinte: una directory vuota e
         * una piena di soli nomi nascosti si somigliano troppo. */
        if (!g_tutti) printf("(vuota, o solo nomi nascosti: prova `ls -a`)\n");
        else          printf("(vuota)\n");
        return 0;
    }

    if (g_modo == MODO_DETTAGLI || g_modo == MODO_DIR)
        printf("\n%u file, %u byte    %u directory\n", r.file, r.byte, r.dir);

    return 0;
}
