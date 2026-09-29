/* =============================================================================
 * lib/exjs/lex.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Da testo a gettoni — il primo pezzo di ExJs
 *
 * -----------------------------------------------------------------------------
 * ! LE ESPRESSIONI REGOLARI NON SI LEGGONO QUI, E IL PERCHE' VA SAPUTO PRIMA
 * DI CERCARE IL DIFETTO.
 *
 * In JavaScript la barra `/` e' divisione oppure l'inizio di
 * un'espressione regolare, e non si puo' decidere guardando i caratteri: si
 * decide da COSA E' VENUTO PRIMA. Dopo un valore e' divisione (`a / b`), dopo
 * un operatore e' una regexp (`a = /x/`). E' l'unico punto in cui un
 * analizzatore lessicale di JavaScript non puo' lavorare da solo.
 *
 * Il primo scaglione non ha le espressioni regolari, quindi qui `/` e' sempre
 * divisione o commento. Quando arriveranno, la soluzione NON e' indovinare da
 * qui: e' che sia chi costruisce l'albero a dire «adesso mi aspetto un valore»
 * prima di chiedere il gettone. Il posto e' gia' segnato piu' sotto.
 *
 * -----------------------------------------------------------------------------
 * ! L'A CAPO SI RICORDA, e non e' un dettaglio di formattazione.
 *
 * JavaScript inserisce i punti e virgola da solo, e la regola piu' feroce
 * riguarda `return`: se dopo `return` c'e' un fine riga, la funzione rende
 * `undefined` — qualunque cosa sia scritta sulla riga successiva. Un motore
 * che non tenesse traccia degli a capo eseguirebbe un programma diverso da
 * quello scritto, e in silenzio. Percio' ogni gettone si porta dietro
 * `a_capo_prima`.
 *
 * -----------------------------------------------------------------------------
 * ! LE STRINGHE ESCONO GIA' SCIOLTE. `\n`, `\t`, `à` diventano i byte che
 * significano gia' qui: chi legge un gettone TK_STRINGA legge il testo vero.
 * Lasciare gli scappamenti dentro vorrebbe dire scioglierli piu' avanti, e
 * «piu' avanti» sono almeno due posti diversi — il costruttore dell'albero e
 * chi confronta i nomi delle proprieta'.
 * ============================================================================= */

#include "exjs_int.h"

/* Il motore non si porta dietro la libc del sistema: i tre aiuti che servono
 * stanno qui, come fa ogni pezzo di questo albero. */
static int uguali_n(const char *a, const char *b, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return b[n] == '\0';
}

static int e_spazio(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
           c == '\v';
}

