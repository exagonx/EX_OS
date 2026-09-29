/* =============================================================================
 * tools/prove/jsprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Il banco di ExJs, che gira SULL'HOST.
 *
 * ! UN MOTORE SI PROVA A PEZZI, E IL PRIMO E' L'ANALIZZATORE LESSICALE. Un
 * difetto qui non si vede mai come un difetto qui: si vede come un programma
 * che fa un'altra cosa. `a >>>= b` letto male diventa un confronto seguito da
 * spazzatura, e il messaggio d'errore parla di un punto lontano da dove sta lo
 * sbaglio. Provarlo da solo, contando i gettoni, e' l'unico modo di sapere che
 * la base regge prima di costruirci sopra.
 *
 *     make prova-exjs
 * ============================================================================= */

#include <stdio.h>
#include <string.h>
#include "exjs_int.h"

static int fatte = 0, sbagliate = 0;

/* -----------------------------------------------------------------------------
 * Attesa: la sequenza dei tipi di gettone che il testo deve produrre.
 * L'elenco finisce con TK_FINE.
 * --------------------------------------------------------------------------- */
static void prova(const char *nome, const char *testo, const int *attesi)
{
    ExJsLex    L;
    ExJsErrore err;
    char       buf[512];
    int        i = 0, t;

    fatte++;
    memset(&err, 0, sizeof(err));
    exjs_lex_apri(&L, testo, (unsigned int)strlen(testo), buf, sizeof(buf), &err);

    for (;;) {
        t = exjs_lex_avanti(&L);

        if (t == TK_ERRORE) {
            /* Un errore atteso si dichiara mettendo TK_ERRORE nell'elenco. */
            if (attesi[i] == TK_ERRORE) {
                printf("ok   %-34s errore atteso: %s\n", nome, err.messaggio);
                return;
            }
            printf("NO   %-34s riga %d col %d: %s\n", nome, err.riga,
                   err.colonna, err.messaggio);
            sbagliate++;
            return;
        }

        if (attesi[i] != t) {
            printf("NO   %-34s gettone %d: atteso %s, trovato %s\n", nome, i,
                   exjs_lex_nome(attesi[i]), exjs_lex_nome(t));
            sbagliate++;
            return;
        }
        if (t == TK_FINE) break;
        i++;
    }

    printf("ok   %-34s %d gettoni\n", nome, i);
}

/* Un solo gettone, e si guarda il VALORE: per i numeri e per le stringhe il
 * tipo non basta — `0x10` e `16` hanno lo stesso tipo e devono avere lo stesso
 * valore, ed e' quello il conto che puo' sbagliare. */
static void prova_numero(const char *nome, const char *testo, double atteso)
{
    ExJsLex    L;
    ExJsErrore err;
    char       buf[256];
    double     d;

    fatte++;
    memset(&err, 0, sizeof(err));
    exjs_lex_apri(&L, testo, (unsigned int)strlen(testo), buf, sizeof(buf), &err);

    if (exjs_lex_avanti(&L) != TK_NUMERO) {
        printf("NO   %-34s non e' stato letto come numero\n", nome);
        sbagliate++;
        return;
    }
    d = L.numero - atteso;
    if (d < 0) d = -d;

    /* ! LA TOLLERANZA C'E' PERCHE' 0.1 NON ESISTE IN BINARIO. Confrontare due
     * double con `==` e' il modo classico di scrivere una prova che fallisce
     * per il motivo sbagliato. */
    if (d > 1e-9) {
        printf("NO   %-34s atteso %g, trovato %g\n", nome, atteso, L.numero);
        sbagliate++;
        return;
    }
    printf("ok   %-34s %g\n", nome, L.numero);
}

static void prova_stringa(const char *nome, const char *testo, const char *atteso)
{
    ExJsLex    L;
    ExJsErrore err;
    char       buf[256];

    fatte++;
    memset(&err, 0, sizeof(err));
    exjs_lex_apri(&L, testo, (unsigned int)strlen(testo), buf, sizeof(buf), &err);

    if (exjs_lex_avanti(&L) != TK_STRINGA) {
        printf("NO   %-34s non e' stata letta come stringa (%s)\n", nome,
               err.messaggio);
        sbagliate++;
        return;
    }
    if (strcmp(L.testo, atteso) != 0) {
        printf("NO   %-34s atteso \"%s\", trovato \"%s\"\n", nome, atteso, L.testo);
        sbagliate++;
        return;
    }
    printf("ok   %-34s \"%s\"\n", nome, L.testo);
}

/* Il conto delle righe: un errore alla riga sbagliata manda a cercare nel
 * posto sbagliato, ed e' l'unica cosa che l'utente legge davvero. */
static void prova_riga(const char *nome, const char *testo, int riga_attesa)
{
    ExJsLex    L;
    ExJsErrore err;
    char       buf[256];
    int        t;

    fatte++;
    memset(&err, 0, sizeof(err));
    exjs_lex_apri(&L, testo, (unsigned int)strlen(testo), buf, sizeof(buf), &err);

    do { t = exjs_lex_avanti(&L); } while (t != TK_FINE && t != TK_ERRORE);

    if (t != TK_ERRORE) {
        printf("NO   %-34s doveva dare errore e non l'ha dato\n", nome);
        sbagliate++;
        return;
    }
    if (err.riga != riga_attesa) {
        printf("NO   %-34s errore atteso a riga %d, dato a riga %d\n", nome,
               riga_attesa, err.riga);
        sbagliate++;
        return;
    }
    printf("ok   %-34s errore alla riga %d\n", nome, err.riga);
}

/* =============================================================================
 * L'ALBERO, STAMPATO COME TESTO
 *
 * ! LA PRECEDENZA E' LA COSA CHE SI SBAGLIA IN SILENZIO. `a + b * c` con le
 * precedenze invertite non da' nessun errore: da' un numero diverso, e il
 * numero diverso si vede tre schermate piu' in la'. L'unico modo di vedere un
 * albero storto e' guardarlo — quindi lo si stampa in una forma senza
 * ambiguita' (tutto fra parentesi) e la si confronta lettera per lettera.
 *
 * ! E LA FORMA E' A PARENTESI, NON RIENTRATA. Un albero rientrato si legge
 * meglio a occhio e si confronta peggio: la prova attesa diventa una stringa
 * su piu' righe piena di spazi, e uno spazio di troppo la fa fallire per il
 * motivo sbagliato.
 * ========================================================================== */
static char  g_out[4096];
static int   g_out_n;

static void em(const char *s)
{
    while (*s && g_out_n + 1 < (int)sizeof(g_out)) g_out[g_out_n++] = *s++;
    g_out[g_out_n] = '\0';
}

static void em_num(double v)
{
    char b[40];
    /* Gli interi si stampano senza virgola: un albero pieno di "1.000000" non
     * si legge, e le prove diventano illeggibili proprio dove servono. */
    if (v == (double)(long)v) snprintf(b, sizeof(b), "%ld", (long)v);
    else                      snprintf(b, sizeof(b), "%g", v);
    em(b);
}

static void stampa(const ExJsAst *A, int i);

/* Una lista concatenata con `prossimo`, tutta di seguito. */
static void stampa_lista(const ExJsAst *A, int i)
{
    while (i >= 0) {
        em(" ");
        stampa(A, i);
        i = A->nodi[i].prossimo;
    }
}

static void stampa(const ExJsAst *A, int i)
{
    const ExJsNodo *N;

    if (i < 0) { em("-"); return; }
    N = &A->nodi[i];

    em("(");
    em(exjs_nodo_nome(N->tipo));

    switch (N->tipo) {
    case N_NUMERO:  em(" "); em_num(N->numero); break;
    case N_STRINGA: em(" \""); em(A->arena + N->testo); em("\""); break;
    case N_NOME:
    case N_PARAMETRO:
        em(" "); em(A->arena + N->testo); break;

    case N_VERO: case N_FALSO: case N_NULLO: case N_QUESTO:
    case N_ROMPI: case N_CONTINUA: case N_VUOTO:
        break;

    case N_UNARIO: case N_BINARIO: case N_LOGICO: case N_ASSEGNA:
    case N_PRE: case N_POST:
        em(" "); em(exjs_lex_nome(N->op));
        em(" "); stampa(A, N->a);
        if (N->tipo == N_BINARIO || N->tipo == N_LOGICO || N->tipo == N_ASSEGNA) {
            em(" "); stampa(A, N->b);
        }
        break;

    case N_MEMBRO:
        em(" "); stampa(A, N->a);
        em(" ."); em(A->arena + N->testo);
        break;

    case N_VOCE:
        em(" "); em(A->arena + N->testo);
        em(" "); stampa(A, N->a);
        break;

    case N_DICHIARA:
        em(" "); em(A->arena + N->testo);
        em(" "); stampa(A, N->a);
        break;

    case N_FUNZIONE:
        em(" "); em(N->testo ? A->arena + N->testo : "<anonima>");
        em(" (par"); stampa_lista(A, N->a); em(")");
        em(" "); stampa(A, N->b);
        break;

    /* I nodi che sono una LISTA: programma, blocco, var, vettore, oggetto. */
    case N_PROGRAMMA: case N_BLOCCO: case N_VAR:
    case N_VETTORE: case N_OGGETTO:
        stampa_lista(A, N->a);
        break;

    /* ! for..in USA a, b E d, NON c. Stampare il buco come "-" non e' un
     * errore ma e' rumore: la prova attesa diventa piu' difficile da leggere
     * proprio dove serve leggerla. */
    case N_PER_IN:
        em(" "); stampa(A, N->a);
        em(" "); stampa(A, N->b);
        em(" "); stampa(A, N->d);
        break;

    /* Chiamata e new: il primo figlio e' chi, il secondo e' una lista. */
    case N_CHIAMATA: case N_NUOVO:
        em(" "); stampa(A, N->a);
        em(" (arg"); stampa_lista(A, N->b); em(")");
        break;

    default:
        /* Il caso generale: fino a quattro figli, e si stampano solo quelli
         * che ci sono. */
        if (N->a != -1 || N->b != -1 || N->c != -1 || N->d != -1) {
            em(" "); stampa(A, N->a);
            if (N->b != -1 || N->c != -1 || N->d != -1) { em(" "); stampa(A, N->b); }
            if (N->c != -1 || N->d != -1)               { em(" "); stampa(A, N->c); }
            if (N->d != -1)                             { em(" "); stampa(A, N->d); }
        }
        break;
    }
    em(")");
}

static void prova_albero(const char *nome, const char *testo, const char *atteso)
{
    static ExJsNodo nodi[512];
    static char     arena_buf[4096];
    ExJsAst         A;
    ExJsErrore      err;
    char            testo_buf[512];

    fatte++;
    memset(&err, 0, sizeof(err));
    exjs_ast_prepara(&A, nodi, 512, arena_buf, sizeof(arena_buf));

    if (!exjs_analizza(&A, testo, (unsigned int)strlen(testo),
                       testo_buf, sizeof(testo_buf), &err)) {
        if (atteso == 0) {
            printf("ok   %-34s rifiutato: %s\n", nome, err.messaggio);
            return;
        }
        printf("NO   %-34s riga %d col %d: %s\n", nome, err.riga, err.colonna,
               err.messaggio);
        sbagliate++;
        return;
    }

    if (atteso == 0) {
        printf("NO   %-34s doveva essere rifiutato e non lo e' stato\n", nome);
        sbagliate++;
        return;
    }

    g_out_n = 0; g_out[0] = '\0';
    stampa(&A, A.radice);

    if (strcmp(g_out, atteso) != 0) {
        printf("NO   %-34s\n     atteso:  %s\n     trovato: %s\n", nome,
               atteso, g_out);
        sbagliate++;
        return;
    }
    printf("ok   %-34s %s\n", nome, g_out);
}

