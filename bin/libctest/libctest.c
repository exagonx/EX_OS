/* =============================================================================
 * bin/libctest/libctest.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Prova della libc DENTRO EX-OS.
 *
 * PERCHE' NON BASTA CHE COMPILI. L'allocatore, i flussi bufferizzati e
 * setjmp/longjmp sono codice che si comporta bene in compilazione e male
 * a runtime: un blocco riusato che si sovrappone al precedente, un
 * buffer non svuotato all'uscita, una posizione di file calcolata senza
 * tenere conto di cio' che sta ancora nel buffer. Nessuna di queste cose
 * si vede in un `make`; tutte si vedono qui.
 *
 * E' anche il collaudo che serve prima di portarci sopra un compilatore:
 * TCC non usera' una funzione sola di quelle provate qui — le usera'
 * tutte, migliaia di volte, e un difetto sotto carico si manifesterebbe
 * come "il compilatore sbaglia", che e' il posto piu' scomodo in cui
 * cercare un bug della libreria.
 *
 * Uscita: 0 se tutto passa, 1 altrimenti — cosi' vale anche come prova
 * automatica e non solo come stampa da guardare.
 * ============================================================================= */

#include "libc.h"
/* intmax_t, imaxdiv_t e la loro famiglia stanno qui e non in libc.h:
 * <inttypes.h> e' l'header che li definisce, e provarli significa anche
 * provare che quell'header sia includibile per conto suo. */
#include "inttypes.h"
/* Le macro di confronto del C99 (isgreater e compagnia): senza di loro la
 * libstdc++ dichiara <math.h> non conforme e rinuncia a mettere in std::
 * decine di funzioni. Vedi lib/include/math.h. */
#include "math.h"

static int passati = 0;
static int falliti = 0;

static void esito(const char *nome, int ok)
{
    if (ok) { passati++; printf("  [ok]     %s\n", nome); }
    else    { falliti++; printf("  [FALLITO] %s\n", nome); }
}

/* =============================================================================
 * Allocatore
 * ============================================================================= */
static void prova_allocatore(void)
{
    char *a, *b, *c, *d;
    void *prima, *dopo;
    int   i, ok;

    printf("\nAllocatore\n");

    a = (char *)malloc(100);
    esito("malloc restituisce memoria", a != NULL);

    /* Il blocco liberato deve tornare disponibile: e' esattamente cio' che
     * la free() vuota di prima non faceva. */
    free(a);
    b = (char *)malloc(100);
    esito("free rende il blocco riusabile", b == a);
    free(b);

    /* Fusione: due blocchi adiacenti liberati devono poter servire una
     * richiesta che nessuno dei due, da solo, coprirebbe. */
    a = (char *)malloc(64);
    b = (char *)malloc(64);
    c = (char *)malloc(64);       /* impedisce che la coda dell'heap confonda */
    free(a);
    free(b);
    d = (char *)malloc(140);
    esito("blocchi adiacenti si fondono", d == a);
    free(d);
    free(c);

    /* La prova che conta per un compilatore: allocare e liberare in ciclo
     * NON deve far crescere l'heap. Con l'allocatore precedente questo
     * ciclo consumava 256 KB e non ne restituiva uno. */
    prima = sbrk(0);
    for (i = 0; i < 2000; i++) {
        void *p = malloc(128);
        if (p == NULL) break;
        free(p);
    }
    dopo = sbrk(0);
    esito("2000 malloc/free non fanno crescere l'heap", prima == dopo);

    /* realloc deve conservare il contenuto, ed e' il caso in cui la
     * versione precedente leggeva oltre la fine del blocco vecchio. */
    a = (char *)malloc(16);
    strcpy(a, "contenuto");
    b = (char *)realloc(a, 512);
    esito("realloc conserva il contenuto",
          b != NULL && strcmp(b, "contenuto") == 0);
    free(b);

    /* -----------------------------------------------------------------
     * ! realloc CHE ALLUNGA SUL POSTO — la prova che ha fatto cadere cc1
     *
     * Quando il vicino successivo e' libero e adiacente, realloc puo'
     * ingrandire il blocco senza copiare. E' il caso frequente di un
     * buffer che raddoppia mentre nessun altro alloca in mezzo, ed e'
     * anche quello che nell'agosto 2026 NON funzionava: la fusione veniva
     * chiesta a una funzione che rifiuta di lavorare sui blocchi
     * allocati, quindi non faceva niente, e realloc restituiva il
     * puntatore dichiarando una dimensione che il blocco non aveva.
     *
     * Il danno non si vedeva subito: si vedeva alla malloc successiva,
     * che consegnava memoria sovrapposta a quella appena "ingrandita".
     * Su cc1 usciva come un puntatore di lista che valeva 3.
     *
     * Per questo la prova NON si limita a guardare che realloc ritorni
     * non-NULL: scrive tutto il blocco, ne alloca un altro, lo riempie, e
     * poi RILEGGE il primo. Se i due si sovrappongono, il primo non e'
     * piu' quello che ci si era scritto.
     * ----------------------------------------------------------------- */
    {
        char *g1, *g2, *g3;
        int   j;

        g1 = (char *)malloc(64);
        g2 = (char *)malloc(64);
        ok = (g1 != NULL && g2 != NULL);

        if (ok) {
            for (j = 0; j < 64; j++) g1[j] = (char)(j + 1);
            free(g2);                       /* libero e adiacente a g1 */

            g1 = (char *)realloc(g1, 120);  /* deve allungarsi sul posto */
            ok = (g1 != NULL);
        }

        if (ok) {
            for (j = 0; j < 120; j++) g1[j] = (char)(0xA0 + (j & 0x0F));

            g3 = (char *)malloc(64);        /* non deve sovrapporsi a g1 */
            ok = (g3 != NULL);
            if (ok) {
                for (j = 0; j < 64; j++) g3[j] = 0x5A;
                for (j = 0; j < 120; j++)
                    if (g1[j] != (char)(0xA0 + (j & 0x0F))) { ok = 0; break; }
                free(g3);
            }
            free(g1);
        }
        esito("realloc che allunga sul posto da' davvero i byte chiesti", ok);
    }

    /* -----------------------------------------------------------------
     * printf in virgola mobile.
     *
     * ! I VALORI ATTESI SONO QUELLI DI glibc, verificati uno per uno sul
     * lato Linux: una printf che stampa "quasi" il numero giusto e' come
     * una sqrt quasi giusta — peggio di una che non c'e', perche' nessuno
     * va a controllarla.
     *
     * `2.5 -> "2"` non e' un errore di trascrizione: e' l'arrotondamento
     * AL PARI che lo standard prescrive, ed e' il caso che la prima
     * stesura sbagliava.
     * ----------------------------------------------------------------- */
    {
        static const struct { const char *fmt; double v; const char *atteso; } pf[] = {
            { "%f",      1.0,              "1.000000"      },
            { "%f",     -1.5,             "-1.500000"      },
            { "%.2f",    3.14159,          "3.14"          },
            { "%.0f",    2.5,              "2"             },  /* al pari */
            { "%.0f",    3.5,              "4"             },  /* al pari */
            { "%.0f",    0.5,              "0"             },  /* al pari */
            { "%.3f",    0.0005,           "0.001"         },
            { "%e",      1234.5,           "1.234500e+03"  },
            { "%.2e",    0.000123,         "1.23e-04"      },
            { "%g",      100000.0,         "100000"        },
            { "%g",      1000000.0,        "1e+06"         },
            { "%g",      0.0001,           "0.0001"        },
            { "%g",      0.00001,          "1e-05"         },
            { "%.3g",    3.14159,          "3.14"          },
            { "%8.2f",   3.5,              "    3.50"      },
            { "%-8.2f",  3.5,              "3.50    "      },
            { "%08.2f", -3.5,             "-0003.50"       },
            { "%+.2f",   3.5,              "+3.50"         },
            { "%f",      0.0,              "0.000000"      },
        };
        char pbuf[64];
        unsigned int k;

        ok = 1;
        for (k = 0; k < sizeof(pf) / sizeof(pf[0]); k++) {
            snprintf(pbuf, sizeof(pbuf), pf[k].fmt, pf[k].v);
            if (strcmp(pbuf, pf[k].atteso) != 0) {
                printf("    %s di %g -> \"%s\", atteso \"%s\"\n",
                       pf[k].fmt, pf[k].v, pbuf, pf[k].atteso);
                ok = 0;
            }
        }
        esito("printf in virgola mobile (%f, %e, %g)", ok);

        snprintf(pbuf, sizeof(pbuf), "%f %f %f", 1.0/0.0, -1.0/0.0, 0.0/0.0);
        esito("printf: inf e nan", strcmp(pbuf, "inf -inf nan") == 0);
    }

    a = (char *)calloc(64, 4);
    ok = (a != NULL);
    for (i = 0; ok && i < 64 * 4; i++) if (a[i] != 0) ok = 0;
    esito("calloc azzera", ok);
    free(a);

    esito("free(NULL) non fa danni", (free(NULL), 1));

    /* -----------------------------------------------------------------
     * Allocazione allineata.
     *
     * Non basta guardare che l'indirizzo sia allineato: il punto delicato
     * e' che il blocco restituito e' stato RITAGLIATO dentro un altro, e
     * quindi free() deve poterlo trattare come un blocco qualunque. Se la
     * lista dell'heap restasse inconsistente il danno non si vedrebbe
     * qui — si vedrebbe alla malloc successiva, consegnando due volte lo
     * stesso indirizzo.
     * ----------------------------------------------------------------- */
    ok = 1;
    for (i = 3; i <= 12; i++) {          /* da 8 a 4096 byte */
        size_t all = (size_t)1 << i;
        void  *p   = memalign(all, 100);
        if (p == NULL || ((uintptr_t)p & (all - 1u)) != 0) { ok = 0; break; }
        memset(p, 0xAA, 100);            /* si deve poter scrivere davvero */
        free(p);
    }
    esito("memalign allinea da 8 a 4096", ok);

    /* L'heap deve restare sano dopo un ciclo di memalign/free: se la
     * lista fosse rotta, questa malloc restituirebbe un indirizzo gia'
     * in uso o NULL. */
    a = (char *)memalign(256, 300);
    b = (char *)malloc(300);
    ok = (a != NULL && b != NULL && (a + 300 <= b || b + 300 <= a));
    esito("l'heap resta sano dopo memalign", ok);
    free(a);
    free(b);

    a = NULL;
    ok = (posix_memalign((void **)&a, 64, 200) == 0) &&
         a != NULL && ((uintptr_t)a & 63u) == 0;
    esito("posix_memalign ritorna 0 e allinea", ok);
    free(a);

    /* ! posix_memalign RITORNA il codice di errore, non lo mette in
     * errno: e' l'eccezione della famiglia, ed e' il modo classico di
     * sbagliare a usarla. */
    esito("posix_memalign rifiuta un allineamento non potenza di due",
          posix_memalign((void **)&a, 24, 200) == EINVAL);

    a = (char *)aligned_alloc(128, 256);
    esito("aligned_alloc allinea", a != NULL && ((uintptr_t)a & 127u) == 0);
    free(a);

    esito("memalign rifiuta un allineamento non potenza di due",
          memalign(48, 100) == NULL);
}

/* =============================================================================
 * Formattazione
 * ============================================================================= */
