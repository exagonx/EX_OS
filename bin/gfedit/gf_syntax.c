/* =============================================================================
 * bin/gfedit/gf_syntax.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * GF Edit — evidenziazione sintattica (modalità "developing").
 *
 * NIENTE THREAD, ed è la differenza di progetto più grossa rispetto
 * all'originale. Là un thread dedicato rianalizzava l'intero documento a
 * ogni modifica, con mutex e variabile di condizione per non pestare i
 * piedi al ciclo di disegno. Qui si colorano SOLO le ventuno righe
 * visibili, al momento di disegnarle: è un lavoro talmente piccolo che
 * un thread costerebbe più della cosa che deve accelerare.
 *
 * L'unica cosa che non si può decidere guardando una riga sola è se sia
 * dentro un commento di blocco aperto molte righe più su. Quella —
 * l'unico stato davvero multi-riga di un evidenziatore — è precalcolata
 * riga per riga in t->in_commento[], un byte per riga, aggiornata quando
 * il documento cambia. Colorare una riga visibile parte da lì e non
 * deve risalire il file.
 * ============================================================================= */

#include "gfedit.h"

/* =============================================================================
 * Parole chiave
 *
 * Elenchi terminati da NULL. Restano corti di proposito: servono a
 * distinguere la struttura del codice a colpo d'occhio, non a essere la
 * grammatica del linguaggio.
 * ============================================================================= */
static const char *kw_c[] = {
    "auto","break","case","char","const","continue","default","do","double",
    "else","enum","extern","float","for","goto","if","inline","int","long",
    "register","restrict","return","short","signed","sizeof","static","struct",
    "switch","typedef","union","unsigned","void","volatile","while",
    NULL
};

static const char *kw_cpp[] = {
    "auto","bool","break","case","catch","char","class","const","constexpr",
    "continue","default","delete","do","double","else","enum","explicit",
    "extern","false","float","for","friend","goto","if","inline","int","long",
    "namespace","new","nullptr","operator","private","protected","public",
    "return","short","signed","sizeof","static","struct","switch","template",
    "this","throw","true","try","typedef","typename","union","unsigned",
    "using","virtual","void","volatile","while",
    NULL
};

static const char *kw_basic[] = {
    "AND","AS","CALL","CASE","CLOSE","CONST","DATA","DIM","DO","ELSE","ELSEIF",
    "END","EXIT","FOR","FUNCTION","GOSUB","GOTO","IF","INPUT","LET","LOOP",
    "NEXT","NOT","OPEN","OR","PRINT","READ","REDIM","RETURN","SELECT","SHARED",
    "STATIC","STEP","SUB","THEN","TO","TYPE","UNTIL","WEND","WHILE","XOR",
    NULL
};

static const char *kw_asm[] = {
    "add","and","call","cli","cmp","dec","div","hlt","in","inc","int","iret",
    "ja","jae","jb","jbe","je","jmp","jne","jnz","jz","lea","mov","movzx",
    "movsx","mul","neg","nop","not","or","out","pop","popa","push","pusha",
    "ret","sti","sub","test","xchg","xor",
    "eax","ebx","ecx","edx","esi","edi","ebp","esp","ax","bx","cx","dx",
    "al","bl","cl","dl","ah","bh","ch","dh",
    NULL
};

static const char **tabella_kw(GfLingua l)
{
    switch (l) {
        case GF_LANG_C:     return kw_c;
        case GF_LANG_CPP:   return kw_cpp;
        case GF_LANG_BASIC: return kw_basic;
        case GF_LANG_ASM:   return kw_asm;
        default:            return NULL;
    }
}

/* Confronto di una parola non terminata da NUL contro una parola chiave.
 * Il BASIC non distingue maiuscole e minuscole, gli altri sì. */
