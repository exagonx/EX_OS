/* =============================================================================
 * lib/exhtml/html.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * HTML — da testo ad albero
 *
 * ! L'HTML NON E' XML, ED E' TUTTA LA DIFFICOLTA'. Un lettore XML puo'
 * rifiutare un documento malformato; un lettore HTML no, perche' quasi nessuna
 * pagina vera e' «ben formata»: i tag restano aperti, si chiudono nell'ordine
 * sbagliato, ne mancano meta'. Un browser che rifiutasse un documento cosi'
 * mostrerebbe una pagina bianca sul novanta per cento del web. Qui si accetta
 * tutto e si CHIUDE PER CONTO PROPRIO secondo regole scritte.
 *
 * ! NON ALLOCA NIENTE, e i buffer li da' chi chiama. Sono la stessa scelta di
 * raster.c: chi apre una pagina e' l'unico che sappia quanta memoria vuole
 * spenderci, e un albero che cresce quanto vuole il documento e' un documento
 * che decide quanta memoria prendere sulla macchina di chi lo guarda.
 *
 * ! E NON INCLUDE LA libc, come http.c, ttf.c e raster.c: si compila con un
 * `cc` sull'host e si prova contro pagine scritte a mano, invece di volere una
 * macchina virtuale per ogni riga cambiata.
 *
 * ! IL TESTO SI COPIA NELL'ARENA E NON SI PUNTA AL DOCUMENTO, e non e' spreco:
 * le entita' («&amp;») si sciolgono mentre si copia, quindi il testo dell'albero
 * e' gia' quello da disegnare. Puntando all'originale, ogni lettore dovrebbe
 * scioglierle da se' — cioe' la stessa tabella in due posti.
 * ============================================================================= */
#ifndef HTML_H
#define HTML_H

#ifdef __cplusplus
extern "C" {
#endif

#define HTML_ELEMENTO   0
#define HTML_TESTO      1

/* ! GLI INDICI SONO int E NON PUNTATORI, cosi' l'albero si puo' copiare,
 * salvare e guardare senza rilocare niente. -1 vuol dire «non c'e'». */
typedef struct {
    unsigned char tipo;
    int           padre;
    int           primo_figlio;
    int           ultimo_figlio;    /* per attaccare in coda senza scorrere */
    int           prossimo;         /* il fratello dopo */
    unsigned int  nome;             /* scostamento nell'arena: solo elementi */
    unsigned int  testo;            /* scostamento nell'arena: solo testo */
    int           attributi;        /* indice del primo attributo, -1 */
} HtmlNodo;

typedef struct {
    unsigned int nome;              /* scostamento nell'arena */
    unsigned int valore;            /* scostamento nell'arena */
    int          prossimo;
} HtmlAttr;

typedef struct {
    HtmlNodo    *nodi;
    unsigned int nodi_max, nodi_n;
    HtmlAttr    *attr;
    unsigned int attr_max, attr_n;
    char        *arena;
    unsigned int arena_max, arena_n;

    int          radice;

    /* ! CHE SIA FINITO LO SPAZIO SI DICE, non si lascia intuire da una pagina
     * a meta'. Un browser che mostra meno di quello che c'e' senza avvisare
     * fa credere che la pagina sia cosi'. */
    int          troncato;
} HtmlDoc;

/* Prepara il documento sui buffer di chi chiama. Va chiamata prima di
 * html_analizza, e rimette tutto a zero: lo stesso documento si puo' riusare
 * per una pagina nuova senza rifare i buffer. */
void html_prepara(HtmlDoc *d,
                  HtmlNodo *nodi, unsigned int nodi_max,
                  HtmlAttr *attr, unsigned int attr_max,
                  char *arena, unsigned int arena_max);

/* Costruisce l'albero. Rende 1 se ha prodotto qualcosa — anche troncato — e 0
 * solo se i buffer sono cosi' piccoli da non starci nemmeno la radice.
 *
 * ! NON RENDE 0 PER UN DOCUMENTO MALFATTO, apposta: vedi in cima. */
int html_analizza(HtmlDoc *d, const char *testo, unsigned int n);

/* Il nome di un elemento, o "" se il nodo e' testo. In MINUSCOLO: i nomi dei
 * tag non distinguono maiuscole, e deciderlo qui evita che ogni lettore
 * confronti due volte. */
const char *html_nome(const HtmlDoc *d, int nodo);

/* Il testo di un nodo di testo, o "". Le entita' sono gia' sciolte. */
const char *html_testo(const HtmlDoc *d, int nodo);

/* Il valore di un attributo, o 0 se l'elemento non ce l'ha. Il nome si
 * confronta senza distinguere maiuscole. */
const char *html_attr(const HtmlDoc *d, int nodo, const char *nome);

#ifdef __cplusplus
}
#endif

#endif /* HTML_H */