static void prova_printf(void)
{
    char buf[64];

    printf("\nFormattazione\n");

    snprintf(buf, sizeof(buf), "[%5d][%-5d][%05d]", 42, 42, 42);
    esito("ampiezza e allineamento", strcmp(buf, "[   42][42   ][00042]") == 0);

    snprintf(buf, sizeof(buf), "[%x][%X][%#x][%o]", 255, 255, 255, 8);
    esito("basi", strcmp(buf, "[ff][FF][0xff][10]") == 0);

    snprintf(buf, sizeof(buf), "[%+d][% d][%.5d]", 7, 7, 42);
    esito("segno e precisione", strcmp(buf, "[+7][ 7][00042]") == 0);

    snprintf(buf, sizeof(buf), "[%.3s][%8s][%-8s]", "abcdefg", "x", "y");
    esito("stringhe", strcmp(buf, "[abc][       x][y       ]") == 0);

    snprintf(buf, sizeof(buf), "[%*d][%c]", 6, 3, 'Z');
    esito("ampiezza da argomento", strcmp(buf, "[     3][Z]") == 0);

    snprintf(buf, sizeof(buf), "%llu", 12345678901234ULL);
    esito("interi a 64 bit", strcmp(buf, "12345678901234") == 0);

    /* snprintf deve dire quanto SAREBBE servito e non superare mai il
     * buffer: e' l'unico modo che ha il chiamante di accorgersi del
     * troncamento. */
    {
        char piccolo[6];
        int  n = snprintf(piccolo, sizeof(piccolo), "%s", "abcdefghij");
        esito("snprintf tronca e riporta la lunghezza vera",
              n == 10 && strcmp(piccolo, "abcde") == 0);
    }
}

/* =============================================================================
 * Flussi
 * ============================================================================= */
static void prova_stdio(void)
{
    const char *nome = "/libctest.tmp";
    FILE       *f;
    char        riga[64];
    long        dim;

    printf("\nFlussi\n");

    f = fopen(nome, "w");
    if (f == NULL) {
        esito("fopen in scrittura", 0);
        return;
    }
    esito("fopen in scrittura", 1);

    fprintf(f, "prima riga\n");
    fprintf(f, "valore=%d\n", 1234);
    fputs("terza\n", f);
    esito("fclose", fclose(f) == 0);

    f = fopen(nome, "r");
    if (f == NULL) { esito("fopen in lettura", 0); return; }
    esito("fopen in lettura", 1);

    esito("fgets prima riga",
          fgets(riga, sizeof(riga), f) != NULL &&
          strcmp(riga, "prima riga\n") == 0);

    esito("fgets seconda riga",
          fgets(riga, sizeof(riga), f) != NULL &&
          strcmp(riga, "valore=1234\n") == 0);

    /* ftell deve tenere conto di cio' che e' ancora nel buffer: senza la
     * correzione direbbe la posizione del kernel, cioe' la fine del
     * pezzo letto in anticipo. */
    esito("ftell dopo due righe", ftell(f) == 23);

    fseek(f, 0, SEEK_END);
    dim = ftell(f);
    esito("fseek alla fine e ftell", dim == 29);

    esito("fseek all'inizio e primo carattere",
          fseek(f, 0, SEEK_SET) == 0 && fgetc(f) == 'p');

    esito("ungetc", ungetc('P', f) == 'P' && fgetc(f) == 'P');

    /* Fine file: deve arrivare, e feof deve dirlo. */
    while (fgetc(f) >= 0) { }
    esito("feof a fine file", feof(f) != 0);

    fclose(f);

    /* Il file deve essere leggibile anche a syscall nude, con la
     * dimensione giusta: e' la prova che il buffer e' finito su disco. */
    {
        int  fd = open(nome, O_RDONLY);
        long n  = (fd >= 0) ? fsize(fd) : -1;
        if (fd >= 0) close(fd);
        esito("il file su disco ha la dimensione attesa", n == 29);
    }

    /* fgetpos/fsetpos: ! ritornano 0/-1, non la posizione — chi li
     * confonde con ftell legge sempre "inizio del file". */
    {
        FILE  *f = fopen(nome, "r");
        fpos_t p;
        int    c1 = 0, c2 = 0;
        int    ok = 0;

        if (f != NULL) {
            (void)fgetc(f);
            (void)fgetc(f);
            ok = (fgetpos(f, &p) == 0);
            c1 = fgetc(f);
            (void)fgetc(f);
            ok = ok && (fsetpos(f, &p) == 0);
            c2 = fgetc(f);
            fclose(f);
        }
        esito("fgetpos e fsetpos tornano allo stesso punto",
              ok && c1 == c2 && c1 != EOF);
    }

    unlink(nome);
}

/* =============================================================================
 * setjmp / longjmp
 * ============================================================================= */
static jmp_buf salto;

/* GCC vede una ricorsione senza uscita e avvisa, ed e' un falso positivo
 * che dice bene cosa stiamo provando: la ricorsione E' limitata da
 * `profondita`, ma l'unica via d'uscita e' longjmp — dichiarata noreturn —
 * quindi per l'analizzatore nessun ramo termina normalmente. E' esattamente
 * il punto: si esce da dodici livelli di stack senza tornare indietro uno
 * per uno. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winfinite-recursion"
static void va_lontano(int profondita)
{
    if (profondita > 0) { va_lontano(profondita - 1); return; }
    longjmp(salto, 7);
}
#pragma GCC diagnostic pop

static void prova_setjmp(void)
{
    volatile int passaggi = 0;
    int          v;

    printf("\nSalti non locali\n");

    v = setjmp(salto);
    if (v == 0) {
        passaggi++;
        va_lontano(12);     /* non torna: rientra dal setjmp qui sopra */
        esito("longjmp non torna al chiamante", 0);
        return;
    }

    esito("setjmp ritorna 0 la prima volta", passaggi == 1);
    esito("longjmp riporta il valore dato", v == 7);
}

/* =============================================================================
 * Conversioni, ordinamento, stringhe
 * ============================================================================= */
static int confronta_int(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

static void prova_resto(void)
{
    int   v[8] = { 42, 7, 99, 1, 13, 7, 64, 2 };
    char *fine;
    int   chiave = 13;
    int  *trovato;
    char  copia[32];

    printf("\nConversioni, ordinamento, stringhe\n");

    esito("strtol base 10", strtol("  -123abc", &fine, 10) == -123 && *fine == 'a');
    esito("strtol base automatica", strtol("0x1f", NULL, 0) == 31);
    esito("strtoul ottale", strtoul("0755", NULL, 0) == 493);
    esito("strtol senza cifre riporta l'inizio",
          (strtol("xyz", &fine, 10) == 0) && fine[0] == 'x');

    qsort(v, 8, sizeof(int), confronta_int);
    esito("qsort ordina", v[0] == 1 && v[7] == 99 && v[2] == 7);

    trovato = (int *)bsearch(&chiave, v, 8, sizeof(int), confronta_int);
    esito("bsearch trova", trovato != NULL && *trovato == 13);

    esito("strstr", strstr("il gatto beve", "gatto") != NULL &&
                    strstr("il gatto beve", "cane") == NULL);

    /* =====================================================================
     * strncpy — le tre prove che servono, e la terza e' quella vera
     *
     * ! IL CASO CHE CONTA E' LA SORGENTE CHE NON CI STA. Con una
     * sorgente piu' corta di n va bene anche un'implementazione rotta:
     * e' quando NON entra che si vede se il conteggio regge. La versione
     * fino ad agosto 2026 usciva dal primo ciclo con n gia' passato per
     * zero — cioe' SIZE_MAX — e il riempimento di zeri proseguiva finche'
     * non trovava una pagina non mappata. Dentro EX-OS si vedeva come un
     * page fault di collect2 al confine del proprio heap, e sembrava un
     * difetto dell'allocatore.
     *
     * La sentinella dopo il buffer e' li' per questo: senza, un
     * riempimento che sfora di poco passerebbe inosservato: il primo
     * ciclo copia i byte giusti e la prova sarebbe verde lo stesso.
     * ===================================================================== */
    {
        struct { char buf[8]; char guardia[8]; } z;
        int i;

        for (i = 0; i < 8; i++) z.guardia[i] = (char)0x7E;

        memset(z.buf, 0x7E, sizeof z.buf);
        strncpy(z.buf, "abc", 8);
        esito("strncpy copia e riempie di zeri",
              memcmp(z.buf, "abc\0\0\0\0\0", 8) == 0);

        memset(z.buf, 0x7E, sizeof z.buf);
        strncpy(z.buf, "abcdefghij", 4);
        esito("strncpy tronca e NON termina",
              memcmp(z.buf, "abcd", 4) == 0 && z.buf[4] == 0x7E);

        esito("strncpy non scrive oltre n (la sorgente non ci sta)",
              memcmp(z.guardia, "\x7E\x7E\x7E\x7E\x7E\x7E\x7E\x7E", 8) == 0);

        memset(z.buf, 0x7E, sizeof z.buf);
        strncpy(z.buf, "abc", 0);
        esito("strncpy con n zero non scrive niente", z.buf[0] == 0x7E);
    }

    {
        char *d = strdup("duplicata");
        esito("strdup", d != NULL && strcmp(d, "duplicata") == 0);
        free(d);
    }

    strcpy(copia, "uno,due,tre");
    {
        char *t1 = strtok(copia, ",");
        char *t2 = strtok(NULL, ",");
        char *t3 = strtok(NULL, ",");
        char *t4 = strtok(NULL, ",");
        esito("strtok", t1 && t2 && t3 && !t4 &&
              strcmp(t1, "uno") == 0 && strcmp(t3, "tre") == 0);
    }

    esito("ctype", isdigit('7') && isalpha('q') && isspace('\t') &&
                   !isalpha('7') && toupper('a') == 'A' && tolower('Z') == 'z');

    esito("memchr", memchr("abcdef", 'd', 6) != NULL &&
                    memchr("abcdef", 'z', 6) == NULL);

    esito("strcasecmp",  strcasecmp("Gatto", "gATTO") == 0 &&
                         strcasecmp("gatto", "gatti") != 0);
    esito("strncasecmp", strncasecmp("GATTOne", "gattoni", 5) == 0 &&
                         strncasecmp("gatto", "cane", 1) != 0);
    esito("strpbrk",     strpbrk("nome.sez,uno", ".,") != NULL &&
                         *strpbrk("nome.sez,uno", ".,") == '.' &&
                         strpbrk("niente", ".,") == NULL);
}

/* =============================================================================
 * Quello che chiede il codice di terzi
 *
 * Non e' una sezione di comodo: sono le funzioni che NON avevamo e che
 * hanno fermato la compilazione di binutils una alla volta. Provarle qui
 * costa meno che riscoprirle al prossimo sorgente esterno.
 * ============================================================================= */
static void prova_terzi(void)
{
    printf("\nInterfacce per il codice di terzi:\n");

    /* EOF: il valore c'era, il nome no. */
    esito("EOF vale -1", EOF == -1);

    /* strftime, sulla data che si conosce a memoria. */
    {
        struct tm t;
        char      buf[64];
        size_t    n;

        t.tm_sec = 5; t.tm_min = 4; t.tm_hour = 13;
        t.tm_mday = 2; t.tm_mon = 7; t.tm_year = 126;   /* 2 agosto 2026 */
        t.tm_wday = 0; t.tm_yday = 213; t.tm_isdst = 0;

        n = strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &t);
        esito("strftime data completa",
              n > 0 && strcmp(buf, "2026-08-02T13:04:05+0000") == 0);

        strftime(buf, sizeof(buf), "%a %b %e %p", &t);
        esito("strftime nomi e ora", strcmp(buf, "Sun Aug  2 PM") == 0);

        strftime(buf, sizeof(buf), "%%%V", &t);
        esito("strftime ricopia cio' che non sa", strcmp(buf, "%%V") == 0);

        esito("strftime dice 0 se non ci sta",
              strftime(buf, 4, "%Y-%m-%d", &t) == 0);
    }

    /* frexp/ldexp stanno nella sezione della virgola mobile. Qui i file. */
    {
        const char *nome = "/terzi.tmp";
        struct stat st;
        FILE       *f;

        unlink(nome);

        f = fopen(nome, "w");
        esito("apre un file da riaprire", f != NULL);
        if (f != NULL) {
            fputs("primo", f);

            /* freopen: lo stesso FILE*, un altro file. */
            esito("freopen cambia file sotto lo stesso FILE*",
                  freopen("/terzi2.tmp", "w", f) == f);
            fputs("secondo", f);
            fclose(f);

            f = fopen(nome, "r");
            if (f != NULL) {
                char riga[32] = { 0 };
                fgets(riga, sizeof(riga), f);
                fclose(f);
                esito("il primo file ha quel che ci era stato scritto",
                      strcmp(riga, "primo") == 0);
            } else {
                esito("il primo file ha quel che ci era stato scritto", 0);
            }

            f = fopen("/terzi2.tmp", "r");
            if (f != NULL) {
                char riga[32] = { 0 };
                fgets(riga, sizeof(riga), f);
                fclose(f);
                esito("il secondo ha quel che e' venuto dopo",
                      strcmp(riga, "secondo") == 0);
            } else {
                esito("il secondo ha quel che e' venuto dopo", 0);
            }
        }

        /* lstat: identica a stat, perche' non ci sono collegamenti. */
        esito("lstat come stat",
              lstat(nome, &st) == 0 && S_ISREG(st.st_mode));
        esito("lstat su un nome assente fallisce",
              lstat("/non-esiste-di-sicuro", &st) != 0);

        /* ! chmod e umask non cambiano niente: si prova che RISPONDANO,
         * che e' tutto cio' che promettono. */
        esito("umask non maschera niente", umask(022) == 0);
        esito("chmod accetta un file che c'e'", chmod(nome, 0644) == 0);
        esito("chmod fallisce su un nome assente",
              chmod("/non-esiste-di-sicuro", 0644) != 0);

        unlink(nome);
        unlink("/terzi2.tmp");
    }

    /* mktemp: da' un nome e non crea niente — e' il suo difetto, ed e'
     * quello che si verifica. */
    {
        char modello[] = "/mkXXXXXX";

        esito("mktemp riempie il modello",
              mktemp(modello) == modello && modello[3] != 'X');
        esito("e NON crea il file", access(modello, F_OK) != 0);
    }

    /* fscanf: la prova che conta e' che il flusso resti dove la scansione
     * si e' fermata, perche' e' li' che una finestra letta e non
     * riportata indietro si vedrebbe. */
    {
        const char *nome = "/scan.tmp";
        FILE       *f = fopen(nome, "w");

        if (f != NULL) {
            fputs("42 abc 3.5\nresto della riga\n", f);
            fclose(f);
        }

        f = fopen(nome, "r");
        esito("apre il file da leggere", f != NULL);
        if (f != NULL) {
            int   n = 0;
            char  parola[16] = { 0 };
            char  riga[32] = { 0 };

            esito("fscanf legge un intero e una parola",
                  fscanf(f, "%d %15s", &n, parola) == 2 &&
                  n == 42 && strcmp(parola, "abc") == 0);

            /* Se la finestra non fosse riportata indietro, qui si
             * leggerebbe la fine del file invece del resto della riga. */
            fgets(riga, sizeof(riga), f);
            esito("il flusso e' rimasto dove doveva",
                  strstr(riga, "3.5") != NULL);

            fclose(f);
        }
        unlink(nome);
    }

    /* realpath: la prova che conta e' che due percorsi DIVERSI dello
     * stesso file diano la stessa risposta, e che due file diversi diano
     * risposte diverse — e' quello che `ld` usa per non collegare un file
     * su se stesso. */
    {
        const char *nome = "/reale.tmp";
        char        a[320], b[320];
        int         fd = open(nome, O_WRONLY | O_CREAT | O_TRUNC);

        if (fd >= 0) close(fd);

        esito("realpath su un percorso gia' assoluto",
              realpath(nome, a) == a && strcmp(a, nome) == 0);
        esito("realpath toglie i '.' e i doppi '/'",
              realpath("//./reale.tmp", b) == b && strcmp(b, nome) == 0);
        esito("realpath risolve il '..'",
              realpath("/bin/../reale.tmp", b) == b && strcmp(b, nome) == 0);
        esito("il '..' sulla radice resta la radice",
              realpath("/../..", b) == b && strcmp(b, "/") == 0);
        esito("realpath su un nome assente fallisce",
              realpath("/non-esiste-di-sicuro", b) == NULL);

        /* Due file diversi non devono dare la stessa risposta: e' il caso
         * in cui ld rifiutava di collegare. */
        {
            char *dinamico = realpath(nome, NULL);
            esito("realpath alloca da se' con NULL",
                  dinamico != NULL && strcmp(dinamico, nome) == 0);
            esito("due file diversi, due percorsi diversi",
                  dinamico != NULL && strcmp(dinamico, "/") != 0);
            free(dinamico);
        }

        unlink(nome);
    }

    /* Le ultime arrivate, una riga a testa. */
    esito("isascii e toascii",
          isascii('A') && !isascii(200) && toascii('A' + 128) == 'A');
    esito("isblank", isblank(' ') && isblank('\t') && !isblank('x'));
    esito("strcoll come strcmp", strcoll("abc", "abd") < 0 &&
                                 strcoll("abc", "abc") == 0);
    esito("atof",  atof("3.5") == 3.5);
    esito("fabs",  fabs(-2.25) == 2.25 && fabs(2.25) == 2.25);
    esito("mbrtowc un byte per volta", mbrtowc(NULL, "A", 1, NULL) == 1);
    esito("MB_CUR_MAX vale 1",         MB_CUR_MAX == 1);

    {
        time_t adesso = time(NULL);
        char  *s = ctime(&adesso);
        /* "Sun Aug  2 17:04:05 2026\n": 25 caratteri, sempre. */
        esito("ctime da' 25 caratteri e un a capo",
              s != NULL && strlen(s) == 25 && s[24] == '\n');
    }

    /* mkdir prende due argomenti da agosto 2026; il secondo si ignora. */
    {
        const char *d = "/duearg.dir";
        rmdir(d);
        esito("mkdir con i permessi", mkdir(d, 0755) == 0);
        rmdir(d);
    }
}

