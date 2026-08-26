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
    prova_albero("switch non c'e' ancora", "switch(a){}", 0);
    prova_albero("parentesi non chiusa",   "f(1;",       0);
    prova_albero("var senza nome",         "var = 1;",   0);

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

    printf("\n%d prove, %d sbagliate\n", fatte, sbagliate);
    return sbagliate ? 1 : 0;
}
