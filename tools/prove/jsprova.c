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

    printf("\n%d prove, %d sbagliate\n", fatte, sbagliate);
    return sbagliate ? 1 : 0;
}