/* =============================================================================
 * errno
 * ============================================================================= */
static void prova_errno(void)
{
    int fd;

    printf("\nErrori\n");

    errno = 0;
    fd = open("/questo_file_non_esiste_di_sicuro", O_RDONLY);
    esito("open di un file assente fallisce", fd < 0);
    esito("errno riporta ENOENT", errno == 2);
    esito("strerror descrive il codice",
          strcmp(strerror(2), "file o directory inesistente") == 0);
}

/* =============================================================================
 * Virgola mobile
 *
 * E' la parte piu' importante di questo file, perche' e' l'unica che puo'
 * sbagliare IN SILENZIO. Un printf rotto si vede; una strtod che ritorna
 * 1.4999999 invece di 1.5 no — si vede molto dopo, dentro il programma
 * che un compilatore ha tradotto con la costante sbagliata.
 *
 * I confronti sono esatti (==) e non "a meno di epsilon", ed e' voluto:
 * tutti i valori provati qui hanno una rappresentazione ESATTA in doppia
 * precisione (mezzi, quarti, interi piccoli), quindi la conversione o li
 * azzecca o e' rotta. Un confronto approssimato nasconderebbe proprio
 * l'errore che si sta cercando.
 * ============================================================================= */
static void prova_virgola(void)
{
    char *fine;

    printf("\nVirgola mobile:\n");

    esito("strtod interi",       strtod("42", NULL) == 42.0);
    esito("strtod frazione",     strtod("1.5", NULL) == 1.5);
    esito("strtod segno",        strtod("-0.25", NULL) == -0.25);
    esito("strtod spazi",        strtod("   7.75xyz", NULL) == 7.75);
    esito("strtod esponente",    strtod("1.5e3", NULL) == 1500.0);
    /* ! IL VALORE ATTESO STA IN UNA VARIABILE, e non e' pignoleria.
     *
     * 0.025 non e' rappresentabile esattamente in binario, ed e' l'unico
     * numero di questa sezione che non lo sia. Su x87 GCC valuta le
     * costanti in virgola mobile alla precisione del coprocessore — 64
     * bit di mantissa — quindi `x == 0.025` compila in un confronto fra
     * il double ritornato da strtod e un letterale caricato con FLDT,
     * cioe' con undici bit di mantissa in piu'. Sono due numeri diversi
     * per costruzione, e il confronto e' falso anche quando strtod ha
     * ritornato ESATTAMENTE il double piu' vicino a 0.025 (verificato:
     * 0x3F9999999999999A da entrambe le parti).
     *
     * Assegnare a un `double` forza l'arrotondamento a 53 bit, che e' la
     * domanda che si voleva fare. La stessa trappola aspetta chiunque
     * scriva un confronto fra virgole mobili in un programma di EX-OS. */
    {
        double atteso = 0.025;
        esito("strtod esp negativo", strtod("2.5e-2", NULL) == atteso);
    }

    /* Dove si e' fermata conta quanto il valore: e' cosi' che un
     * compilatore sa dove riprendere a leggere il sorgente. */
    fine = NULL;
    esito("strtod fine",         strtod("3.5abc", &fine) == 3.5 &&
                                 fine && *fine == 'a');

    /* "1e" non e' un numero con esponente: e' 1 seguito da una lettera. */
    fine = NULL;
    esito("strtod 'e' spaiata",  strtod("1e", &fine) == 1.0 &&
                                 fine && *fine == 'e');

    /* Niente cifre: nessuna conversione, e `fine` torna all'inizio. */
    fine = NULL;
    esito("strtod non numero",   strtod("pippo", &fine) == 0.0 &&
                                 fine && *fine == 'p');

    esito("strtof",              strtof("2.5", NULL) == 2.5f);
    esito("ldexp positivo",      ldexp(1.0, 10) == 1024.0);
    esito("ldexp negativo",      ldexp(48.0, -4) == 3.0);
    esito("ldexp zero",          ldexp(7.5, 0) == 7.5);

    /* L'aritmetica vera, quella che gira sull'x87: se il kernel non
     * avesse inizializzato la FPU, questa riga non arriverebbe in fondo. */
    {
        double a = 1.0, b = 3.0, c = a / b;
        esito("aritmetica x87",  c * 3.0 == 1.0 || (c > 0.333 && c < 0.334));
    }

    /* Lo stato x87 sopravvive al cambio di contesto? Il conto e' spezzato
     * da sched_yield(), che manda il processo in fondo alla coda: se
     * nessuno salvasse i registri del coprocessore, al ritorno il valore
     * sarebbe quello di qualcun altro — o spazzatura. */
    {
        double acc = 1.0;
        int    i;
        for (i = 0; i < 20; i++) {
            acc = acc * 2.0;
            sched_yield();
        }
        esito("x87 attraverso lo switch", acc == 1048576.0);
    }

    /* frexp e ldexp sono una l'inversa dell'altra: la prova che vale e'
     * il giro completo, non i due pezzi separati. */
    {
        int    e = 12345;
        double m = frexp(48.0, &e);

        esito("frexp mantissa in [0.5,1)", m == 0.75);
        esito("frexp esponente",           e == 6);
        esito("frexp/ldexp si annullano",  ldexp(m, e) == 48.0);

        m = frexp(-0.125, &e);
        esito("frexp tiene il segno",      m == -0.5 && e == -2);

        m = frexp(0.0, &e);
        esito("frexp di zero",             m == 0.0 && e == 0);
    }

    /* sqrt e' `fsqrt` dell'x87, quindi correttamente arrotondata: sui
     * quadrati perfetti il risultato dev'essere ESATTO, non "vicino". */
    esito("sqrt esatta sui quadrati",
          sqrt(4.0) == 2.0 && sqrt(16.0) == 4.0 && sqrt(0.25) == 0.5);
    esito("sqrt di zero e uno", sqrt(0.0) == 0.0 && sqrt(1.0) == 1.0);
    {
        /* 2 non e' un quadrato: si controlla che il quadrato del
         * risultato ritorni a 2 entro un ULP, che e' quanto si puo'
         * chiedere. */
        double r = sqrt(2.0);
        double d = r * r - 2.0;
        esito("sqrt di 2", (d < 1e-15) && (d > -1e-15));
    }

    esito("strtoull 64 bit",     strtoull("12345678901", NULL, 10) == 12345678901ULL);
    esito("strtoll negativo",    strtoll("-9000000000", NULL, 10) == -9000000000LL);
    esito("strtoull esadecimale", strtoull("0xFFFFFFFF", NULL, 16) == 4294967295ULL);

    /* -----------------------------------------------------------------
     * I confronti che non sollevano eccezioni.
     *
     * ! NON SONO SINONIMI DI <, <=, >, >=. Con un operando NaN danno la
     * stessa RISPOSTA degli operatori (falso) ma non lo stesso EFFETTO:
     * l'operatore solleva "invalid" sull'x87, questi no. E' l'unica
     * ragione per cui esistono.
     *
     * La prova che conta e' l'ultima: `isunordered` deve dire di si' su
     * un NaN, e tutti gli altri di no — compreso `islessgreater`, dove
     * l'errore tipico e' scriverlo come `a != b` (che su NaN e' VERO).
     * ----------------------------------------------------------------- */
    {
        double uno = 1.0, due = 2.0;
        double nan_ = 0.0 / 0.0;

        esito("isgreater",      isgreater(due, uno) && !isgreater(uno, due));
        esito("isgreaterequal", isgreaterequal(uno, uno) &&
                                !isgreaterequal(uno, due));
        esito("isless",         isless(uno, due) && !isless(due, uno));
        esito("islessequal",    islessequal(uno, uno) && !islessequal(due, uno));
        esito("islessgreater",  islessgreater(uno, due) &&
                                !islessgreater(uno, uno));

        esito("isunordered dice si' sul NaN", isunordered(nan_, uno));
        esito("e tutti gli altri dicono no",
              !isgreater(nan_, uno) && !isless(nan_, uno) &&
              !isgreaterequal(nan_, uno) && !islessequal(nan_, uno) &&
              !islessgreater(nan_, uno));
        esito("mentre due numeri veri sono ordinati",
              !isunordered(uno, due));
    }
}

