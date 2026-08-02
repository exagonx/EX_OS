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

    a = (char *)calloc(64, 4);
    ok = (a != NULL);
    for (i = 0; ok && i < 64 * 4; i++) if (a[i] != 0) ok = 0;
    esito("calloc azzera", ok);
    free(a);

    esito("free(NULL) non fa danni", (free(NULL), 1));
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

        /* ⚠️ chmod e umask non cambiano niente: si prova che RISPONDANO,
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
    /* ⚠️ IL VALORE ATTESO STA IN UNA VARIABILE, e non e' pignoleria.
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

    esito("strtoull 64 bit",     strtoull("12345678901", NULL, 10) == 12345678901ULL);
    esito("strtoll negativo",    strtoll("-9000000000", NULL, 10) == -9000000000LL);
    esito("strtoull esadecimale", strtoull("0xFFFFFFFF", NULL, 16) == 4294967295ULL);
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

    esito("sysconf pagina",   sysconf(_SC_PAGESIZE) == 4096);
    esito("sysconf sconosciuta fallisce", sysconf(9999) == -1);
    esito("setlocale C",      setlocale(LC_ALL, "C") != NULL);
    esito("setlocale altro no", setlocale(LC_ALL, "it_IT.UTF-8") == NULL);
    esito("strsignal",        strsignal(SIGSEGV) != NULL);
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

    /* ⚠️ La posizione NON e' condivisa: qui si legge da capo perche' `b`
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
    prova_temporanei();
    prova_terzi();
    prova_dup();
    prova_spawn();

    printf("\n%d prove superate, %d fallite\n", passati, falliti);
    return (falliti == 0) ? 0 : 1;
}
