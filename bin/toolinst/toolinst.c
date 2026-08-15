/* =============================================================================
 * bin/toolinst/toolinst.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Installa su disco gli strumenti di sviluppo del CD tools, e li rende
 * operativi.
 *
 *     toolinst                  installa nel sistema in esecuzione
 *     toolinst /disco           installa nel sistema montato in /disco
 *     toolinst -a               AGGIORNA: solo cio' che sul CD e' cambiato
 *     toolinst -n               mostra cosa farebbe, non copia niente
 *     toolinst -y               non chiede niente (per gli script)
 *     toolinst -s /cdrom        da dove prendere l'albero
 *     toolinst -p /exos         che percorso avra' l'albero all'avvio
 *
 * -----------------------------------------------------------------------------
 * ! IL PUNTO DI TUTTO: GCC E FBC SI CERCANO ADDOSSO, NON NEL PATH
 *
 * Sia il driver di GCC sia `fbc` calcolano il proprio prefisso da DOVE STA
 * IL LORO BINARIO — `dirname(argv[0])/..` — e da li' vanno a prendere
 * tutto il resto. Si vede a occhio nudo nella traccia di `fbc -v`:
 *
 *     assembling: /cdrom/exos/bin/../bin/as --32 ...
 *                 /cdrom/exos/bin/../lib/gcc/i386-exos/17.0.0/libgcc.a
 *
 * La conseguenza e' che copiare i binari in /bin e aggiungere una voce al
 * PATH NON BASTA, e non e' un dettaglio di configurazione: e' la
 * differenza fra un compilatore che funziona e uno che non parte.
 *
 *     /cdrom/bin/gcc -c prova.c        -> cannot execute 'cc1'
 *     /cdrom/exos/bin/gcc -c prova.c   -> compila
 *
 * Stesso binario, stesso PATH, stessa riga di comando. Cambia solo da dove
 * e' lanciato. Da /bin/gcc il prefisso diventa `/` e si cercano
 * /libexec/gcc/..., /lib/gcc/..., /include/freebasic: non esistono, e il
 * messaggio che ne esce parla di header mancanti mentre il difetto e' nel
 * percorso.
 *
 * Percio' questo programma copia L'ALBERO INTERO conservandone la forma, e
 * mette nel PATH la sua bin — non i binari sciolti da qualche altra parte.
 *
 * -----------------------------------------------------------------------------
 * ! SI TOGLIE, NON SI ELENCA
 *
 * I gruppi opzionali (C++, FreeBASIC, OpenSSL) sono definiti dai percorsi
 * da SALTARE quando non si vogliono, non dai file da copiare quando si
 * vogliono. La differenza conta il giorno che sul CD compare un file
 * nuovo: con un elenco di cose da copiare resterebbe indietro in silenzio,
 * e nessuno collegherebbe il link fallito di sei mesi dopo a questo file.
 * Cosi' invece finisce sul disco insieme al resto, e l'unico modo di
 * perderlo e' averlo scritto qui dentro.
 *
 * Il gruppo C non si sceglie: cc1plus, fbc e libcrypto passano tutti da
 * `as` e `ld` per arrivare a un eseguibile, e un C++ senza assemblatore e'
 * un compilatore che compila e non produce niente.
 *
 * -----------------------------------------------------------------------------
 * ! MOSTRA PRIMA, CHIEDE POI, SCRIVE PER ULTIMO
 *
 * La stessa forma di `install -a` e di `hwconfig`. Qui in piu' c'e' che si
 * copiano centinaia di megabyte su un disco che potrebbe non averli: il
 * conto dei file e dei byte si fa PRIMA, con una passata a vuoto, cosi'
 * chi risponde sa cosa sta accettando.
 * ============================================================================= */

#include "libc.h"

#define PERC_MAX   320
#define BLOCCO     4096
#define RIGA_MAX   256
#define CFG_MAX    16384

static char buf[BLOCCO];

static int  opt_n = 0;          /* guarda e basta */
static int  opt_y = 0;          /* non chiedere */
static int  opt_a = 0;          /* aggiorna: copia solo cio' che e' cambiato */

static long n_file = 0, n_dir = 0, n_byte = 0, n_errori = 0;
static long n_uguali = 0, n_nuovi = 0;

/* ─────────────────────────────────────────────────────────────────────────────
 * Il catalogo
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * ! LO PORTA IL CD, NON QUESTO BINARIO. Fino ad agosto 2026 i gruppi erano
 * tre liste di percorsi scritte qui dentro: mettere uno strumento nuovo sul
 * CD voleva dire ricompilare l'installatore, e finche' non lo si faceva
 * quello strumento c'era e non si poteva scegliere. Adesso il CD si
 * descrive da se' in `exos/strumenti.txt`, che documenta anche il proprio
 * formato.
 *
 * ! SENZA CATALOGO SI USA QUELLO DI SCORTA, e non e' una gentilezza: i CD
 * gia' masterizzati non ce l'hanno, e un installatore che si rifiuta di
 * lavorare su un disco valido sarebbe un peggioramento. Vedi
 * catalogo_di_scorta().
 * ───────────────────────────────────────────────────────────────────────── */

#define GRUPPI_MAX   12
#define SOLO_MAX     10
#define NOMI_MAX     4
#define NOTE_MAX     8
#define ID_MAX       24
#define ETICH_MAX    48
#define DICE_MAX     96
#define VOCE_MAX     96

typedef struct {
    char id[ID_MAX];                 /* "cpp", dalle quadre */
    char nome[ETICH_MAX];            /* "C++" */
    char dice[DICE_MAX];             /* la riga di descrizione */
    char prova[VOCE_MAX];            /* percorso che ne attesta la presenza */
    char vuole[ID_MAX];              /* id del gruppo da cui dipende */
    long mbyte;
    int  sempre;                     /* non si puo' togliere */

    char solo[SOLO_MAX][VOCE_MAX];   /* percorsi suoi e di nessun altro */
    int  n_solo;
    char file[NOMI_MAX][ID_MAX];     /* nomi di file, ovunque si trovino */
    int  n_file;
    char nota[NOTE_MAX][VOCE_MAX];   /* avvertimenti da stampare */
    int  n_note;

    int  c_e;                        /* trovato davvero sul CD */
    int  voluto;
} Gruppo;

static Gruppo g_gruppo[GRUPPI_MAX];
static int    g_ngruppi = 0;
static int    g_da_catalogo = 0;     /* 0 = catalogo di scorta */

/* Dove cercare l'albero quando -s non lo dice. Come per i driver, non e'
 * un elenco di contenuti: e' un elenco di posti dove guardare. */
static const char *sorgenti_pred[] = { "/cdrom", "/", NULL };


/* ─────────────────────────────────────────────────────────────────────────────
 * Utilita'
 * ───────────────────────────────────────────────────────────────────────────── */

static void unisci(char *out, const char *a, const char *b)
{
    int i = 0, j;

    for (j = 0; a[j] && i < PERC_MAX - 2; j++) out[i++] = a[j];
    if (i > 0 && out[i - 1] != '/') out[i++] = '/';
    for (j = 0; b[j] && i < PERC_MAX - 1; j++) out[i++] = b[j];
    out[i] = '\0';
}