/* =============================================================================
 * Lettura formattata e ora
 * ============================================================================= */
static void prova_sscanf(void)
{
    int    a = 0, b = 0, n = 0;
    char   parola[32];
    double d = 0.0;
    unsigned u = 0;

    printf("\nsscanf e tempo:\n");

    esito("sscanf due interi",  sscanf("12 34", "%d %d", &a, &b) == 2 &&
                                a == 12 && b == 34);
    esito("sscanf larghezza",   sscanf("1234", "%2d", &a) == 1 && a == 12);
    esito("sscanf esadecimale", sscanf("ff", "%x", &u) == 1 && u == 255u);
    esito("sscanf stringa",     sscanf("  ciao mondo", "%s", parola) == 1 &&
                                strcmp(parola, "ciao") == 0);
    esito("sscanf double",      sscanf("2.5", "%lf", &d) == 1 && d == 2.5);
    esito("sscanf letterale",   sscanf("x=7", "x=%d", &a) == 1 && a == 7);
    esito("sscanf %n",          sscanf("123 xyz", "%d%n", &a, &n) == 1 &&
                                a == 123 && n == 3);
    esito("sscanf soppressione", sscanf("1 2", "%*d %d", &a) == 1 && a == 2);

    /* I tre valori di ritorno che vanno distinti: riuscite, nessuna, EOF. */
    esito("sscanf non combacia", sscanf("abc", "%d", &a) == 0);
    esito("sscanf ingresso finito", sscanf("", "%d", &a) == -1);

    /* Il tempo. Non si puo' provare CHE ora sia, ma si puo' provare che
     * la conversione avanti e indietro sia coerente — che e' l'unico
     * modo in cui l'aritmetica delle date puo' sbagliare. */
    {
        time_t     t = time(NULL);
        struct tm *tm;

        esito("time risponde", t > 0);

        tm = localtime(&t);
        esito("localtime coerente",
              tm != NULL && tm->tm_year + 1900 >= 2020 &&
              tm->tm_mon >= 0 && tm->tm_mon <= 11 &&
              tm->tm_mday >= 1 && tm->tm_mday <= 31 &&
              tm->tm_hour <= 23 && tm->tm_min <= 59 && tm->tm_sec <= 60);
    }

    {
        struct timeval tv;
        esito("gettimeofday", gettimeofday(&tv, NULL) == 0 && tv.tv_sec > 0);
    }
}

/* =============================================================================
 * stat nella forma POSIX
 * ============================================================================= */
static void prova_stat(void)
{
    struct stat st;
    const char *nome = "/provastat.txt";
    FILE       *f;

    printf("\nstat:\n");

    f = fopen(nome, "w");
    if (f == NULL) { esito("stat: creazione del file di prova", 0); return; }
    fwrite("dodici byte", 1, 11, f);
    fclose(f);

    esito("stat riesce",     stat(nome, &st) == 0);
    esito("stat dimensione", st.st_size == 11);
    esito("stat e' regolare", S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode));

    esito("stat su directory", stat("/", &st) == 0 && S_ISDIR(st.st_mode));

    {
        int fd = open(nome, O_RDONLY);
        esito("fstat dimensione", fd >= 0 && fstat(fd, &st) == 0 && st.st_size == 11);
        if (fd >= 0) close(fd);
    }

    esito("stat inesistente", stat("/non-esiste-di-sicuro", &st) < 0);

    unlink(nome);
}

/* =============================================================================
 * Ambiente, directory, temporanei — cio' che serve a un compilatore
 * ospitato, provato dentro EX-OS
 * ============================================================================= */
static void prova_ambiente(void)
{
    printf("\nAmbiente:\n");

    /* PATH arriva dalla sezione [env] di kernel.cfg attraverso il ripiego
     * di getenv(): e' la prova che la vecchia strada non si e' rotta. */
    esito("getenv trova PATH",        getenv("PATH") != NULL);
    esito("getenv su chiave assente", getenv("NON_ESISTE_DI_SICURO") == NULL);

    esito("setenv crea",     setenv("PROVA_EXOS", "uno", 1) == 0);
    esito("getenv rilegge",  getenv("PROVA_EXOS") != NULL &&
                             strcmp(getenv("PROVA_EXOS"), "uno") == 0);

    esito("setenv senza sovrascrivere lascia stare",
          setenv("PROVA_EXOS", "due", 0) == 0 &&
          strcmp(getenv("PROVA_EXOS"), "uno") == 0);

    esito("setenv sovrascrive",
          setenv("PROVA_EXOS", "due", 1) == 0 &&
          strcmp(getenv("PROVA_EXOS"), "due") == 0);

    esito("unsetenv toglie",
          unsetenv("PROVA_EXOS") == 0 && getenv("PROVA_EXOS") == NULL);

    /* environ resta percorribile dopo le modifiche */
    {
        int n = 0;
        if (environ != NULL) while (environ[n] != NULL) n++;
        esito("environ e' una lista valida", n >= 0 && n < 1000);
    }
}

static void prova_directory(void)
{
    DIR           *d;
    struct dirent *v;
    int            trovati = 0, trovata_bin = 0;

    printf("\nDirectory:\n");

    d = opendir("/");
    if (d == NULL) { esito("opendir /", 0); return; }
    esito("opendir /", 1);

    while ((v = readdir(d)) != NULL) {
        trovati++;
        if (strcmp(v->d_name, "BIN") == 0 || strcmp(v->d_name, "bin") == 0) {
            trovata_bin = 1;
            esito("readdir marca bin come directory", v->d_type == DT_DIR);
        }
    }
    esito("readdir elenca la root", trovati > 0);
    esito("readdir trova bin",      trovata_bin);

    /* rewinddir deve far ricominciare: se non lo facesse, il secondo giro
     * darebbe zero voci e sembrerebbe una directory vuota. */
    rewinddir(d);
    esito("rewinddir ricomincia", readdir(d) != NULL);

    esito("closedir",  closedir(d) == 0);
    {
        /* Stessa ragione: un file che ci mettiamo noi. */
        const char *tmp = "/opendir.tmp";
        int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd >= 0) close(fd);
        esito("opendir su un file fallisce", opendir(tmp) == NULL);
        unlink(tmp);
    }
    esito("opendir su un nome assente fallisce", opendir("/nonesiste") == NULL);
}

/* =============================================================================
 * Entropia
 *
 * ! NON SI PUO' PROVARE CHE DEI BYTE SIANO CASUALI, e pretenderlo
 * sarebbe la prova sbagliata: qualunque sequenza fissa passa un test
 * statistico se e' abbastanza corta, e qualunque sequenza vera lo
 * fallisce ogni tanto. Quello che si puo' provare — e che coglie i
 * difetti veri — e' molto piu' modesto:
 *
 *   - che i byte non siano TUTTI UGUALI (il difetto di un buffer mai
 *     scritto, o di un RDRAND che fallisce senza dirlo);
 *   - che due chiamate diano risultati DIVERSI (il difetto di un
 *     serbatoio che non si consuma);
 *   - che il contratto sia rispettato: getentropy riempie tutto o
 *     fallisce, e sopra 256 byte rifiuta.
 * ============================================================================= */
/* ! fclose SUI FLUSSI STANDARD DEVE RIUSCIRE, e non e' pignoleria: il
 * kernel rifiuta close(0/1/2) di proposito, e finche' fclose riportava
 * quel rifiuto ogni programma che ne guarda l'esito moriva alla fine del
 * proprio lavoro. Ci e' cascato cc1. */
/* ! QUI SI GUARDA IL VALORE, non solo che la chiamata riesca. GCC
 * calcola `tv_sec * 1000000000 + tv_usec * 1000` e poi VERIFICA che la
 * somma dei tempi delle fasi non superi il totale: con un orologio che
 * salta, quel controllo fallisce e cc1 muore con un internal compiler
 * error DOPO aver fatto tutto il lavoro. E' successo. */
static void prova_orologio(void)
{
    struct timeval a, b, c;
    long long      na, nb;
    int            i;

    printf("\nOrologio:\n");

    /* ! SI STAMPA IN DUE META' DA 32 BIT invece che con %lld: se il
     * difetto fosse nella printf, un %lld rotto mostrerebbe un numero
     * sbagliato e manderebbe a cercare il guasto nell'orologio. Due
     * interi non possono mentire su questo. */
    {
        time_t adesso = time(NULL);

        printf("  time()  = %u (alto %u)\n",
               (unsigned)(adesso & 0xFFFFFFFFu),
               (unsigned)((unsigned long long)adesso >> 32));
        esito("time() e' plausibile (2020-2100)",
              adesso > (time_t)1577836800 && adesso < (time_t)4102444800LL);
    }

    esito("gettimeofday riesce", gettimeofday(&a, NULL) == 0);
    printf("  tv_sec  = %u (alto %u)  tv_usec = %u\n",
           (unsigned)((unsigned long long)a.tv_sec & 0xFFFFFFFFu),
           (unsigned)((unsigned long long)a.tv_sec >> 32),
           (unsigned)a.tv_usec);

    /* Fra il 2020 e il 2100, altrimenti non e' un orologio: e' un numero. */
    esito("tv_sec e' plausibile (2020-2100)",
          (long long)a.tv_sec > 1577836800LL && (long long)a.tv_sec < 4102444800LL);
    esito("tv_usec sta sotto il milione", a.tv_usec >= 0 && a.tv_usec < 1000000);

    /* Non deve MAI tornare indietro, e va provato piu' volte: un salto
     * ogni tanto e' peggio di uno sempre, perche' passa le prove corte. */
    gettimeofday(&b, NULL);
    for (i = 0; i < 200; i++) {
        gettimeofday(&c, NULL);
        na = (long long)b.tv_sec * 1000000LL + b.tv_usec;
        nb = (long long)c.tv_sec * 1000000LL + c.tv_usec;
        if (nb < na) break;
        b = c;
    }
    esito("non torna mai indietro in 200 letture", i == 200);

    /* E il prodotto che fa GCC deve restare sensato. */
    na = (long long)a.tv_sec * 1000000000LL + (long long)a.tv_usec * 1000LL;
    esito("tv_sec*1e9 + tv_usec*1e3 non trabocca", na > 0);
}

static void prova_chiusura_standard(void)
{
    printf("\nFlussi standard:\n");

    esito("fclose(stdout) riesce", fclose(stdout) == 0);
    /* E dopo si deve poter ancora scrivere: il descrittore non e' stato
     * chiuso davvero, ed e' il punto. */
    printf("  (questa riga esce dopo fclose(stdout))\n");
    esito("si scrive ancora dopo fclose(stdout)", 1);
    esito("fclose(stderr) riesce", fclose(stderr) == 0);
}