/* =============================================================================
 * ESEGUIRE
 *
 * ! SI GUARDA IL VALORE DELL'ULTIMA ESPRESSIONE, come fa una console. E' il
 * modo piu' corto di provare un motore: si scrive un programma che finisce con
 * il numero che deve venire fuori, e si confronta quel numero. Senza,
 * servirebbe una `console.log` finta e un confronto sull'uscita — cioe' due
 * cose da far funzionare prima di poter provare la prima.
 * ========================================================================== */
/* ! console.log FINISCE QUI DENTRO, e non sullo schermo: il banco deve poter
 * CONFRONTARE quello che uno script ha stampato, non guardarlo passare. E' la
 * ragione per cui exjs non decide da se' dove scrivere. */
static char g_console[2048];
static int  g_console_n;

static void raccogli(const char *testo, unsigned int n, void *dato)
{
    unsigned int i;
    (void)dato;
    for (i = 0; i < n && g_console_n + 1 < (int)sizeof(g_console); i++)
        g_console[g_console_n++] = testo[i];
    g_console[g_console_n] = '\0';
}

#define MOTORE_OGG   400
#define MOTORE_ARENA 16384

/* Un orologio fermo per le prove di Date: 2001-09-09T01:46:40.123Z, una
 * domenica. Con l'ora vera ogni esecuzione darebbe un risultato diverso. */
static double orologio_fisso(void *dato)
{
    (void)dato;
    return 1000000000123.0;
}

static void prova_esegui(const char *nome, const char *codice, const char *atteso)
{
    static unsigned char memoria[1 << 20];
    ExJsCtx    *c;
    ExJsVal     r;
    ExJsErrore  err;
    const char *s;

    fatte++;
    memset(&err, 0, sizeof(err));

    c = exjs_apri(memoria, sizeof(memoria), MOTORE_OGG, MOTORE_ARENA);
    if (!c) { printf("NO   %-34s il contesto non si apre\n", nome); sbagliate++; return; }
    g_console_n = 0; g_console[0] = '\0';
    exjs_uscita_metti(c, raccogli, 0);
    exjs_orologio_metti(c, orologio_fisso, 0);

    if (!exjs_esegui(c, codice, (unsigned int)strlen(codice), &r, &err)) {
        if (atteso == 0) {
            printf("ok   %-34s rifiutato: %s\n", nome, err.messaggio);
            return;
        }
        printf("NO   %-34s riga %d: %s\n", nome, err.riga, err.messaggio);
        sbagliate++;
        return;
    }

    if (atteso == 0) {
        printf("NO   %-34s doveva fallire e non l'ha fatto\n", nome);
        sbagliate++;
        return;
    }

    s = exjs_a_stringa(c, r);
    if (strcmp(s, atteso) != 0) {
        printf("NO   %-34s atteso \"%s\", trovato \"%s\"\n", nome, atteso, s);
        sbagliate++;
        return;
    }
    printf("ok   %-34s %s\n", nome, s);
}

/* Come prova_esegui, ma guarda cio' che lo script ha STAMPATO invece del
 * valore dell'ultima espressione. Serve a provare console.log e tutto cio' che
 * un giorno scrivera' da solo. */
static void prova_stampa(const char *nome, const char *codice, const char *atteso)
{
    static unsigned char memoria[1 << 20];
    ExJsCtx    *c;
    ExJsVal     r;
    ExJsErrore  err;

    fatte++;
    memset(&err, 0, sizeof(err));

    c = exjs_apri(memoria, sizeof(memoria), MOTORE_OGG, MOTORE_ARENA);
    if (!c) { printf("NO   %-34s il contesto non si apre\n", nome); sbagliate++; return; }
    g_console_n = 0; g_console[0] = '\0';
    exjs_uscita_metti(c, raccogli, 0);

    if (!exjs_esegui(c, codice, (unsigned int)strlen(codice), &r, &err)) {
        printf("NO   %-34s riga %d: %s\n", nome, err.riga, err.messaggio);
        sbagliate++;
        return;
    }
    if (strcmp(g_console, atteso) != 0) {
        printf("NO   %-34s atteso \"%s\", stampato \"%s\"\n", nome, atteso, g_console);
        sbagliate++;
        return;
    }
    printf("ok   %-34s stampato: %s", nome, g_console);
}

/* =============================================================================
 * I TEMPI, con un orologio INVENTATO
 *
 * ! E' TUTTO IL SENSO DI AVER TENUTO IL TEMPO FUORI DALLA LIBRERIA. Qui l'ora
 * la decide la prova: si eseguono gli script, poi si pompa a 50, a 150, a 250
 * millisecondi, e si guarda cosa e' partito. La stessa prova dara' lo stesso
 * risultato fra dieci anni e su una macchina mille volte piu' lenta — cosa che
 * un motore con l'orologio dentro non puo' promettere.
 * ========================================================================== */
static void prova_tempo(const char *nome, const char *codice,
                        const unsigned int *ore, int n_ore, const char *atteso)
{
    static unsigned char memoria[1 << 20];
    ExJsCtx    *c;
    ExJsVal     r;
    ExJsErrore  err;
    int         i;

    fatte++;
    memset(&err, 0, sizeof(err));

    c = exjs_apri(memoria, sizeof(memoria), MOTORE_OGG, MOTORE_ARENA);
    if (!c) { printf("NO   %-34s il contesto non si apre\n", nome); sbagliate++; return; }
    g_console_n = 0; g_console[0] = '\0';
    exjs_uscita_metti(c, raccogli, 0);

    if (!exjs_esegui(c, codice, (unsigned int)strlen(codice), &r, &err)) {
        printf("NO   %-34s riga %d: %s\n", nome, err.riga, err.messaggio);
        sbagliate++;
        return;
    }

    for (i = 0; i < n_ore; i++) exjs_pompa(c, ore[i]);

    if (strcmp(g_console, atteso) != 0) {
        printf("NO   %-34s atteso \"%s\", stampato \"%s\"\n", nome, atteso, g_console);
        sbagliate++;
        return;
    }
    printf("ok   %-34s %s", nome, g_console[0] ? g_console : "(niente)\n");
}

/* ! DUE SCRIPT NELLA STESSA PAGINA, ed e' la prova del difetto corretto
 * insieme alla coda: una funzione e' un INDICE DENTRO L'ALBERO, e finche'
 * l'albero si rifaceva a ogni exjs_esegui il secondo <script> faceva puntare
 * la funzione del primo a nodi diversi. Non un errore: una funzione che esegue
 * il codice di un'altra. */
static void prova_due_script(const char *nome, const char *uno, const char *due,
                             unsigned int ora, const char *atteso)
{
    static unsigned char memoria[1 << 20];
    ExJsCtx    *c;
    ExJsVal     r;
    ExJsErrore  err;

    fatte++;
    memset(&err, 0, sizeof(err));

    c = exjs_apri(memoria, sizeof(memoria), MOTORE_OGG, MOTORE_ARENA);
    if (!c) { printf("NO   %-34s il contesto non si apre\n", nome); sbagliate++; return; }
    g_console_n = 0; g_console[0] = '\0';
    exjs_uscita_metti(c, raccogli, 0);

    if (!exjs_esegui(c, uno, (unsigned int)strlen(uno), &r, &err) ||
        !exjs_esegui(c, due, (unsigned int)strlen(due), &r, &err)) {
        printf("NO   %-34s riga %d: %s\n", nome, err.riga, err.messaggio);
        sbagliate++;
        return;
    }
    exjs_pompa(c, ora);

    if (strcmp(g_console, atteso) != 0) {
        printf("NO   %-34s atteso \"%s\", stampato \"%s\"\n", nome, atteso, g_console);
        sbagliate++;
        return;
    }
    printf("ok   %-34s %s", nome, g_console[0] ? g_console : "(niente)\n");
}

