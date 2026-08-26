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

/* =============================================================================
 * GLI ASCOLTATORI
 *
 * ! STANNO IN UNA TABELLA DEL PONTE, non appesi all'involucro del nodo. La
 * strada breve sarebbe stata una proprieta' nascosta sull'oggetto JavaScript
 * — `elemento.__ascoltatori` — e sarebbe stata sbagliata due volte: uno script
 * la vedrebbe (e potrebbe cancellarla), e ogni nodo con un gestore pagherebbe
 * un vettore. Qui e' una tabella sola, con un tetto che sceglie chi apre la
 * pagina, come tutto il resto in questo sistema.
 *
 * ! IL NOME DELL'EVENTO STA DENTRO IL RECORD e non in un'arena. Trentadue
 * caratteri tengono ogni nome che esiste davvero — il piu' lungo che si
 * incontri e' "webkitTransitionEnd" — e un'arena avrebbe voluto dire un terzo
 * buffer da dimensionare per risparmiare qualche decina di byte. Un nome piu'
 * lungo si RIFIUTA invece di troncarlo: troncare farebbe rispondere allo
 * stesso gestore due eventi diversi, che e' peggio del non registrarlo.
 * ========================================================================== */
#define TIPO_MAX   32

typedef struct {
    int           usato;
    int           nodo;
    char          tipo[TIPO_MAX];
    ExJsVal       f;
    unsigned char cattura;
    /* ! GLI `onclick` STANNO NELLA STESSA TABELLA, con una bandiera. Sono
     * un'altra cosa dal DOM — ce n'e' UNO per tipo, e riassegnarlo sostituisce
     * invece di aggiungere — ma girano nello stesso momento e nello stesso
     * ordine, e tenerli in due posti avrebbe voluto dire due meccanismi di
     * propagazione da tenere d'accordo. */
    unsigned char attributo;
} Ascolto;

struct ExDom {
    ExJsCtx     *js;
    HtmlDoc     *doc;
    unsigned int nodi_max;
    unsigned int testo_max;
    char        *testo;             /* dove si rimette in marcatore */
    Legame      *leg;
    Ascolto     *asc;
    unsigned int asc_max;
    ExJsVal      proto;             /* i metodi, in un posto solo */
    ExJsVal      documento;
    int          troncato;
    int          perso;
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

/* La tabella degli ascolti si tocca dai ganci — `elemento.onclick` e' una
 * proprieta' — e il codice degli eventi sta piu' sotto. */
static int  asc_trova(ExDom *D, int nodo, const char *tipo, ExJsVal f,
                      int cattura, int attributo);
static int  asc_aggiungi(ExDom *D, int nodo, const char *tipo, ExJsVal f,
                         int cattura, int attributo);
static void asc_togli(ExDom *D, int i);
static int  tipo_copia(char *dest, const char *s);

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

    /* ! `elemento.onclick` RENDE IL GESTORE, o null. Il DOM fa cosi', e le
     * pagine ci contano per due cose: sapere se ce n'e' gia' uno, e
     * richiamarlo a mano. Rendere `undefined` avrebbe fatto passare il primo
     * controllo e fallire il secondo. */
    if (nome[0] == 'o' && nome[1] == 'n' && nome[2]) {
        int i = asc_trova(D, n, nome + 2, exjs_indefinito(), 0, 1);

        *fuori = (i >= 0) ? D->asc[i].f : exjs_nullo();
        return 1;
    }

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