static void prova_entropia(void)
{
    unsigned char a[32], b[32], grande[300];
    int i, uguali, tutti_uguali;

    printf("\nEntropia:\n");

    memset(a, 0, sizeof(a));
    memset(b, 0, sizeof(b));

    if (getentropy(a, sizeof(a)) != 0) {
        /* Non e' un fallimento della prova: su una macchina appena
         * accesa senza RDRAND e senza nessuno che digiti puo' davvero non
         * essercene. Va DETTO, non nascosto. */
        printf("  [--]     getentropy: non abbastanza entropia (errno %d)\n", errno);
        printf("           Non e' un difetto: batti qualche tasto e rilancia.\n");
        return;
    }
    esito("getentropy riempie", 1);

    tutti_uguali = 1;
    for (i = 1; i < (int)sizeof(a); i++) if (a[i] != a[0]) tutti_uguali = 0;
    esito("i byte non sono tutti uguali", !tutti_uguali);

    esito("sopra 256 byte rifiuta", getentropy(grande, sizeof(grande)) != 0);

    /* =====================================================================
     * ! LA SECONDA CHIAMATA PUO' LEGITTIMAMENTE FALLIRE, e la prima
     * stesura di questa prova non lo prevedeva: dava per scontato che
     * l'entropia fosse infinita.
     *
     * Non lo e', ed e' il punto di tutto il meccanismo. Trentadue byte
     * sono 256 bit, cioe' tutto il serbatoio: chiederne altri trentadue
     * subito dopo trova la casa vuota, e la risposta giusta e' -EAGAIN.
     * Un sistema che avesse risposto comunque avrebbe passato la prova
     * sbagliata.
     *
     * Quindi qui si accetta ENTRAMBI gli esiti, ma NON un terzo: se
     * riesce, i byte devono essere diversi dai primi.
     * ===================================================================== */
    if (getentropy(b, sizeof(b)) == 0) {
        uguali = 1;
        for (i = 0; i < (int)sizeof(a); i++) if (a[i] != b[i]) uguali = 0;
        esito("la seconda presa da byte diversi dalla prima", !uguali);
    } else {
        esito("il serbatoio vuoto si dichiara invece di inventare",
              errno == EAGAIN);
    }

    /* getrandom: forma Linux. Stessa storia — o ritorna dei byte, o
     * dichiara che non ce ne sono. Quello che non deve fare e' ritornare
     * zero byte fingendo di averne dati. */
    {
        int n = (int)getrandom(a, 16, 0);

        esito("getrandom o serve o dichiara",
              (n > 0 && n <= 16) || (n < 0 && errno == EAGAIN));
    }
}

static void prova_temporanei(void)
{
    char modello[] = "/tmpXXXXXX";
    int  fd;

    printf("\nFile temporanei e interrogazioni:\n");

    fd = mkstemp(modello);
    esito("mkstemp apre", fd >= 0);
    if (fd >= 0) {
        esito("mkstemp ha riempito il modello",
              modello[4] != 'X' && modello[9] != 'X');
        esito("il temporaneo si scrive", write(fd, "x", 1) == 1);
        close(fd);
        esito("access lo trova",       access(modello, F_OK) == 0);

        /* rename: copia e cancella, ma il risultato dev'essere quello */
        esito("rename sposta",         rename(modello, "/rinominato.tmp") == 0);
        esito("il vecchio nome non c'e' piu'", access(modello, F_OK) != 0);
        esito("il nuovo nome c'e'",    access("/rinominato.tmp", F_OK) == 0);
        unlink("/rinominato.tmp");
    }

    /* -----------------------------------------------------------------
     * tmpnam segue TMPDIR
     *
     * ! SI PROVA SU UN NOME, NON SU UN FILE. La directory di prova non
     * esiste, e non deve: qui la domanda e' "dove ha deciso di metterlo",
     * non "riesce a crearlo". Creare davvero vorrebbe dire un volume
     * scrivibile montato, cioe' un'altra prova che fallisce per un altro
     * motivo — ed e' esattamente il tipo di prova che, quando diventa
     * rossa, non dice niente.
     * ----------------------------------------------------------------- */
    {
        char *n1, *n2;

        setenv("TMPDIR", "/disco", 1);
        n1 = tmpnam(NULL);
        esito("tmpnam segue TMPDIR",
              n1 != NULL && strncmp(n1, "/disco/t", 8) == 0);

        /* Una barra finale non deve raddoppiarsi: il nome che finisce in
         * un messaggio d'errore dev'essere ridigitabile. */
        setenv("TMPDIR", "/disco/", 1);
        n2 = tmpnam(NULL);
        esito("tmpnam non raddoppia la barra",
              n2 != NULL && strncmp(n2, "/disco/t", 8) == 0);

        unsetenv("TMPDIR");
        n1 = tmpnam(NULL);
        esito("senza TMPDIR il temporaneo sta nella radice",
              n1 != NULL && n1[0] == '/' && n1[1] == 't');
    }

    /* -----------------------------------------------------------------
     * rename — ! DALLA 0.161 NON COPIA PIU'.
     *
     * Prima era copia+cancella: costava quanto il file e RIALLOCAVA i
     * blocchi. Ora e' una syscall che riscrive la voce di directory, e la
     * garanzia che da' — i dati non si spostano — e' cio' su cui si regge
     * `install`, che verifica la mappa dei settori del kernel prima di
     * dargli il nome definitivo.
     *
     * ! Due differenze da POSIX, ed e' giusto provarle: NON sostituisce
     * la destinazione (EEXIST) e NON attraversa directory (ENOSYS).
     * ----------------------------------------------------------------- */
    {
        const char *a = "/rin-a.tmp", *b = "/rin-b.tmp";
        FILE *f;
        char  riga[64];
        int   ok;

        unlink(a); unlink(b);

        f = fopen(a, "w");
        if (f) { fputs("contenuto che non deve muoversi\n", f); fclose(f); }

        /* ! SU UNA RADICE IN SOLA LETTURA QUESTE PROVE NON SI POSSONO
         * FARE, e non e' un fallimento: avviando da CD la root e' un ISO
         * 9660, che non si scrive. Segnalarlo come guasto insegnerebbe a
         * ignorare le righe rosse — e la risposta EROFS e' proprio quella
         * giusta. Si distingue il caso invece di pretendere un mondo che
         * non c'e'. */
        if (f == NULL && errno == EROFS) {
            printf("  = radice in sola lettura (avvio da CD): prove di "
                   "rename saltate\n");
            return;
        }

        esito("rename riesce", rename(a, b) == 0);
        esito("il vecchio nome non c'e' piu'", access(a, F_OK) != 0);

        riga[0] = '\0';
        f = fopen(b, "r");
        ok = (f != NULL) && (fgets(riga, sizeof(riga), f) != NULL);
        if (f) fclose(f);
        esito("e il contenuto e' quello di prima",
              ok && strstr(riga, "non deve muoversi") != NULL);

        /* ! SOSTITUISCE, E LA PROVA ERA RIMASTA AL CONTRATTO DI PRIMA.
         * Fino al 20 agosto 2026 `rename` rifiutava una destinazione che
         * esisteva, e qui c'era scritto «chi vuole sostituire cancella
         * prima». Poi vfs_rename ha imparato le regole di POSIX — e la
         * ragione non e' la conformita': «cancella prima» NON e' equivalente
         * a uno scambio, perche' fra la cancellazione e lo scambio il file
         * NON ESISTE. Questa riga e' rimasta indietro di quattro giorni e
         * accusava il kernel di un difetto che era suo. */
        f = fopen(a, "w");
        if (f) { fputs("altro\n", f); fclose(f); }
        esito("rename sostituisce la destinazione", rename(a, b) == 0);
        esito("e il nome di partenza non c'e' piu'", access(a, F_OK) != 0);

        riga[0] = '\0';
        f = fopen(b, "r");
        ok = (f != NULL) && (fgets(riga, sizeof(riga), f) != NULL);
        if (f) fclose(f);
        esito("e dentro c'e' quello che e' arrivato",
              ok && strstr(riga, "altro") != NULL);

        /* ! E SU SE STESSO NON FA NIENTE, che e' l'unico modo di non essere
         * distruttivo: sostituire vuol dire togliere di mezzo la
         * destinazione, e la destinazione qui e' il file stesso. POSIX lo
         * dice, e chi costruisce i due nomi separatamente ci arriva. */
        esito("rename di un file su se stesso non lo cancella",
              rename(b, b) == 0 && access(b, F_OK) == 0);

        /* Da qui in poi serve di nuovo un `a`: la sostituzione se l'e'
         * portato via. */
        f = fopen(a, "w");
        if (f) { fputs("altro\n", f); fclose(f); }

        /* ! Non attraversa directory: sarebbe una copia piu' una
         * cancellazione, cioe' un'altra operazione. */
        esito("rename fra directory diverse da ENOSYS",
              rename(a, "/bin/rin-a.tmp") != 0 && errno == ENOSYS);

        unlink(a); unlink(b);
    }

    esito("access su un nome assente fallisce",
          access("/non-esiste-di-sicuro", F_OK) != 0);

    esito("isatty su stdout",      isatty(1) == 1);
    {
        /* Un file CREATO QUI, non uno che si spera esista: /KERNEL.BIN c'e'
         * sul floppy e non su un sistema installato su ext2, e la prova
         * falliva li' per il motivo sbagliato. */
        const char *tmp = "/isatty.tmp";
        int f2 = open(tmp, O_WRONLY | O_CREAT | O_TRUNC);
        esito("isatty su un file no", f2 >= 0 && isatty(f2) == 0);
        if (f2 >= 0) close(f2);
        unlink(tmp);
    }

    /* -----------------------------------------------------------------
     * Quello che <cstdlib> della libstdc++ pretende che esista.
     * ----------------------------------------------------------------- */
    {
        div_t  d  = div(-7, 2);
        ldiv_t ld = ldiv(100L, 7L);
        wchar_t w = 0;
        char    b = 0;
        int     i, dentro = 1, diversi = 0, primo;

        /* ! Il troncamento e' verso lo ZERO: -3 e -1, non -4 e +1. */
        esito("div tronca verso zero", d.quot == -3 && d.rem == -1);
        esito("ldiv",  ld.quot == 14 && ld.rem == 2);

        esito("mblen conta un byte",   mblen("abc", 3) == 1);
        esito("mblen sul NUL da' 0",   mblen("", 1) == 0);
        esito("mbtowc promuove",       mbtowc(&w, "Z", 1) == 1 && w == (wchar_t)'Z');
        esito("wctomb converte",       wctomb(&b, (wchar_t)'Q') == 1 && b == 'Q');
        esito("wctomb rifiuta sopra 255", wctomb(&b, (wchar_t)0x1F600) == -1);

        srand(12345);
        primo = rand();
        for (i = 0; i < 200; i++) {
            int r = rand();
            if (r < 0 || r > RAND_MAX) dentro = 0;
            if (r != primo) diversi = 1;
        }
        esito("rand resta dentro RAND_MAX", dentro);
        esito("rand non da' sempre lo stesso", diversi);

        /* Lo stesso seme deve dare la stessa sequenza: e' cio' che rende
         * ripetibile una prova che usa rand. */
        srand(12345);
        esito("srand ripete la sequenza", rand() == primo);

        /* ! QUESTE DUE PROVE DICEVANO IL CONTRARIO fino ad agosto 2026:
         * «system() esiste ma non esegue, -1 con ENOSYS», e system(NULL)
         * doveva rispondere 0. Era vero, ed e' smesso di esserlo il giorno
         * che system() ha imparato a passare da /bin/sh -c (serviva a
         * FreeBASIC). Le prove sono rimaste indietro e fallivano su un
         * sistema che funzionava — il modo piu' rapido perche' una
         * suite di prove smetta di essere creduta. */
        esito("system(NULL) dice che una shell c'e'", system(NULL) != 0);
        esito("system esegue davvero", system("echo ciao") == 0);

        esito("atoll", atoll("-9000000000") == -9000000000LL);
        esito("imaxabs", imaxabs((intmax_t)-9000000000LL) == 9000000000LL);
        {
            imaxdiv_t im = imaxdiv((intmax_t)-9000000000LL, (intmax_t)7);
            esito("imaxdiv", im.quot == -1285714285LL && im.rem == -5LL);
        }
        esito("strtoimax", strtoimax("-9000000000", NULL, 10) == -9000000000LL);
        esito("strtoumax", strtoumax("18000000000", NULL, 10) == 18000000000ULL);
        esito("llabs", llabs(-9000000000LL) == 9000000000LL);
        {
            lldiv_t l = lldiv(-9000000000LL, 7LL);
            esito("lldiv", l.quot == -1285714285LL && l.rem == -5LL);
        }

        /* ! strxfrm ritorna la lunghezza dell'ORIGINALE, non di quanto ha
         * copiato: e' l'unico modo che il chiamante ha di accorgersi che
         * il buffer era corto. */
        {
            char corto[4];
            esito("strxfrm dice la lunghezza vera",
                  strxfrm(corto, "abcdefgh", sizeof(corto)) == 8);
        }

        esito("difftime", difftime((time_t)100, (time_t)40) == 60.0);
        {
            struct timespec ts;
            /* ! Ritorna `base`, non 0: e' la convenzione di questa
             * funzione e non quella di tutte le altre. */
            esito("timespec_get ritorna TIME_UTC",
                  timespec_get(&ts, TIME_UTC) == TIME_UTC);
            esito("timespec_get rifiuta una base sconosciuta",
                  timespec_get(&ts, 99) == 0);
        }

        /* ! setvbuf dice di NO invece di fingere: la bufferizzazione di
         * EX-OS non e' regolabile, e rispondere 0 farebbe credere a un
         * programma che il suo _IONBF sia stato accolto. */
        esito("setvbuf rifiuta cio' che non sa fare",
              setvbuf(stdout, NULL, _IONBF, 0) != 0);

        /* ! localeconv: i campi non specificati valgono 127 (CHAR_MAX) e
         * NON zero. Zero vorrebbe dire "zero cifre decimali", 127 vuol
         * dire "questa locale non lo dice" — e' la distinzione su cui
         * sbaglia chi riempie la struttura a memoria. */
        {
            struct lconv *lc = localeconv();
            esito("localeconv da' il punto decimale",
                  lc != NULL && strcmp(lc->decimal_point, ".") == 0);
            esito("e i campi non specificati valgono 127, non 0",
                  lc != NULL && lc->frac_digits == 127 &&
                  lc->grouping[0] == '\0');
        }

        /* mktime: l'inversa di gmtime, e ! NORMALIZZA la struttura. */
        {
            struct tm t;
            time_t    q;

            /* 2000-01-01 00:00:00 UTC = 946684800 */
            t.tm_year = 100; t.tm_mon = 0;  t.tm_mday = 1;
            t.tm_hour = 0;   t.tm_min = 0;  t.tm_sec  = 0;
            t.tm_isdst = 0;  t.tm_wday = 99; t.tm_yday = 99;
            q = mktime(&t);
            esito("mktime da' l'istante giusto", q == (time_t)946684800);
            /* Il 2000-01-01 era un sabato: tm_wday = 6. Il valore assurdo
             * messo sopra dev'essere stato ignorato e riscritto. */
            esito("e riscrive tm_wday che aveva ignorato", t.tm_wday == 6);

            /* Il mese 12 e' gennaio dell'anno dopo: e' cosi' che si fa
             * aritmetica sulle date. */
            t.tm_year = 100; t.tm_mon = 12; t.tm_mday = 1;
            t.tm_hour = 0;   t.tm_min = 0;  t.tm_sec  = 0;
            (void)mktime(&t);
            esito("mktime normalizza il mese 12 in gennaio dell'anno dopo",
                  t.tm_mon == 0 && t.tm_year == 101);

            /* E l'andata e ritorno con gmtime deve chiudere il giro. */
            {
                time_t     adesso = (time_t)1234567890;
                struct tm *g = gmtime(&adesso);
                struct tm  copia = *g;
                esito("gmtime e mktime sono l'una l'inversa dell'altra",
                      mktime(&copia) == adesso);
            }
        }
    }

    /* ! sleep RITORNA unsigned int — i secondi che restavano da dormire
     * quando un segnale l'ha interrotta. Su EX-OS non ci sono segnali che
     * possano interromperla, quindi e' sempre 0: la firma dice la verita'
     * sul contratto, il valore dice la verita' su questo sistema. Con un
     * ritorno void il `while ((s = sleep(s))) {}` della libstdc++ non
     * compilava nemmeno. */
    /* ! I CAMPI DI struct stat HANNO I TIPI DI POSIX, non `unsigned int`.
     * Sul nostro bersaglio hanno la stessa larghezza, quindi i VALORI
     * erano corretti anche prima e nessuno se ne era accorto — il tipo si
     * vede solo quando qualcuno prende l'INDIRIZZO di un campo, ed e'
     * quello che fa libcpp di GCC con &st.st_size. Qui si controlla che
     * `off_t *` sia il tipo giusto: se st_size tornasse `unsigned int`,
     * questa riga non compilerebbe. */
    {
        struct stat s;
        off_t      *punta = &s.st_size;
        int         ok = (stat("/bin/hello", &s) == 0);

        *punta = *punta;    /* usa il puntatore: e' il controllo di tipo */
        esito("stat riesce su /bin/hello", ok);
        esito("st_size e' un off_t e non e' zero", ok && s.st_size > 0);
        esito("st_blksize e' il settore", ok && s.st_blksize == 512);
        esito("st_blocks copre st_size",
              ok && (blkcnt_t)s.st_blocks * 512 >= (blkcnt_t)s.st_size);
    }

    esito("sleep(0) ritorna 0, non void",      sleep(0) == 0);
    esito("usleep ritorna 0 e non fallisce",   usleep(1000) == 0);

    /* -----------------------------------------------------------------
     * Le funzioni che chiede GCC quando gira come programma ospite.
     * ----------------------------------------------------------------- */
    {
        struct rusage u;
        void  *p;

        esito("getpagesize",  getpagesize() == 4096);
        esito("e combacia con sysconf",
              getpagesize() == (int)sysconf(_SC_PAGESIZE));

        /* ! ru_utime e' un LIMITE SUPERIORE (il tempo trascorso dall'avvio),
         * non il tempo di CPU di questo processo: EX-OS non tiene
         * contabilita' per processo. Si prova che la chiamata riesca e che
         * i campi che non sappiamo riempire siano davvero azzerati. */
        esito("getrusage riesce", getrusage(RUSAGE_SELF, &u) == 0);
        esito("e azzera cio' che non sa",
              u.ru_stime.tv_sec == 0 && u.ru_maxrss == 0 && u.ru_majflt == 0);
        esito("getrusage rifiuta un `chi` sconosciuto",
              getrusage(42, &u) == -1 && errno == EINVAL);

        /* ! mmap ritorna MAP_FAILED, cioe' (void*)-1, NON NULL. */
        p = mmap(NULL, 3 * 4096, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        esito("mmap anonima riesce", p != MAP_FAILED);
        if (p != MAP_FAILED) {
            esito("ed e' allineata alla pagina",
                  ((uintptr_t)p & 4095u) == 0);
            memset(p, 0x9C, 3 * 4096);      /* dev'essere scrivibile davvero */
            esito("e si scrive fino in fondo",
                  ((unsigned char *)p)[3 * 4096 - 1] == 0x9C);
            esito("munmap riesce", munmap(p, 3 * 4096) == 0);
        }

        /* ! Mappare un FILE non si puo', e si dice invece di consegnare
         * zeri: una mmap che finge darebbe un programma che legge dati
         * sbagliati senza che niente lo segnali. */
        esito("mmap di un file e' rifiutata",
              mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, 1, 0) == MAP_FAILED &&
              errno == ENODEV);
    }

    esito("sysconf pagina",   sysconf(_SC_PAGESIZE) == 4096);
    esito("sysconf sconosciuta fallisce", sysconf(9999) == -1);
    esito("setlocale C",      setlocale(LC_ALL, "C") != NULL);
    esito("setlocale altro no", setlocale(LC_ALL, "it_IT.UTF-8") == NULL);
    esito("strsignal",        strsignal(SIGSEGV) != NULL);
}