static int parola_uguale(const char *testo, int len, const char *kw, int ignora_caso)
{
    int i;

    for (i = 0; i < len; i++) {
        char a = testo[i];
        char b = kw[i];
        if (!b) return 0;
        if (ignora_caso) { a = gf_minuscolo(a); b = gf_minuscolo(b); }
        if (a != b) return 0;
    }
    return kw[len] == '\0';
}

static int e_keyword(GfLingua l, const char *testo, int len)
{
    const char **tab = tabella_kw(l);
    int ignora_caso  = (l == GF_LANG_BASIC);
    int i;

    if (!tab || len <= 0) return 0;

    for (i = 0; tab[i]; i++) {
        if (parola_uguale(testo, len, tab[i], ignora_caso)) return 1;
    }
    return 0;
}

/* =============================================================================
 * I commenti di riga cambiano da linguaggio a linguaggio; quelli di
 * blocco esistono solo in C e C++.
 * ============================================================================= */
static int commento_di_riga(GfLingua l, const char *s, int i)
{
    switch (l) {
        case GF_LANG_C:
        case GF_LANG_CPP:
            return s[i] == '/' && s[i + 1] == '/';

        case GF_LANG_ASM:
            return s[i] == ';';

        case GF_LANG_BASIC:
            if (s[i] == '\'') return 1;
            return (gf_minuscolo(s[i])     == 'r' &&
                    gf_minuscolo(s[i + 1]) == 'e' &&
                    gf_minuscolo(s[i + 2]) == 'm' &&
                    (s[i + 3] == '\0' || gf_e_spazio(s[i + 3])));

        default:
            return 0;
    }
}

static int ha_blocchi(GfLingua l)
{
    return l == GF_LANG_C || l == GF_LANG_CPP;
}

/* =============================================================================
 * gf_evidenzia_riga — classifica ogni carattere della riga idx
 *
 * Scrive in out[] un GfToken per carattere. Il chiamante passa un buffer
 * grande almeno quanto la riga: quello che non ci sta non viene
 * colorato, non viene scritto fuori.
 * ============================================================================= */