static int e_dir(const char *p)
{
    struct stat st;
    return (stat(p, &st) == 0 && S_ISDIR(st.st_mode));
}

static int esiste(const char *p)
{
    return (access(p, F_OK) == 0);
}

/* Confronto senza distinzione fra maiuscole e minuscole: i nomi arrivano
 * da ISO 9660, che secondo il livello puo' renderli in maiuscolo, e una
 * regola di esclusione che salta "include/c++" ma non "INCLUDE/C++"
 * copierebbe 24 MB che l'utente aveva detto di non volere. */
static int uguale_i(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a++, cb = *b++;

        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
    }
    return (*a == '\0' && *b == '\0');
}

static void copia_str(char *dst, const char *src, int max)
{
    int i = 0;

    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* `voce` compare in `elenco`, che e' separato da virgole? Serve a -g. */
static int in_elenco(const char *elenco, const char *voce)
{
    const char *p = elenco;

    while (*p) {
        const char *fine = p;
        char        pezzo[ID_MAX];
        int         l = 0;

        while (*fine && *fine != ',') fine++;
        while (p < fine && l < ID_MAX - 1) pezzo[l++] = *p++;
        pezzo[l] = '\0';
        if (uguale_i(pezzo, voce)) return 1;
        p = (*fine == ',') ? fine + 1 : fine;
    }
    return 0;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Il catalogo: lettura di exos/strumenti.txt
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * Formato e chiavi sono documentati nel file stesso. Qui vale la pena
 * ripetere solo la regola che tiene insieme le versioni:
 *
 * ! UNA CHIAVE SCONOSCIUTA SI IGNORA, NON E' UN ERRORE. Un catalogo
 * scritto per un toolinst piu' nuovo deve restare leggibile da uno piu'
 * vecchio: la parte che capisce la usa, il resto lo lascia stare. Il
 * contrario — rifiutarsi di leggere — trasformerebbe l'aggiunta di una
 * chiave in un CD che non si installa piu'.
 * ───────────────────────────────────────────────────────────────────────── */

static Gruppo *gruppo_per_id(const char *id)
{
    int i;

    for (i = 0; i < g_ngruppi; i++)
        if (uguale_i(g_gruppo[i].id, id)) return &g_gruppo[i];
    return NULL;
}

/* Toglie spazi e tabulazioni davanti e dietro, sul posto. */
static char *ripulisci(char *s)
{
    int l;

    while (*s == ' ' || *s == '\t') s++;
    l = (int)strlen(s);
    while (l > 0 && (s[l-1] == ' ' || s[l-1] == '\t' || s[l-1] == '\r')) s[--l] = '\0';
    return s;
}

/* "chiave = valore" -> rende il valore ripulito, o NULL se la riga non e'
 * quella chiave. */
static char *valore_se(char *riga, const char *chiave)
{
    char *p = riga;
    int   i = 0;

    while (*p == ' ' || *p == '\t') p++;
    while (chiave[i]) {
        char c = p[i];

        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != chiave[i]) return NULL;
        i++;
    }
    p += i;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=') return NULL;
    p++;
    return ripulisci(p);
}

/* Il catalogo che si usa quando il CD non ne porta uno. E' esattamente
 * quello che fino ad agosto 2026 stava scritto dentro da_saltare(). */
static void catalogo_di_scorta(void)
{
    Gruppo *g;
    int     k;

    /* ! SI AZZERA PRIMA. Ci si arriva anche DOPO un tentativo di lettura
     * andato a male a meta' — un catalogo che c'e' ma non contiene nessun
     * gruppo valido — e in quel caso le strutture portano gia' i pezzi
     * raccolti da quel giro: n_solo diverso da zero farebbe accodare i
     * percorsi di scorta a quelli letti, cioe' un catalogo che non ha mai
     * scritto nessuno. */
    for (k = 0; k < (int)sizeof(g_gruppo); k++) ((char *)g_gruppo)[k] = 0;

    g_ngruppi = 0;
    g_da_catalogo = 0;

    g = &g_gruppo[g_ngruppi++];
    copia_str(g->id, "base", ID_MAX);
    copia_str(g->nome, "C", ETICH_MAX);
    copia_str(g->dice, "gcc, cpp, as, ld, la libc e gli header", DICE_MAX);
    copia_str(g->prova, "bin/gcc", VOCE_MAX);
    g->sempre = 1;
    g->mbyte  = 0;

    g = &g_gruppo[g_ngruppi++];
    copia_str(g->id, "cpp", ID_MAX);
    copia_str(g->nome, "C++", ETICH_MAX);
    copia_str(g->dice, "cc1plus, libstdc++", DICE_MAX);
    copia_str(g->prova, "bin/g++", VOCE_MAX);
    copia_str(g->vuole, "base", ID_MAX);
    copia_str(g->solo[g->n_solo++], "bin/g++", VOCE_MAX);
    copia_str(g->solo[g->n_solo++], "include/c++", VOCE_MAX);
    copia_str(g->solo[g->n_solo++], "lib/libstdc++.a", VOCE_MAX);
    copia_str(g->file[g->n_file++], "cc1plus", ID_MAX);

    g = &g_gruppo[g_ngruppi++];
    copia_str(g->id, "fb", ID_MAX);
    copia_str(g->nome, "FreeBASIC", ETICH_MAX);
    copia_str(g->dice, "fbc, libfb e i .bi", DICE_MAX);
    copia_str(g->prova, "bin/fbc", VOCE_MAX);
    copia_str(g->vuole, "base", ID_MAX);
    copia_str(g->solo[g->n_solo++], "bin/fbc", VOCE_MAX);
    copia_str(g->solo[g->n_solo++], "include/freebasic", VOCE_MAX);
    copia_str(g->solo[g->n_solo++], "lib/freebasic", VOCE_MAX);

    g = &g_gruppo[g_ngruppi++];
    copia_str(g->id, "ssl", ID_MAX);
    copia_str(g->nome, "OpenSSL", ETICH_MAX);
    copia_str(g->dice, "libcrypto e i suoi header", DICE_MAX);
    copia_str(g->prova, "lib/libcrypto.a", VOCE_MAX);
    copia_str(g->vuole, "base", ID_MAX);
    copia_str(g->solo[g->n_solo++], "lib/libcrypto.a", VOCE_MAX);
    copia_str(g->solo[g->n_solo++], "include/openssl", VOCE_MAX);
}

/* Rende 0 se ha letto un catalogo dal CD. */
static int leggi_catalogo(const char *albero)
{
    static char testo[16384];
    char   perc[PERC_MAX];
    int    fd, n, i;
    Gruppo *g = NULL;

    unisci(perc, albero, "strumenti.txt");
    fd = open(perc, O_RDONLY);
    if (fd < 0) return -1;

    n = (int)read(fd, testo, sizeof(testo) - 1);
    close(fd);
    if (n <= 0) return -1;
    testo[n] = '\0';

    g_ngruppi = 0;
    i = 0;
    while (i < n) {
        char  riga[RIGA_MAX];
        char *r, *v;
        int   l = 0;

        while (i < n && testo[i] != '\n' && l < RIGA_MAX - 1) riga[l++] = testo[i++];
        riga[l] = '\0';
        while (i < n && testo[i] != '\n') i++;      /* riga piu' lunga: si tronca */
        if (i < n) i++;

        r = ripulisci(riga);
        if (r[0] == '\0' || r[0] == '#') continue;

        /* Intestazione di gruppo: [id] e nient'altro sulla riga. */
        if (r[0] == '[') {
            char *fine = r;
            int   k;

            while (*fine && *fine != ']') fine++;
            if (*fine != ']' || *ripulisci(fine + 1) != '\0') continue;
            *fine = '\0';

            if (g_ngruppi >= GRUPPI_MAX) {
                printf("  ! piu' di %d gruppi nel catalogo: ignoro il resto\n",
                       GRUPPI_MAX);
                break;
            }
            g = &g_gruppo[g_ngruppi++];
            for (k = 0; k < (int)sizeof(Gruppo); k++) ((char *)g)[k] = 0;
            copia_str(g->id, ripulisci(r + 1), ID_MAX);
            /* Il nome per esteso e' facoltativo: senza, si mostra l'id.
             * Meglio "[fb] si'" che una colonna vuota accanto a una
             * risposta — chi legge deve poter capire a cosa ha detto di
             * si'. */
            copia_str(g->nome, g->id, ETICH_MAX);
            continue;
        }

        if (g == NULL) continue;    /* chiavi prima del primo gruppo: nessuno */

        if ((v = valore_se(r, "nome"))  != NULL) { copia_str(g->nome, v, ETICH_MAX); continue; }
        if ((v = valore_se(r, "dice"))  != NULL) { copia_str(g->dice, v, DICE_MAX); continue; }
        if ((v = valore_se(r, "prova")) != NULL) { copia_str(g->prova, v, VOCE_MAX); continue; }
        if ((v = valore_se(r, "vuole")) != NULL) { copia_str(g->vuole, v, ID_MAX); continue; }
        if ((v = valore_se(r, "mbyte")) != NULL) { g->mbyte = atol(v); continue; }
        if ((v = valore_se(r, "sempre")) != NULL) {
            g->sempre = (v[0] == 's' || v[0] == 'S' || v[0] == '1');
            continue;
        }
        if ((v = valore_se(r, "solo")) != NULL) {
            if (g->n_solo < SOLO_MAX) copia_str(g->solo[g->n_solo++], v, VOCE_MAX);
            else printf("  ! %s: piu' di %d 'solo', ignoro '%s'\n", g->id, SOLO_MAX, v);
            continue;
        }
        if ((v = valore_se(r, "file")) != NULL) {
            if (g->n_file < NOMI_MAX) copia_str(g->file[g->n_file++], v, ID_MAX);
            else printf("  ! %s: piu' di %d 'file', ignoro '%s'\n", g->id, NOMI_MAX, v);
            continue;
        }
        if ((v = valore_se(r, "nota")) != NULL) {
            if (g->n_note < NOTE_MAX) copia_str(g->nota[g->n_note++], v, VOCE_MAX);
            continue;
        }
        /* Chiave sconosciuta: si ignora. Vedi il commento in testa. */
    }

    if (g_ngruppi == 0) return -1;
    g_da_catalogo = 1;
    return 0;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Il volume che riceve
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * ! SI GUARDA PRIMA DI COPIARE, e i due motivi valgono entrambi da soli.
 *
 * SOLA LETTURA — copiare 150 MB per scoprirlo al primo file e' tempo
 * buttato, ma soprattutto il messaggio che ne esce parla del file, non del
 * montaggio, e manda a cercare il difetto nel posto sbagliato.
 *
 * NOMI LUNGHI — questo e' il difetto che nessuno collegherebbe alla causa.
 * Su FAT la scrittura dei nomi lunghi non c'e' ancora (in lettura si', in
 * scrittura no): l'albero degli strumenti e' pieno di nomi che l'8.3 non
 * regge — `bits/stdc++.h`, `left_child_next_sibling_heap_`, `libstdc++.a`
 * — e su un volume FAT arriverebbero troncati. La copia riuscirebbe,
 * l'installazione sembrerebbe finita, e il compilatore risponderebbe che
 * non trova header che sul disco ci sono, con un altro nome. Su ext2 i
 * nomi arrivano interi.
 * ───────────────────────────────────────────────────────────────────────────── */

static const char *nome_fs(unsigned int fs)
{
    switch (fs) {
        case 2:  return "ext2";
        case 12: return "FAT12";
        case 16: return "FAT16";
        case 32: return "FAT32";
        default: return "sconosciuto";
    }
}

/* Il montaggio che regge `percorso`: quello il cui punto e' il prefisso
 * piu' lungo. Rende 0 se l'ha trovato. */
static int montaggio_di(const char *percorso, MountInfo *out)
{
    MountInfo m[16];
    int       n, i, migliore = -1, lung = -1;

    n = mountinfo(m, 16, 0);
    if (n <= 0) return -1;

    for (i = 0; i < n; i++) {
        int l = (int)strlen(m[i].punto);

        if (strncmp(percorso, m[i].punto, (unsigned int)l) != 0) continue;
        /* "/disco" regge "/disco/exos" ma non "/discone/exos": dopo il
         * prefisso ci vuole una barra o la fine della stringa. */
        if (l > 1 && percorso[l] != '\0' && percorso[l] != '/') continue;
        if (l > lung) { lung = l; migliore = i; }
    }
    if (migliore < 0) return -1;
    *out = m[migliore];
    return 0;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Le regole di esclusione
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * `rel` e' il percorso relativo alla radice dell'albero, con le barre:
 * "bin/g++", "include/c++", "lib/gcc/i386-exos/17.0.0/libgcc.a".
 *
 * ! SI CONFRONTA IL PERCORSO INTERO, non il solo nome. Una regola sul
 * nome "c++" salterebbe anche una futura "lib/c++"; una sul percorso dice
 * esattamente cosa esclude, e si legge accanto a cio' che esclude. */
static int da_saltare(const char *rel)
{
    const char *base = rel, *p;
    int         i, k;

    for (p = rel; *p; p++) if (*p == '/') base = p + 1;

    /* ! IL CATALOGO NON SI COPIA MAI. Descrive cosa c'e' SUL CD, e sul
     * disco descriverebbe una cosa che non esiste piu': l'albero installato
     * contiene solo i gruppi scelti, e un catalogo che ne elenca quattro
     * accanto a due farebbe cercare roba mai copiata. */
    if (uguale_i(rel, "strumenti.txt")) return 1;

    for (i = 0; i < g_ngruppi; i++) {
        const Gruppo *g = &g_gruppo[i];

        if (g->voluto) continue;

        for (k = 0; k < g->n_solo; k++)
            if (uguale_i(rel, g->solo[k])) return 1;

        /* Il confronto per NOME serve dove il percorso contiene la versione
         * di GCC — libexec/gcc/i386-exos/17.0.0/cc1plus — e quindi non si
         * puo' scrivere per intero nel catalogo: la versione cambia a ogni
         * ricostruzione della toolchain, il nome del file no. */
        for (k = 0; k < g->n_file; k++)
            if (uguale_i(base, g->file[k])) return 1;
    }
    return 0;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Che cosa e' cambiato sul CD
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * ! LE DATE NON SI GUARDANO, E NON E' UNA SVISTA. `install -a` confronta
 * dimensione e data, e li' e' giusto: i file del supporto di avvio portano
 * la data di quando sono stati costruiti. Sul CD no. tools/mkiso.py scrive
 * in OGNI record la data di costruzione dell'ISO — c'e' scritto anche
 * perche', nel commento su _COSTRUZIONE — quindi dopo ogni `make iso` tutti
 * i file risultano piu' recenti di tutto. Un aggiornamento che si fida
 * delle date ricopierebbe 139 MB per un binario cambiato, e direbbe di aver
 * aggiornato quando non ha distinto niente.
 *
 * Resta il contenuto. Costa: per i file che combaciano in dimensione si
 * legge tutto da tutte e due le parti. E' il prezzo dell'unica risposta
 * vera alla domanda «questo file e' cambiato?», e si paga una volta sola,
 * nella passata che precede la domanda.
 * ───────────────────────────────────────────────────────────────────────── */

#define STATO_UGUALE    0
#define STATO_MANCANTE  1
#define STATO_DIVERSO   2

static char buf_a[BLOCCO];
static char buf_b[BLOCCO];

static int contenuto_uguale(const char *da, const char *a)
{
    int fa, fb, na, nb, i, uguale = 1;

    fa = open(da, O_RDONLY);
    if (fa < 0) return 0;
    fb = open(a, O_RDONLY);
    if (fb < 0) { close(fa); return 0; }

    for (;;) {
        na = (int)read(fa, buf_a, BLOCCO);
        nb = (int)read(fb, buf_b, BLOCCO);

        /* Letture di lunghezza diversa: o un errore, o due file che la
         * stat diceva uguali e non lo sono. In entrambi i casi «diverso»
         * e' la risposta prudente — si ricopia, e il peggio che succede
         * e' un file copiato per niente. */
        if (na != nb) { uguale = 0; break; }
        if (na <= 0) break;

        for (i = 0; i < na; i++) {
            if (buf_a[i] != buf_b[i]) { uguale = 0; break; }
        }
        if (!uguale) break;
    }

    close(fa);
    close(fb);
    return uguale;
}

static int confronta(const char *da, const char *a)
{
    struct stat s, d;

    if (stat(a, &d) != 0)  return STATO_MANCANTE;
    if (stat(da, &s) != 0) return STATO_UGUALE;   /* non c'e' niente da copiare */

    /* La dimensione risponde da sola quando basta, e non costa letture. */
    if (s.st_size != d.st_size) return STATO_DIVERSO;

    return contenuto_uguale(da, a) ? STATO_UGUALE : STATO_DIVERSO;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Copia
 * ───────────────────────────────────────────────────────────────────────────── */

static int copia_file(const char *da, const char *a)
{
    int fs, fd, n, tot = 0;

    fs = open(da, O_RDONLY);
    if (fs < 0) {
        printf("  ! %s: %s\n", da, strerror(errno));
        n_errori++;
        return -1;
    }

    fd = open(a, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        printf("  ! %s: %s\n", a, strerror(errno));
        close(fs);
        n_errori++;
        return -1;
    }

    while ((n = (int)read(fs, buf, BLOCCO)) > 0) {
        int scritti = 0;

        while (scritti < n) {
            int w = (int)write(fd, buf + scritti, (unsigned int)(n - scritti));

            if (w <= 0) {
                printf("  ! %s: scrittura interrotta a %d byte", a,
                       tot + scritti);
                if (w < 0) printf(": %s", strerror(errno));
                printf("\n");
                close(fs); close(fd);
                n_errori++;
                return -1;
            }
            scritti += w;
        }
        tot += scritti;
    }

    close(fs); close(fd);

    if (n < 0) {
        printf("  ! %s: lettura interrotta dopo %d byte\n", da, tot);
        n_errori++;
        return -1;
    }
    return tot;
}

/* Scende in `sorg` e ricostruisce l'albero sotto `dest`.
 *
 * `rel` e' il percorso relativo alla radice dell'albero, quello su cui si
 * decidono le esclusioni. Cresce a ogni livello e non si azzera mai: e'
 * l'unico modo perche' una regola come "lib/freebasic" significhi quella
 * directory e non una qualunque `freebasic` incontrata sotto un altro
 * ramo. */
static void scendi(const char *sorg, const char *dest, const char *rel)
{
    DIR           *d;
    struct dirent *e;

    d = opendir(sorg);
    if (!d) {
        printf("  ! %s: %s\n", sorg, strerror(errno));
        n_errori++;
        return;
    }

    /* ! LA DIRECTORY SI CREA QUI, NON PRIMA DI SCENDERE. Crearla in
     * anticipo lascerebbe sul disco lo scheletro vuoto di ogni ramo
     * escluso: directory che non contengono niente e che fanno cercare a
     * chi guarda un contenuto che nessuno ha mai copiato. */
    if (!opt_n) mkdir(dest, 0755);
    n_dir++;

    while ((e = readdir(d)) != NULL) {
        char p_sorg[PERC_MAX], p_dest[PERC_MAX], p_rel[PERC_MAX];

        if (e->d_name[0] == '.') continue;      /* '.', '..' e i nascosti */

        unisci(p_sorg, sorg, e->d_name);
        unisci(p_dest, dest, e->d_name);
        if (rel[0]) {
            unisci(p_rel, rel, e->d_name);
        } else {
            strncpy(p_rel, e->d_name, PERC_MAX - 1);
            p_rel[PERC_MAX - 1] = '\0';
        }

        if (da_saltare(p_rel)) continue;

        if (e->d_type == DT_DIR) {
            scendi(p_sorg, p_dest, p_rel);
        } else {
            struct stat st;

            /* ── Modo aggiornamento: si copia SOLO cio' che e' cambiato ──
             *
             * La passata a vuoto elenca e conta; quella vera copia. Le due
             * fanno lo stesso confronto, quindi cio' che si e' visto
             * elencato e' esattamente cio' che verra' copiato. */
            if (opt_a) {
                int stato = confronta(p_sorg, p_dest);

                if (stato == STATO_UGUALE) { n_uguali++; continue; }

                if (stat(p_sorg, &st) == 0) n_byte += (long)st.st_size;
                n_file++;
                if (stato == STATO_MANCANTE) n_nuovi++;

                if (opt_n) {
                    printf("  %s %s\n",
                           (stato == STATO_MANCANTE) ? "+" : "~", p_rel);
                    continue;
                }
                printf("  %s\n", p_rel);
                copia_file(p_sorg, p_dest);
                continue;
            }

            if (stat(p_sorg, &st) == 0) n_byte += (long)st.st_size;
            n_file++;

            if (opt_n) continue;

            /* Una riga per file: l'albero e' di centinaia di file su un
             * supporto lento, e un programma muto e un programma bloccato
             * si somigliano troppo. Stessa ragione di `cp -r`. */
            printf("  %s\n", p_rel);
            copia_file(p_sorg, p_dest);
        }
    }
    closedir(d);
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Il PATH: si aggiunge a [env] di kernel.cfg
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * ! SI MODIFICA LA RIGA, NON SI RIGENERA IL FILE. hwconfig il kernel.cfg
 * lo riscrive per intero, e li' e' giusto: sta proponendo una
 * configurazione completa. Qui si sta aggiungendo una voce a un elenco, e
 * riscrivere tutto vorrebbe dire buttare via i montaggi, la disposizione
 * di tastiera e i duecento commenti che qualcuno potrebbe aver modificato
 * — per aggiungere quindici caratteri.
 *
 * ! IL PERCORSO SCRITTO E' QUELLO CHE L'ALBERO AVRA' ALL'AVVIO, non
 * quello dove lo stiamo copiando adesso. Installando da un CD su un disco
 * montato in /disco si copia in /disco/exos, ma al prossimo avvio quel
 * disco sara' la radice e l'albero sara' in /exos. Scrivere /disco/exos/bin
 * darebbe un PATH che punta a una directory che non esistera' piu'.
 * ============================================================================= */

static int riga_e_chiave(const char *r, const char *chiave)
{
    int i = 0;

    while (r[i] == ' ' || r[i] == '\t') i++;
    while (*chiave) {
        char c = r[i++], k = *chiave++;

        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != k) return 0;
    }
    while (r[i] == ' ' || r[i] == '\t') i++;
    return (r[i] == '=');
}

/* Il valore dopo l'uguale, senza spazi ai bordi. */
static const char *valore_di(const char *r)
{
    while (*r && *r != '=') r++;
    if (*r == '=') r++;
    while (*r == ' ' || *r == '\t') r++;
    return r;
}

static int contiene(const char *elenco, const char *voce)
{
    int lv = (int)strlen(voce);
    const char *p = elenco;

    while (*p) {
        const char *fine = p;

        while (*fine && *fine != ':') fine++;
        if ((int)(fine - p) == lv && strncmp(p, voce, (unsigned int)lv) == 0)
            return 1;
        p = (*fine == ':') ? fine + 1 : fine;
    }
    return 0;
}

/* Compone in `out` il PATH `vecchio` con dentro `bin`, messo PRIMA della
 * prima voce che sta su /cdrom. Se non ce ne sono, va in coda.
 *
 * ! IN CODA NON BASTAVA, e il difetto costava mezza giornata a chiunque.
 * Il PATH predefinito contiene /cdrom/exos/bin, cioe' i compilatori DEL
 * CD; accodando la bin appena installata, `gcc` continuava a risolversi
 * su quella del lettore. Nel migliore dei casi si usava il CD credendo di
 * usare il disco — e bastava togliere il CD perche' un sistema
 * «installato» smettesse di compilare. Nel peggiore, con la copia rotta
 * che stava in /cdrom/bin fino al 10 agosto 2026, si otteneva
 *
 *     gcc: fatal error: cannot execute 'cc1'
 *
 * su una macchina dove gli strumenti c'erano ed erano a posto.
 *
 * Davanti a TUTTO no: /bin e /dev sono il sistema, e un albero di
 * strumenti non deve poter sostituire un comando di base. Prima del CD, e
 * dopo il resto. */
static void inserisci_nel_path(char *out, int max, const char *vecchio,
                               const char *bin)
{
    const char *p = vecchio;
    int         o = 0, messo = 0;

    while (*p) {
        const char *fine = p;
        int         l;

        while (*fine && *fine != ':') fine++;
        l = (int)(fine - p);

        if (!messo && l >= 6 && strncmp(p, "/cdrom", 6) == 0) {
            o += snprintf(out + o, max - o, "%s%s", o ? ":" : "", bin);
            messo = 1;
        }
        o += snprintf(out + o, max - o, "%s%.*s", o ? ":" : "", l, p);
        p = (*fine == ':') ? fine + 1 : fine;
    }

    if (!messo) o += snprintf(out + o, max - o, "%s%s", o ? ":" : "", bin);
    (void)o;
}

/* Scrive PATH e TMPDIR in [env] di kernel.cfg, IN UNA PASSATA SOLA.
 *
 * ! UNA PASSATA, NON DUE, e il motivo e' la copia di sicurezza. Con due
 * giri il .bak del secondo sarebbe il file gia' modificato dal primo:
 * chi tornasse indietro si ritroverebbe una configurazione a meta' — con
 * il PATH nuovo e senza TMPDIR — cioe' uno stato che non e' mai esistito
 * e che nessuno ha mai deciso.
 *
 * `tmpdir` a NULL lascia stare quella voce.
 *
 * ! TMPDIR NON SI SOVRASCRIVE SE C'E' GIA'. Il PATH e' un elenco e la
 * nostra bin ci si aggiunge; TMPDIR e' una scelta singola, e chi l'ha
 * fatta sa dove ha spazio meglio di noi. */
static int aggiorna_cfg(const char *radice, const char *prefisso,
                        const char *tmpdir)
{
    char  cfg[PERC_MAX], bak[PERC_MAX], bin[PERC_MAX];
    static char testo[CFG_MAX];
    static char nuovo[CFG_MAX];
    int   fd, n, i, o = 0, in_env = 0;
    int   fatto_path = 0, fatto_tmp = 0;

    unisci(cfg, radice, "boot");
    unisci(cfg, cfg, "kernel.cfg");
    snprintf(bak, sizeof(bak), "%s.bak", cfg);
    snprintf(bin, sizeof(bin), "%s/bin", prefisso);

    fd = open(cfg, O_RDONLY);
    if (fd < 0) {
        printf("  ! %s: %s\n", cfg, strerror(errno));
        printf("    Non l'ho toccato. Aggiungi a mano, in [env]:\n");
        printf("      PATH   = /bin:/dev:%s\n", bin);
        if (tmpdir) printf("      TMPDIR = %s\n", tmpdir);
        return -1;
    }
    n = (int)read(fd, testo, sizeof(testo) - 1);
    close(fd);
    if (n < 0) n = 0;
    testo[n] = '\0';

    /* Passata sulle righe. Si ricopia tutto tale e quale tranne la riga
     * PATH dentro [env], che viene riscritta. */
    i = 0;
    while (i < n) {
        char riga[RIGA_MAX];
        int  l = 0;

        while (i < n && testo[i] != '\n' && l < RIGA_MAX - 1) riga[l++] = testo[i++];
        riga[l] = '\0';
        if (i < n && testo[i] == '\n') i++;

        {
            int k = 0;
            while (riga[k] == ' ' || riga[k] == '\t') k++;
            if (riga[k] == '[') {
                in_env = (riga[k+1] == 'e' && riga[k+2] == 'n' &&
                          riga[k+3] == 'v' && riga[k+4] == ']');
            }
        }

        if (in_env && !fatto_path && riga_e_chiave(riga, "path")) {
            const char *v = valore_di(riga);

            if (contiene(v, bin)) {
                printf("  %s c'era gia' nel PATH\n", bin);
                o += snprintf(nuovo + o, CFG_MAX - o, "%s\n", riga);
            } else {
                char nuovo_path[RIGA_MAX];

                inserisci_nel_path(nuovo_path, sizeof(nuovo_path), v, bin);
                o += snprintf(nuovo + o, CFG_MAX - o, "PATH        = %s\n",
                              nuovo_path);
                printf("  PATH   = %s\n", nuovo_path);
            }
            fatto_path = 1;
            continue;
        }

        if (in_env && !fatto_tmp && riga_e_chiave(riga, "tmpdir")) {
            printf("  TMPDIR = %s  (c'era gia', lo lascio)\n", valore_di(riga));
            fatto_tmp = 1;
            o += snprintf(nuovo + o, CFG_MAX - o, "%s\n", riga);
            continue;
        }

        o += snprintf(nuovo + o, CFG_MAX - o, "%s\n", riga);
        if (o >= CFG_MAX - RIGA_MAX) {
            printf("  ! %s e' piu' grande di %d byte: non lo riscrivo\n",
                   cfg, CFG_MAX);
            return -1;
        }
    }

    if (!fatto_path || (tmpdir && !fatto_tmp)) {
        /* Manca almeno una delle due voci.
         *
         * ! SI APRE UNA [env] NUOVA IN FONDO invece di infilare le righe
         * dentro quella che c'e' gia'. Inserire a meta' file vuol dire
         * spostare tutto cio' che segue, e un errore li' dentro si vede
         * solo al riavvio successivo. Il parser INI di cfg.c riprende la
         * sezione dove l'aveva lasciata: due [env] sono la stessa
         * sezione, e questa e' l'unica scrittura in coda che non puo'
         * rompere niente di quello che c'era. */
        o += snprintf(nuovo + o, CFG_MAX - o,
                      "\n[env]\n# Aggiunte da toolinst.\n");

        if (!fatto_path) {
            o += snprintf(nuovo + o, CFG_MAX - o,
                          "PATH        = /bin:/dev:%s\n", bin);
            printf("  PATH   = /bin:/dev:%s  (voce nuova)\n", bin);
        }
        if (tmpdir && !fatto_tmp) {
            /* ! SENZA TMPDIR IL COMPILATORE NON COMPILA, e il messaggio
             * non lo dice. Il driver di GCC e mkstemp ci mettono i file di
             * passaggio (l'assembly fra cc1 e as): senza questa voce
             * finiscono nella directory corrente, e da una radice in sola
             * lettura — cioe' avviando da CD — non ci finiscono affatto.
             * Ne esce «Cannot create temporary file in ./», che parla di
             * un file temporaneo e manda a cercare il guasto in GCC. */
            o += snprintf(nuovo + o, CFG_MAX - o,
                          "# Dove i compilatori mettono i file di passaggio.\n"
                          "TMPDIR      = %s\n", tmpdir);
            printf("  TMPDIR = %s  (voce nuova)\n", tmpdir);
        }
    }

    if (opt_n) return 0;

    /* Il file di prima non si perde: stessa regola di hwconfig. */
    {
        int fb = open(cfg, O_RDONLY);

        if (fb >= 0) {
            int fo = open(bak, O_WRONLY | O_CREAT | O_TRUNC);

            if (fo >= 0) {
                int m;
                while ((m = (int)read(fb, buf, BLOCCO)) > 0) write(fo, buf, (unsigned int)m);
                close(fo);
            }
            close(fb);
        }
    }

    fd = open(cfg, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        printf("  ! %s: %s\n", cfg, strerror(errno));
        return -1;
    }
    {
        int scritti = 0;

        while (scritti < o) {
            int w = (int)write(fd, nuovo + scritti, (unsigned int)(o - scritti));
            if (w <= 0) { close(fd); printf("  ! %s: scrittura interrotta\n", cfg); return -1; }
            scritti += w;
        }
    }
    close(fd);
    printf("  scritto %s  (il precedente e' in %s)\n", cfg, bak);
    return 0;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Domande
 * ───────────────────────────────────────────────────────────────────────────── */

static int chiedi_si(const char *domanda)
{
    char r[16];
    int  n;

    printf("%s [si/no] ", domanda);
    n = (int)read(0, r, sizeof(r) - 1);
    if (n <= 0) return 0;
    r[n] = '\0';
    return (r[0] == 's' || r[0] == 'S');
}

static void chiedi_riga(const char *domanda, char *dst, int max)
{
    char r[PERC_MAX];
    int  n;

    printf("%s [%s] ", domanda, dst);
    n = (int)read(0, r, sizeof(r) - 1);
    if (n <= 0) return;
    r[n] = '\0';
    while (n > 0 && (r[n-1] == '\n' || r[n-1] == '\r' || r[n-1] == ' ')) r[--n] = '\0';
    if (n > 0) { strncpy(dst, r, (unsigned int)(max - 1)); dst[max - 1] = '\0'; }
}

static void uso(void)
{
    printf("uso: toolinst [-a] [-n] [-y] [-g elenco] [-s sorgente]\n");
    printf("              [-p prefisso] [-t tmpdir | -T] [radice]\n\n");
    printf("Installa gli strumenti di sviluppo del CD tools e li rende\n");
    printf("operativi: PATH e TMPDIR in kernel.cfg. Si raggiunge anche\n");
    printf("come  install -tools.\n\n");
    printf("  -a            AGGIORNA: copia solo cio' che sul CD e' cambiato,\n");
    printf("                e prima lo elenca\n");
    printf("  -n            mostra cosa farebbe, non copia e non scrive\n");
    printf("  -y            non chiede niente (installa tutto quel che c'e')\n");
    printf("  -g a,b,c      installa questi gruppi e basta, senza chiedere\n");
    printf("  -s <dir>      dove sta l'albero `exos` (cercato in /cdrom, /)\n");
    printf("  -p <perc>     che percorso avra' l'albero all'avvio (/exos)\n");
    printf("  -t <perc>     la TMPDIR da scrivere in kernel.cfg (/tmp)\n");
    printf("  -T            non toccare TMPDIR\n");
    printf("  <radice>      dove e' montato il sistema bersaglio (/)\n\n");
    printf("I gruppi li dichiara il CD in exos/strumenti.txt: `toolinst -n`\n");
    printf("li elenca con quello che pesano.\n\n");
    printf("!! -a CONFRONTA IL CONTENUTO, non le date. Sul CD ogni file porta\n");
    printf("   la data di costruzione dell'ISO, non la propria: dopo un\n");
    printf("   `make iso` le date direbbero che e' cambiato tutto. Leggere i\n");
    printf("   file costa qualche minuto ed e' l'unica risposta vera.\n\n");
    printf("!! TMPDIR non e' un vezzo: senza, i file di passaggio fra cc1 e\n");
    printf("   as finiscono nella directory corrente, e da una radice in\n");
    printf("   sola lettura non ci finiscono affatto.\n\n");
    printf("Installando da CD su un disco montato in /disco:\n");
    printf("  toolinst /disco\n");
    printf("copia in /disco/exos e scrive /exos/bin nel PATH, perche' al\n");
    printf("prossimo avvio quel disco sara' la radice.\n\n");
    printf("!! L'albero va copiato INTERO e con la sua forma: gcc e fbc si\n");
    printf("   cercano cc1, as, ld e gli header in <dir del binario>/..,\n");
    printf("   non nel PATH. Binari sciolti in /bin non funzionano.\n");
}


/* ─────────────────────────────────────────────────────────────────────────────
 * main
 * ───────────────────────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    char sorgente[PERC_MAX] = "";
    char radice[PERC_MAX]   = "/";
    char prefisso[PERC_MAX] = "/exos";
    char albero[PERC_MAX], dest[PERC_MAX];
    char scelti[128] = "";
    char tmpdir[PERC_MAX] = "/tmp";
    int  i, prefisso_dato = 0, senza_tmp = 0;

    for (i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "-n") == 0) opt_n = 1;
        else if (strcmp(argv[i], "-y") == 0) opt_y = 1;
        else if (strcmp(argv[i], "-a") == 0) opt_a = 1;
        else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            strncpy(sorgente, argv[++i], PERC_MAX - 1);
        } else if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) {
            strncpy(scelti, argv[++i], sizeof(scelti) - 1);
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            strncpy(tmpdir, argv[++i], PERC_MAX - 1);
        } else if (strcmp(argv[i], "-T") == 0) {
            senza_tmp = 1;
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            strncpy(prefisso, argv[++i], PERC_MAX - 1);
            prefisso_dato = 1;
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "-help") == 0 ||
                   strcmp(argv[i], "--help") == 0) { uso(); return 0; }
        else if (argv[i][0] == '-') { uso(); return 1; }
        else strncpy(radice, argv[i], PERC_MAX - 1);
    }

    /* ── L'albero: dove prenderlo ──────────────────────────────────────── */
    if (sorgente[0] == '\0') {
        int k;

        for (k = 0; sorgenti_pred[k]; k++) {
            unisci(albero, sorgenti_pred[k], "exos");
            if (e_dir(albero)) {
                strncpy(sorgente, sorgenti_pred[k], PERC_MAX - 1);
                break;
            }
        }
    }
    if (sorgente[0] == '\0') {
        printf("Non trovo l'albero degli strumenti.\n");
        printf("  E' la directory `exos` del CD tools. Cercata in:\n");
        for (i = 0; sorgenti_pred[i]; i++) printf("    %s/exos\n", sorgenti_pred[i]);
        printf("  Monta il CD:  mount cd0 /cdrom\n");
        printf("  oppure indica dove sta:  toolinst -s /altro\n");
        return 1;
    }
    unisci(albero, sorgente, "exos");

    printf("Strumenti di sviluppo\n\n");
    printf("  albero      %s\n", albero);

    /* ── Il catalogo, e cosa c'e' davvero sul CD ───────────────────────── */
    if (leggi_catalogo(albero) != 0) {
        catalogo_di_scorta();
        printf("  catalogo    di scorta (il CD non porta exos/strumenti.txt)\n");
    } else {
        printf("  catalogo    %s/strumenti.txt\n", albero);
    }

    {
        char p[PERC_MAX];
        int  i, primo = 1;

        /* ! IL CATALOGO DICE COSA POTREBBE ESSERCI, IL CD DICE COSA C'E'.
         * Un gruppo elencato e non presente non si propone: chiederlo
         * darebbe una domanda la cui risposta non cambia niente. E il
         * contrario — un CD con dentro qualcosa che il catalogo non
         * nomina — finisce nella base e viene copiato, che e' il
         * comportamento voluto: si toglie, non si elenca. */
        printf("  contiene    ");
        for (i = 0; i < g_ngruppi; i++) {
            Gruppo *g = &g_gruppo[i];

            if (g->prova[0]) {
                unisci(p, albero, g->prova);
                g->c_e = esiste(p);
            } else {
                g->c_e = 1;
            }
            g->voluto = g->c_e;

            if (g->c_e) {
                printf("%s%s", primo ? "" : ", ", g->nome);
                primo = 0;
            }
        }
        if (primo) printf("(niente di riconoscibile)");
        printf("\n\n");

        if (scelti[0] != '\0') {
            /* -g: la scelta arriva dalla riga di comando. */
            for (i = 0; i < g_ngruppi; i++) {
                Gruppo *g = &g_gruppo[i];
                g->voluto = g->c_e && (g->sempre || in_elenco(scelti, g->id));
            }
        } else if (!opt_y) {
            for (i = 0; i < g_ngruppi; i++) {
                Gruppo *g = &g_gruppo[i];
                char    d[160];

                if (!g->c_e) continue;
                if (g->sempre) {
                    printf("%s e' obbligatorio: %s\n", g->nome,
                           g->dice[0] ? g->dice : "serve a tutti gli altri");
                    continue;
                }
                snprintf(d, sizeof(d), "  %s (%s)?", g->nome, g->dice);
                g->voluto = chiedi_si(d);
            }
            printf("\n");
        }

        /* ── Le dipendenze ─────────────────────────────────────────────
         *
         * ! SI TIRANO DENTRO, NON SI RIFIUTA LA SCELTA. Chi ha chiesto il
         * C++ vuole compilare in C++, e rispondergli «allora rifai la
         * scelta» gli fa ribattere la stessa cosa sapendo una regola in
         * piu'. Si aggiunge cio' che manca e lo si dice. */
        {
            int giro;

            for (giro = 0; giro < g_ngruppi; giro++) {
                int cambiato = 0;

                for (i = 0; i < g_ngruppi; i++) {
                    Gruppo *g = &g_gruppo[i], *d;

                    if (!g->voluto || g->vuole[0] == '\0') continue;
                    d = gruppo_per_id(g->vuole);
                    if (d == NULL) {
                        printf("  ! %s dichiara di volere '%s', che nel "
                               "catalogo non c'e'\n", g->id, g->vuole);
                        g->vuole[0] = '\0';
                        continue;
                    }
                    if (!d->c_e) {
                        printf("  ! %s vuole %s, che su questo CD non c'e': "
                               "lo tolgo\n", g->nome, d->nome);
                        g->voluto = 0;
                        cambiato = 1;
                        continue;
                    }
                    if (!d->voluto) {
                        printf("  %s ha bisogno di %s: aggiungo anche quello\n",
                               g->nome, d->nome);
                        d->voluto = 1;
                        cambiato = 1;
                    }
                }
                if (!cambiato) break;
            }
        }

        /* Gli avvertimenti, solo di cio' che si sta per installare. */
        {
            int k, intestato = 0;

            for (i = 0; i < g_ngruppi; i++) {
                Gruppo *g = &g_gruppo[i];

                if (!g->voluto || g->n_note == 0) continue;
                if (!intestato) { printf("\nDa sapere\n\n"); intestato = 1; }
                printf("  %s:\n", g->nome);
                for (k = 0; k < g->n_note; k++) printf("    %s\n", g->nota[k]);
            }
            if (intestato) printf("\n");
        }
    }

    /* ── Dove va, e con che nome all'avvio ─────────────────────────────── */
    if (!opt_y && !prefisso_dato) {
        printf("Percorso che l'albero avra' ALL'AVVIO del sistema bersaglio.\n");
        printf("Non e' dove lo copio adesso: quello lo ricavo dalla radice.\n");
        chiedi_riga("  prefisso?", prefisso, PERC_MAX);
        printf("\n");
    }
    if (prefisso[0] != '/') {
        printf("Il prefisso dev'essere un percorso assoluto: '%s' non lo e'.\n",
               prefisso);
        return 1;
    }

    /* dest = <radice> + <prefisso>. Con radice "/" resta il prefisso. */
    unisci(dest, radice, prefisso + 1);

    if (!esiste(radice)) {
        printf("%s non esiste.\n", radice);
        printf("  E' il punto in cui hai montato il sistema bersaglio.\n");
        printf("  Montalo prima:  mount hd0p1 %s\n", radice);
        return 1;
    }

    /* ── Il volume che riceve ──────────────────────────────────────────── */
    {
        MountInfo m;

        if (montaggio_di(dest, &m) == 0) {
            printf("  volume      %s su %s, %s%s\n", m.dev, m.punto,
                   nome_fs(m.fs), m.sola_lettura ? ", SOLA LETTURA" : "");

            if (m.sola_lettura) {
                printf("\n%s e' montato in sola lettura: non ci si puo'\n",
                       m.punto);
                printf("copiare niente. Rimontalo in lettura/scrittura.\n");
                return 1;
            }
            if (m.fs != 2) {
                printf("\n!!  %s e' %s, e su FAT i nomi lunghi si scrivono\n",
                       m.punto, nome_fs(m.fs));
                printf("   troncati alla forma 8.3. L'albero ne e' pieno\n");
                printf("   (bits/stdc++.h, libstdc++.a): la copia riuscirebbe\n");
                printf("   e il compilatore non troverebbe piu' i propri\n");
                printf("   header, che sul disco ci sono con un altro nome.\n");
                printf("   Serve ext2:  mkfs -t ext2 -L strumenti hd0p1\n\n");
                if (opt_y) {
                    printf("   (-y: non insisto, ma non lo faccio.)\n");
                    return 1;
                }
                if (!chiedi_si("Continuo lo stesso, sapendolo?")) {
                    printf("Annullato.\n");
                    return 0;
                }
                printf("\n");
            }
        }
    }

    /* ── Passata a vuoto: quanti file, quanti byte ─────────────────────── */
    if (opt_a) {
        /* ! L'ELENCO VIENE PRIMA DELLA DOMANDA. «Aggiorno 3 file?» e
         * «aggiorno 800 file?» sono due decisioni diverse, e chi risponde
         * deve poter vedere QUALI prima di dire di si'. Un aggiornamento
         * che tocca tutto quando ci si aspettava un ritocco e' il momento
         * in cui ci si accorge di aver montato il CD sbagliato. Stessa
         * forma di `install -a`. */
        printf("\nConfronto di %s con %s\n", dest, albero);
        printf("  +  da creare    ~  da sostituire\n");
        printf("  (i file che combaciano in dimensione si leggono per\n");
        printf("   intero da tutte e due le parti: ci vuole qualche minuto)\n\n");
    }
    {
        int salva_n = opt_n;

        opt_n = 1;
        scendi(albero, dest, "");
        opt_n = salva_n;
    }

    printf("\nRiepilogo\n\n");
    {
        int  i;
        long dichiarati = 0;

        for (i = 0; i < g_ngruppi; i++) {
            Gruppo *g = &g_gruppo[i];

            if (!g->c_e) continue;
            printf("  %-12s %s", g->nome, g->voluto ? "si' " : "no  ");
            if (g->voluto && g->mbyte) printf("  %ld MB", g->mbyte);
            printf("\n");
            if (g->voluto) dichiarati += g->mbyte;
        }
        printf("\n");
        if (opt_a) {
            printf("  cambiati   %ld file (%ld nuovi, %ld sostituiti)\n",
                   n_file, n_nuovi, n_file - n_nuovi);
            printf("  invariati  %ld file: non li tocco\n", n_uguali);
            printf("  copio      %ld KB\n", n_byte / 1024);
            if (n_file == 0)
                printf("\n  Non c'e' niente da aggiornare: il disco e' gia'\n"
                       "  allineato al CD.\n");
        } else {
            printf("  copio      %ld file in %ld directory\n", n_file, n_dir);
            printf("  totale     %ld KB", n_byte / 1024);
            if (dichiarati) printf("  (il catalogo ne dichiarava %ld MB)", dichiarati);
            printf("\n");
        }
    }
    printf("  da         %s\n", albero);
    printf("  a          %s\n", dest);
    printf("  nel PATH   %s/bin\n", prefisso);
    if (!senza_tmp) printf("  TMPDIR     %s\n", tmpdir);
    printf("\n");

    /* ! LO SPAZIO LIBERO NON LO SO, E LO DICO. EX-OS non espone ancora
     * quanti blocchi restano su un volume montato (il kernel lo sa —
     * ext2_blocchi_liberi() — ma nessuna syscall lo consegna). Tacere
     * lascerebbe credere di aver controllato: il conto qui sopra e' quello
     * che serve, e va confrontato a mano con la dimensione del volume. */
    printf("  Non so quanto spazio sia libero: EX-OS non lo dice ancora.\n");
    printf("  Se il disco si riempie a meta' copia lo dico e mi fermo.\n\n");

    if (opt_n) {
        printf("(-n: non ho copiato niente e non ho toccato kernel.cfg)\n");
        return 0;
    }

    /* Niente da fare: si salta la domanda e la copia, ma NON la
     * configurazione — PATH e TMPDIR possono essere rimasti indietro anche
     * con l'albero allineato, ed e' proprio il caso in cui uno rilancia
     * toolinst per capire perche' `gcc` non si trova. */
    if (!opt_a || n_file > 0) {
        if (!opt_y && !chiedi_si("Procedo?")) {
            printf("Annullato: niente e' stato copiato.\n");
            return 0;
        }

        n_file = n_dir = n_byte = 0;
        n_uguali = n_nuovi = 0;

        printf(opt_a ? "\nAggiornamento\n" : "\nCopia\n");
        scendi(albero, dest, "");
        printf("\n  %ld file, %ld KB", n_file, n_byte / 1024);
        if (n_errori) printf(", %ld errori", n_errori);
        printf("\n");
    }

    /* La directory dei temporanei si CREA, non si nomina soltanto: una
     * TMPDIR che punta a una directory inesistente e' peggio di nessuna
     * TMPDIR, perche' la libc non ripiega — ci prova e fallisce. */
    if (!senza_tmp && tmpdir[0] == '/') {
        char t[PERC_MAX];

        unisci(t, radice, tmpdir + 1);
        if (!e_dir(t) && mkdir(t, 0755) != 0 && !e_dir(t))
            printf("  ! %s non si crea (%s): TMPDIR puntera' a una "
                   "directory che non c'e'\n", t, strerror(errno));
    }

    printf("\nConfigurazione\n");
    aggiorna_cfg(radice, prefisso, senza_tmp ? NULL : tmpdir);

    if (n_errori) {
        printf("\nFinito con %ld errori: controllali prima di fidarti\n",
               n_errori);
        printf("del compilatore installato.\n");
        return 1;
    }

    printf("\nFatto. Al prossimo avvio del sistema in %s:\n", radice);
    printf("  gcc -o prog prog.c        compila e collega\n");
    {
        Gruppo *g = gruppo_per_id("cpp");
        if (g && g->voluto) printf("  g++ -o prog prog.cpp      C++\n");
        g = gruppo_per_id("fb");
        if (g && g->voluto) printf("  fbc prog.bas              FreeBASIC\n");
    }
    printf("\nAdesso, senza riavviare, i compilatori si chiamano col\n");
    printf("percorso intero: %s/bin/gcc\n", dest);
    return 0;
}