/* =============================================================================
 * Variabili __thread — il thread pointer
 *
 * EX-OS ha un filo per processo, quindi una variabile thread-local e' una
 * variabile globale con un nome piu' lungo: qui non si prova che siano
 * SEPARATE fra fili — non ci sono fili — si prova che **esistano**, cioe'
 * che `%gs:0` punti al blocco TLS del processo invece che a zero.
 *
 * E' esattamente cio' su cui e' morto il primo `as` nativo:
 *
 *     [FAULT] page fault a 0x00000000 (lettura, EIP=...)
 *      mov %gs:0x0,%ebx      <- bfd_init, terza istruzione
 *
 * La prova che conta e' la terza: il valore deve sopravvivere a
 * sched_yield(). Il descrittore GDT del TLS e' UNO SOLO per tutto il
 * sistema e la sua base la riscrive lo scheduler a ogni cambio di
 * contesto; se qualcuno se ne dimenticasse — o se un interrupt
 * ricaricasse GS con il selettore dati normale, come facevano gli stub
 * fino ad agosto 2026 — la lettura dopo lo switch tornerebbe da un
 * indirizzo diverso.
 * ============================================================================= */
static __thread int  tls_contatore = 1234;
static __thread char tls_testo[16] = "thread";

static void prova_tls(void)
{
    printf("\nVariabili __thread:\n");

    /* .tdata: il valore iniziale arriva dall'immagine nel file. */
    esito("il valore iniziale c'e'", tls_contatore == 1234);
    esito("e anche quello di una stringa", strcmp(tls_testo, "thread") == 0);

    tls_contatore += 1;
    esito("si scrive", tls_contatore == 1235);

    {
        int i;
        for (i = 0; i < 20; i++) {
            tls_contatore++;
            sched_yield();
        }
        esito("sopravvive al cambio di contesto", tls_contatore == 1255);
    }

    /* .tbss: quello che non ha valore iniziale dev'essere azzerato, come
     * ogni altra variabile statica. */
    {
        static __thread int  vuoto;
        static __thread char buffer[32];
        int i, tutti_zero = 1;

        for (i = 0; i < 32; i++) if (buffer[i] != 0) tutti_zero = 0;
        esito("la parte .tbss e' azzerata", vuoto == 0 && tutti_zero);
    }

    /* L'indirizzo di una variabile thread-local sta nel blocco del
     * processo, non nel segmento dati: deve essere ben lontano da dove
     * vive il resto. */
    esito("l'indirizzo non e' zero", &tls_contatore != NULL);
}

/* =============================================================================
 * IL TETTO DELLO HEAP (kernel 0.156)
 *
 * Sta QUI, dopo prova_tls, e non insieme al resto dell'allocatore: la
 * prova ha bisogno di `tls_contatore`, perche' il danno che il tetto
 * impedisce e' proprio quello.
 *
 * COSA ANDAVA STORTO. Fino alla 0.155 sys_sbrk cresceva finche' il PMM
 * aveva pagine da dare: l'unico limite era la RAM FISICA, non lo spazio
 * di indirizzamento. Ma sopra lo heap non c'e' il vuoto — c'e' il blocco
 * TLS del processo, e sopra quello la riserva dello stack. E
 * paging_map_page() sovrascrive una PTE gia' presente SENZA DIRE NIENTE.
 * Uno heap abbastanza grande avrebbe rimappato il proprio blocco TLS su
 * pagine nuove azzerate: il thread pointer sarebbe andato a zero e ogni
 * variabile __thread avrebbe cominciato a leggere memoria altrui, senza
 * un fault e senza un log.
 *
 * ! QUESTA PROVA NON RAGGIUNGE IL TETTO, E NON PUO'. Fra lo heap e la
 * riserva dello stack ci sono quasi 3 GB, mentre QEMU qui ha 32 MB: la
 * memoria fisica finisce molto prima dello spazio di indirizzamento. Il
 * tetto serve alla macchina che di RAM ne ha abbastanza — quella su cui
 * un giorno girera' cc1.
 *
 * Quello che si prova qui e' l'altra meta', ed e' altrettanto importante:
 * che un sbrk RIFIUTATO lasci il processo esattamente com'era. Un rifiuto
 * a meta' strada — con qualche pagina gia' mappata e heap_end avanzato di
 * un valore che il chiamante non ha mai visto — sarebbe peggio del
 * difetto che sostituisce.
 * ============================================================================= */