static int e_cifra(char c)      { return c >= '0' && c <= '9'; }
static int e_esa(char c)
{
    return e_cifra(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static int val_esa(char c)
{
    if (c <= '9') return c - '0';
    return ((c | 0x20) - 'a') + 10;
}

/* ! IL DOLLARO E IL TRATTINO BASSO SONO LETTERE, in JavaScript. Dimenticarlo
 * vuol dire non saper leggere meta' del codice che gira sul web, dove `$` e'
 * il nome piu' usato che ci sia. */
static int e_inizio_nome(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           c == '_' || c == '$' || (unsigned char)c >= 0x80;
}

static int e_dentro_nome(char c)
{
    return e_inizio_nome(c) || e_cifra(c);
}

/* -----------------------------------------------------------------------------
 * Le parole chiave
 *
 * ! SI CONFRONTANO PER LUNGHEZZA PRIMA CHE PER LETTERE, e non e' furbizia: le
 * parole chiave sono trenta e i nomi in una pagina vera sono migliaia. Saltare
 * subito tutte quelle di lunghezza diversa costa un confronto invece di trenta.
 * --------------------------------------------------------------------------- */
typedef struct { const char *parola; int tipo; } Chiave;

static const Chiave CHIAVI[] = {
    { "var",        TK_VAR },        { "function",   TK_FUNCTION },
    { "return",     TK_RETURN },     { "if",         TK_IF },
    { "else",       TK_ELSE },       { "while",      TK_WHILE },
    { "for",        TK_FOR },        { "break",      TK_BREAK },
    { "continue",   TK_CONTINUE },   { "new",        TK_NEW },
    { "delete",     TK_DELETE },     { "typeof",     TK_TYPEOF },
    { "in",         TK_IN },         { "instanceof", TK_INSTANCEOF },
    { "this",       TK_THIS },       { "null",       TK_NULL },
    { "true",       TK_TRUE },       { "false",      TK_FALSE },
    { "do",         TK_DO },         { "switch",     TK_SWITCH },
    { "case",       TK_CASE },       { "default",    TK_DEFAULT },
    { "try",        TK_TRY },        { "catch",      TK_CATCH },
    { "finally",    TK_FINALLY },    { "throw",      TK_THROW },
    { "void",       TK_VOID },
    { "let",        TK_LET },        { "const",      TK_CONST },
    { "class",      TK_CLASS },      { "extends",    TK_EXTENDS },
    { "super",      TK_SUPER },
    { 0, 0 }
};

static int chiave_di(const char *p, unsigned int n)
{
    int i;
    for (i = 0; CHIAVI[i].parola; i++)
        if (uguali_n(p, CHIAVI[i].parola, n)) return CHIAVI[i].tipo;
    return TK_NOME;
}

/* -----------------------------------------------------------------------------
 * Gli errori
 * --------------------------------------------------------------------------- */
static void copia(char *dst, unsigned int max, const char *s)
{
    unsigned int i = 0;
    while (s[i] && i + 1 < max) { dst[i] = s[i]; i++; }
    dst[i] = '\0';
}

static int errore(ExJsLex *L, const char *messaggio)
{
    if (L->err) {
        L->err->riga       = L->t_riga;
        L->err->colonna    = L->t_colonna;
        L->err->posizione  = L->inizio;
        copia(L->err->messaggio, EXJS_ERR_LEN, messaggio);
    }
    L->tipo = TK_ERRORE;
    return TK_ERRORE;
}

/* -----------------------------------------------------------------------------
 * Avanzare di un carattere, tenendo il conto delle righe
 * --------------------------------------------------------------------------- */
static char avanti1(ExJsLex *L)
{
    char c = L->sorgente[L->pos++];

    if (c == '\n') { L->riga++; L->colonna = 1; }
    else            L->colonna++;
    return c;
}

static char guarda(const ExJsLex *L, unsigned int avanti)
{
    unsigned int p = L->pos + avanti;
    return p < L->n ? L->sorgente[p] : '\0';
}

void exjs_lex_apri(ExJsLex *L, const char *sorgente, unsigned int n,
                   char *buffer_testo, unsigned int buffer_max,
                   ExJsErrore *err)
{
    L->sorgente  = sorgente;
    L->n         = n;
    L->pos       = 0;
    L->riga      = 1;
    L->colonna   = 1;
    L->tipo      = TK_FINE;
    L->inizio    = 0;
    L->fine      = 0;
    L->t_riga    = 1;
    L->t_colonna = 1;
    L->a_capo_prima = 0;
    L->numero    = 0;
    L->testo     = buffer_testo;
    L->testo_max = buffer_max;
    L->testo_n   = 0;
    L->err       = err;
}

/* -----------------------------------------------------------------------------
 * Gli spazi e i commenti
 *
 * ! UN COMMENTO CHE CONTIENE UN A CAPO CONTA COME UN A CAPO, e le pagine vere
 * ci contano: e' del tutto normale trovare un `return` seguito da un commento
 * su piu' righe e dal
 * valore sulla riga dopo. Trattare quel commento come uno spazio qualunque
 * cambierebbe il significato del programma.
 * --------------------------------------------------------------------------- */
static void salta_vuoto(ExJsLex *L)
{
    L->a_capo_prima = 0;

    for (;;) {
        char c;

        if (L->pos >= L->n) return;
        c = L->sorgente[L->pos];

        if (e_spazio(c)) {
            if (c == '\n') L->a_capo_prima = 1;
            avanti1(L);
            continue;
        }

        if (c == '/' && guarda(L, 1) == '/') {
            while (L->pos < L->n && L->sorgente[L->pos] != '\n') avanti1(L);
            continue;
        }

        if (c == '/' && guarda(L, 1) == '*') {
            avanti1(L); avanti1(L);
            while (L->pos < L->n) {
                if (L->sorgente[L->pos] == '*' && guarda(L, 1) == '/') {
                    avanti1(L); avanti1(L);
                    break;
                }
                if (L->sorgente[L->pos] == '\n') L->a_capo_prima = 1;
                avanti1(L);
            }
            continue;
        }

        return;
    }
}

/* -----------------------------------------------------------------------------
 * I numeri
 *
 * ! SI ACCETTANO 0x, LA VIRGOLA E L'ESPONENTE, e si rifiuta l'ottale con lo
 * zero davanti. `010` in ES3 valeva otto, ed e' una delle poche cose che ES5
 * ha tolto perche' faceva piu' danni che comodo: qui vale dieci, come si
 * aspetta chiunque scriva codice oggi.
 * --------------------------------------------------------------------------- */
static int leggi_numero(ExJsLex *L)
{
    double v = 0.0;

    if (L->sorgente[L->pos] == '0' &&
        (guarda(L, 1) == 'x' || guarda(L, 1) == 'X')) {
        avanti1(L); avanti1(L);
        if (!e_esa(guarda(L, 0))) return errore(L, "cifra esadecimale attesa dopo 0x");
        while (L->pos < L->n && e_esa(L->sorgente[L->pos]))
            v = v * 16.0 + (double)val_esa(avanti1(L));
        L->numero = v;
        L->tipo   = TK_NUMERO;
        return TK_NUMERO;
    }

    /* ! LE CIFRE IN UN INTERO, L'ESPONENTE A PARTE, E UNA SOLA MOLTIPLICAZIONE
     * ALLA FINE (29 settembre 2026). Sommare 0.1, 0.01... cifra per cifra
     * accumulava errori: `0.0000001` non era 1e-7, e si scriveva
     * 1.0000000000000005e-7. Diciannove cifre stanno in 64 bit; le altre
     * contano solo come esponente. Il long double e' quello di val.c. */
    {
        unsigned long long m = 0;
        int  esp10 = 0, cifre_m = 0;

        while (L->pos < L->n && e_cifra(L->sorgente[L->pos])) {
            int k = avanti1(L) - '0';
            if (cifre_m < 19) { m = m * 10ULL + (unsigned long long)k; if (m) cifre_m++; }
            else esp10++;
        }
        if (L->pos < L->n && L->sorgente[L->pos] == '.') {
            avanti1(L);
            while (L->pos < L->n && e_cifra(L->sorgente[L->pos])) {
                int k = avanti1(L) - '0';
                if (cifre_m < 19) { m = m * 10ULL + (unsigned long long)k; if (m) cifre_m++; esp10--; }
            }
        }
        if (L->pos < L->n && (L->sorgente[L->pos] == 'e' || L->sorgente[L->pos] == 'E')) {
            int segno = 1, esp = 0, cifre = 0;

            avanti1(L);
            if (L->pos < L->n && (L->sorgente[L->pos] == '+' || L->sorgente[L->pos] == '-'))
                segno = (avanti1(L) == '-') ? -1 : 1;
            while (L->pos < L->n && e_cifra(L->sorgente[L->pos])) {
                esp = esp * 10 + (avanti1(L) - '0');
                cifre++;
                if (esp > 4096) esp = 4096;     /* oltre e' comunque infinito */
            }
            if (!cifre) return errore(L, "esponente senza cifre");
            esp10 += segno * esp;
        }
        {
            long double x = (long double)m, b = 10.0L, p = 1.0L;
            int         e = esp10 < 0 ? -esp10 : esp10;

            while (e) { if (e & 1) p *= b; b *= b; e >>= 1; }
            x = (esp10 < 0) ? x / p : x * p;
            v = (double)x;
        }
    }

    L->numero = v;
    L->tipo   = TK_NUMERO;
    return TK_NUMERO;
}

/* -----------------------------------------------------------------------------
 * Le stringhe
 * --------------------------------------------------------------------------- */
static int metti(ExJsLex *L, char c)
{
    if (L->testo_n + 1 >= L->testo_max) return 0;
    L->testo[L->testo_n++] = c;
    return 1;
}

/* ! UN CARATTERE UNICODE SI SCRIVE IN UTF-8, perche' e' quello che il resto
 * del sistema legge: exhtml scioglie le entita' in UTF-8 e exfont disegna
 * UTF-8. Un motore che rendesse UTF-16 costringerebbe a convertire a ogni
 * confronto di stringa. */
static int metti_unicode(ExJsLex *L, unsigned int u)
{
    if (u < 0x80) return metti(L, (char)u);
    if (u < 0x800)
        return metti(L, (char)(0xC0 | (u >> 6))) &&
               metti(L, (char)(0x80 | (u & 0x3F)));
    return metti(L, (char)(0xE0 | (u >> 12))) &&
           metti(L, (char)(0x80 | ((u >> 6) & 0x3F))) &&
           metti(L, (char)(0x80 | (u & 0x3F)));
}

static int leggi_stringa(ExJsLex *L)
{
    char virgoletta = avanti1(L);

    L->testo_n = 0;

    for (;;) {
        char c;

        if (L->pos >= L->n) return errore(L, "stringa non chiusa");

        c = avanti1(L);
        if (c == virgoletta) break;

        /* ! UN A CAPO DENTRO UNA STRINGA E' UN ERRORE, non un carattere. Le
         * stringhe su piu' righe in ES3 non esistono, e accettarle vorrebbe
         * dire trasformare una virgoletta dimenticata in una stringa che si
         * mangia il resto del programma senza dire niente. */
        if (c == '\n') return errore(L, "a capo dentro una stringa: manca una virgoletta?");

        if (c != '\\') { if (!metti(L, c)) return errore(L, "stringa troppo lunga"); continue; }

        if (L->pos >= L->n) return errore(L, "stringa non chiusa dopo \\");
        c = avanti1(L);

        switch (c) {
        case 'n':  if (!metti(L, '\n')) return errore(L, "stringa troppo lunga"); break;
        case 't':  if (!metti(L, '\t')) return errore(L, "stringa troppo lunga"); break;
        case 'r':  if (!metti(L, '\r')) return errore(L, "stringa troppo lunga"); break;
        case 'b':  if (!metti(L, '\b')) return errore(L, "stringa troppo lunga"); break;
        case 'f':  if (!metti(L, '\f')) return errore(L, "stringa troppo lunga"); break;
        case 'v':  if (!metti(L, '\v')) return errore(L, "stringa troppo lunga"); break;
        case '0':  if (!metti(L, '\0')) return errore(L, "stringa troppo lunga"); break;

        /* ! UNA BARRA PRIMA DI UN A CAPO UNISCE LE DUE RIGHE, e non produce
         * niente: e' l'unico modo legittimo di spezzare una stringa lunga. */
        case '\n': break;

        case 'x': {
            unsigned int u = 0;
            int i;
            for (i = 0; i < 2; i++) {
                if (L->pos >= L->n || !e_esa(L->sorgente[L->pos]))
                    return errore(L, "\\x vuole due cifre esadecimali");
                u = u * 16 + (unsigned int)val_esa(avanti1(L));
            }
            if (!metti_unicode(L, u)) return errore(L, "stringa troppo lunga");
            break;
        }

        case 'u': {
            unsigned int u = 0;
            int i;
            for (i = 0; i < 4; i++) {
                if (L->pos >= L->n || !e_esa(L->sorgente[L->pos]))
                    return errore(L, "\\u vuole quattro cifre esadecimali");
                u = u * 16 + (unsigned int)val_esa(avanti1(L));
            }
            if (!metti_unicode(L, u)) return errore(L, "stringa troppo lunga");
            break;
        }

        /* Qualunque altro carattere dopo la barra vale se stesso: e' cosi' che
         * si scrivono \\ e \" e \'. */
        default:
            if (!metti(L, c)) return errore(L, "stringa troppo lunga");
            break;
        }
    }

    L->testo[L->testo_n] = '\0';
    L->tipo = TK_STRINGA;
    return TK_STRINGA;
}

/* -----------------------------------------------------------------------------
 * Gli operatori
 *
 * ! I PIU' LUNGHI SI PROVANO PER PRIMI, sempre. Provando `>` prima di `>>>=`
 * si spezzerebbe uno spostamento in un confronto seguito da roba senza senso —
 * e il messaggio d'errore parlerebbe di un punto lontano da dove sta lo
 * sbaglio, che qui non c'e' nemmeno.
 * --------------------------------------------------------------------------- */
static int operatore(ExJsLex *L)
{
    char c = L->sorgente[L->pos];
    char d = guarda(L, 1);
    char e = guarda(L, 2);
    char f = guarda(L, 3);
    int  t = 0, quanti = 1;

    switch (c) {
    case '>':
        if (d == '>' && e == '>' && f == '=') { t = TK_SHR_U_UG;   quanti = 4; }
        else if (d == '>' && e == '>')        { t = TK_SHR_U;      quanti = 3; }
        else if (d == '>' && e == '=')        { t = TK_SHR_UG;     quanti = 3; }
        else if (d == '>')                    { t = TK_SHR;        quanti = 2; }
        else if (d == '=')                    { t = TK_MAG_UG;     quanti = 2; }
        else                                  { t = '>';           quanti = 1; }
        break;
    case '<':
        if (d == '<' && e == '=')             { t = TK_SHL_UG;     quanti = 3; }
        else if (d == '<')                    { t = TK_SHL;        quanti = 2; }
        else if (d == '=')                    { t = TK_MIN_UG;     quanti = 2; }
        else                                  { t = '<';           quanti = 1; }
        break;
    case '=':
        if (d == '=' && e == '=')             { t = TK_ID_UGUALE;  quanti = 3; }
        else if (d == '>')                    { t = TK_FRECCIA;    quanti = 2; }
        else if (d == '=')                    { t = TK_UGUALE;     quanti = 2; }
        else                                  { t = '=';           quanti = 1; }
        break;
    case '!':
        if (d == '=' && e == '=')             { t = TK_ID_DIVERSO; quanti = 3; }
        else if (d == '=')                    { t = TK_DIVERSO;    quanti = 2; }
        else                                  { t = '!';           quanti = 1; }
        break;
    case '+':
        if (d == '+')                         { t = TK_PIU_PIU;    quanti = 2; }
        else if (d == '=')                    { t = TK_PIU_UG;     quanti = 2; }
        else                                  { t = '+';           quanti = 1; }
        break;
    case '-':
        if (d == '-')                         { t = TK_MENO_MENO;  quanti = 2; }
        else if (d == '=')                    { t = TK_MENO_UG;    quanti = 2; }
        else                                  { t = '-';           quanti = 1; }
        break;
    case '*':
        if (d == '*' && e == '=')             { t = TK_POT_UG;     quanti = 3; }
        else if (d == '*')                    { t = TK_POT;        quanti = 2; }
        else if (d == '=')                    { t = TK_PER_UG;     quanti = 2; }
        else                                  { t = '*';           quanti = 1; }
        break;
    /* ! QUI, IL GIORNO DELLE ESPRESSIONI REGOLARI. Oggi la barra e' sempre
     * divisione perche' i commenti li ha gia' tolti salta_vuoto(). Quando
     * serviranno le regexp, la decisione arrivera' da chi costruisce l'albero:
     * vedi il commento in cima al file. */
    case '/':
        if (d == '=')                         { t = TK_DIV_UG;     quanti = 2; }
        else                                  { t = '/';           quanti = 1; }
        break;
    case '%':
        if (d == '=')                         { t = TK_MOD_UG;     quanti = 2; }
        else                                  { t = '%';           quanti = 1; }
        break;
    case '&':
        if (d == '&' && e == '=')             { t = TK_E_E_UG;     quanti = 3; }
        else if (d == '&')                    { t = TK_E_E;        quanti = 2; }
        else if (d == '=')                    { t = TK_AND_UG;     quanti = 2; }
        else                                  { t = '&';           quanti = 1; }
        break;
    case '|':
        if (d == '|' && e == '=')             { t = TK_O_O_UG;     quanti = 3; }
        else if (d == '|')                    { t = TK_O_O;        quanti = 2; }
        else if (d == '=')                    { t = TK_OR_UG;      quanti = 2; }
        else                                  { t = '|';           quanti = 1; }
        break;
    case '^':
        if (d == '=')                         { t = TK_XOR_UG;     quanti = 2; }
        else                                  { t = '^';           quanti = 1; }
        break;

    case '?':
        /* `?.` ma non `a?.5:1`, dove il punto comincia un numero */
        if (d == '?' && e == '=')             { t = TK_NULLISH_UG; quanti = 3; }
        else if (d == '?')                    { t = TK_NULLISH;    quanti = 2; }
        else if (d == '.' && !(e >= '0' && e <= '9')) { t = TK_OPZ; quanti = 2; }
        else                                  { t = '?';           quanti = 1; }
        break;
    case '.':
        if (d == '.' && e == '.')             { t = TK_PUNTINI;    quanti = 3; }
        else                                  { t = '.';           quanti = 1; }
        break;

    case '(': case ')': case '{': case '}': case '[': case ']':
    case ';': case ',': case ':': case '~':
        t = (int)(unsigned char)c; quanti = 1;
        break;

    default:
        return errore(L, "carattere che non fa parte di JavaScript");
    }

    while (quanti--) avanti1(L);
    L->tipo = t;
    return t;
}

/* -----------------------------------------------------------------------------
 * I modelli `...${...}...` (29 settembre 2026)
 *
 * ! QUI SI TROVA SOLO LA FINE, il resto lo fa il costruttore dell'albero: le
 * espressioni dentro `${}` sono JavaScript intero, con stringhe, graffe e
 * altri modelli, e chi sa leggerle e' lui. Il lessico deve solo saltarle
 * senza sbagliare la graffa che le chiude.
 * --------------------------------------------------------------------------- */
static int salta_modello(ExJsLex *L);

static int salta_espressione(ExJsLex *L)
{
    int prof = 1;

    while (L->pos < L->n) {
        char c = L->sorgente[L->pos];

        if (c == '{') { prof++; avanti1(L); continue; }
        if (c == '}') { avanti1(L); if (--prof == 0) return 1; continue; }
        if (c == '`') { if (!salta_modello(L)) return 0; continue; }
        if (c == '"' || c == '\'') {
            char q = c;
            avanti1(L);
            while (L->pos < L->n && L->sorgente[L->pos] != q) {
                if (L->sorgente[L->pos] == '\\') avanti1(L);
                if (L->pos < L->n) avanti1(L);
            }
            if (L->pos >= L->n) return 0;
            avanti1(L);
            continue;
        }
        avanti1(L);
    }
    return 0;
}

static int salta_modello(ExJsLex *L)
{
    avanti1(L);                                 /* il ` di apertura */
    while (L->pos < L->n) {
        char c = L->sorgente[L->pos];

        if (c == '\\') { avanti1(L); if (L->pos < L->n) avanti1(L); continue; }
        if (c == '`')  { avanti1(L); return 1; }
        if (c == '$' && guarda(L, 1) == '{') {
            avanti1(L); avanti1(L);
            if (!salta_espressione(L)) return 0;
            continue;
        }
        avanti1(L);
    }
    return 0;
}

static int leggi_modello(ExJsLex *L)
{
    if (!salta_modello(L)) return errore(L, "modello ` non chiuso");
    L->tipo = TK_MODELLO;
    return TK_MODELLO;
}

int exjs_lex_avanti(ExJsLex *L)
{
    char c;

    salta_vuoto(L);

    L->inizio    = L->pos;
    L->t_riga    = L->riga;
    L->t_colonna = L->colonna;

    if (L->pos >= L->n) { L->tipo = TK_FINE; L->fine = L->pos; return TK_FINE; }

    c = L->sorgente[L->pos];

    if (e_cifra(c) || (c == '.' && e_cifra(guarda(L, 1)))) {
        int t = leggi_numero(L);
        L->fine = L->pos;
        return t;
    }

    if (c == '"' || c == '\'') {
        int t = leggi_stringa(L);
        L->fine = L->pos;
        return t;
    }

    if (c == '`') {
        int t = leggi_modello(L);
        L->fine = L->pos;
        return t;
    }

    if (e_inizio_nome(c)) {
        unsigned int p0 = L->pos;

        while (L->pos < L->n && e_dentro_nome(L->sorgente[L->pos])) avanti1(L);
        L->fine = L->pos;
        L->tipo = chiave_di(L->sorgente + p0, L->pos - p0);
        return L->tipo;
    }

    {
        int t = operatore(L);
        L->fine = L->pos;
        return t;
    }
}

/* -----------------------------------------------------------------------------
 * Il nome di un gettone, per i messaggi
 *
 * ! SERVE A DIRE COSA C'ERA, non solo cosa manca. «atteso ')' ma c'e' 'var'»
 * indica il difetto; «errore di sintassi» manda a rileggere tutto.
 * --------------------------------------------------------------------------- */
const char *exjs_lex_nome(int tipo)
{
    static char uno[2];
    int i;

    switch (tipo) {
    case TK_FINE:       return "fine del testo";
    case TK_ERRORE:     return "errore";
    case TK_NOME:       return "un nome";
    case TK_NUMERO:     return "un numero";
    case TK_STRINGA:    return "una stringa";
    case TK_UGUALE:     return "==";
    case TK_DIVERSO:    return "!=";
    case TK_ID_UGUALE:  return "===";
    case TK_ID_DIVERSO: return "!==";
    case TK_MIN_UG:     return "<=";
    case TK_MAG_UG:     return ">=";
    case TK_E_E:        return "&&";
    case TK_O_O:        return "||";
    case TK_PIU_PIU:    return "++";
    case TK_MENO_MENO:  return "--";
    case TK_PIU_UG:     return "+=";
    case TK_MENO_UG:    return "-=";
    case TK_PER_UG:     return "*=";
    case TK_DIV_UG:     return "/=";
    case TK_MOD_UG:     return "%=";
    case TK_SHL:        return "<<";
    case TK_SHR:        return ">>";
    case TK_SHR_U:      return ">>>";
    case TK_SHL_UG:     return "<<=";
    case TK_SHR_UG:     return ">>=";
    case TK_SHR_U_UG:   return ">>>=";
    case TK_AND_UG:     return "&=";
    case TK_OR_UG:      return "|=";
    case TK_XOR_UG:     return "^=";
    case TK_FRECCIA:    return "=>";
    case TK_NULLISH:    return "?\?";
    case TK_OPZ:        return "?.";
    case TK_POT:        return "**";
    case TK_POT_UG:     return "**=";
    case TK_PUNTINI:    return "...";
    case TK_NULLISH_UG: return "?\?=";
    case TK_E_E_UG:     return "&&=";
    case TK_O_O_UG:     return "||=";
    case TK_MODELLO:    return "un modello `...`";
    default: break;
    }

    for (i = 0; CHIAVI[i].parola; i++)
        if (CHIAVI[i].tipo == tipo) return CHIAVI[i].parola;

    if (tipo > 0 && tipo < 128) { uno[0] = (char)tipo; uno[1] = '\0'; return uno; }
    return "?";
}