    /* ! SI PRENDE `on...` SOLO SE IL VALORE E' UNA FUNZIONE O `null`, e non
     * ogni nome che comincia per "on". Uno script che scrive `el.onda = 3` sta
     * usando l'elemento come un cassetto, come ha diritto di fare; prendersi
     * quel nome vorrebbe dire fargli sparire un dato senza spiegazioni. */
    if (nome[0] == 'o' && nome[1] == 'n' && nome[2]) {
        int tipo_ok = exjs_tipo(c, v);

        if (tipo_ok == EXJS_FUNZIONE) {
            char tipo[TIPO_MAX];

            if (!tipo_copia(tipo, nome + 2)) { D->perso = 1; return 1; }
            asc_aggiungi(D, n, tipo, v, 0, 1);
            return 1;
        }
        if (tipo_ok == EXJS_NULLO) {
            asc_togli(D, asc_trova(D, n, nome + 2, exjs_indefinito(), 0, 1));
            return 1;
        }
    }

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
 * GLI EVENTI
 * ========================================================================== */

/* Rende 0 se il nome non ci sta: si rifiuta, non si tronca. Il perche' sta
 * accanto alla struttura Ascolto. */
static void azzera_errore(ExJsErrore *e)
{
    unsigned int i;

    e->riga = e->colonna = 0;
    e->posizione = 0;
    for (i = 0; i < EXJS_ERR_LEN; i++) e->messaggio[i] = '\0';
}

static int tipo_copia(char *dest, const char *s)
{
    unsigned int i = 0;

    while (s[i]) {
        if (i >= TIPO_MAX - 1) return 0;
        dest[i] = s[i];
        i++;
    }
    dest[i] = '\0';
    return i > 0;
}

static int asc_trova(ExDom *D, int nodo, const char *tipo, ExJsVal f,
                     int cattura, int attributo)
{
    unsigned int i;

    for (i = 0; i < D->asc_max; i++) {
        Ascolto *A = &D->asc[i];

        if (!A->usato || A->nodo != nodo) continue;
        if (A->attributo != (unsigned char)attributo) continue;
        if (A->cattura != (unsigned char)cattura) continue;
        if (!ugu(A->tipo, tipo)) continue;
        /* ! PER UN `onclick` NON SI CONFRONTA LA FUNZIONE, perche' ce n'e' uno
         * solo: riassegnarlo deve SOSTITUIRE. Per addEventListener invece la
         * funzione fa parte dell'identita', ed e' cosi' che due gestori diversi
         * sullo stesso evento convivono. */
        if (attributo || A->f == f) return (int)i;
    }
    return -1;
}

static int asc_aggiungi(ExDom *D, int nodo, const char *tipo, ExJsVal f,
                        int cattura, int attributo)
{
    unsigned int i;
    int          gia;

    if (!tipo || !tipo[0]) return 0;

    gia = asc_trova(D, nodo, tipo, f, cattura, attributo);
    if (gia >= 0) {
        /* ! LO STESSO GESTORE DUE VOLTE SI REGISTRA UNA VOLTA SOLA, come dice
         * il DOM. Sembra pignoleria, e invece e' cio' che salva le pagine che
         * chiamano addEventListener dentro una funzione richiamata piu' volte:
         * senza, il gestore girerebbe dieci volte per un clic. */
        D->asc[gia].f = f;
        return 1;
    }

    for (i = 0; i < D->asc_max; i++) {
        if (D->asc[i].usato) continue;
        if (!tipo_copia(D->asc[i].tipo, tipo)) { D->perso = 1; return 0; }
        D->asc[i].usato     = 1;
        D->asc[i].nodo      = nodo;
        D->asc[i].f         = f;
        D->asc[i].cattura   = (unsigned char)cattura;
        D->asc[i].attributo = (unsigned char)attributo;
        return 1;
    }
    D->perso = 1;
    return 0;
}

static void asc_togli(ExDom *D, int i)
{
    if (i >= 0 && i < (int)D->asc_max) D->asc[i].usato = 0;
}

/* -----------------------------------------------------------------------------
 * L'oggetto evento
 *
 * ! LE DUE BANDIERE SONO PROPRIETA' VERE, non stato tenuto qui in C.
 * `defaultPrevented` e `cancelBubble` esistono nel DOM e le pagine le leggono;
 * tenerle in C avrebbe voluto dire scriverle anche nell'oggetto per farle
 * vedere, cioe' la stessa cosa in due posti che devono restare d'accordo. Qui
 * le scrivono i due metodi e le rilegge la propagazione, con exjs_prendi.
 * --------------------------------------------------------------------------- */
static ExJsVal m_preventDefault(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                                int n_arg, void *dato)
{
    (void)a; (void)n_arg; (void)dato;
    exjs_metti(c, questo, "defaultPrevented", exjs_booleano(1));
    return exjs_indefinito();
}

static ExJsVal m_stopPropagation(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                                 int n_arg, void *dato)
{
    (void)a; (void)n_arg; (void)dato;
    exjs_metti(c, questo, "cancelBubble", exjs_booleano(1));
    return exjs_indefinito();
}

static ExJsVal crea_evento(ExDom *D, const char *tipo, int bersaglio)
{
    ExJsCtx *c = D->js;
    ExJsVal  e = exjs_oggetto(c);

    if (exjs_tipo(c, e) != EXJS_OGGETTO) return exjs_indefinito();
    exjs_metti(c, e, "type",             stringa_c(c, tipo));
    exjs_metti(c, e, "target",           exdom_avvolgi(D, bersaglio));
    exjs_metti(c, e, "currentTarget",    exjs_nullo());
    exjs_metti(c, e, "defaultPrevented", exjs_booleano(0));
    exjs_metti(c, e, "cancelBubble",     exjs_booleano(0));
    exjs_metti(c, e, "preventDefault",
               exjs_nativa(c, m_preventDefault, D, "preventDefault"));
    exjs_metti(c, e, "stopPropagation",
               exjs_nativa(c, m_stopPropagation, D, "stopPropagation"));
    return e;
}

static int fermato(ExDom *D, ExJsVal ev)
{
    return exjs_a_booleano(D->js, exjs_prendi(D->js, ev, "cancelBubble"));
}

/* -----------------------------------------------------------------------------
 * I gestori scritti nell'attributo: onclick="..."
 *
 * ! IL TESTO SI ESEGUE COME UNO SCRIPT, e non diventa una funzione, perche' un
 * costruttore Function ExJs non ce l'ha ancora. Le due differenze si vedono e
 * vanno sapute: `this` dentro non e' l'elemento, e le variabili dichiarate li'
 * finiscono fra le globali. `event` invece c'e', perche' e' `window.event`,
 * che nel DOM esiste davvero — e con quello la stragrande maggioranza degli
 * attributi veri, che sono chiamate a una funzione gia' scritta altrove,
 * funziona come deve.
 *
 * ! E SI RIANALIZZA A OGNI EVENTO. Su un clic non si sente; su un mousemove
 * si sentirebbe, e quel giorno il posto dove rimediare e' qui — compilando una
 * volta e tenendo la funzione nella tabella, come gli altri.
 * --------------------------------------------------------------------------- */
static void gestore_attributo(ExDom *D, int nodo, const char *tipo,
                              ExJsVal ev, ExJsErrore *err, int *avuto)
{
    char        nome[TIPO_MAX + 4];
    unsigned int i = 0;
    const char *testo;
    ExJsErrore  mio;
    ExJsVal     r;

    if (D->doc->nodi[nodo].tipo != HTML_ELEMENTO) return;

    nome[0] = 'o'; nome[1] = 'n';
    while (tipo[i] && i < TIPO_MAX) { nome[2 + i] = tipo[i]; i++; }
    nome[2 + i] = '\0';

    testo = html_attr(D->doc, nodo, nome);
    if (!testo || !testo[0]) return;

    exjs_metti(D->js, exjs_globale(D->js), "event", ev);
    if (!exjs_esegui(D->js, testo, lung(testo), &r, &mio)) {
        if (err && !*avuto) { *err = mio; *avuto = 1; }
    }
}

/* Fa girare i gestori registrati su un nodo per una fase. Rende 0 se qualcuno
 * ha fermato la propagazione. */
static int ascolti_di(ExDom *D, int nodo, const char *tipo, int cattura,
                      ExJsVal ev, ExJsErrore *err, int *avuto)
{
    ExJsCtx     *c = D->js;
    unsigned int i;

    exjs_metti(c, ev, "currentTarget", exdom_avvolgi(D, nodo));

    /* ! L'ATTRIBUTO GIRA PER PRIMO, e solo in risalita. Nel browser vero
     * `onclick="..."` diventa la proprieta' onclick al momento in cui la
     * pagina si legge, quindi e' il primo registrato; e un attributo non
     * cattura mai, perche' non c'e' modo di scriverlo. */
    if (!cattura) {
        gestore_attributo(D, nodo, tipo, ev, err, avuto);
        if (fermato(D, ev)) return 0;
    }

    for (i = 0; i < D->asc_max; i++) {
        Ascolto *A = &D->asc[i];
        ExJsVal  arg[1];
        ExJsErrore mio;

        if (!A->usato || A->nodo != nodo) continue;
        if (A->cattura != (unsigned char)cattura) continue;
        if (!ugu(A->tipo, tipo)) continue;

        arg[0] = ev;
        azzera_errore(&mio);
        /* ! `this` DENTRO UN GESTORE E' L'ELEMENTO SU CUI E' REGISTRATO, non
         * quello su cui e' successo il fatto. E' la differenza fra target e
         * currentTarget, ed e' proprio cio' che serve a chi mette un gestore
         * solo sul contenitore per servire cento figli. */
        exjs_invoca(c, A->f, exdom_avvolgi(D, nodo), arg, 1, &mio);
        if (mio.messaggio[0] && err && !*avuto) { *err = mio; *avuto = 1; }

        if (fermato(D, ev)) return 0;
    }
    return 1;
}

#define STRADA_MAX  64

int exdom_evento(ExDom *D, int nodo, const char *tipo, ExJsErrore *err)
{
    int     strada[STRADA_MAX];
    int     n_strada = 0, i, avuto = 0;
    ExJsVal ev;

    if (!D || !tipo || !tipo[0]) return 1;
    if (nodo < 0 || nodo >= (int)D->doc->nodi_n) return 1;

    ev = crea_evento(D, tipo, nodo);
    if (exjs_tipo(D->js, ev) != EXJS_OGGETTO) return 1;
    if (err) { err->messaggio[0] = '\0'; err->riga = 0; }

    /* La strada dal bersaglio in su. ! IL BERSAGLIO NON CI STA DENTRO: la sua
     * fase e' una sola e si fa in mezzo, con tutt'e due i tipi di gestore. */
    for (i = D->doc->nodi[nodo].padre; i >= 0 && n_strada < STRADA_MAX;
         i = D->doc->nodi[i].padre)
        strada[n_strada++] = i;

    /* --- discesa: dalla radice giu' fino al padre del bersaglio --------- */
    for (i = n_strada - 1; i >= 0; i--)
        if (!ascolti_di(D, strada[i], tipo, 1, ev, err, &avuto)) goto finito;

    /* --- il bersaglio: le due fasi si toccano qui ----------------------- */
    if (!ascolti_di(D, nodo, tipo, 1, ev, err, &avuto)) goto finito;
    if (!ascolti_di(D, nodo, tipo, 0, ev, err, &avuto)) goto finito;

    /* --- risalita ------------------------------------------------------- */
    for (i = 0; i < n_strada; i++)
        if (!ascolti_di(D, strada[i], tipo, 0, ev, err, &avuto)) goto finito;

finito:
    /* ! `window.event` SI TOGLIE DI MEZZO A COSE FATTE. Lasciarlo li' vorrebbe
     * dire che uno script eseguito dopo, che non c'entra niente, trova un
     * evento vecchio e ci crede. */
    exjs_metti(D->js, exjs_globale(D->js), "event", exjs_indefinito());
    return exjs_a_booleano(D->js, exjs_prendi(D->js, ev, "defaultPrevented"))
           ? 0 : 1;
}

int exdom_perso(const ExDom *D) { return D ? D->perso : 0; }

/* -----------------------------------------------------------------------------
 * I tre metodi
 * --------------------------------------------------------------------------- */
static ExJsVal m_addEventListener(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                                  int n_arg, void *dato)
{
    ExDom *D = (ExDom *)dato;
    int    n = nodo_di(D, questo);
    char   tipo[TIPO_MAX];

    if (n < 0 || n_arg < 2) return exjs_indefinito();
    if (exjs_tipo(c, a[1]) != EXJS_FUNZIONE) return exjs_indefinito();
    if (!tipo_copia(tipo, exjs_a_stringa(c, a[0]))) { D->perso = 1; return exjs_indefinito(); }

    asc_aggiungi(D, n, tipo, a[1],
                 (n_arg >= 3) ? exjs_a_booleano(c, a[2]) : 0, 0);
    return exjs_indefinito();
}

static ExJsVal m_removeEventListener(ExJsCtx *c, ExJsVal questo,
                                     const ExJsVal *a, int n_arg, void *dato)
{
    ExDom *D = (ExDom *)dato;
    int    n = nodo_di(D, questo);
    char   tipo[TIPO_MAX];

    if (n < 0 || n_arg < 2) return exjs_indefinito();
    if (!tipo_copia(tipo, exjs_a_stringa(c, a[0]))) return exjs_indefinito();

    asc_togli(D, asc_trova(D, n, tipo, a[1],
                           (n_arg >= 3) ? exjs_a_booleano(c, a[2]) : 0, 0));
    return exjs_indefinito();
}

static ExJsVal m_dispatchEvent(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                               int n_arg, void *dato)
{
    ExDom *D = (ExDom *)dato;
    int    n = nodo_di(D, questo);
    char   tipo[TIPO_MAX];

    if (n < 0 || n_arg < 1) return exjs_booleano(1);

    /* ! SI ACCETTA SIA UNA STRINGA SIA UN OGGETTO CON `type`. Il DOM vuole un
     * oggetto Event, che si costruisce con `new Event('click')` — e un
     * costruttore Event qui non c'e' ancora. Accettare la stringa da alle
     * pagine di prova un modo di far partire un evento senza aspettare quel
     * pezzo, e non toglie niente a chi passa l'oggetto giusto. */
    if (exjs_tipo(c, a[0]) == EXJS_OGGETTO)
        tipo_copia(tipo, exjs_a_stringa(c, exjs_prendi(c, a[0], "type")));
    else if (!tipo_copia(tipo, exjs_a_stringa(c, a[0])))
        return exjs_booleano(1);

    return exjs_booleano(exdom_evento(D, n, tipo, 0));
}

/* =============================================================================
 * APRIRE IL PONTE
 * ========================================================================== */
#define ALLINEA8(x)  (((x) + 7u) & ~7u)

unsigned int exdom_quanto_serve(unsigned int nodi_max, unsigned int testo_max,
                                unsigned int ascolti_max)
{
    return ALLINEA8(sizeof(ExDom))
           + ALLINEA8(nodi_max * (unsigned int)sizeof(Legame))
           + ALLINEA8(ascolti_max * (unsigned int)sizeof(Ascolto))
           + ALLINEA8(testo_max);
}

static void metti_metodo(ExDom *D, ExJsVal dove, const char *nome, ExJsNativa f)
{
    exjs_metti(D->js, dove, nome, exjs_nativa(D->js, f, D, nome));
}

ExDom *exdom_apri(void *memoria, unsigned int byte,
                  ExJsCtx *js, HtmlDoc *doc,
                  unsigned int nodi_max, unsigned int testo_max,
                  unsigned int ascolti_max)
{
    unsigned char *p = (unsigned char *)memoria;
    ExDom         *D;
    unsigned int   i;

    if (!memoria || !js || !doc || nodi_max == 0 || testo_max < 64) return 0;
    if (byte < exdom_quanto_serve(nodi_max, testo_max, ascolti_max)) return 0;

    D = (ExDom *)p;                          p += ALLINEA8(sizeof(ExDom));
    D->leg = (Legame *)p;                    p += ALLINEA8(nodi_max * (unsigned int)sizeof(Legame));
    D->asc = (Ascolto *)p;                   p += ALLINEA8(ascolti_max * (unsigned int)sizeof(Ascolto));
    D->testo = (char *)p;

    D->js        = js;
    D->doc       = doc;
    D->nodi_max  = nodi_max;
    D->testo_max = testo_max;
    D->asc_max   = ascolti_max;
    D->troncato  = 0;
    D->perso     = 0;

    for (i = 0; i < ascolti_max; i++) D->asc[i].usato = 0;

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
    metti_metodo(D, D->proto, "addEventListener",       m_addEventListener);
    metti_metodo(D, D->proto, "removeEventListener",    m_removeEventListener);
    metti_metodo(D, D->proto, "dispatchEvent",          m_dispatchEvent);

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