static void prova_tetto_heap(void)
{
    void *prima;
    long  totale = 0;
    int   passi  = 0;
    int   rifiutato = 0;
    char *p;
    /* ! Si legge PRIMA, invece di confrontare con 1234: prova_tls()
     * incrementa il contatore, e un valore atteso scritto a mano qui
     * diventerebbe falso il giorno che quella prova cambia di un giro. */
    int   tls_prima = tls_contatore;

    printf("\nCrescita dello heap:\n");

    prima = sbrk(0);
    esito("sbrk(0) da' la cima dell'heap", prima != (void *)-1);

    /* ! IL TETTO SI PROVA CON UNA RICHIESTA ENORME, NON CRESCENDO A 1 MB.
     *
     * Qui c'era un ciclo che chiedeva 1 MB per 64 volte e pretendeva un
     * rifiuto. Passava — su una macchina da 64 MB, dove a fallire era la
     * RAM. Con `EXOS_RAM=512M` i 64 MB ci stanno tutti, il rifiuto non
     * arriva e la prova diventava rossa su un sistema perfettamente sano:
     * misurava quanto e' grande la macchina, non se sbrk ha un tetto.
     *
     * E non poteva essere altrimenti: il tetto vero e' `heap_max`, che sta
     * a quasi 3 GB da heap_start (vedi Process.heap_max in sched.h). A
     * 1 MB per volta non lo si raggiunge nemmeno con 64 passi.
     *
     * Una richiesta piu' grande dello spazio di indirizzamento invece
     * dev'essere rifiutata su QUALUNQUE macchina, con qualunque RAM: e' la
     * proprieta' che si voleva provare, ed e' vera indipendentemente da
     * dove gira la prova. */
    esito("sbrk rifiuta una richiesta piu' grande dello spazio",
          sbrk(0x7FFF0000) == (void *)-1);

    /* La crescita a blocchi resta, ma come MISURA e non come verdetto: se
     * il rifiuto arriva dice che la RAM e' finita — vero su una macchina
     * piccola, falso su una grande — e in entrambi i casi cio' che conta
     * e' che il sistema regga, cosa che provano le righe qui sotto. */
    while (passi < 64) {
        if (sbrk(1024 * 1024) == (void *)-1) { rifiutato = 1; break; }
        totale += 1024 * 1024;
        passi++;
    }
    printf("           (cresciuto di %d MB%s)\n", passi,
           rifiutato ? ", poi la RAM e' finita" : ", senza esaurire la RAM");

    /* ! SI RESTITUISCE TUTTO, e non e' pulizia facoltativa: free() non
     * chiama mai sbrk con un incremento negativo, quindi senza questa riga
     * il processo terrebbe tutta la RAM libera del sistema fino alla
     * propria uscita, e le prove successive non troverebbero piu' niente. */
    if (totale > 0) esito("sbrk negativo restituisce la memoria",
                          sbrk(-(int)totale) != (void *)-1);

    esito("e lo heap torna dov'era", sbrk(0) == prima);

    /* Le due prove che dicono se il rifiuto ha toccato cio' che sta
     * SOPRA lo heap. La prima e' il blocco TLS, che e' il vicino piu'
     * prossimo; la seconda e' lo stack, dove vivono queste variabili. */
    esito("le variabili __thread sono intatte",
          tls_contatore == tls_prima && strcmp(tls_testo, "thread") == 0);

    p = (char *)malloc(64 * 1024);
    if (p != NULL) memset(p, 0x5A, 64 * 1024);
    esito("l'heap funziona ancora dopo aver preso e restituito", p != NULL);
    free(p);

    /* -----------------------------------------------------------------
     * LA MEMORIA TORNA AL KERNEL (0.157)
     *
     * Fino alla 0.156 free() non chiamava mai sbrk con un incremento
     * negativo: un blocco liberato tornava disponibile per il PROCESSO,
     * non per il SISTEMA. Un compilatore, che costruisce e butta un
     * albero di sintassi per funzione, teneva il picco massimo fino
     * all'uscita — e il processo dopo trovava la macchina piena.
     *
     * ! LA PROVA GUARDA sbrk(0), NON malloc(). Che malloc riesca lo
     * sapevamo gia': la lista dei blocchi liberi bastava a quello. La
     * domanda e' se il CONFINE si e' abbassato, ed e' l'unica cosa che
     * distingue "memoria riusabile" da "memoria restituita".
     * ----------------------------------------------------------------- */
    {
        void *base, *dopo_alloc, *dopo_free;
        char *grosso;

        base   = sbrk(0);
        grosso = (char *)malloc(2 * 1024 * 1024);
        if (grosso != NULL) memset(grosso, 0x33, 2 * 1024 * 1024);
        dopo_alloc = sbrk(0);
        esito("2 MB fanno salire il confine",
              grosso != NULL && dopo_alloc > base);

        free(grosso);
        dopo_free = sbrk(0);
        esito("e la free lo fa riscendere", dopo_free < dopo_alloc);

        /* ! NON si controlla `dopo_free >= base`, e la prima versione di
         * questa prova lo faceva SBAGLIANDO. Il blocco da 2 MB si fonde
         * con la coda libera che c'era gia' prima di `base`, quindi
         * comincia PIU' IN BASSO di dove stava il confine quando l'abbiamo
         * campionato: restituire fin sotto `base` e' corretto, non un
         * difetto.
         *
         * Quello che «una coda si tiene» vuol dire davvero e' questo: una
         * piccola allocazione subito dopo NON deve toccare il kernel. Se
         * la coda fosse stata restituita tutta, questa malloc farebbe una
         * sbrk e il confine si muoverebbe. */
        {
            void *prima_piccola = sbrk(0);
            char *piccolo = (char *)malloc(1024);
            esito("ma una coda si tiene (nessuna syscall per 1 KB)",
                  piccolo != NULL && sbrk(0) == prima_piccola);
            free(piccolo);
        }

        /* E dopo aver restituito, l'heap deve essere ancora sano: se il
         * blocco in coda fosse rimasto con una dimensione che non
         * corrisponde piu' alla memoria mappata, la prima scrittura qui
         * dentro sarebbe un page fault. */
        grosso = (char *)malloc(128 * 1024);
        if (grosso != NULL) memset(grosso, 0x77, 128 * 1024);
        esito("e si rialloca senza danni", grosso != NULL);
        free(grosso);
    }
}

/* =============================================================================
 * dup, dup2, fcntl
 *
 * La prova che conta e' l'ULTIMA: quella che il file resta aperto dopo che
 * si e' chiuso il primo dei due descrittori. E' il gesto che fanno `ar`,
 * `objcopy` e `arsup` di binutils — `fd = dup(fd)` per sopravvivere alla
 * close() di chi possedeva l'originale — ed e' il motivo per cui dup()
 * esiste in EX-OS. Senza il conteggio dei riferimenti nel VFS, la prima
 * close() chiuderebbe il file sotto i piedi all'altro fd, e la lettura che
 * viene dopo risponderebbe EBADF.
 * ============================================================================= */
static void prova_dup(void)
{
    const char *nome = "/dup.tmp";
    int         a, b, n;
    char        buf[8];

    printf("\nDescrittori duplicati:\n");

    unlink(nome);

    a = open(nome, O_RDWR | O_CREAT | O_TRUNC);
    esito("apre il file di prova", a >= 0);
    if (a < 0) return;

    write(a, "ABCDEF", 6);

    b = dup(a);
    esito("dup da' un descrittore diverso", b >= 0 && b != a);

    /* ! La posizione NON e' condivisa: qui si legge da capo perche' `b`
     * parte da dove stava `a` (in coda) e ci si posiziona a mano. Su un
     * sistema POSIX questa lseek spostererebbe anche `a`. */
    esito("lseek sul duplicato",   lseek(b, 0, SEEK_SET) == 0);
    n = (int)read(b, buf, 3);
    esito("si legge dal duplicato", n == 3 && buf[0] == 'A');

    /* Il punto di tutto: chiudo il primo, il file deve restare aperto. */
    close(a);
    esito("il file resta aperto dopo la close del primo",
          lseek(b, 0, SEEK_SET) == 0 && read(b, buf, 1) == 1 && buf[0] == 'A');

    /* dup2 su un numero scelto: quello vecchio, se occupato, si chiude. */
    {
        int c = open(nome, O_RDONLY);
        esito("apre un secondo descrittore", c >= 0);
        if (c >= 0) {
            esito("dup2 mette il file dove dico", dup2(b, c) == c);
            esito("e il descrittore ci legge dentro",
                  lseek(c, 0, SEEK_SET) == 0 && read(c, buf, 1) == 1);
            close(c);
        }
    }

    esito("dup2 di un fd su se stesso non fa niente", dup2(b, b) == b);
    esito("dup di un fd chiuso fallisce",             dup(a) < 0);

    /* fcntl */
    esito("F_GETFD su un fd valido", fcntl(b, F_GETFD) == 0);
    esito("F_SETFD accetta",         fcntl(b, F_SETFD, FD_CLOEXEC) == 0);
    esito("F_GETFL rida' i flag di open",
          (fcntl(b, F_GETFL) & O_ACCMODE) == O_RDWR);
    {
        int d = fcntl(b, F_DUPFD, 5);
        esito("F_DUPFD duplica da un numero in su", d >= 5);
        if (d >= 0) close(d);
    }
    esito("fcntl su un fd chiuso fallisce", fcntl(a, F_GETFD) < 0);
    esito("un comando sconosciuto fallisce", fcntl(b, 999) < 0);

    close(b);
    unlink(nome);
}

/* =============================================================================
 * LA PROVA CHE SERVE AL COMPILATORE: lanciare un figlio e prenderne
 * l'uscita.
 *
 * E' quello che fa `gcc` con cc1, as e ld — e finche' non funziona, avere
 * un compilatore sul disco non serve a niente. Si lancia /bin/hello con
 * stdout rediretto su un file, si aspetta che finisca, e si rilegge il
 * file: se dentro c'e' cio' che hello stampa, allora spawn, redirezione e
 * waitpid funzionano tutti e tre.
 * ============================================================================= */
static void prova_spawn(void)
{
    const char *uscita = "/uscita.txt";
    SpawnRedir  red;
    char       *argv[2];
    int         pid, stato = 0;

    printf("\nProcessi:\n");

    unlink(uscita);

    argv[0] = (char *)"/bin/hello";
    argv[1] = NULL;

    red.fd       = 1;                               /* stdout del figlio */
    red.flags    = O_WRONLY | O_CREAT | O_TRUNC;
    red.percorso = uscita;
    red.fd_padre = -1;                              /* non si eredita niente */

    pid = spawn_ex("/bin/hello", argv, environ, &red, 1);
    esito("spawn con redirezione", pid > 0);
    if (pid <= 0) return;

    esito("waitpid raccoglie il figlio", waitpid(pid, &stato, 0) == pid);

    {
        FILE *f = fopen(uscita, "r");
        char  riga[128];
        int   letto = 0;

        if (f != NULL) {
            letto = (fgets(riga, sizeof(riga), f) != NULL);
            fclose(f);
        }
        esito("l'uscita del figlio e' finita nel file", letto);
        esito("e il contenuto e' quello giusto",
              letto && strstr(riga, "Ciao") != NULL);
    }

    unlink(uscita);

    /* Un figlio che non esiste deve fallire, non restare appeso. */
    esito("spawn di un programma assente fallisce",
          spawn("/bin/non-esiste-di-sicuro", argv) < 0);
}