void gf_evidenzia_riga(GfTab *t, int idx, unsigned char *out, int max)
{
    const char *s   = gf_riga(t, idx);
    int         len = (int)strlen(s);
    GfLingua    l   = t->lingua;
    int         i   = 0;
    int         in_blocco;
    int         solo_spazi = 1;

    if (max < len) len = max;
    for (i = 0; i < len; i++) out[i] = GF_TOK_NORMALE;

    if (l == GF_LANG_NONE) return;

    in_blocco = (idx >= 0 && idx < GF_MAX_LINES) ? t->in_commento[idx] : 0;

    i = 0;
    while (i < len) {
        /* --- dentro un commento di blocco ------------------------------- */
        if (in_blocco) {
            out[i] = GF_TOK_COMMENTO;
            if (s[i] == '*' && i + 1 < len && s[i + 1] == '/') {
                out[i + 1] = GF_TOK_COMMENTO;
                in_blocco = 0;
                i += 2;
            } else {
                i++;
            }
            continue;
        }

        /* --- direttiva del preprocessore -------------------------------- */
        if (solo_spazi && s[i] == '#' && (l == GF_LANG_C || l == GF_LANG_CPP)) {
            while (i < len) out[i++] = GF_TOK_PREPROC;
            break;
        }

        /* --- apertura di un commento ------------------------------------ */
        if (ha_blocchi(l) && s[i] == '/' && i + 1 < len && s[i + 1] == '*') {
            out[i] = out[i + 1] = GF_TOK_COMMENTO;
            in_blocco = 1;
            i += 2;
            continue;
        }

        if (commento_di_riga(l, s, i)) {
            while (i < len) out[i++] = GF_TOK_COMMENTO;
            break;
        }

        /* --- stringhe e caratteri --------------------------------------- */
        if (s[i] == '"' || (s[i] == '\'' && l != GF_LANG_BASIC)) {
            char apice = s[i];
            out[i++] = GF_TOK_STRINGA;
            while (i < len) {
                /* L'escape vale in C e C++; in BASIC e assembly la
                 * barra rovesciata dentro una stringa è un carattere
                 * come un altro. */
                if (s[i] == '\\' && (l == GF_LANG_C || l == GF_LANG_CPP) && i + 1 < len) {
                    out[i++] = GF_TOK_STRINGA;
                    out[i++] = GF_TOK_STRINGA;
                    continue;
                }
                out[i] = GF_TOK_STRINGA;
                i++;
                if (s[i - 1] == apice) break;
            }
            solo_spazi = 0;
            continue;
        }

        /* --- numeri ------------------------------------------------------ */
        if (gf_e_cifra(s[i]) && (i == 0 || !gf_e_ident(s[i - 1]))) {
            while (i < len && (gf_e_ident(s[i]) || s[i] == '.')) out[i++] = GF_TOK_NUMERO;
            solo_spazi = 0;
            continue;
        }

        /* --- identificatori: parola chiave, chiamata, o niente ----------- */
        if (gf_e_alpha(s[i]) || s[i] == '_') {
            int inizio = i;
            int j;

            while (i < len && gf_e_ident(s[i])) i++;

            if (e_keyword(l, s + inizio, i - inizio)) {
                for (j = inizio; j < i; j++) out[j] = GF_TOK_KEYWORD;
            } else {
                /* Una parola seguita da parentesi è una chiamata (o una
                 * definizione): non serve un parser per riconoscerla, e
                 * distinguerla a colori aiuta a leggere il codice più di
                 * quanto un errore occasionale disturbi. */
                int k = i;
                while (k < len && gf_e_spazio(s[k])) k++;
                if (k < len && s[k] == '(') {
                    for (j = inizio; j < i; j++) out[j] = GF_TOK_FUNZIONE;
                }
            }
            solo_spazi = 0;
            continue;
        }

        if (!gf_e_spazio(s[i])) solo_spazi = 0;
        i++;
    }
}

/* =============================================================================
 * gf_ricalcola_commenti — per ogni riga, "comincia dentro un blocco?"
 *
 * Va rifatto quando il documento cambia in modo che possa spostare
 * l'apertura o la chiusura di un blocco. Costa una scansione del
 * documento, che per 512 righe è nulla: farlo dopo ogni modifica è più
 * semplice — e più difficile da sbagliare — che tenere traccia di quali
 * modifiche siano capaci di alterarlo.
 * ============================================================================= */
void gf_ricalcola_commenti(GfTab *t)
{
    int in_blocco = 0;
    int r;

    if (!t->d) return;

    if (!ha_blocchi(t->lingua)) {
        memset(t->in_commento, 0, sizeof(t->in_commento));
        return;
    }

    for (r = 0; r < t->num_lines && r < GF_MAX_LINES; r++) {
        const char *s = t->d->text[r];
        int i = 0;

        t->in_commento[r] = (unsigned char)in_blocco;

        while (s[i]) {
            if (in_blocco) {
                if (s[i] == '*' && s[i + 1] == '/') { in_blocco = 0; i += 2; }
                else i++;
                continue;
            }

            /* Un'apertura di commento dentro una stringa, o dopo un
             * commento di riga, non apre niente: saltare quei due
             * contesti è il minimo per non far diventare verde metà
             * file per un carattere. */
            if (s[i] == '/' && s[i + 1] == '/') break;
            if (s[i] == '"' || s[i] == '\'') {
                char apice = s[i++];
                while (s[i]) {
                    if (s[i] == '\\' && s[i + 1]) { i += 2; continue; }
                    if (s[i] == apice) { i++; break; }
                    i++;
                }
                continue;
            }
            if (s[i] == '/' && s[i + 1] == '*') { in_blocco = 1; i += 2; continue; }
            i++;
        }
    }

    for (; r < GF_MAX_LINES; r++) t->in_commento[r] = 0;
}