int main(void)
{
    printf("=== ExJs: l'analizzatore lessicale ===\n\n");

    {
        static const int a[] = { TK_VAR, TK_NOME, '=', TK_NUMERO, ';', TK_FINE };
        prova("var a = 1;", "var a = 1;", a);
    }
    {
        static const int a[] = { TK_NOME, '(', TK_STRINGA, ')', ';', TK_FINE };
        prova("chiamata con stringa", "log('ciao');", a);
    }
    {
        /* ! IL PIU' LUNGO PRIMA: se `>` vincesse su `>>>=`, questa prova
         * darebbe cinque gettoni invece di tre. */
        static const int a[] = { TK_NOME, TK_SHR_U_UG, TK_NUMERO, TK_FINE };
        prova("a >>>= 2", "a >>>= 2", a);
    }
    {
        static const int a[] = { TK_NOME, TK_ID_UGUALE, TK_NOME, TK_E_E,
                                 TK_NOME, TK_ID_DIVERSO, TK_NULL, TK_FINE };
        prova("=== && !==", "a === b && c !== null", a);
    }
    {
        static const int a[] = { TK_NOME, TK_PIU_PIU, ',', TK_MENO_MENO,
                                 TK_NOME, TK_FINE };
        prova("++ e --", "i++, --j", a);
    }
    {
        static const int a[] = { TK_FUNCTION, TK_NOME, '(', TK_NOME, ')', '{',
                                 TK_RETURN, TK_NOME, ';', '}', TK_FINE };
        prova("una funzione", "function f(x) { return x; }", a);
    }
    {
        /* I commenti spariscono, e il codice attorno resta intero. */
        static const int a[] = { TK_NOME, '=', TK_NUMERO, ';', TK_FINE };
        prova("commenti", "a /* in mezzo */ = // fino a fine riga\n 1;", a);
    }
    {
        /* ! IL DOLLARO E' UNA LETTERA: questo e' UN nome, non tre gettoni. */
        static const int a[] = { TK_NOME, '.', TK_NOME, '(', ')', TK_FINE };
        prova("$ e _ nei nomi", "$_pippo.x()", a);
    }
    {
        static const int a[] = { TK_NOME, '=', '[', TK_NUMERO, ',', TK_NUMERO,
                                 ']', ';', TK_FINE };
        prova("un vettore", "v = [1, 2];", a);
    }
    {
        static const int a[] = { TK_NOME, '=', '{', TK_NOME, ':', TK_STRINGA,
                                 '}', ';', TK_FINE };
        prova("un oggetto", "o = {a: 'b'};", a);
    }

    printf("\n=== i numeri ===\n\n");
    prova_numero("intero",            "42",        42.0);
    prova_numero("esadecimale",       "0x10",      16.0);
    prova_numero("con la virgola",    "3.5",       3.5);
    prova_numero("che comincia col .",".5",        0.5);
    prova_numero("esponente",         "2e3",       2000.0);
    prova_numero("esponente negativo","5e-2",      0.05);
    /* ! ES3 DICEVA OTTO, E QUI DICE DIECI. Vedi il commento in lex.c: l'ottale
     * con lo zero davanti fa piu' danni che comodo. */
    prova_numero("010 non e' ottale", "010",       10.0);

    printf("\n=== le stringhe ===\n\n");
    prova_stringa("virgolette doppie", "\"ciao\"",        "ciao");
    prova_stringa("virgolette singole","'ciao'",          "ciao");
    prova_stringa("scappamenti",       "'a\\nb\\tc'",     "a\nb\tc");
    prova_stringa("virgoletta dentro", "'l\\'ora'",       "l'ora");
    prova_stringa("\\x",               "'\\x41'",         "A");
    prova_stringa("\\u in UTF-8",      "'\\u00e0'",       "\xc3\xa0");
    prova_stringa("barra e a capo",    "'a\\\nb'",        "ab");

    printf("\n=== gli errori dicono DOVE ===\n\n");
    prova_riga("stringa non chiusa",  "var a = 1;\nvar b = 'x;\n", 2);
    prova_riga("carattere ignoto",    "a = 1;\n\nb = @;\n",        3);
    {
        static const int a[] = { TK_ERRORE };
        prova("\\u a meta'", "'\\u00'", a);
    }

    printf("\n=== l'albero: le precedenze ===\n\n");
    prova_albero("1 + 2 * 3", "1+2*3;",
                 "(programma (espr (binario + (numero 1) "
                 "(binario * (numero 2) (numero 3)))))");
    prova_albero("1 * 2 + 3", "1*2+3;",
                 "(programma (espr (binario + (binario * (numero 1) "
                 "(numero 2)) (numero 3))))");
    /* ! A SINISTRA: 1-2-3 fa -4, non 2. Con l'associativita' invertita non
     * c'e' nessun errore, solo un numero diverso. */
    prova_albero("1 - 2 - 3 (a sinistra)", "1-2-3;",
                 "(programma (espr (binario - (binario - (numero 1) "
                 "(numero 2)) (numero 3))))");
    /* ! A DESTRA: a = b = 1 assegna 1 a b e poi b ad a. */
    prova_albero("a = b = 1 (a destra)", "a=b=1;",
                 "(programma (espr (assegna = (nome a) "
                 "(assegna = (nome b) (numero 1)))))");
    prova_albero("&& lega piu' di ||", "a||b&&c;",
                 "(programma (espr (logico || (nome a) "
                 "(logico && (nome b) (nome c)))))");
    prova_albero("confronto sotto la somma", "a<b+1;",
                 "(programma (espr (binario < (nome a) "
                 "(binario + (nome b) (numero 1)))))");
    prova_albero("condizionale", "a?b:c;",
                 "(programma (espr (condizione (nome a) (nome b) (nome c))))");
    prova_albero("unario meno", "-a*b;",
                 "(programma (espr (binario * (unario - (nome a)) (nome b))))");
    prova_albero("typeof", "typeof a;",
                 "(programma (espr (unario typeof (nome a))))");

    printf("\n=== l'albero: chiamate, membri, new ===\n\n");
    prova_albero("a.b.c", "a.b.c;",
                 "(programma (espr (membro (membro (nome a) .b) .c)))");
    prova_albero("f(1, 2)", "f(1,2);",
                 "(programma (espr (chiamata (nome f) "
                 "(arg (numero 1) (numero 2)))))");
    prova_albero("a[0]()", "a[0]();",
                 "(programma (espr (chiamata (indice (nome a) (numero 0)) (arg))))");
    /* ! new SI PRENDE a.b, NON a: e' il punto in cui i motori scritti in
     * fretta sbagliano, e non da' nessun errore — costruisce l'oggetto
     * sbagliato. */
    prova_albero("new a.b()", "new a.b();",
                 "(programma (espr (nuovo (membro (nome a) .b) (arg))))");
    prova_albero("new F(1).x", "new F(1).x;",
                 "(programma (espr (membro (nuovo (nome F) (arg (numero 1))) .x)))");
    prova_albero("i++", "i++;",
                 "(programma (espr (post ++ (nome i))))");
    prova_albero("++i", "++i;",
                 "(programma (espr (pre ++ (nome i))))");

    printf("\n=== l'albero: le istruzioni ===\n\n");
    prova_albero("var con due nomi", "var a=1,b;",
                 "(programma (var (dichiara a (numero 1)) (dichiara b -)))");
    prova_albero("if/else", "if(a)b();else c();",
                 "(programma (se (nome a) (espr (chiamata (nome b) (arg))) "
                 "(espr (chiamata (nome c) (arg)))))");
    prova_albero("while", "while(a){b;}",
                 "(programma (mentre (nome a) (blocco (espr (nome b)))))");
    prova_albero("for classico", "for(var i=0;i<3;i++){}",
                 "(programma (per (var (dichiara i (numero 0))) "
                 "(binario < (nome i) (numero 3)) (post ++ (nome i)) (blocco)))");
    /* ! for..in E for CLASSICO COMINCIANO UGUALI, e si distinguono solo dopo
     * aver letto l'inizializzazione con `in` spento come operatore. */
    prova_albero("for..in", "for(var k in o){}",
                 "(programma (per_in (var (dichiara k -)) (nome o) (blocco)))");
    prova_albero("una funzione", "function f(a,b){return a+b;}",
                 "(programma (funzione f (par (parametro a) (parametro b)) "
                 "(blocco (ritorna (binario + (nome a) (nome b))))))");
    prova_albero("funzione anonima", "var f=function(){};",
                 "(programma (var (dichiara f (funzione <anonima> (par) (blocco)))))");

    printf("\n=== l'albero: oggetti e vettori ===\n\n");
    prova_albero("oggetto", "o={a:1,\"b\":2};",
                 "(programma (espr (assegna = (nome o) (oggetto "
                 "(voce a (numero 1)) (voce b (numero 2))))))");
    /* ! UNA PAROLA CHIAVE E' UN NOME DI PROPRIETA' LEGITTIMO, e i
     * minificatori lo usano di continuo. */
    prova_albero("chiave che e' parola chiave", "o={if:1,in:2};",
                 "(programma (espr (assegna = (nome o) (oggetto "
                 "(voce if (numero 1)) (voce in (numero 2))))))");
    prova_albero("vettore", "v=[1,2];",
                 "(programma (espr (assegna = (nome v) "
                 "(vettore (numero 1) (numero 2)))))");
    prova_albero("virgola finale", "v=[1,2,];",
                 "(programma (espr (assegna = (nome v) "
                 "(vettore (numero 1) (numero 2)))))");

    printf("\n=== il punto e virgola che non c'e' ===\n\n");
    /* ! LA REGOLA FEROCE: `return` piu' a capo rende undefined, e il valore
     * sotto diventa un'istruzione a se'. Un motore che la ignorasse
     * eseguirebbe un programma diverso da quello scritto. */
    prova_albero("return + a capo", "function f(){return\n1;}",
                 "(programma (funzione f (par) (blocco (ritorna) "
                 "(espr (numero 1)))))");
    prova_albero("return sulla stessa riga", "function f(){return 1;}",
                 "(programma (funzione f (par) (blocco (ritorna (numero 1)))))");
    prova_albero("senza ';' a fine riga", "a=1\nb=2\n",
                 "(programma (espr (assegna = (nome a) (numero 1))) "
                 "(espr (assegna = (nome b) (numero 2))))");
    /* ! `a` e `++b` su due righe sono DUE istruzioni: il postfisso non
     * attraversa un a capo. */
    prova_albero("++ non attraversa l'a capo", "a\n++b\n",
                 "(programma (espr (nome a)) (espr (pre ++ (nome b))))");

    printf("\n=== cio' che deve essere rifiutato ===\n\n");
    prova_albero("try senza catch ne' finally", "try{}", 0);
    prova_albero("due default",            "switch(a){default:;default:;}", 0);
    prova_albero("throw e poi a capo",     "throw\n1;", 0);
    prova_albero("parentesi non chiusa",   "f(1;",       0);
    prova_albero("var senza nome",         "var = 1;",   0);

    printf("\n=== throw, try, switch, prototype, instanceof (29 settembre 2026) ===\n\n");
    prova_esegui("catch prende il valore",   "var r; try { throw 5; } catch (e) { r = e + 1; } r;", "6");
    prova_esegui("throw da una funzione",    "function f(){ throw 'x'; } var r='no'; try { f(); } catch(e) { r = e; } r;", "x");
    prova_esegui("dopo il throw non si va",  "var r=1; try { throw 0; r=2; } catch(e) {} r;", "1");
    prova_esegui("finally dopo catch",       "var r=''; try { throw 1; } catch(e) { r+='c'; } finally { r+='f'; } r;", "cf");
    prova_esegui("finally senza errore",     "var r=''; try { r+='t'; } finally { r+='f'; } r;", "tf");
    prova_esegui("finally col return",       "function f(){ try { return 1; } finally { g=2; } } var g=0; f()*10+g;", "12");
    prova_esegui("return nel finally vince", "function f(){ try { return 1; } finally { return 2; } } f();", "2");
    prova_esegui("finally e poi risale",     "var r=''; try { try { throw 'a'; } finally { r+='f'; } } catch(e) { r+=e; } r;", "fa");
    prova_esegui("catch senza nome",         "var r=0; try { throw 1; } catch { r=7; } r;", "7");
    prova_esegui("errore del motore preso",  "var r; try { undefined(); } catch(e) { r = e instanceof TypeError; } r;", "true");
    prova_esegui("il suo messaggio",         "var r; try { var o = {}; o.f(); } catch(e) { r = e.name + '|' + e.message; } r;", "TypeError|non e' una funzione");
    prova_esegui("un nome che non c'e'",      "var r; try { pippo; } catch(e) { r = e.name; } r;", "ReferenceError");
    prova_esegui("new Error e message",      "var e = new Error('guasto'); e.message;", "guasto");
    prova_esegui("Error come testo",         "'' + new RangeError('fuori');", "RangeError: fuori");
    prova_esegui("Error senza new",          "TypeError('t') instanceof Error;", "true");
    prova_esegui("throw non preso",          "throw new Error('ahi');", 0);
    prova_esegui("rilanciare",               "var r; try { try { throw 1; } catch(e) { throw e+1; } } catch(e) { r=e; } r;", "2");
    prova_esegui("una nativa che lancia",    "var r; try { new Date(NaN).toISOString(); } catch(e) { r = e.name; } r;", "RangeError");
    prova_esegui("throw dentro forEach",     "var r=0; try { [1,2,3].forEach(function(x){ if (x==2) throw x; r+=x; }); } catch(e) { r+=e*10; } r;", "21");
    prova_esegui("catch in un ciclo",        "var n=0; for (var i=0;i<5;i++) { try { if (i%2) throw i; } catch(e) { n+=e; } } n;", "4");
    prova_esegui("switch: il caso giusto",   "var r; switch (2) { case 1: r='a'; break; case 2: r='b'; break; default: r='z'; } r;", "b");
    prova_esegui("switch: default",          "var r; switch (9) { case 1: r='a'; break; default: r='z'; } r;", "z");
    prova_esegui("switch: si cade",          "var r=''; switch (1) { case 1: r+='a'; case 2: r+='b'; break; case 3: r+='c'; } r;", "ab");
    prova_esegui("switch: default in mezzo", "var r=''; switch (3) { default: r+='d'; case 1: r+='a'; break; case 3: r+='c'; } r;", "c");
    prova_esegui("switch: ===, non ==",      "var r='no'; switch ('1') { case 1: r='num'; break; case '1': r='str'; } r;", "str");
    prova_esegui("switch: continue esce",    "var n=0; for (var i=0;i<4;i++) { switch (i) { case 1: continue; } n++; } n;", "3");
    prova_esegui("switch: return",           "function f(x){ switch(x){ case 'a': return 1; } return 0; } f('a')+f('b');", "1");
    prova_esegui("prototype e new",          "function P(x){ this.x=x; } P.prototype.doppio=function(){ return this.x*2; }; new P(4).doppio();", "8");
    prova_esegui("constructor",              "function P(){} var p = new P(); p.constructor === P;", "true");
    prova_esegui("instanceof",               "function A(){} function B(){} var a = new A(); (a instanceof A) + ',' + (a instanceof B);", "true,false");
    prova_esegui("instanceof ereditato",     "function A(){} function B(){} B.prototype = new A(); new B() instanceof A;", "true");
    prova_esegui("instanceof su un numero",  "function A(){} 5 instanceof A;", "false");
    prova_esegui("proprio copre il proto",   "function P(){} P.prototype.v=1; var p=new P(); p.v=2; p.v + new P().v;", "3");

    printf("\n=== la libreria che mancava, e i numeri (29 settembre 2026) ===\n\n");
    prova_esegui("numeri: le cifre piu' corte", "0.1+0.2;", "0.30000000000000004");
    prova_esegui("numeri: 0.1 resta 0.1", "0.1;", "0.1");
    prova_esegui("numeri: 1e21", "1e21;", "1e+21");
    prova_esegui("numeri: 1e20 per intero", "1e20;", "100000000000000000000");
    prova_esegui("numeri: piccoli", "0.0000001;", "1e-7");
    prova_esegui("numeri: 0.000001", "0.000001;", "0.000001");
    prova_esegui("numeri: un terzo", "1/3;", "0.3333333333333333");
    prova_esegui("numeri: grandi interi", "Math.pow(2,53);", "9007199254740992");
    prova_esegui("numeri: -0", "-0;", "0");
    prova_esegui("toFixed", "(1.005).toFixed(2) + '|' + (2.5).toFixed(0) + '|' + (-1.5).toFixed(1);", "1.00|3|-1.5");
    prova_esegui("toString(16) e (2)", "(255).toString(16) + '|' + (5).toString(2);", "ff|101");
    prova_esegui("stringa[i]", "'abc'[1] + 'abc'[5];", "bundefined");
    prova_esegui("Object.keys / values / entries", "Object.keys({a:1,b:2}).join() + '|' + Object.values({a:1,b:2}).join() + '|' + Object.entries({a:1})[0].join();", "a,b|1,2|a,1");
    prova_esegui("Object.assign", "var o = Object.assign({a:1}, {b:2}, {a:3}); o.a + o.b;", "5");
    prova_esegui("Object.create", "var p = {v: 4}; Object.create(p).v;", "4");
    prova_esegui("hasOwnProperty", "var p = {v:1}; var o = Object.create(p); o.w = 2; o.hasOwnProperty('w') + ',' + o.hasOwnProperty('v');", "true,false");
    prova_esegui("Array.isArray / from / of", "Array.isArray([]) + ',' + Array.isArray({}) + ',' + Array.from('abc').join('-') + ',' + Array.of(1,2).length;", "true,false,a-b-c,2");
    prova_esegui("new Array(n)", "new Array(3).length + ',' + new Array(1,2).join();", "3,1,2");
    prova_esegui("includes (anche NaN)", "[1,NaN].includes(NaN) + ',' + [1].includes('1');", "true,false");
    prova_esegui("some / every / find / findIndex", "var v=[1,2,3]; v.some(function(x){return x>2;}) + ',' + v.every(function(x){return x>1;}) + ',' + v.find(function(x){return x>1;}) + ',' + v.findIndex(function(x){return x>5;});", "true,false,2,-1");
    prova_esegui("reduce e reduceRight", "[1,2,3].reduce(function(a,b){return a+b;}) + ',' + ['a','b'].reduceRight(function(a,b){return a+b;}, '');", "6,ba");
    prova_esegui("reduce vuoto lancia", "var r; try { [].reduce(function(){}); } catch(e) { r = e.name; } r;", "TypeError");
    prova_esegui("splice", "var v=[1,2,3,4]; var t=v.splice(1,2,'x'); v.join() + '|' + t.join();", "1,x,4|2,3");
    prova_esegui("fill, at, flat", "[0,0,0].fill(7,1).join() + '|' + [1,2,3].at(-1) + '|' + [1,[2,[3]]].flat().length;", "0,7,7|3|3");
    prova_esegui("stringhe: includes / starts / ends", "'abcd'.includes('bc') + ',' + 'abcd'.startsWith('ab') + ',' + 'abcd'.endsWith('cd') + ',' + 'abcd'.endsWith('b', 2);", "true,true,true,true");
    prova_esegui("padStart / padEnd / repeat", "'5'.padStart(3,'0') + '|' + 'a'.padEnd(3) + '|' + 'ab'.repeat(3);", "005|a  |ababab");
    prova_esegui("trimStart / trimEnd / concat", "'[' + '  a '.trimStart() + '][' + ' a  '.trimEnd() + ']' + 'x'.concat(1, 'y');", "[a ][ a]x1y");
    prova_esegui("call / apply", "function f(a,b){ return this.k + a + b; } f.call({k:1}, 2, 3) + ',' + f.apply({k:1}, [2, 3]);", "6,6");
    prova_esegui("bind con argomenti", "function f(a,b){ return this.k * a + b; } var g = f.bind({k:10}, 2); g(3);", "23");
    prova_esegui("encodeURIComponent e ritorno", "var s = 'a b&c/d'; encodeURIComponent(s) + '|' + decodeURIComponent(encodeURIComponent(s)) + '|' + encodeURI('a b/c?d');", "a%20b%26c%2Fd|a b&c/d|a%20b/c?d");
    prova_esegui("Number.isInteger e costanti", "Number.isInteger(5) + ',' + Number.isInteger(5.5) + ',' + Number.isNaN('x') + ',' + Number.MAX_SAFE_INTEGER;", "true,false,false,9007199254740991");
    prova_esegui("un'eccezione ferma some", "var n=0; try { [1,2,3].some(function(x){ n++; if (x==2) throw 0; }); } catch(e) {} n;", "2");

    printf("\n=== la sintassi di oggi: ES2015 e dopo (29 settembre 2026) ===\n\n");
    prova_esegui("let e const", "let a = 1; const b = 2; a + b;", "3");
    prova_esegui("let copre quello di fuori", "let x = 1; { let x = 2; } x;", "1");
    prova_esegui("let nel blocco di una funzione", "var x = 'g'; function f(){ let x = 'l'; return x; } f() + x;", "lg");
    prova_esegui("let in un blocco copre il var", "var x = 1; if (true) { let x = 2; } x;", "1");
    prova_esegui("for (let) copre quello di fuori", "let i = 5; for (let i = 0; i < 2; i++) {} i;", "5");
    prova_esegui("for..of con let copre", "let v = 'f'; for (let v of [1,2]) {} v;", "f");
    prova_esegui("let in un ciclo lungo non finisce gli oggetti", "var s = 0; for (var i = 0; i < 3000; i++) { let q = i; s += q; } s;", "4498500");
    prova_esegui("let senza valore si azzera a ogni giro", "var r=''; for (var i=0;i<3;i++) { let y; if (i==1) y = 'x'; r += (y === undefined ? '-' : y); } r;", "-x-");
    prova_esegui("for (let) e le chiusure", "var f = []; for (let i = 0; i < 3; i++) f.push(function(){ return i; }); f[0]() + ',' + f[1]() + ',' + f[2]();", "0,1,2");
    prova_esegui("for (var) e le chiusure", "var f = []; for (var i = 0; i < 3; i++) f.push(function(){ return i; }); f[0]() + ',' + f[2]();", "3,3");
    prova_esegui("for (let) senza chiusure", "var s = 0; for (let i = 0; i < 100; i++) s += i; s;", "4950");
    prova_esegui("const in un blocco con chiusura", "var f = []; for (var i = 0; i < 2; i++) { const k = i * 10; f.push(() => k); } f[0]() + f[1]();", "10");
    prova_esegui("freccia con espressione", "var f = (a, b) => a + b; f(2, 3);", "5");
    prova_esegui("freccia con un parametro", "var f = x => x * x; f(7);", "49");
    prova_esegui("freccia senza parametri", "var f = () => 42; f();", "42");
    prova_esegui("freccia con corpo", "var f = (x) => { var y = x + 1; return y * 2; }; f(1);", "4");
    prova_esegui("freccia e this di fuori", "var o = { v: 5, f: function(){ var g = () => this.v; return g(); } }; o.f();", "5");
    prova_esegui("freccia e arguments di fuori", "function f(){ var g = () => arguments[0]; return g(9); } f(3);", "3");
    prova_esegui("freccia che rende un oggetto", "var f = () => ({a: 1}); f().a;", "1");
    prova_esegui("modello semplice", "var n = 3; `n vale ${n}!`;", "n vale 3!");
    prova_esegui("modello: espressioni e modelli dentro", "var a = [1,2]; `${a.map(x => `<${x}>`).join('')}|${ {k:1}.k }`;", "<1><2>|1");
    prova_esegui("modello: scappamenti", "`a\\tb\\`c\\${d}`;", "a\tb`c${d}");
    prova_esegui("modello su piu' righe", "`a\nb`.length;", "3");
    prova_esegui("modello che comincia con un numero", "`${1}${2}`;", "12");
    prova_esegui("parametri predefiniti", "function f(a, b = a * 2){ return a + b; } f(1) + ',' + f(1, 1);", "3,2");
    prova_esegui("parametro undefined prende il predefinito", "function f(a = 5){ return a; } f(undefined);", "5");
    prova_esegui("parametri: il resto", "function f(a, ...r){ return a + ':' + r.join('-'); } f(1, 2, 3);", "1:2-3");
    prova_esegui("parametro modello", "function f({a, b = 2}, [c]){ return a + b + c; } f({a: 1}, [10]);", "13");
    prova_esegui("destrutturare un oggetto", "var {a, b: c, d = 4} = {a: 1, b: 2}; a + c + d;", "7");
    prova_esegui("destrutturare un vettore con buchi", "var [x, , y = 9, ...r] = [1, 2, undefined, 4, 5]; x + y + r.length;", "12");
    prova_esegui("destrutturare annidato", "const {p: {q: [z]}} = {p: {q: [8]}}; z;", "8");
    prova_esegui("destrutturare il resto di un oggetto", "var {a, ...r} = {a: 1, b: 2, c: 3}; Object.keys(r).join();", "b,c");
    prova_esegui("scambiare con [a, b] = [b, a]", "var a = 1, b = 2; [a, b] = [b, a]; a + ',' + b;", "2,1");
    prova_esegui("destrutturare null lancia", "var r; try { var {a} = null; } catch(e) { r = e.name; } r;", "TypeError");
    prova_esegui("spread nei vettori", "var a = [2, 3]; [1, ...a, 4].join();", "1,2,3,4");
    prova_esegui("spread di una stringa", "[...'abc'].join('-');", "a-b-c");
    prova_esegui("spread negli argomenti", "function f(a, b, c){ return a + b + c; } f(...[1, 2], 3);", "6");
    prova_esegui("spread negli oggetti", "var o = {...{a: 1, b: 2}, b: 3}; o.a + o.b;", "4");
    prova_esegui("chiavi abbreviate e metodi", "var a = 1; var o = {a, f(){ return this.a + 1; }}; o.f();", "2");
    prova_esegui("chiavi calcolate", "var k = 'x'; var o = {[k + 'y']: 5}; o.xy;", "5");
    prova_esegui("get e set", "var o = { _v: 1, get v(){ return this._v * 10; }, set v(x){ this._v = x; } }; o.v = 3; o.v;", "30");
    prova_esegui("get in JSON: la voce salta", "JSON.stringify({ get a(){ return 1; }, b: 2 });", "{\"b\":2}");
    prova_esegui("defineProperty con get", "var o = {}; Object.defineProperty(o, 'x', { get: function(){ return 7; } }); o.x;", "7");
    prova_esegui("for..of su un vettore", "var s = 0; for (const x of [1, 2, 3]) s += x; s;", "6");
    prova_esegui("for..of su una stringa", "var r = ''; for (let ch of 'ab') r = ch + r; r;", "ba");
    prova_esegui("for..of con destrutturazione", "var r = ''; for (const [k, v] of Object.entries({a: 1, b: 2})) r += k + v; r;", "a1b2");
    prova_esegui("for..of e break", "var r = 0; for (var x of [1, 2, 3, 4]) { if (x > 2) break; r += x; } r;", "3");
    prova_esegui("?\? e ||", "(null ?\? 'a') + (0 ?\? 'b') + (0 || 'c');", "a0c");
    prova_esegui("?. su undefined", "var o; (o?.x) === undefined;", "true");
    prova_esegui("?. corto circuito", "var o = null; o?.a.b.c;", "undefined");
    prova_esegui("?. chiamata", "var o = {f: null}; (o.f?.()) === undefined && o.g?.() === undefined;", "true");
    prova_esegui("?. con valore", "var o = {a: {b: 2}}; o?.a?.b;", "2");
    prova_esegui("?.[]", "var o = {k: [5]}; o?.['k']?.[0];", "5");
    prova_esegui("** e la sua precedenza", "2 ** 3 ** 2 + ',' + (-2) ** 2 + ',' + 2 ** -1;", "512,4,0.5");
    prova_esegui("**= e ?\?= ||= &&=", "var a = 3; a **= 2; var b = null; b ?\?= 7; var c = 0; c ||= 4; var d = 1; d &&= 5; a + b + c + d;", "25");
    prova_esegui("class: metodi e costruttore", "class P { constructor(x){ this.x = x; } doppio(){ return this.x * 2; } } new P(4).doppio();", "8");
    prova_esegui("class: extends e super", "class A { constructor(n){ this.n = n; } saluta(){ return 'A' + this.n; } } class B extends A { constructor(n){ super(n + 1); } saluta(){ return 'B' + super.saluta(); } } new B(1).saluta();", "BA2");
    prova_esegui("class: costruttore implicito del figlio", "class A { constructor(a, b){ this.s = a + b; } } class B extends A {} new B(2, 3).s;", "5");
    prova_esegui("class: static ereditato", "class A { static crea(){ return 'k'; } } class B extends A {} B.crea();", "k");
    prova_esegui("class: campi", "class C { a = 1; b = this.a + 1; static z = 9; } var c = new C(); c.a + c.b + C.z;", "12");
    prova_esegui("class: campi dopo super", "class A { constructor(){ this.base = 1; } } class B extends A { x = this.base + 1; } new B().x;", "2");
    prova_esegui("class: instanceof e typeof", "class A {} class B extends A {} var b = new B(); (b instanceof A) + ',' + typeof B;", "true,function");
    prova_esegui("class: accessori", "class Q { constructor(){ this._v = 2; } get v(){ return this._v; } set v(x){ this._v = x * 2; } } var q = new Q(); q.v = 5; q.v;", "10");
    prova_esegui("etichette: break di fuori", "var n = 0; fuori: for (var i = 0; i < 3; i++) { for (var j = 0; j < 3; j++) { if (j == 1) continue fuori; if (i == 2) break fuori; n++; } } n;", "2");
    prova_esegui("etichetta su un blocco", "var r = 'a'; blocco: { r += 'b'; break blocco; r += 'c'; } r;", "ab");
    prova_esegui("switch dentro un ciclo con etichetta", "var n = 0; giro: for (var i = 0; i < 5; i++) { switch (i) { case 3: break giro; default: n++; } } n;", "3");
    prova_esegui("delete toglie davvero", "var o = {a: 1, b: 2}; delete o.a; ('a' in o) + ',' + Object.keys(o).join();", "false,b");
    prova_esegui("Math: sin cos log exp", "Math.round(Math.sin(Math.PI/2)*1000) + ',' + Math.cos(0) + ',' + Math.log(Math.E) + ',' + Math.exp(0) + ',' + Math.log2(8) + ',' + Math.log10(1000);", "1000,1,1,1,3,3");
    prova_esegui("Math: atan2 hypot cbrt trunc sign", "Math.round(Math.atan2(1,1)*4*1000)/1000 + ',' + Math.hypot(3,4) + ',' + Math.cbrt(27) + ',' + Math.trunc(-4.7) + ',' + Math.sign(-3);", "3.142,5,3,-4,-1");
    prova_esegui("Math.pow con esponente decimale", "Math.pow(4, 0.5) + ',' + 2 ** 0.5;", "2,1.4142135623730951");
    prova_esegui("Map", "var m = new Map([['a', 1]]); m.set('b', 2).set('a', 3); m.get('a') + m.get('b') + m.size + ',' + m.has('b') + ',' + m.delete('a') + m.size;", "7,true,true1");
    prova_esegui("Map con chiavi oggetto e for..of", "var k = {}; var m = new Map(); m.set(k, 'o'); var r = ''; for (const [kk, v] of m) r += (kk === k) + v; r;", "trueo");
    prova_esegui("Set", "var s = new Set([1, 2, 2, 3]); s.add(4); s.size + ',' + s.has(2) + ',' + [...s].join('');", "4,true,1234");
    prova_esegui("Map.forEach e keys", "var m = new Map([[1, 'x'], [2, 'y']]); var r = ''; m.forEach(function(v, k){ r += k + v; }); r + [...m.keys()].join('');", "1x2y12");

    printf("\n=== le espressioni regolari (29 settembre 2026) ===\n\n");
    prova_esegui("this fuori dalle funzioni", "var q = 5; (function (G) { return G.q; })(this);", "5");
    prova_esegui("regexp: test e letterale", "/a+b/.test('xaaab') + ',' + /^b/.test('ab');", "true,false");
    prova_esegui("regexp: divisione resta divisione", "var a = 8, b = 2, g = 2; a / b / g;", "2");
    prova_esegui("regexp: /= resta divisione", "var x = 9; x /= 3; x;", "3");
    prova_esegui("regexp: /=.../ e' una regexp", "/=a/.test('b=a');", "true");
    prova_esegui("regexp: exec e gruppi", "var m = /(\\d+)-(\\d+)/.exec('tel 12-345'); m[0] + '|' + m[1] + '|' + m[2] + '|' + m.index;", "12-345|12|345|4");
    prova_esegui("regexp: gruppo che non partecipa", "var m = /a(x)?b/.exec('ab'); m[1] === undefined;", "true");
    prova_esegui("regexp: gruppi con nome", "var m = /(?<a>\\w+)@(?<b>\\w+)/.exec('io@qui'); m.groups.a + ',' + m.groups.b;", "io,qui");
    prova_esegui("regexp: classi e negazione", "/^[a-c]+[^0-9]$/.test('abcx') + ',' + /^[a-c]+[^0-9]$/.test('abc1');", "true,false");
    prova_esegui("regexp: \\d \\w \\s", "/^\\d\\w\\s\\S$/.test('1a b') + ',' + /\\D/.test('123');", "true,false");
    prova_esegui("regexp: flag i", "/HELLO/i.test('hello') + ',' + /[A-Z]+/i.exec('abc')[0];", "true,abc");
    prova_esegui("regexp: ancore e flag m", "/^b/m.test('a\\nb') + ',' + /^b/.test('a\\nb') + ',' + /a$/m.test('a\\nb');", "true,false,true");
    prova_esegui("regexp: . e flag s", "/a.b/.test('a\\nb') + ',' + /a.b/s.test('a\\nb');", "false,true");
    prova_esegui("regexp: confini di parola", "'il gatto gattone'.replace(/\\bgatto\\b/g, 'X');", "il X gattone");
    prova_esegui("regexp: alternanza", "/^(cane|gatto)$/.test('gatto') + ',' + /^(cane|gatto)$/.test('gattone');", "true,false");
    prova_esegui("regexp: quantificatori {n,m}", "/^a{2,3}$/.test('aa') + ',' + /^a{2,3}$/.test('aaaa') + ',' + /^a{2}$/.test('aa') + ',' + /^a{2,}$/.test('aaaaa');", "true,false,true,true");
    prova_esegui("regexp: avido e pigro", "/<.+>/.exec('<a><b>')[0] + '|' + /<.+?>/.exec('<a><b>')[0];", "<a><b>|<a>");
    prova_esegui("regexp: pigro con gruppo", "/(ab)+?c/.exec('ababc')[0];", "ababc");
    prova_esegui("regexp: riferimento all'indietro", "/(\\w)\\1/.exec('abccd')[0] + ',' + /(?<x>a)\\k<x>/.test('aa');", "cc,true");
    prova_esegui("regexp: guardare avanti", "'100px 20em'.match(/\\d+(?=px)/)[0] + ',' + /a(?!b)/.exec('abac').index;", "100,2");
    prova_esegui("regexp: \\x \\u e scappati", "/\\x41\\u0042\\./.test('AB.') + ',' + /\\//.test('a/b');", "true,true");
    prova_esegui("regexp: g e lastIndex", "var r = /o/g, s = 'foo'; r.exec(s).index + ',' + r.lastIndex + ',' + r.exec(s).index + ',' + (r.exec(s) === null) + ',' + r.lastIndex;", "1,2,2,true,0");
    prova_esegui("regexp: sticky", "var r = /a/y; r.test('ba') + ',' + (r.lastIndex = 1, r.test('ba'));", "false,true");
    prova_esegui("regexp: new RegExp", "new RegExp('a' + '+', 'g').exec('baa')[0] + ',' + RegExp('x').test('x') + ',' + new RegExp(/q/i).flags;", "aa,true,i");
    prova_esegui("regexp: source e toString", "var r = /a\\/b/gi; r.source + ' ' + r.flags + ' ' + r;", "a\\/b gi /a\\/b/gi");
    prova_esegui("regexp: sintassi sbagliata lancia", "var e; try { new RegExp('(a'); } catch (x) { e = x.name; } e;", "SyntaxError");
    prova_esegui("replace: $1 e $&", "'ciao mondo'.replace(/(\\w+) (\\w+)/, '$2 $1 [$&]');", "mondo ciao [ciao mondo]");
    prova_esegui("replace: $<nome> e $$", "'a-b'.replace(/(?<x>a)-(?<y>b)/, '$<y>$$$<x>');", "b$a");
    prova_esegui("replace: $` e $'", "'xAy'.replace('A', '[$`|$\\']');", "x[x|y]y");
    prova_esegui("replace: funzione", "'1 2 3'.replace(/\\d/g, function (m) { return m * 2; });", "2 4 6");
    prova_esegui("replace: funzione con gruppi e posizione", "'a1b2'.replace(/([a-z])(\\d)/g, (m, l, n, i) => l.toUpperCase() + n + i);", "A10B22");
    prova_esegui("replace: stringa, solo la prima", "'a.a.a'.replace('.', '-');", "a-a.a");
    prova_esegui("replaceAll: stringa", "'a.a.a'.replaceAll('.', '-');", "a-a-a");
    prova_esegui("replace: match vuoti", "'abc'.replace(/x*/g, '-');", "-a-b-c-");
    prova_esegui("split: regexp", "'a1b22c'.split(/\\d+/).join('|');", "a|b|c");
    prova_esegui("split: con gruppi", "'a1b2c'.split(/(\\d)/).join('|');", "a|1|b|2|c");
    prova_esegui("split: stringa com'era", "'a,b,,c'.split(',').length + ',' + 'abc'.split('').join('.');", "4,a.b.c");
    prova_esegui("split: limite", "'a b c d'.split(/ /, 2).join('|');", "a|b");
    prova_esegui("match: senza g e con g", "'x1y22'.match(/\\d+/)[0] + ',' + 'x1y22'.match(/\\d+/g).join('|') + ',' + ('abc'.match(/z/g) === null);", "1,1|22,true");
    prova_esegui("matchAll", "var r = ''; for (const m of 'a1b2'.matchAll(/([a-z])(\\d)/g)) r += m[1] + m[2] + '@' + m.index + ' '; r;", "a1@0 b2@2 ");
    prova_esegui("search", "'abcd'.search(/c/) + ',' + 'abcd'.search(/z/);", "2,-1");
    prova_esegui("regexp: niente blocco su (a*)*b", "var e = 'no'; try { /(a*)*b/.test('aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaac'); } catch (x) { e = x.name; } e;", "RangeError");
    prova_esegui("regexp: .* su un testo lungo", "var s = 'x'.repeat(3000) + 'fine', e = 'no'; try { /^(x)*f/.test(s); } catch (x) { e = x.name; } /^x*fine$/.test(s) + ',' + /^.*e$/.test(s) + ',' + /^(?:xx)+fine$/.test('xx'.repeat(150) + 'fine') + ',' + e;", "true,true,true,RangeError");
    prova_esegui("regexp: in un ciclo", "var n = 0, r = /\\d/; for (var i = 0; i < 200; i++) if (r.test('a' + i)) n++; n;", "200");

    printf("\n=== eseguire: i conti ===\n\n");
    prova_esegui("somma",              "1+2;",              "3");
    prova_esegui("precedenza",         "1+2*3;",            "7");
    prova_esegui("parentesi",          "(1+2)*3;",          "9");
    prova_esegui("divisione",          "7/2;",              "3.5");
    prova_esegui("modulo",             "7%3;",              "1");
    /* ! DIVIDERE PER ZERO NON E' UN ERRORE in JavaScript: fa Infinity. Un
     * motore che desse errore fermerebbe pagine che funzionano. */
    prova_esegui("uno diviso zero",    "1/0;",              "Infinity");
    prova_esegui("zero diviso zero",   "0/0;",              "NaN");
    prova_esegui("bit",                "6&3;",              "2");
    prova_esegui("spostamento",        "1<<10;",            "1024");
    /* ! GLI OPERATORI SUI BIT LAVORANO SU INTERI A 32 BIT CON SEGNO, e la
     * conversione fa parte della definizione. */
    prova_esegui("2147483648|0",       "2147483648|0;",     "-2147483648");

    printf("\n=== eseguire: le conversioni, dove stanno le sorprese ===\n\n");
    prova_esegui("stringa + numero",   "'a'+1;",            "a1");
    prova_esegui("numero + stringa",   "1+'2';",            "12");
    prova_esegui("stringa * numero",   "'5'*2;",            "10");
    prova_esegui("numero + numero",    "1+2+'a';",          "3a");
    prova_esegui("'5'-2",              "'5'-2;",            "3");
    prova_esegui("true+1",             "true+1;",           "2");
    prova_esegui("null+1",             "null+1;",           "1");
    prova_esegui("undefined+1",        "undefined+1;",      "NaN");
    prova_esegui("'abc'*1",            "'abc'*1;",          "NaN");

    printf("\n=== eseguire: la verita' e i confronti ===\n\n");
    /* ! SONO FALSI: false, 0, NaN, "", null, undefined. Tutto il resto e'
     * vero, COMPRESO "0" — chi sbaglia questo elenco scrive `if` che prendono
     * il ramo sbagliato senza dare errore. */
    prova_esegui("if('0')",            "if('0'){1;}else{2;}",   "1");
    prova_esegui("if('')",             "if(''){1;}else{2;}",    "2");
    prova_esegui("if(0)",              "if(0){1;}else{2;}",     "2");
    prova_esegui("1=='1'",             "1=='1';",           "true");
    prova_esegui("1==='1'",            "1==='1';",          "false");
    prova_esegui("null==undefined",    "null==undefined;",  "true");
    prova_esegui("null===undefined",   "null===undefined;", "false");
    prova_esegui("NaN!=NaN",           "0/0!=0/0;",         "true");
    prova_esegui("'a'<'b'",            "'a'<'b';",          "true");
    /* ! IL CORTO CIRCUITO RENDE L'OPERANDO, non un booleano: e' il modo in
     * cui si scrivono i valori predefiniti. */
    prova_esegui("|| rende l'operando", "0||'niente';",     "niente");
    prova_esegui("&& rende l'operando", "1&&'si';",         "si");

    printf("\n=== eseguire: variabili, cicli, funzioni ===\n\n");
    prova_esegui("var e assegnazione",  "var a=1;a=a+2;a;",  "3");
    prova_esegui("un ciclo",            "var s=0;for(var i=1;i<=10;i++)s=s+i;s;", "55");
    prova_esegui("while",               "var i=0;while(i<5)i++;i;", "5");
    prova_esegui("do..while gira una volta", "var i=9;do{i++;}while(i<5);i;", "10");
    prova_esegui("break",               "var i=0;while(1){i++;if(i>3)break;}i;", "4");
    prova_esegui("continue",            "var s=0,i;for(i=0;i<5;i++){if(i==2)continue;s=s+i;}s;", "8");
    prova_esegui("una funzione",        "function f(a,b){return a+b;}f(2,3);", "5");
    /* ! LE DICHIARAZIONI SI ISSANO: `f` si puo' chiamare prima di dov'e'
     * scritta, e il codice vero lo fa di continuo. */
    prova_esegui("chiamata prima della definizione", "var x=f(4);function f(a){return a*2;}x;", "8");
    prova_esegui("ricorsione",          "function f(n){return n<2?1:n*f(n-1);}f(5);", "120");
    prova_esegui("i++ rende il valore prima", "var i=1,j=i++;j;", "1");
    prova_esegui("++i rende quello dopo",     "var i=1,j=++i;j;", "2");

    printf("\n=== eseguire: le chiusure ===\n\n");
    /* ! LA FUNZIONE SI RICORDA L'AMBITO IN CUI E' NATA, non quello in cui e'
     * chiamata. E' tutta la differenza, ed e' il pezzo che rende JavaScript
     * quello che e'. */
    prova_esegui("un contatore",
                 "function conta(){var n=0;return function(){n=n+1;return n;};}"
                 "var c=conta();c();c();c();", "3");
    prova_esegui("due contatori indipendenti",
                 "function conta(){var n=0;return function(){return ++n;};}"
                 "var a=conta(),b=conta();a();a();b();", "1");

    printf("\n=== eseguire: oggetti e vettori ===\n\n");
    prova_esegui("oggetto",             "var o={a:1,b:2};o.a+o.b;", "3");
    prova_esegui("proprieta' nuova",    "var o={};o.x=5;o.x;",      "5");
    prova_esegui("proprieta' che non c'e'", "var o={};o.x;",        "undefined");
    prova_esegui("vettore",             "var v=[1,2,3];v[1];",      "2");
    prova_esegui("lunghezza",           "[1,2,3].length;",          "3");
    prova_esegui("vettore che cresce",  "var v=[];v[0]=7;v.length;", "1");
    prova_esegui("vettore come testo",  "''+[1,2,3];",              "1,2,3");
    prova_esegui("lunghezza di una stringa", "'ciao'.length;",      "4");
    /* ! `this` E' L'OGGETTO PRIMA DEL PUNTO, e lo decide la FORMA della
     * chiamata: per questo `var g=o.f; g()` perde `this`. */
    prova_esegui("this dentro un metodo",
                 "var o={n:7,dammi:function(){return this.n;}};o.dammi();", "7");
    prova_esegui("new",
                 "function P(x){this.x=x;}var p=new P(3);p.x;", "3");
    prova_esegui("for..in su un oggetto",
                 "var o={a:1,b:2},s='';for(var k in o)s=s+k;s.length;", "2");

    printf("\n=== eseguire: typeof, e cio' che non c'e' ===\n\n");
    /* ! typeof SU UN NOME CHE NON ESISTE NON E' UN ERRORE: e' l'unico modo di
     * chiedere se una cosa c'e', e mezzo web comincia proprio cosi'. */
    prova_esegui("typeof di un nome assente", "typeof pippo;", "undefined");
    prova_esegui("typeof numero",       "typeof 1;",          "number");
    prova_esegui("typeof stringa",      "typeof 'a';",        "string");
    prova_esegui("typeof funzione",     "typeof function(){};","function");
    prova_esegui("typeof null",         "typeof null;",       "object");
    prova_esegui("nome non definito",   "pippo+1;",           0);
    /* ! LE DUE GUARDIE: un ciclo senza fine e una ricorsione senza fine non
     * devono poter portarsi via la macchina. */
    prova_esegui("ricorsione senza fine", "function f(){return f();}f();", 0);

    printf("\n=== la libreria di base: i nomi globali ===\n\n");
    /* ! parseInt E Number NON SONO LA STESSA COSA, e le pagine si appoggiano
     * alla differenza: chi le confonde legge male ogni misura CSS. */
    prova_esegui("parseInt('12px')",   "parseInt('12px');",   "12");
    prova_esegui("Number('12px')",     "Number('12px');",     "NaN");
    prova_esegui("parseInt('0x1f')",   "parseInt('0x1f');",   "31");
    prova_esegui("parseInt('101',2)",  "parseInt('101',2);",  "5");
    prova_esegui("parseFloat('3.5em')","parseFloat('3.5em');","3.5");
    prova_esegui("isNaN",              "isNaN('a');",         "true");
    prova_esegui("isFinite(1/0)",      "isFinite(1/0);",      "false");
    prova_esegui("String(12)",         "String(12);",         "12");

    printf("\n=== la libreria di base: Math ===\n\n");
    prova_esegui("floor",              "Math.floor(3.7);",    "3");
    prova_esegui("floor negativo",     "Math.floor(-3.2);",   "-4");
    prova_esegui("ceil",               "Math.ceil(3.2);",     "4");
    prova_esegui("round",              "Math.round(2.5);",    "3");
    prova_esegui("abs",                "Math.abs(-7);",       "7");
    prova_esegui("sqrt",               "Math.sqrt(144);",     "12");
    prova_esegui("pow",                "Math.pow(2,10);",     "1024");
    prova_esegui("pow negativo",       "Math.pow(2,-2);",     "0.25");
    prova_esegui("min",                "Math.min(3,1,2);",    "1");
    prova_esegui("max",                "Math.max(3,1,2);",    "3");
    prova_esegui("random sta fra 0 e 1",
                 "var r=Math.random();r>=0&&r<1;",            "true");

    printf("\n=== la libreria di base: Date e valueOf ===\n\n");
    prova_esegui("Date(0) ISO",        "new Date(0).toISOString();", "1970-01-01T00:00:00.000Z");
    prova_esegui("Date.now dall'orologio", "Date.now();",     "1000000000123");
    prova_esegui("new Date() e' adesso", "new Date().getTime();", "1000000000123");
    prova_esegui("adesso in ISO",      "new Date().toISOString();", "2001-09-09T01:46:40.123Z");
    prova_esegui("getDay domenica",    "new Date().getDay();", "0");
    prova_esegui("campi",              "var d=new Date(2024,1,29,13,5,9,7);"
                 "[d.getFullYear(),d.getMonth(),d.getDate(),d.getHours(),"
                 "d.getMinutes(),d.getSeconds(),d.getMilliseconds()].join(' ');",
                 "2024 1 29 13 5 9 7");
    /* ! IL MESE 12 E' GENNAIO DELL'ANNO DOPO, il giorno 0 l'ultimo del mese
     * prima: i calendari scritti a mano ci contano. */
    prova_esegui("mese che trabocca",  "new Date(2023,12,1).toISOString();", "2024-01-01T00:00:00.000Z");
    prova_esegui("giorno 0",           "new Date(2024,2,0).getDate();", "29");
    prova_esegui("anno a due cifre",   "new Date(99,0).getFullYear();", "1999");
    prova_esegui("prima del 1970",     "new Date(-1).toISOString();", "1969-12-31T23:59:59.999Z");
    prova_esegui("Date.UTC",           "Date.UTC(2000,0,1);", "946684800000");
    prova_esegui("leggere ISO",        "Date.parse('2000-01-01T00:00:00Z');", "946684800000");
    prova_esegui("leggere ISO +02:00", "Date.parse('2000-01-01T02:00+02:00');", "946684800000");
    prova_esegui("leggere solo data",  "new Date('2024-03-15').getDate();", "15");
    prova_esegui("leggere toUTCString","Date.parse('Sat, 01 Jan 2000 00:00:00 GMT');", "946684800000");
    prova_esegui("leggere toString",
                 "var d=new Date(123456789000);Date.parse(d.toString())==d.getTime()-d.getMilliseconds();",
                 "true");
    prova_esegui("leggere Jan 1, 2000","Date.parse('Jan 1, 2000');", "946684800000");
    prova_esegui("stringa sbagliata",  "isNaN(new Date('pippo').getTime());", "true");
    prova_esegui("Invalid Date",       "String(new Date('pippo'));", "Invalid Date");
    prova_esegui("toUTCString",        "new Date(0).toUTCString();", "Thu, 01 Jan 1970 00:00:00 GMT");
    prova_esegui("toString",           "new Date(0).toString();",
                 "Thu Jan 01 1970 00:00:00 GMT+0000 (Coordinated Universal Time)");
    prova_esegui("toLocaleString",     "new Date(0).toLocaleString();", "1/1/1970, 12:00:00 AM");
    prova_esegui("getTimezoneOffset",  "new Date().getTimezoneOffset();", "0");
    prova_esegui("setDate che trabocca","var d=new Date(2024,0,31);d.setDate(32);d.getMonth();", "1");
    prova_esegui("setHours rende il tempo","new Date(0).setHours(1);", "3600000");
    prova_esegui("setFullYear",        "var d=new Date(0);d.setFullYear(2000,5,15);d.toISOString();",
                 "2000-06-15T00:00:00.000Z");
    prova_esegui("setTime",            "var d=new Date();d.setTime(5);d.valueOf();", "5");
    prova_esegui("Date() senza new",   "typeof Date();", "string");
    prova_esegui("new Date(data)",     "var a=new Date(77);new Date(a).getTime();", "77");
    /* ! LA DIFFERENZA FRA DUE DATE E' UN NUMERO: e' il modo in cui ogni
     * pagina misura il tempo, e prima dava NaN. */
    prova_esegui("b - a",              "var a=new Date(1000),b=new Date(4500);b-a;", "3500");
    prova_esegui("confronto fra date", "new Date(1)<new Date(2);", "true");
    prova_esegui("+data",              "+new Date(42);", "42");
    prova_esegui("data + '' e' testo", "(new Date(0)+'').slice(0,3);", "Thu");
    prova_esegui("JSON di una data",   "JSON.stringify({d:new Date(0)});",
                 "{\"d\":\"1970-01-01T00:00:00.000Z\"}");
    prova_esegui("for..in non vede il tempo",
                 "var d=new Date(0),n=0,k;for(k in d)n++;n;", "0");
    prova_esegui("valueOf nel *",      "var o={valueOf:function(){return 5}};o*2;", "10");
    prova_esegui("valueOf nel +",      "var o={valueOf:function(){return 5}};o+1;", "6");
    prova_esegui("toString se valueOf e' un oggetto",
                 "var o={toString:function(){return '7'}};o*2;", "14");
    prova_esegui("vettore + numero",   "[1,2]+3;", "1,23");
    prova_esegui("oggetto + testo",    "({})+'!';", "[object Object]!");
    /* ! LA VIRGOLA IN TESTA: con la prima voce saltata JSON.stringify
     * scriveva {,"b":1}. */
    prova_esegui("JSON salta la prima voce", "JSON.stringify({a:undefined,b:1});", "{\"b\":1}");

    printf("\n=== la libreria di base: le stringhe ===\n\n");
    prova_esegui("charAt",             "'ciao'.charAt(1);",   "i");
    prova_esegui("charCodeAt",         "'A'.charCodeAt(0);",  "65");
    prova_esegui("fromCharCode",       "String.fromCharCode(65,66);", "AB");
    /* ! indexOf RENDE -1 QUANDO NON C'E', e mezzo web scrive `>= 0`. */
    prova_esegui("indexOf trovato",    "'ciao'.indexOf('a');","2");
    prova_esegui("indexOf assente",    "'ciao'.indexOf('z');","-1");
    prova_esegui("lastIndexOf",        "'abab'.lastIndexOf('ab');", "2");
    /* ! slice E substring SI COMPORTANO DIVERSAMENTE COI NEGATIVI, e le pagine
     * usano tutt'e due. */
    prova_esegui("slice negativo",     "'ciao'.slice(-2);",   "ao");
    prova_esegui("substring negativo", "'ciao'.substring(-2);","ciao");
    prova_esegui("toUpperCase",        "'ciao'.toUpperCase();","CIAO");
    prova_esegui("toLowerCase",        "'CIAO'.toLowerCase();","ciao");
    prova_esegui("trim",               "'  x  '.trim();",     "x");
    prova_esegui("split e lunghezza",  "'a,b,c'.split(',').length;", "3");
    prova_esegui("split e primo pezzo","'a,b,c'.split(',')[0];", "a");
    prova_esegui("split('')",          "'ab'.split('').length;", "2");
    prova_esegui("split() senza nulla","'ab'.split().length;", "1");
    prova_esegui("replace",            "'a-b-c'.replace('-','+');", "a+b-c");
    prova_esegui("metodi in catena",   "'  Ciao Mondo '.trim().toLowerCase();",
                 "ciao mondo");

    printf("\n=== la libreria di base: i vettori ===\n\n");
    prova_esegui("push rende la lunghezza", "var v=[1];v.push(2,3);", "3");
    prova_esegui("push e poi leggi",   "var v=[];v.push(9);v[0];", "9");
    prova_esegui("pop",                "var v=[1,2,3];v.pop();", "3");
    prova_esegui("pop accorcia",       "var v=[1,2,3];v.pop();v.length;", "2");
    prova_esegui("join",               "[1,2,3].join('-');",  "1-2-3");
    prova_esegui("join con null",      "[1,null,2].join('-');", "1--2");
    prova_esegui("indexOf",            "[1,2,3].indexOf(2);", "1");
    /* ! IL CONFRONTO E' STRETTO: [1].indexOf('1') rende -1. */
    prova_esegui("indexOf e' stretto", "[1].indexOf('1');",   "-1");
    prova_esegui("reverse",            "[1,2,3].reverse().join('');", "321");
    prova_esegui("slice",              "[1,2,3,4].slice(1,3).join('');", "23");
    prova_esegui("slice negativo",     "[1,2,3,4].slice(-2).join('');", "34");

    printf("\n=== il C che chiama JavaScript ===\n\n");
    /* ! QUESTE TRE SONO LA PROVA CHE exjs_chiama FUNZIONA DA DENTRO UNA
     * NATIVA: e' il meccanismo che servira' a ogni gestore di evento del DOM. */
    prova_esegui("forEach",
                 "var s=0;[1,2,3].forEach(function(x){s=s+x;});s;", "6");
    prova_esegui("forEach vede l'indice",
                 "var s='';[9,8].forEach(function(x,i){s=s+i;});s;", "01");
    prova_esegui("map",   "[1,2,3].map(function(x){return x*2;}).join('');", "246");
    prova_esegui("filter","[1,2,3,4].filter(function(x){return x>2;}).join('');", "34");
    prova_esegui("map e chiusura",
                 "var k=10;[1,2].map(function(x){return x+k;}).join('-');", "11-12");

    printf("\n=== console.log ===\n\n");
    prova_stampa("una riga",       "console.log('ciao');",        "ciao\n");
    prova_stampa("piu' argomenti", "console.log(1,'a',true);",    "1 a true\n");
    prova_stampa("un vettore",     "console.log([1,2]);",         "1,2\n");
    prova_stampa("dentro un ciclo",
                 "for(var i=0;i<3;i++)console.log(i);",           "0\n1\n2\n");

    printf("\n=== JSON: scrivere ===\n\n");
    prova_esegui("numero",       "JSON.stringify(1);",            "1");
    prova_esegui("stringa",      "JSON.stringify('a');",          "\"a\"");
    prova_esegui("booleano",     "JSON.stringify(true);",         "true");
    prova_esegui("null",         "JSON.stringify(null);",         "null");
    prova_esegui("vettore",      "JSON.stringify([1,2,3]);",      "[1,2,3]");
    prova_esegui("oggetto",      "JSON.stringify({a:1,b:2});",    "{\"a\":1,\"b\":2}");
    prova_esegui("annidati",     "JSON.stringify({a:[1,{b:2}]});","{\"a\":[1,{\"b\":2}]}");
    prova_esegui("vuoti",        "JSON.stringify([])+JSON.stringify({});", "[]{}");
    /* ! LE VIRGOLETTE E GLI A CAPO VANNO PROTETTI: un a capo dentro una
     * stringa JSON e' vietato dalla norma, e lasciarcelo produce un file che
     * nessun altro analizzatore accetta. */
    prova_esegui("virgolette dentro", "JSON.stringify('di\"co');",  "\"di\\\"co\"");
    prova_esegui("a capo dentro",     "JSON.stringify('a\\nb');",   "\"a\\nb\"");
    /* ! NaN E Infinity NON ESISTONO IN JSON e diventano null: e' quello che fa
     * JavaScript, e un file con dentro NaN non lo rilegge nessuno. */
    prova_esegui("NaN diventa null",  "JSON.stringify(0/0);",       "null");
    prova_esegui("Infinity idem",     "JSON.stringify(1/0);",       "null");
    /* ! undefined SPARISCE da un oggetto e diventa null in un vettore: li'
     * toglierlo cambierebbe gli indici di tutti gli altri. */
    prova_esegui("undefined nel vettore", "JSON.stringify([1,undefined,2]);",
                 "[1,null,2]");
    prova_esegui("undefined nell'oggetto","JSON.stringify({a:1,b:undefined});",
                 "{\"a\":1}");
    prova_esegui("le funzioni spariscono",
                 "JSON.stringify({a:1,f:function(){}});", "{\"a\":1}");
    /* ! UN CICLO NON DEVE PORTARSI VIA LA PILA. `var o={};o.io=o` e' un attimo
     * da scrivere, e in una struttura vera — un nodo che punta al padre — e'
     * normale. */
    prova_esegui("un ciclo si ferma",
                 "var o={};o.io=o;typeof JSON.stringify(o);", "undefined");

    printf("\n=== JSON: leggere ===\n\n");
    prova_esegui("numero",       "JSON.parse('1')+1;",            "2");
    prova_esegui("stringa",      "JSON.parse('\"ciao\"');",       "ciao");
    prova_esegui("true",         "JSON.parse('true');",           "true");
    prova_esegui("null",         "typeof JSON.parse('null');",    "object");
    prova_esegui("vettore",      "JSON.parse('[1,2,3]')[1];",     "2");
    prova_esegui("lunghezza",    "JSON.parse('[1,2,3]').length;", "3");
    prova_esegui("oggetto",      "JSON.parse('{\"a\":7}').a;",    "7");
    prova_esegui("annidato",     "JSON.parse('{\"a\":[1,{\"b\":9}]}').a[1].b;", "9");
    prova_esegui("scappamenti",  "JSON.parse('\"a\\\\nb\"').length;", "3");
    prova_esegui("\\u",          "JSON.parse('\"\\\\u0041\"');",  "A");
    prova_esegui("spazi attorno","JSON.parse('  { \"a\" : 1 } ').a;", "1");
    prova_esegui("negativi",     "JSON.parse('[-1.5]')[0];",      "-1.5");

    printf("\n=== JSON: cio' che deve essere RIFIUTATO ===\n\n");
    /* ! JSON NON E' JavaScript. Accettare anche il resto sarebbe piu' comodo e
     * sbagliato: passerebbe qui roba che ogni altro sistema rifiuta. */
    prova_esegui("chiave senza virgolette", "typeof JSON.parse('{a:1}');",  "undefined");
    prova_esegui("virgolette singole",      "typeof JSON.parse(\"'a'\");",  "undefined");
    prova_esegui("virgola finale",          "typeof JSON.parse('[1,]');",   "undefined");
    prova_esegui("spazzatura in coda",      "typeof JSON.parse('1 x');",    "undefined");
    prova_esegui("parentesi non chiusa",    "typeof JSON.parse('[1');",     "undefined");

    printf("\n=== JSON: andata e ritorno ===\n\n");
    /* La prova che conta: cio' che esce da stringify deve rientrare identico. */
    prova_esegui("giro completo",
                 "var o={n:'x',v:[1,2],b:true};"
                 "JSON.stringify(JSON.parse(JSON.stringify(o)));",
                 "{\"n\":\"x\",\"v\":[1,2],\"b\":true}");

    printf("\n=== i vettori, il resto ===\n\n");
    prova_esegui("shift",          "var v=[1,2,3];v.shift();",       "1");
    prova_esegui("shift accorcia", "var v=[1,2,3];v.shift();v.join('');", "23");
    prova_esegui("unshift",        "var v=[3];v.unshift(1,2);v.join('');", "123");
    /* ! UN VETTORE PASSATO A concat SI APRE, un valore qualunque no. */
    prova_esegui("concat con vettore", "[1].concat([2,3]).join('');", "123");
    prova_esegui("concat con valore",  "[1].concat(2).join('');",     "12");
    prova_esegui("lastIndexOf",    "[1,2,1].lastIndexOf(1);",         "2");

    printf("\n=== sort ===\n\n");
    /* ! LA SORPRESA PIU' FAMOSA DI JavaScript: senza confronto si ordina come
     * TESTO, anche i numeri. Sembra un difetto e non lo e': e' la norma, e un
     * motore che ordinasse per valore darebbe risultati diversi da ogni altro. */
    prova_esegui("senza confronto e' testo", "[10,9,1].sort().join(',');", "1,10,9");
    prova_esegui("con il confronto",
                 "[10,9,1].sort(function(a,b){return a-b;}).join(',');", "1,9,10");
    prova_esegui("al contrario",
                 "[1,9,10].sort(function(a,b){return b-a;}).join(',');", "10,9,1");
    prova_esegui("stringhe",       "['pera','mela'].sort().join(',');", "mela,pera");
    prova_esegui("gia' ordinato",  "[1,2,3].sort().join('');",          "123");
    prova_esegui("uno solo",       "[5].sort().join('');",              "5");
    prova_esegui("vuoto",          "[].sort().length;",                 "0");
    /* ! `undefined` VA IN FONDO SEMPRE, e non passa dal confronto: e' l'unica
     * eccezione scritta nella norma. */
    prova_esegui("undefined in fondo",
                 "var v=[3,undefined,1];v.sort(function(a,b){return a-b;});"
                 "typeof v[2];", "undefined");
    prova_esegui("sort rende lo stesso vettore",
                 "var v=[2,1];v.sort()===v;", "true");

    printf("\n=== i tempi, con un orologio inventato ===\n\n");
    {
        static const unsigned int ore[] = { 50, 150, 250 };

        prova_tempo("setTimeout non parte subito",
                    "setTimeout(function(){console.log('poi');},100);",
                    ore, 0, "");
        prova_tempo("e non parte nemmeno a 50",
                    "setTimeout(function(){console.log('poi');},100);",
                    ore, 1, "");
        prova_tempo("parte a 150",
                    "setTimeout(function(){console.log('poi');},100);",
                    ore, 2, "poi\n");
        prova_tempo("una volta sola",
                    "setTimeout(function(){console.log('x');},100);",
                    ore, 3, "x\n");
        /* ! setInterval SI RIACCODA, e la prossima scadenza si conta da
         * ADESSO: una pagina rimasta ferma non spara tutte le esecuzioni
         * perse una dietro l'altra. */
        prova_tempo("setInterval si ripete",
                    "setInterval(function(){console.log('t');},100);",
                    ore, 3, "t\nt\n");
        prova_tempo("clearTimeout ferma",
                    "var id=setTimeout(function(){console.log('mai');},100);"
                    "clearTimeout(id);",
                    ore, 3, "");
        /* ! L'ORDINE E' QUELLO DELLE SCADENZE, non quello di creazione. */
        prova_tempo("in ordine di scadenza",
                    "setTimeout(function(){console.log('b');},90);"
                    "setTimeout(function(){console.log('a');},10);",
                    ore, 3, "a\nb\n");
        /* ! UNA CHIUSURA SOPRAVVIVE ALLO SCRIPT CHE L'HA CREATA: e' il motivo
         * per cui l'albero adesso si allunga invece di rifarsi. */
        prova_tempo("una chiusura in coda",
                    "var n=7;setTimeout(function(){console.log(n);},10);",
                    ore, 3, "7\n");
    }

    printf("\n=== due script nella stessa pagina ===\n\n");
    prova_due_script("le variabili restano",
                     "var a=1;", "console.log(a+1);", 0, "2\n");
    prova_due_script("una funzione del primo",
                     "function f(){console.log('dal primo');}",
                     "f();", 0, "dal primo\n");
    /* ! LA PROVA DEL DIFETTO CORRETTO: il timer nasce nel PRIMO script, il
     * secondo allunga l'albero, e la funzione deve ancora eseguire il codice
     * suo. Con l'albero che si rifaceva, qui usciva altro — o niente. */
    prova_due_script("un timer del primo script",
                     "var n=42;setTimeout(function(){console.log(n);},10);",
                     "var altro=1;function g(){return 99;}",
                     100, "42\n");
    prova_due_script("e la chiusura vede il suo valore",
                     "function crea(){var v='mio';"
                     "return function(){console.log(v);};}"
                     "setTimeout(crea(),10);",
                     "var v='di un altro';",
                     100, "mio\n");
    /* =========================================================================
     * LE STRINGHE LUNGHE
     *
     * ! QUESTE PROVE NASCONO DA UN DIFETTO, e vale la pena dire quale: ogni
     * metodo di String copiava il soggetto in un buffer da 512 byte e lavorava
     * sui primi 511 caratteri. `pagina.indexOf('riquadro')` rendeva -1 su un
     * testo che quella parola ce l'aveva — nessun errore, nessun avviso, la
     * risposta sbagliata. Si e' visto quando XMLHttpRequest ha cominciato a
     * consegnare documenti interi agli script; prima, di stringhe cosi' lunghe
     * in giro ce n'erano poche.
     *
     * ! IL NUMERO 512 NON COMPARE IN NESSUNA DI QUESTE PROVE, ed e' voluto:
     * si prova che il tetto NON C'E', non che e' piu' alto. Mille caratteri
     * bastano a farlo vedere, e chi domani lo abbassasse per sbaglio se ne
     * accorgerebbe qui.
     * ====================================================================== */
    printf("\n=== le stringhe lunghe (piu' di un buffer) ===\n");

/* ! LA STRINGA SI RADDOPPIA INVECE DI ALLUNGARSI DI DIECI, e non e' un vezzo:
 * ExJs non ha un raccoglitore di memoria, quindi ogni concatenazione lascia
 * nell'arena la stringa di prima. Cento passi da dieci caratteri ne
 * consumavano cinquantamila e la finivano; sette raddoppi ne consumano
 * duemilacinquecento e arrivano piu' lontano. E' una lezione sulle stringhe in
 * questo motore, non solo un modo di scrivere la prova. */
#define LUNGA "var s = '0123456789';" \
              "for (var i = 0; i < 7; i++) s = s + s;"

    prova_esegui("milleduecentottanta caratteri ci sono tutti",
                 LUNGA "s.length", "1280");
    prova_esegui("indexOf trova oltre il buffer",
                 LUNGA "s = s + 'AGO'; s.indexOf('AGO')", "1280");
    prova_esegui("e rende -1 solo quando davvero non c'e'",
                 LUNGA "s.indexOf('AGO')", "-1");
    prova_esegui("lastIndexOf pure",
                 LUNGA "s.lastIndexOf('89')", "1278");
    prova_esegui("charAt oltre il buffer",
                 LUNGA "s.charAt(1279)", "9");
    prova_esegui("charCodeAt oltre il buffer",
                 LUNGA "s.charCodeAt(1279)", "57");
    prova_esegui("slice tiene la coda intera",
                 LUNGA "s.slice(900).length", "380");
    prova_esegui("substring pure",
                 LUNGA "s.substring(500, 1000).length", "500");
    prova_esegui("toUpperCase non accorcia",
                 LUNGA "s = s + 'coda'; s.toUpperCase().length", "1284");
    prova_esegui("e cambia caso in fondo",
                 LUNGA "s = s + 'coda'; s.toUpperCase().slice(1280)", "CODA");
    prova_esegui("split taglia dove deve, anche in fondo",
                 LUNGA "s = s + '|ultimo'; var p = s.split('|');"
                       "p.length + ' ' + p[1]", "2 ultimo");
    prova_esegui("replace sostituisce e non tronca",
                 LUNGA "s = s + 'AGO'; var r = s.replace('AGO', 'X');"
                       "r.length + ' ' + r.slice(1278)", "1281 89X");
    prova_esegui("replace in testa tiene tutta la coda",
                 LUNGA "var r = s.replace('012', 'X');"
                       "r.length + ' ' + r.slice(-3)", "1278 789");
    prova_esegui("trim non accorcia quel che sta in mezzo",
                 LUNGA "('  ' + s + '  ').trim().length", "1280");
    /* ! DUE TESTI LUNGHI CHE DIFFERISCONO IN FONDO SONO DIVERSI, e con la
     * copia troncata risultavano uguali. */
    /* @NAVMETA, 28 settembre 2026: toString implicito, e join senza tetto */
    prova_esegui("vettore annidato in testa, String()",
                 "String([[1,2],3])", "1,2,3");
    prova_esegui("vettori annidati in join",
                 "[[1,2],[3,[4,5]]].join('-')", "1,2-3,4,5");
    prova_esegui("join di 2000 caratteri non si taglia",
                 "var a=[]; for (var i=0;i<200;i++) a.push('abcdefghij'); a.join('').length", "2000");
    prova_esegui("join con un separatore lungo",
                 "['a','b'].join('----------------------------------------separatore')",
                 "a----------------------------------------separatoreb");
    prova_esegui("toString implicito nella concatenazione",
                 "var o={toString:function(){return 'io'}}; 'x'+o", "xio");
    prova_esegui("toString implicito in String()",
                 "var o={toString:function(){return 'io'}}; String(o)", "io");
    prova_esegui("toString degli elementi in join",
                 "var o={toString:function(){return 'io'}}; [o,1,o].join('+')", "io+1+io");
    prova_esegui("un oggetto senza toString resta [object Object]",
                 "String({a:1})", "[object Object]");
    prova_esegui("un vettore che contiene se stesso non gira per sempre",
                 "var a=[1]; a.push(a); String(a)", "1,");
    prova_esegui("un toString che rende un numero",
                 "var o={toString:function(){return 42}}; 'n='+o", "n=42");
    prova_esegui("due lunghe si confrontano per intero",
                 LUNGA "var a = s + 'A'; var b = s + 'B'; (a < b) + ' ' + (a == b)",
                 "true false");
    prova_esegui("JSON.stringify non taglia una stringa lunga",
                 LUNGA "JSON.parse(JSON.stringify(s)).length", "1280");
    prova_esegui("e JSON.parse rilegge un documento lungo",
                 LUNGA "var o = JSON.parse('{\"t\":\"' + s + '\"}'); o.t.length",
                 "1280");
#undef LUNGA


    printf("\n%d prove, %d sbagliate\n", fatte, sbagliate);
    return sbagliate ? 1 : 0;
}