/* =============================================================================
 * Le pipe
 *
 * ! LE PROVE CHE CONTANO SONO QUELLE DI CONFINE, non il giro di andata e
 * ritorno. Una pipe che trasporta byte quando tutto va bene e' facile;
 * quella che serve e' quella che sa distinguere «aspetta» da «e' finita»,
 * e che non lascia nessuno bloccato per sempre quando l'altro sparisce.
 *
 * Qui si provano, in ordine:
 *   1. il giro di andata e ritorno dentro UN processo (il caso facile);
 *   2. ! la fine dei dati: chiusa la scrittura, read deve dare 0;
 *   3. ! EPIPE: chiuso il lettore, write deve fallire e non aspettare;
 *   4. ! due processi veri, con l'estremita' ereditata da spawn.
 * ============================================================================= */
static void prova_pipe(void)
{
    int  p[2];
    char buf[64];
    int  n;

    printf("\nPipe:\n");

    esito("pipe() riesce", pipe(p) == 0);
    esito("e da' due descrittori diversi e validi",
          p[0] >= 3 && p[1] >= 3 && p[0] != p[1]);

    /* 1. Andata e ritorno. */
    n = (int)write(p[1], "in un tubo", 10);
    esito("si scrive nel tubo", n == 10);
    memset(buf, 0, sizeof(buf));
    n = (int)read(p[0], buf, sizeof(buf));
    esito("e si rilegge dall'altra parte",
          n == 10 && memcmp(buf, "in un tubo", 10) == 0);

    /* ! Leggere dall'estremita' di scrittura, o scrivere in quella di
     * lettura, e' un errore e non un'attesa: la direzione e' parte del
     * descrittore. */
    esito("leggere dall'estremita' sbagliata fallisce",
          read(p[1], buf, 1) < 0);
    esito("scrivere in quella sbagliata fallisce",
          write(p[0], "x", 1) < 0);

    /* 2. ! FINE DEI DATI. Chiusa l'ultima scrittura, una read su pipe
     * vuota deve dare 0 — non bloccare. Questa e' la prova che il
     * conteggio degli scrittori esiste e funziona: senza, il processo
     * resterebbe qui per sempre e la prova non stamperebbe mai. */
    close(p[1]);
    n = (int)read(p[0], buf, sizeof(buf));
    esito("chiusa la scrittura, read da' 0 (fine dei dati)", n == 0);
    close(p[0]);

    /* 3. ! EPIPE. Chiuso il lettore, una write deve FALLIRE: quei byte
     * non li leggera' nessuno. Su Unix arriverebbe anche SIGPIPE, che
     * EX-OS non ha. */
    esito("una seconda pipe si apre", pipe(p) == 0);
    close(p[0]);
    esito("senza lettori, write da' EPIPE",
          write(p[1], "nessuno legge", 13) < 0 && errno == EPIPE);
    close(p[1]);

    /* 4. ! DUE PROCESSI VERI. E' il caso per cui le pipe esistono, e
     * quello che non funzionerebbe senza l'eredita' dei descrittori:
     * si lancia /bin/hello con stdout attaccato alla pipe e si legge
     * qui quello che stampa.
     *
     * ! IL PADRE CHIUDE SUBITO LA SUA COPIA DELL'ESTREMITA' DI
     * SCRITTURA. Se non lo facesse, la pipe conterebbe ancora uno
     * scrittore vivo — lui — e la read qui sotto non finirebbe mai. */
    {
        SpawnRedir red;
        char      *argv[2];
        int        pid, stato = 0, tot = 0;

        argv[0] = (char *)"/bin/hello";
        argv[1] = NULL;

        if (pipe(p) != 0) { esito("pipe fra due processi", 0); return; }

        red.fd       = 1;        /* stdout del figlio */
        red.flags    = 0;
        red.percorso = NULL;     /* NULL = eredita un descrittore */
        red.fd_padre = p[1];     /* ...questo */

        pid = spawn_ex("/bin/hello", argv, environ, &red, 1);
        esito("spawn con un'estremita' di pipe ereditata", pid > 0);

        close(p[1]);             /* ! indispensabile: vedi sopra */

        if (pid > 0) {
            memset(buf, 0, sizeof(buf));
            for (;;) {
                n = (int)read(p[0], buf + tot,
                              (int)sizeof(buf) - 1 - tot);
                if (n <= 0) break;
                tot += n;
                if (tot >= (int)sizeof(buf) - 1) break;
            }
            esito("si legge cio' che il figlio ha stampato",
                  tot > 0 && strstr(buf, "Ciao") != NULL);
            esito("e la lettura finisce da sola quando il figlio esce",
                  n == 0);
            waitpid(pid, &stato, 0);
        }
        close(p[0]);
    }
}

/* =============================================================================
 * L'interruzione — «smettila», detto da fuori
 *
 * ! IL FIGLIO SI FA BLOCCARE IN UNA LETTURA, ed e' il caso che conta: un
 * processo fermo dentro il kernel ad aspettare qualcosa che non arrivera' mai.
 * Interrompere uno che gira e basta sarebbe piu' facile e proverebbe meno —
 * quello si accorge del flag alla prima syscall, mentre questo va SVEGLIATO,
 * deve rendere -EINTR dalla sua attesa e morire prima di tornare in ring 3.
 * ============================================================================= */
static void prova_interruzione(void)
{
    int  p[2];
    int  pid, stato = 0;
    char *argv[2];
    SpawnRedir red;

    printf("\nInterruzione:\n");

    esito("interrompere se stessi e' rifiutato", interrompi(getpid()) == -EINVAL);
    esito("init non si tocca",                   interrompi(1) == -EPERM);
    esito("un pid che non esiste da' -ESRCH",    interrompi(60000) == -ESRCH);

    /* Il figlio legge da una pipe in cui non scrivera' nessuno: resta li'
     * finche' non lo si interrompe. */
    if (pipe(p) != 0) { esito("pipe per la prova", 0); return; }

    memset(&red, 0, sizeof(red));
    red.fd       = 0;               /* la pipe diventa il suo stdin */
    red.percorso = 0;               /* si passa un descrittore, non un file */
    red.fd_padre = p[0];

    /* ! IL FIGLIO E' UNA SHELL, e non un programma qualunque: e' esattamente
     * cio' che stara' dall'altra parte di un pty — una shell che aspetta
     * comandi da un tubo in cui non arriva niente. */
    argv[0] = "/bin/sh";
    argv[1] = 0;

    pid = spawn_ex("/bin/sh", argv, environ, &red, 1);
    esito("il figlio parte", pid > 0);
    if (pid <= 0) { close(p[0]); close(p[1]); return; }

    /* Gli si da' il tempo di arrivare alla lettura e di bloccarcisi. */
    usleep(300000);

    esito("interrompere un figlio nostro riesce", interrompi(pid) == 0);

    /* ! E SI ASPETTA CHE MUOIA DAVVERO, che e' l'unica cosa che prova qualcosa.
     * Se il flag non svegliasse chi dorme, waitpid resterebbe qui per sempre e
     * la prova si pianterebbe invece di fallire — ed e' il motivo per cui il
     * figlio e' `cat` su una pipe muta e non un ciclo infinito. */
    esito("waitpid lo raccoglie", waitpid(pid, &stato, 0) == pid);
    esito("ed e' uscito con 130 (128 + 2, come un Ctrl+C)", stato == 130);

    close(p[0]);
    close(p[1]);
}

/* =============================================================================
 * Lo pseudo-terminale — la disciplina di linea, che una pipe non ha
 * ============================================================================= */
static void prova_pty(void)
{
    int  fd[2];
    char buf[256];
    int  n;

    printf("\nPseudo-terminali:\n");

    esito("pty_apri() riesce", pty_apri(fd) == 0);
    esito("e da' due descrittori diversi", fd[0] != fd[1] && fd[0] >= 3);

    /* 1. L'eco: cio' che si batte torna indietro dal master, e NON e' ancora
     *    arrivato allo slave — la riga non e' finita. */
    write(fd[0], "ab", 2);
    n = read(fd[0], buf, sizeof(buf));
    buf[n > 0 ? n : 0] = '\0';
    esito("l'eco torna dal master", n == 2 && buf[0] == 'a' && buf[1] == 'b');

    /* 2. Backspace: tre caratteri di eco, e la lettera sparisce dalla riga. */
    write(fd[0], "\b", 1);
    n = read(fd[0], buf, sizeof(buf));
    esito("Backspace fa eco «indietro, spazio, indietro»",
          n == 3 && buf[0] == '\b' && buf[1] == ' ' && buf[2] == '\b');

    /* 3. L'Invio consegna la riga allo slave, e prima non arriva niente. */
    write(fd[0], "c\n", 2);
    n = read(fd[1], buf, sizeof(buf));
    buf[n > 0 ? n : 0] = '\0';
    esito("lo slave riceve la riga solo all'Invio",
          n == 3 && strcmp(buf, "ac\n") == 0);

    /* ! PRIMA SI SVUOTA L'ECO, e la prima stesura di questa prova non lo
     * faceva: l'eco di «c» e dell'Invio era ancora nel tubo di ritorno, e il
     * confronto trovava «c\nci» al posto di «ciao». Non era il pty a
     * sbagliare — era la prova a credere che il master fosse vuoto. */
    n = read(fd[0], buf, sizeof(buf));
    esito("l'eco dell'Invio era li' e si legge", n == 2);

    /* 4. Quello che lo slave scrive esce dal master senza essere toccato. */
    write(fd[1], "ciao", 4);
    n = read(fd[0], buf, sizeof(buf));
    buf[n > 0 ? n : 0] = '\0';
    esito("l'uscita del programma esce dal master", n == 4 && strcmp(buf, "ciao") == 0);

    /* 5. La misura: si scrive e si rilegge. */
    esito("la misura si imposta",
          pty_ctl(fd[0], PTY_CTL_MISURA, (24u << 16) | 80u) == 0);
    esito("e si rilegge",
          pty_ctl(fd[1], PTY_CTL_LEGGI_MISURA, 0) == (int)((24u << 16) | 80u));

    /* 6. In modo grezzo ogni byte passa subito, senza aspettare l'Invio. */
    esito("si passa al modo grezzo",
          pty_ctl(fd[0], PTY_CTL_MODO, PTY_ECO) == 0);
    write(fd[0], "x", 1);
    n = read(fd[1], buf, sizeof(buf));
    esito("e il byte arriva subito allo slave", n == 1 && buf[0] == 'x');

    /* 7. Ctrl+C interrompe chi si e' dichiarato in primo piano. */
    {
        int  pid, stato = 0;
        char *argv[2];
        SpawnRedir red;

        pty_ctl(fd[0], PTY_CTL_MODO, PTY_CANONICO | PTY_ECO);

        memset(&red, 0, sizeof(red));
        red.fd       = 0;
        red.percorso = 0;
        red.fd_padre = fd[1];

        argv[0] = "/bin/sh";
        argv[1] = 0;

        pid = spawn_ex("/bin/sh", argv, environ, &red, 1);
        esito("una shell parte sullo slave", pid > 0);

        if (pid > 0) {
            usleep(300000);
            esito("la si dichiara in primo piano",
                  pty_ctl(fd[0], PTY_CTL_FG, (unsigned int)pid) == 0);

            /* ! IL BYTE 3 NON ARRIVA ALLA SHELL: lo mangia la disciplina, che
             * interrompe. E' la differenza fra un pty e una pipe, in un byte. */
            write(fd[0], "\003", 1);

            esito("Ctrl+C la interrompe", waitpid(pid, &stato, 0) == pid);
            esito("ed e' uscita con 130", stato == 130);
        }
    }

    close(fd[0]);
    close(fd[1]);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    printf("libctest — prova della libc di EX-OS\n");

    prova_allocatore();
    prova_printf();
    prova_stdio();
    prova_setjmp();
    prova_resto();
    prova_errno();
    prova_virgola();
    prova_sscanf();
    prova_stat();
    prova_ambiente();
    prova_directory();
    prova_orologio();
    prova_chiusura_standard();
    prova_entropia();
    prova_temporanei();
    prova_terzi();
    prova_tls();
    prova_tetto_heap();   /* DOPO prova_tls: usa le sue variabili __thread */
    prova_dup();
    prova_spawn();
    prova_pipe();
    prova_interruzione();
    prova_pty();

    printf("\n%d prove superate, %d fallite\n", passati, falliti);
    return (falliti == 0) ? 0 : 1;
}
