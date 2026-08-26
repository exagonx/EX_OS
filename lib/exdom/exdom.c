/* =============================================================================
 * lib/exdom/exdom.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il ponte fra l'albero HTML e il motore JavaScript. Il perche' delle scelte
 * grosse sta in exdom.h; qui ci sono quelle che si vedono solo scrivendo.
 * ============================================================================= */

#include "exdom.h"

/* -----------------------------------------------------------------------------
 * Spiccioli. Questa libreria gira anche sull'ospite per le prove, quindi non
 * chiama la libc: le tre funzioni che servono stanno qui e sono corte.
 * --------------------------------------------------------------------------- */
static int ugu(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == '\0' && *b == '\0';
}

static unsigned int lung(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

static int minuscola(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int maiuscola(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

/* =============================================================================
 * IL LEGAME
 *
 * ! IL `dato` DI UN INVOLUCRO NON PUO' ESSERE SOLO IL DOCUMENTO, perche' un
 * gancio deve sapere anche QUALE nodo sta servendo, e ExJs porta un puntatore
 * solo. La strada furba sarebbe stata nascondere l'indice nei bit bassi del
 * puntatore; questa e' una struttura per nodo, otto byte in piu' ciascuno, e
 * si legge senza doverci pensare. Su una macchina da 32 MB otto byte per nodo
 * di una pagina sono un prezzo che non si sente.
 * ========================================================================== */
typedef struct {
    ExDom  *D;
    int     nodo;
    ExJsVal val;                    /* l'involucro, se e' gia' stato fatto */
} Legame;

struct ExDom {
    ExJsCtx     *js;
    HtmlDoc     *doc;
    unsigned int nodi_max;
    unsigned int testo_max;
    char        *testo;             /* dove si rimette in marcatore */
    Legame      *leg;
    ExJsVal      proto;             /* i metodi, in un posto solo */
    ExJsVal      documento;
    int          troncato;
};

/* -----------------------------------------------------------------------------
 * Un pezzo di testo che si riempie senza uscire dai bordi.
 * --------------------------------------------------------------------------- */
typedef struct {
    char        *p;
    unsigned int max, n;
    int          pieno;
} Testo;

static void t_car(Testo *t, int c)
{
    if (t->n + 1 < t->max) t->p[t->n++] = (char)c;
    else                   t->pieno = 1;
}

static void t_stringa(Testo *t, const char *s)
{
    while (*s) t_car(t, (unsigned char)*s++);
}

static void t_chiudi(Testo *t)
{
    if (t->max > 0) t->p[(t->n < t->max) ? t->n : t->max - 1] = '\0';
}

/* =============================================================================
 * GLI INVOLUCRI
 * ========================================================================== */
static int leggi_prop(ExJsCtx *c, void *dato, const char *nome, ExJsVal *fuori);
static int scrivi_prop(ExJsCtx *c, void *dato, const char *nome, ExJsVal v);

ExJsVal exdom_avvolgi(ExDom *D, int nodo)
{
    Legame *L;

    /* ! UN NODO CHE NON C'E' DIVENTA `null`, NON `undefined`. La differenza la
     * usano le pagine vere: `while (n.parentNode)` finisce perche' il padre
     * della radice e' null, e mezzo web controlla `if (x === null)`. Renderli
     * la stessa cosa vorrebbe dire cicli che non finiscono su codice giusto. */
    if (!D || nodo < 0 || nodo >= (int)D->nodi_max) return exjs_nullo();

    L = &D->leg[nodo];
    if (exjs_tipo(D->js, L->val) == EXJS_OGGETTO) return L->val;

    L->D    = D;
    L->nodo = nodo;
    L->val  = exjs_esotico(D->js, leggi_prop, scrivi_prop, L);
    if (exjs_tipo(D->js, L->val) != EXJS_OGGETTO) return exjs_nullo();

    /* ! I METODI STANNO SUL PROTOTIPO, non nell'involucro. Una pagina con
     * duemila nodi altrimenti pagherebbe duemila copie di appendChild, di
     * setAttribute e di tutto il resto — e sono proprieta' vere, che occupano
     * caselle vere in un motore che non ha un raccoglitore. */
    exjs_proto_metti(D->js, L->val, D->proto);
    return L->val;
}

int exdom_nodo(ExDom *D, ExJsVal v)
{
    Legame *L;

    if (!D) return -1;
    L = (Legame *)exjs_esotico_dato(D->js, v);
    /* ! SI CONTROLLA CHE IL LEGAME SIA NOSTRO. Un altro oggetto esotico —
     * oggi non ce ne sono, domani si' — porterebbe un `dato` di forma diversa,
     * e leggerlo come un Legame vorrebbe dire un indice inventato. */
    if (!L || L->D != D) return -1;
    return L->nodo;
}

ExJsVal exdom_documento(ExDom *D)  { return D ? D->documento : exjs_indefinito(); }
int     exdom_troncato(const ExDom *D) { return D ? D->troncato : 0; }

/* =============================================================================
 * CAMMINARE NELL'ALBERO
 *
 * ! LE VISITE SONO ITERATIVE, e non e' pignoleria. La ricorsione qui scenderebbe
 * profonda quanto l'albero, e da quando ci sono le mutazioni l'albero e'
 * profondo quanto vuole uno script: dieci righe di appendChild in un ciclo
 * bastano a far arrivare una getElementsByTagName in fondo alla pila. Un
 * puntatore che sale e scende costa tre righe in piu' e non ha fondo.
 * ========================================================================== */
static int prossimo_in_visita(const HtmlDoc *d, int n, int radice)
{
    if (d->nodi[n].primo_figlio >= 0) return d->nodi[n].primo_figlio;
    while (n >= 0 && n != radice) {
        if (d->nodi[n].prossimo >= 0) return d->nodi[n].prossimo;
        n = d->nodi[n].padre;
    }
    return -1;
}

static int fratello_prima(const HtmlDoc *d, int n)
{
    int p = d->nodi[n].padre, f, prec = -1;

    if (p < 0) return -1;
    for (f = d->nodi[p].primo_figlio; f >= 0; f = d->nodi[f].prossimo) {
        if (f == n) return prec;
        prec = f;
    }
    return -1;
}

/* Il testo di un sottoalbero, tutto attaccato: e' `textContent`. */
static void raccogli_testo(const HtmlDoc *d, int radice, Testo *t)
{
    int n = radice;

    while (n >= 0) {
        if (d->nodi[n].tipo == HTML_TESTO) t_stringa(t, d->arena + d->nodi[n].testo);
        n = prossimo_in_visita(d, n, radice);
    }
}

/* ! UNA CLASSE SI CONFRONTA A PAROLE, non come stringa intera:
 * `class="bottone grande"` deve rispondere a `grande`, e un confronto secco
 * direbbe di no. E' un errore che non si vede finche' non si prova su una
 * pagina vera, dove le classi sono quasi sempre piu' di una. */
static int ha_classe(const char *elenco, const char *cercata)
{
    unsigned int i = 0, k;

    if (!elenco || !cercata || !cercata[0]) return 0;
    while (elenco[i]) {
        while (elenco[i] == ' ' || elenco[i] == '\t' ||
               elenco[i] == '\n' || elenco[i] == '\r') i++;
        k = 0;
        while (elenco[i + k] && elenco[i + k] == cercata[k]) k++;
        if (cercata[k] == '\0' &&
            (elenco[i + k] == '\0' || elenco[i + k] == ' ' ||
             elenco[i + k] == '\t' || elenco[i + k] == '\n' ||
             elenco[i + k] == '\r'))
            return 1;
        while (elenco[i] && elenco[i] != ' ' && elenco[i] != '\t' &&
               elenco[i] != '\n' && elenco[i] != '\r') i++;
    }
    return 0;
}

static int trova_per_id(const HtmlDoc *d, int radice, const char *id)
{
    int n = radice;

    while (n >= 0) {
        if (d->nodi[n].tipo == HTML_ELEMENTO) {
            const char *v = html_attr((HtmlDoc *)d, n, "id");
            if (v && ugu(v, id)) return n;
        }
        n = prossimo_in_visita(d, n, radice);
    }
    return -1;
}

static int primo_tag(const HtmlDoc *d, int radice, const char *tag)
{
    int n = radice;

    while (n >= 0) {
        if (d->nodi[n].tipo == HTML_ELEMENTO && ugu(html_nome((HtmlDoc *)d, n), tag))
            return n;
        n = prossimo_in_visita(d, n, radice);
    }
    return -1;
}

/* =============================================================================
 * LE PROPRIETA' CHE SI LEGGONO
 * ========================================================================== */
static ExJsVal stringa_c(ExJsCtx *c, const char *s)
{
    return exjs_stringa(c, s ? s : "", -1);
}

static ExJsVal nome_del_nodo(ExDom *D, int n)
{
    char         b[64];
    unsigned int i = 0;
    const char  *s;

    if (D->doc->nodi[n].tipo == HTML_TESTO) return stringa_c(D->js, "#text");

    /* ! IL NOME DEL TAG ESCE IN MAIUSCOLO, e dentro l'albero sta in minuscolo.
     * Non e' un'incoerenza: e' quello che il DOM promette — `nodeName` di un
     * `<div>` e' "DIV" — e le pagine ci confrontano. Chi vuole il nome com'e'
     * scritto nell'albero usa html_nome, che e' un'altra domanda. */
    s = html_nome(D->doc, n);
    while (s[i] && i < sizeof(b) - 1) { b[i] = (char)maiuscola((unsigned char)s[i]); i++; }
    b[i] = '\0';
    return stringa_c(D->js, b);
}

static ExJsVal marcatore(ExDom *D, int n, int con_se_stesso)
{
    unsigned int serve = html_serializza(D->doc, n, con_se_stesso,
                                         D->testo, D->testo_max);

    /* ! SE NON C'E' STATO TUTTO LO SI SEGNA. La stringa resta quella troncata,
     * perche' renderne una vuota sarebbe peggio — chi guarda vedrebbe una
     * pagina senza contenuto invece di una pagina incompleta — ma il browser
     * ha un modo di saperlo, e non deve dedurlo dal risultato. */
    if (serve >= D->testo_max) D->troncato = 1;
    return stringa_c(D->js, D->testo);
}

static ExJsVal contenuto_testo(ExDom *D, int n)
{
    Testo t;

    t.p = D->testo; t.max = D->testo_max; t.n = 0; t.pieno = 0;
    raccogli_testo(D->doc, n, &t);
    t_chiudi(&t);
    if (t.pieno) D->troncato = 1;
    return stringa_c(D->js, D->testo);
}

static ExJsVal figli_vettore(ExDom *D, int n, int solo_elementi)
{
    ExJsVal      v = exjs_vettore(D->js);
    unsigned int k = 0;
    int          f;

    for (f = D->doc->nodi[n].primo_figlio; f >= 0; f = D->doc->nodi[f].prossimo) {
        if (solo_elementi && D->doc->nodi[f].tipo != HTML_ELEMENTO) continue;
        exjs_indice_metti(D->js, v, k++, exdom_avvolgi(D, f));
    }
    return v;
}

static int leggi_prop(ExJsCtx *c, void *dato, const char *nome, ExJsVal *fuori)
{
    Legame   *L = (Legame *)dato;
    ExDom    *D = L->D;
    HtmlDoc  *d = D->doc;
    int       n = L->nodo;
    int       elemento;

    if (n < 0 || n >= (int)d->nodi_n) return 0;
    elemento = (d->nodi[n].tipo == HTML_ELEMENTO);

    /* --- comuni a tutti i nodi ---------------------------------------- */
    if (ugu(nome, "nodeType")) {
        /* 1 = elemento, 3 = testo: sono i numeri del DOM, non i nostri. Il
         * codice delle pagine li scrive a mano. */
        *fuori = exjs_numero(c, elemento ? 1.0 : 3.0);
        return 1;
    }
    if (ugu(nome, "nodeName"))  { *fuori = nome_del_nodo(D, n); return 1; }
    if (ugu(nome, "parentNode")) {
        /* ! IL PADRE DELLA RADICE E' null, non il documento: la radice E' il
         * documento. */
        *fuori = exdom_avvolgi(D, d->nodi[n].padre);
        return 1;
    }
    if (ugu(nome, "firstChild")) { *fuori = exdom_avvolgi(D, d->nodi[n].primo_figlio); return 1; }
    if (ugu(nome, "lastChild"))  { *fuori = exdom_avvolgi(D, d->nodi[n].ultimo_figlio); return 1; }
    if (ugu(nome, "nextSibling")){ *fuori = exdom_avvolgi(D, d->nodi[n].prossimo); return 1; }
    if (ugu(nome, "previousSibling")) {
        *fuori = exdom_avvolgi(D, fratello_prima(d, n));
        return 1;
    }
    if (ugu(nome, "childNodes")) { *fuori = figli_vettore(D, n, 0); return 1; }
    if (ugu(nome, "ownerDocument")) { *fuori = D->documento; return 1; }
    if (ugu(nome, "outerHTML"))  { *fuori = marcatore(D, n, 1); return 1; }
    if (ugu(nome, "textContent") || ugu(nome, "innerText")) {
        *fuori = contenuto_testo(D, n);
        return 1;
    }

    /* --- solo i nodi di testo ----------------------------------------- */
    if (!elemento) {
        if (ugu(nome, "nodeValue") || ugu(nome, "data")) {
            *fuori = stringa_c(c, html_testo(d, n));
            return 1;
        }
        if (ugu(nome, "length")) {
            *fuori = exjs_numero(c, (double)lung(html_testo(d, n)));
            return 1;
        }
        return 0;
    }

    /* --- solo gli elementi -------------------------------------------- */
    if (ugu(nome, "tagName"))   { *fuori = nome_del_nodo(D, n); return 1; }
    if (ugu(nome, "innerHTML")) { *fuori = marcatore(D, n, 0); return 1; }
    if (ugu(nome, "children"))  { *fuori = figli_vettore(D, n, 1); return 1; }
    if (ugu(nome, "id")) {
        /* ! UN ATTRIBUTO CHE NON C'E' DA "" E NON `null`, per `id` e `class`.
         * E' quel che fa il DOM, ed e' il motivo per cui `if (el.className)`
         * funziona; `getAttribute` invece rende null, e li' la differenza e'
         * voluta anche nel DOM vero. Due comportamenti diversi per la stessa
         * cosa sembrano un capriccio, ma le pagine contano su tutt'e due. */
        *fuori = stringa_c(c, html_attr(d, n, "id"));
        return 1;
    }
    if (ugu(nome, "className")) { *fuori = stringa_c(c, html_attr(d, n, "class")); return 1; }

    /* --- solo il documento -------------------------------------------- */
    if (n == d->radice) {
        if (ugu(nome, "documentElement")) {
            int h = primo_tag(d, d->radice, "html");
            *fuori = exdom_avvolgi(D, h >= 0 ? h : d->nodi[d->radice].primo_figlio);
            return 1;
        }
        if (ugu(nome, "body")) { *fuori = exdom_avvolgi(D, primo_tag(d, d->radice, "body")); return 1; }
        if (ugu(nome, "head")) { *fuori = exdom_avvolgi(D, primo_tag(d, d->radice, "head")); return 1; }
        if (ugu(nome, "title")) {
            int t = primo_tag(d, d->radice, "title");
            *fuori = (t >= 0) ? contenuto_testo(D, t) : stringa_c(c, "");
            return 1;
        }
    }
    return 0;
}

/* =============================================================================
 * LE PROPRIETA' CHE SI SCRIVONO
 * ========================================================================== */
static int scrivi_prop(ExJsCtx *c, void *dato, const char *nome, ExJsVal v)
{
    Legame  *L = (Legame *)dato;
    ExDom   *D = L->D;
    HtmlDoc *d = D->doc;
    int      n = L->nodo;
    int      elemento;

    if (n < 0 || n >= (int)d->nodi_n) return 0;
    elemento = (d->nodi[n].tipo == HTML_ELEMENTO);

    if (!elemento) {
        if (ugu(nome, "nodeValue") || ugu(nome, "data") ||
            ugu(nome, "textContent")) {
            html_testo_metti(d, n, exjs_a_stringa(c, v));
            return 1;
        }
        return 0;
    }

    if (ugu(nome, "innerHTML")) {
        /* ! SI SVUOTA PRIMA E SI ANALIZZA POI, e sono due chiamate separate
         * apposta: l'altra meta' del DOM — aggiungere senza cancellare — e' la
         * stessa cosa senza la prima riga. */
        const char *s = exjs_a_stringa(c, v);
        html_svuota(d, n);
        html_analizza_in(d, n, s, lung(s));
        return 1;
    }
    if (ugu(nome, "textContent") || ugu(nome, "innerText")) {
        /* ! IL TESTO NON DIVENTA MARCATORE, ed e' tutta la differenza fra
         * questa proprieta' e innerHTML. Una pagina che scrive in un elemento
         * quel che ha battuto l'utente usa textContent proprio per questo: se
         * qui si analizzasse, un utente che scrive `<script>` si troverebbe
         * uno script. E' la porta da cui entra meta' del male del web. */
        const char *s = exjs_a_stringa(c, v);
        int t;

        html_svuota(d, n);
        if (s[0]) {
            t = html_crea_testo(d, s);
            if (t >= 0) html_aggiungi(d, n, t);
        }
        return 1;
    }
    if (ugu(nome, "id"))        { html_attr_metti(d, n, "id", exjs_a_stringa(c, v)); return 1; }
    if (ugu(nome, "className")) { html_attr_metti(d, n, "class", exjs_a_stringa(c, v)); return 1; }

    if (n == d->radice && ugu(nome, "title")) {
        int t = primo_tag(d, d->radice, "title");

        /* ! SE IL <title> NON C'E' NON LO SI INVENTA, e la scrittura si perde.
         * Costruirlo vorrebbe dire costruire anche <head>, e magari <html>:
         * un documento che nessuno ha scritto, generato da un assegnamento.
         * Meglio non fare niente che fare una cosa che chi legge il sorgente
         * non si aspetta — e chi apre una pagina vera il <title> ce l'ha. */
        if (t >= 0) {
            const char *s = exjs_a_stringa(c, v);
            int         f = d->nodi[t].primo_figlio;

            if (f >= 0 && d->nodi[f].tipo == HTML_TESTO) html_testo_metti(d, f, s);
            else {
                int nuovo = html_crea_testo(d, s);
                html_svuota(d, t);
                if (nuovo >= 0) html_aggiungi(d, t, nuovo);
            }
        }
        return 1;
    }
    return 0;
}

/* =============================================================================
 * I METODI
 *
 * ! OGNUNO RITROVA IL PROPRIO NODO DA `questo`, non da `dato`. `dato` porta il
 * ponte, che e' lo stesso per tutti i metodi di tutti gli elementi; il nodo lo
 * dice l'oggetto su cui il metodo e' stato chiamato — che e' esattamente cosa
 * vuol dire chiamare un metodo. Se il nodo fosse in `dato` servirebbe una
 * copia di ogni metodo per ogni elemento.
 * ========================================================================== */
static int nodo_di(ExDom *D, ExJsVal questo)
{
    return exdom_nodo(D, questo);
}

static const char *arg_str(ExJsCtx *c, const ExJsVal *a, int n, int i)
{
    return (i < n) ? exjs_a_stringa(c, a[i]) : "";
}

static ExJsVal m_getAttribute(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                              int n_arg, void *dato)
{
    ExDom      *D = (ExDom *)dato;
    int         n = nodo_di(D, questo);
    const char *v;

    if (n < 0 || n_arg < 1) return exjs_nullo();
    v = html_attr(D->doc, n, exjs_a_stringa(c, a[0]));
    /* ! QUI L'ASSENZA E' `null`, e non "" come per `.id`. Il DOM fa cosi', e
     * ci si appoggia: `getAttribute('x') === null` e' il modo di distinguere
     * un attributo che non c'e' da uno scritto vuoto. */
    return v ? exjs_stringa(c, v, -1) : exjs_nullo();
}

static ExJsVal m_setAttribute(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                              int n_arg, void *dato)
{
    ExDom *D = (ExDom *)dato;
    int    n = nodo_di(D, questo);
    char   nm[64];
    unsigned int i = 0;
    const char  *s;

    if (n < 0 || n_arg < 1) return exjs_indefinito();
    s = exjs_a_stringa(c, a[0]);
    while (s[i] && i < sizeof(nm) - 1) { nm[i] = (char)minuscola((unsigned char)s[i]); i++; }
    nm[i] = '\0';
    html_attr_metti(D->doc, n, nm, arg_str(c, a, n_arg, 1));
    return exjs_indefinito();
}

static ExJsVal m_removeAttribute(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                                 int n_arg, void *dato)
{
    ExDom *D = (ExDom *)dato;
    int    n = nodo_di(D, questo);

    if (n >= 0 && n_arg >= 1) html_attr_togli(D->doc, n, exjs_a_stringa(c, a[0]));
    return exjs_indefinito();
}

static ExJsVal m_hasAttribute(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                              int n_arg, void *dato)
{
    ExDom *D = (ExDom *)dato;
    int    n = nodo_di(D, questo);

    if (n < 0 || n_arg < 1) return exjs_booleano(0);
    return exjs_booleano(html_attr(D->doc, n, exjs_a_stringa(c, a[0])) != 0);
}

static ExJsVal m_appendChild(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                             int n_arg, void *dato)
{
    ExDom *D = (ExDom *)dato;
    int    p = nodo_di(D, questo), f;

    (void)c;
    if (p < 0 || n_arg < 1) return exjs_nullo();
    f = exdom_nodo(D, a[0]);
    if (f < 0) return exjs_nullo();
    if (!html_aggiungi(D->doc, p, f)) return exjs_nullo();
    /* Il DOM rende il figlio, non il padre: serve a incatenare. */
    return a[0];
}

static ExJsVal m_insertBefore(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                              int n_arg, void *dato)
{
    ExDom *D = (ExDom *)dato;
    int    p = nodo_di(D, questo), f, r;

    (void)c;
    if (p < 0 || n_arg < 1) return exjs_nullo();
    f = exdom_nodo(D, a[0]);
    r = (n_arg >= 2) ? exdom_nodo(D, a[1]) : -1;
    if (f < 0) return exjs_nullo();
    if (!html_inserisci_prima(D->doc, p, f, r)) return exjs_nullo();
    return a[0];
}

static ExJsVal m_removeChild(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                             int n_arg, void *dato)
{
    ExDom *D = (ExDom *)dato;
    int    p = nodo_di(D, questo), f;

    (void)c;
    if (p < 0 || n_arg < 1) return exjs_nullo();
    f = exdom_nodo(D, a[0]);
    /* ! SI CONTROLLA CHE SIA DAVVERO FIGLIO DI QUESTO. Staccarlo dal padre che
     * ha, chiunque sia, farebbe funzionare `a.removeChild(b)` anche quando b
     * sta altrove — e una pagina che sbaglia il padre se ne accorgerebbe molto
     * dopo, guardando un pezzo sparito da un'altra parte. */
    if (f < 0 || D->doc->nodi[f].padre != p) return exjs_nullo();
    html_togli(D->doc, f);
    return a[0];
}

static ExJsVal m_hasChildNodes(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                               int n_arg, void *dato)
{
    ExDom *D = (ExDom *)dato;
    int    n = nodo_di(D, questo);

    (void)c; (void)a; (void)n_arg;
    if (n < 0) return exjs_booleano(0);
    return exjs_booleano(D->doc->nodi[n].primo_figlio >= 0);
}

static ExJsVal m_getElementsByTagName(ExJsCtx *c, ExJsVal questo,
                                      const ExJsVal *a, int n_arg, void *dato)
{
    ExDom       *D = (ExDom *)dato;
    int          r = nodo_di(D, questo), n;
    ExJsVal      v = exjs_vettore(c);
    unsigned int k = 0;
    char         tag[64];
    unsigned int i = 0;
    const char  *s;
    int          tutti;

    if (r < 0 || n_arg < 1) return v;
    s = exjs_a_stringa(c, a[0]);
    while (s[i] && i < sizeof(tag) - 1) { tag[i] = (char)minuscola((unsigned char)s[i]); i++; }
    tag[i] = '\0';
    tutti = (tag[0] == '*' && tag[1] == '\0');

    for (n = prossimo_in_visita(D->doc, r, r); n >= 0;
         n = prossimo_in_visita(D->doc, n, r)) {
        if (D->doc->nodi[n].tipo != HTML_ELEMENTO) continue;
        if (tutti || ugu(html_nome(D->doc, n), tag))
            exjs_indice_metti(c, v, k++, exdom_avvolgi(D, n));
    }
    return v;
}

static ExJsVal m_getElementsByClassName(ExJsCtx *c, ExJsVal questo,
                                        const ExJsVal *a, int n_arg, void *dato)
{
    ExDom       *D = (ExDom *)dato;
    int          r = nodo_di(D, questo), n;
    ExJsVal      v = exjs_vettore(c);
    unsigned int k = 0;
    const char  *cl;

    if (r < 0 || n_arg < 1) return v;
    cl = exjs_a_stringa(c, a[0]);
    for (n = prossimo_in_visita(D->doc, r, r); n >= 0;
         n = prossimo_in_visita(D->doc, n, r)) {
        if (D->doc->nodi[n].tipo != HTML_ELEMENTO) continue;
        if (ha_classe(html_attr(D->doc, n, "class"), cl))
            exjs_indice_metti(c, v, k++, exdom_avvolgi(D, n));
    }
    return v;
}

static ExJsVal m_getElementById(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                                int n_arg, void *dato)
{
    ExDom *D = (ExDom *)dato;
    int    n;

    (void)questo;
    if (n_arg < 1) return exjs_nullo();
    n = trova_per_id(D->doc, D->doc->radice, exjs_a_stringa(c, a[0]));
    return exdom_avvolgi(D, n);
}

static ExJsVal m_createElement(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                               int n_arg, void *dato)
{
    ExDom *D = (ExDom *)dato;
    int    n;

    (void)questo;
    if (n_arg < 1) return exjs_nullo();
    n = html_crea_elemento(D->doc, exjs_a_stringa(c, a[0]));
    return exdom_avvolgi(D, n);
}

static ExJsVal m_createTextNode(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                                int n_arg, void *dato)
{
    ExDom *D = (ExDom *)dato;
    int    n;

    (void)questo;
    if (n_arg < 1) return exjs_nullo();
    n = html_crea_testo(D->doc, exjs_a_stringa(c, a[0]));
    return exdom_avvolgi(D, n);
}

/* =============================================================================
 * APRIRE IL PONTE
 * ========================================================================== */
#define ALLINEA8(x)  (((x) + 7u) & ~7u)

unsigned int exdom_quanto_serve(unsigned int nodi_max, unsigned int testo_max)
{
    return ALLINEA8(sizeof(ExDom)) + ALLINEA8(nodi_max * (unsigned int)sizeof(Legame))
           + ALLINEA8(testo_max);
}

static void metti_metodo(ExDom *D, ExJsVal dove, const char *nome, ExJsNativa f)
{
    exjs_metti(D->js, dove, nome, exjs_nativa(D->js, f, D, nome));
}

ExDom *exdom_apri(void *memoria, unsigned int byte,
                  ExJsCtx *js, HtmlDoc *doc,
                  unsigned int nodi_max, unsigned int testo_max)
{
    unsigned char *p = (unsigned char *)memoria;
    ExDom         *D;
    unsigned int   i;

    if (!memoria || !js || !doc || nodi_max == 0 || testo_max < 64) return 0;
    if (byte < exdom_quanto_serve(nodi_max, testo_max)) return 0;

    D = (ExDom *)p;                          p += ALLINEA8(sizeof(ExDom));
    D->leg = (Legame *)p;                    p += ALLINEA8(nodi_max * (unsigned int)sizeof(Legame));
    D->testo = (char *)p;

    D->js        = js;
    D->doc       = doc;
    D->nodi_max  = nodi_max;
    D->testo_max = testo_max;
    D->troncato  = 0;

    for (i = 0; i < nodi_max; i++) {
        D->leg[i].D    = D;
        D->leg[i].nodo = (int)i;
        D->leg[i].val  = exjs_numero(js, 0.0);   /* «non ancora avvolto» */
    }

    /* ! IL PROTOTIPO SI COSTRUISCE PRIMA DI QUALUNQUE INVOLUCRO, perche'
     * exdom_avvolgi lo aggancia e un involucro nato prima resterebbe senza
     * metodi per sempre — e sarebbe un elemento che non sa fare appendChild
     * senza che nessuno possa capire perche'. */
    D->proto = exjs_oggetto(js);
    metti_metodo(D, D->proto, "getAttribute",           m_getAttribute);
    metti_metodo(D, D->proto, "setAttribute",           m_setAttribute);
    metti_metodo(D, D->proto, "removeAttribute",        m_removeAttribute);
    metti_metodo(D, D->proto, "hasAttribute",           m_hasAttribute);
    metti_metodo(D, D->proto, "appendChild",            m_appendChild);
    metti_metodo(D, D->proto, "insertBefore",           m_insertBefore);
    metti_metodo(D, D->proto, "removeChild",            m_removeChild);
    metti_metodo(D, D->proto, "hasChildNodes",          m_hasChildNodes);
    metti_metodo(D, D->proto, "getElementsByTagName",   m_getElementsByTagName);
    metti_metodo(D, D->proto, "getElementsByClassName", m_getElementsByClassName);

    D->documento = exdom_avvolgi(D, doc->radice);
    if (exjs_tipo(js, D->documento) != EXJS_OGGETTO) return 0;

    /* ! QUESTI TRE STANNO SUL DOCUMENTO E NON SUL PROTOTIPO, apposta: nel DOM
     * `createElement` e `getElementById` sono del documento, e una pagina che
     * li trovasse anche su un `<div>` girerebbe qui e non altrove. Un motore
     * piu' permissivo del vero e' un motore che lascia scrivere pagine rotte. */
    metti_metodo(D, D->documento, "getElementById",  m_getElementById);
    metti_metodo(D, D->documento, "createElement",   m_createElement);
    metti_metodo(D, D->documento, "createTextNode",  m_createTextNode);

    exjs_metti(js, exjs_globale(js), "document", D->documento);

    /* ! `window` E' L'OGGETTO GLOBALE, non un oggetto a parte. E' cosi' che il
     * browser e' fatto: `window.x = 1` poi `x` deve valere 1, e viceversa. Due
     * oggetti distinti richiederebbero di tenerli d'accordo a ogni scrittura,
     * cioe' un gancio fisso su tutto il globale per una cosa che il linguaggio
     * gia' faceva da se'. */
    exjs_metti(js, exjs_globale(js), "window", exjs_globale(js));
    return D;
}
