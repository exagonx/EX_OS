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
    /* ! LO STILE HA UN PROTOTIPO SUO, e non i metodi dell'elemento: un
     * CSSStyleDeclaration non fa appendChild. Tenerli insieme avrebbe voluto
     * dire che `el.style.appendChild` esiste e fa una cosa senza senso. */
    ExJsVal      proto_stile;
    ExJsVal      proto_classi;
    ExJsVal      promessa;          /* vedi `document.fonts`, in fondo */
    ExJsVal      luogo;             /* `location`                       */
    ExJsVal      proto_promessa;
    ExJsVal      proto_risposta;
    ExJsVal      proto_xhr;
    ExDomRete    rete;
    void        *rete_dato;
    char         url[EXDOM_URL_MAX];    /* dove siamo, detto da fuori   */
    char         vai[EXDOM_URL_MAX];    /* dove uno script vuole andare */
    char         biscotti[EXDOM_BISCOTTI_MAX];
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
static ExJsVal stile_oggetto(ExDom *D, Legame *L);
static void metti_metodo(ExDom *D, ExJsVal dove, const char *nome, ExJsNativa f);
static int  biscotto_pezzo(const char *s, char *fuori, unsigned int max);
static void biscotto_aggiungi(ExDom *D, const char *nuovo);
static ExJsVal dati_oggetto(ExDom *D, Legame *L);
static ExJsVal classi_oggetto(ExDom *D, Legame *L);
static int riflesso_leggi(ExJsCtx *c, HtmlDoc *d, int n, const char *nome,
                          ExJsVal *fuori);
static int riflesso_scrivi(ExJsCtx *c, HtmlDoc *d, int n, const char *nome,
                           ExJsVal v);

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
    /* ! SI CONTROLLA CHE IL LEGAME SIA NOSTRO, e ADESSO IL DOMANI E' ARRIVATO:
     * qui c'era scritto «oggi non ce ne sono, domani si'» e confrontava il
     * campo `D`. Da quando esiste `el.style` gli oggetti esotici sono di due
     * specie e portano lo STESSO Legame — e' cosi' che un satellite ritrova il
     * suo elemento — quindi il campo `D` combacia per tutt'e due e non
     * distingue piu' niente. Servono due controlli, e sono diversi:
     *
     *   - che il puntatore stia nella NOSTRA tabella (l'intervallo e' una
     *     verifica esatta; il campo `D` era solo probabile);
     *   - che il valore sia proprio L'INVOLUCRO del nodo. Senza,
     *     `padre.appendChild(el.style)` avrebbe spostato `el`. */
    if (!L || L < D->leg || L >= D->leg + D->nodi_max) return -1;
    if (v != L->val) return -1;
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
    if (ugu(nome, "style")) {
        ExJsVal s = stile_oggetto(D, L);

        if (exjs_tipo(c, s) != EXJS_OGGETTO) return 0;
        *fuori = s;
        return 1;
    }
    if (ugu(nome, "dataset")) {
        ExJsVal s = dati_oggetto(D, L);

        if (exjs_tipo(c, s) != EXJS_OGGETTO) return 0;
        *fuori = s;
        return 1;
    }
    if (ugu(nome, "classList")) {
        ExJsVal s = classi_oggetto(D, L);

        if (exjs_tipo(c, s) != EXJS_OGGETTO) return 0;
        *fuori = s;
        return 1;
    }

    /* ! LE PROPRIETA' RIFLESSE NON VALGONO SULLA RADICE. `title` e' nella
     * tabella per QUALUNQUE elemento, e la radice del documento e' un elemento
     * anche lei (con il nome vuoto): senza questa riga `document.title`
     * diventerebbe l'attributo `title` della radice — che non esiste — invece
     * del testo del <title>, che sta trenta righe piu' giu'. */
    if (n != d->radice && riflesso_leggi(c, d, n, nome, fuori)) return 1;

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
        /* ! `document.cookie` RENDE UNA STRINGA, ANCHE VUOTA, e mai
         * `undefined`: mezza pagina moderna fa `document.cookie.length` senza
         * guardare, e su `undefined` quella riga e' la fine dello script. */
        if (ugu(nome, "cookie")) { *fuori = stringa_c(c, D->biscotti); return 1; }
        if (ugu(nome, "URL") || ugu(nome, "documentURI")) {
            *fuori = stringa_c(c, D->url);
            return 1;
        }
        /* ! `referrer` E' VUOTO E NON MANCA. Da dove si arriva il ponte non lo
         * sa — lo sa il browser, che tiene la cronologia — e una stringa vuota
         * e' quel che rende il DOM quando non c'e' pagina di provenienza. */
        if (ugu(nome, "referrer")) { *fuori = stringa_c(c, ""); return 1; }
        /* ! «complete» PERCHE' QUANDO UNO SCRIPT PUO' CHIEDERLO IL DOCUMENTO
         * C'E' GIA' TUTTO: qui gli script girano dopo l'analisi, non durante.
         * Dire «loading» sarebbe far aspettare una pagina che aspetta un
         * evento che non arrivera' mai. */
        if (ugu(nome, "readyState")) {
            *fuori = stringa_c(c, "complete");
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

    /* ! `el.style = "color:red"` E' LEGALE e vale come cssText; `el.style = un
     * oggetto` NO — e quel caso qui e' NOSTRO, non di chi scrive la pagina:
     * e' stile_oggetto() che si ricorda l'involucro mettendolo come proprieta'
     * propria, e quella scrittura passa da questo gancio. Tirandosi indietro
     * per gli oggetti, la memoria finisce nella cache; prendendola, finirebbe
     * dentro l'attributo `style` sotto forma di "[object Object]". */
    if (ugu(nome, "style")) {
        if (exjs_tipo(c, v) == EXJS_OGGETTO) return 0;
        html_attr_metti(d, n, "style", exjs_a_stringa(c, v));
        return 1;
    }

    /* ! `dataset` E `classList` SI LEGGONO E BASTA, e nel DOM e' cosi': chi
     * vuole sostituire tutte le classi in un colpo scrive `className`. Un
     * OGGETTO pero' si lascia passare, ed e' la memoria del satellite che si
     * scrive da se' (vedi satellite()): prendersela vorrebbe dire non
     * ricordarsi piu' niente e rifare l'oggetto a ogni lettura. */
    if (ugu(nome, "dataset") || ugu(nome, "classList"))
        return exjs_tipo(c, v) != EXJS_OGGETTO;

    if (n != d->radice && riflesso_scrivi(c, d, n, nome, v)) return 1;

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

    if (n == d->radice && ugu(nome, "cookie")) {
        char b[EXDOM_BISCOTTI_MAX];

        if (biscotto_pezzo(exjs_a_stringa(c, v), b, sizeof(b)))
            biscotto_aggiungi(D, b);
        return 1;
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
 * LO STILE INLINE — `el.style` COME OGGETTO
 *
 * ! NON PASSA DA excss, E LA NOTA IN CIMA A exdom.h DICEVA CHE CI SAREBBE
 * PASSATO. Ci si era detti che serviva «un pezzo di excss per sciogliere le
 * dichiarazioni»; scrivendolo si e' visto che sarebbe stato sbagliato due
 * volte, e la nota vecchia e' corretta di conseguenza:
 *
 *   - excss conosce DODICI proprieta' e le riduce a una CssStile. Uno script
 *     che scrive `el.style.zIndex = 5` e lo rilegge deve ritrovarcelo: farlo
 *     passare di li' vorrebbe dire perderlo in silenzio, cioe' esattamente la
 *     bugia che gli oggetti esotici sono nati per rendere impossibile.
 *   - il ponte non decide che cosa VUOL DIRE una dichiarazione: la conserva.
 *     Chi le da' un senso e' css_calcola(), che l'attributo `style` lo legge
 *     gia' da prima — quindi scrivere li' basta perche' l'impaginazione se ne
 *     accorga, e exdom non impara una riga di CSS.
 *
 * ! LO STATO STA NELL'ATTRIBUTO, e non in una struttura accanto. `el.style`
 * non possiede niente: legge e riscrive `style="..."`. Cosi' setAttribute,
 * `el.style.color = ...` e il foglio di stile guardano tutti la stessa cosa e
 * non c'e' un secondo posto da tenere d'accordo — che e' il difetto che si
 * paga sempre, e che in questo browser si e' gia' pagato una volta con l'arena
 * dell'impaginazione.
 *
 * ! E L'OGGETTO SI FA UNA VOLTA SOLA PER NODO, ma senza un campo per nodo: si
 * ricorda come proprieta' PROPRIA dell'involucro. In lettura le proprie
 * vengono prima del gancio (vedi exjs.h), quindi dalla seconda volta in poi il
 * gancio non viene nemmeno chiamato — e un nodo che `style` non lo tocca mai
 * non paga niente. Un campo in Legame sarebbe costato otto byte per ogni nodo
 * della pagina, cioe' duecento kilobyte su un documento da ventiquattromila.
 * ========================================================================== */
#define STILE_MAX   2048        /* quanto puo' diventare lungo un `style=` */
#define PROP_MAX      96
#define VAL_MAX      512

static int bianco(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

/* Da `backgroundColor` a `background-color`. Un nome che i trattini ce li ha
 * gia' passa intatto: e' cosi' che arriva da setProperty. Rende 0 se non ci
 * sta, e allora il gancio dira' «non e' mia» invece di troncare un nome. */
static int nome_css(char *dest, unsigned int max, const char *js)
{
    unsigned int k = 0, i;

    /* ! `cssFloat` E' L'UNICO NOME CHE IL DOM HA DOVUTO CAMBIARE: `float` era
     * una parola riservata nel JavaScript del 1996, e la proprieta' CSS piu'
     * usata di quegli anni non poteva chiamarsi come si chiamava. */
    if (ugu(js, "cssFloat") || ugu(js, "styleFloat")) js = "float";

    for (i = 0; js[i]; i++) {
        unsigned char ch = (unsigned char)js[i];

        if (ch >= 'A' && ch <= 'Z') {
            /* `WebkitTransform` -> `-webkit-transform`: la maiuscola iniziale
             * fa il trattino come tutte le altre, ed e' quel che vuole il DOM. */
            if (k + 2 >= max) return 0;
            dest[k++] = '-';
            dest[k++] = (char)(ch + 32);
        } else {
            if (k + 1 >= max) return 0;
            dest[k++] = (char)ch;
        }
    }
    dest[k] = '\0';
    return k > 0;
}

/* Dove finisce la dichiarazione che comincia a `i`.
 * ! LE PARENTESI SI CONTANO, perche' `background:url(a;b)` e' legale e un
 * punto e virgola dentro le tonde non separa niente. */
static unsigned int fine_dich(const char *st, unsigned int i)
{
    int liv = 0;

    while (st[i]) {
        if (st[i] == '(') liv++;
        else if (st[i] == ')') { if (liv > 0) liv--; }
        else if (st[i] == ';' && liv == 0) break;
        i++;
    }
    return i;
}

/* Spezza [a,b) in nome e valore, senza gli spazi ai bordi. Rende 0 se li'
 * dentro non c'e' una dichiarazione (niente «:», o nome vuoto). */
static int dich_pezzi(const char *st, unsigned int a, unsigned int b,
                      unsigned int *n0, unsigned int *n1,
                      unsigned int *v0, unsigned int *v1)
{
    unsigned int i = a, dp;

    while (i < b && bianco(st[i])) i++;
    dp = i;
    while (dp < b && st[dp] != ':') dp++;
    if (dp >= b) return 0;
    *n0 = i; *n1 = dp;
    while (*n1 > *n0 && bianco(st[*n1 - 1])) (*n1)--;
    i = dp + 1;
    while (i < b && bianco(st[i])) i++;
    *v0 = i; *v1 = b;
    while (*v1 > *v0 && bianco(st[*v1 - 1])) (*v1)--;
    return *n1 > *n0;
}

static int nome_uguale(const char *st, unsigned int a, unsigned int b,
                       const char *prop)
{
    unsigned int i;

    for (i = 0; a + i < b; i++) {
        if (!prop[i]) return 0;
        if (minuscola((unsigned char)st[a + i]) !=
            minuscola((unsigned char)prop[i])) return 0;
    }
    return prop[b - a] == '\0';
}

/* Il valore di `prop` dentro `st`, o "" se non c'e'.
 * ! SI TIENE L'ULTIMA E NON LA PRIMA. `style="color:red;color:blue"` e' rosso
 * per chi legge da sinistra e blu per il CSS, che a parita' di peso fa vincere
 * chi arriva dopo. Rendere la prima vorrebbe dire che `el.style.color` e
 * quello che si vede sullo schermo non sono la stessa cosa. */
static int stile_valore(const char *st, const char *prop,
                        char *fuori, unsigned int max)
{
    unsigned int i = 0, b, n0, n1, v0, v1, k;
    int          trovata = 0;

    fuori[0] = '\0';
    if (!st) return 0;
    while (st[i]) {
        b = fine_dich(st, i);
        if (dich_pezzi(st, i, b, &n0, &n1, &v0, &v1) &&
            nome_uguale(st, n0, n1, prop)) {
            for (k = 0; v0 + k < v1 && k + 1 < max; k++) fuori[k] = st[v0 + k];
            fuori[k] = '\0';
            trovata = 1;
        }
        i = st[b] ? b + 1 : b;
    }
    return trovata;
}

static unsigned int stile_conta(const char *st)
{
    unsigned int i = 0, b, n0, n1, v0, v1, n = 0;

    if (!st) return 0;
    while (st[i]) {
        b = fine_dich(st, i);
        if (dich_pezzi(st, i, b, &n0, &n1, &v0, &v1)) n++;
        i = st[b] ? b + 1 : b;
    }
    return n;
}

/* Il nome della dichiarazione numero `k`, per `style.item(k)`. */
static int stile_nome_k(const char *st, unsigned int k,
                        char *fuori, unsigned int max)
{
    unsigned int i = 0, b, n0, n1, v0, v1, n = 0, j;

    fuori[0] = '\0';
    if (!st) return 0;
    while (st[i]) {
        b = fine_dich(st, i);
        if (dich_pezzi(st, i, b, &n0, &n1, &v0, &v1)) {
            if (n == k) {
                for (j = 0; n0 + j < n1 && j + 1 < max; j++)
                    fuori[j] = (char)minuscola((unsigned char)st[n0 + j]);
                fuori[j] = '\0';
                return 1;
            }
            n++;
        }
        i = st[b] ? b + 1 : b;
    }
    return 0;
}

static void t_dich(Testo *t, const char *nome, unsigned int nn,
                   const char *val, unsigned int vn)
{
    unsigned int k;

    if (t->n) { t_car(t, ';'); t_car(t, ' '); }
    for (k = 0; k < nn; k++) t_car(t, nome[k]);
    t_car(t, ':'); t_car(t, ' ');
    for (k = 0; k < vn; k++) t_car(t, val[k]);
}

static unsigned int lung_u(const char *s) { return s ? lung(s) : 0; }

/* Riscrive `style=` con `prop` posta a `val`; `val` vuoto la toglie.
 *
 * ! IL POSTO NELL'ELENCO SI CONSERVA. Sostituire vuol dire riscrivere quella
 * dichiarazione dov'e', non toglierla e rimetterla in fondo: per il CSS
 * cambierebbe poco, ma un `style=` che si rimescola a ogni assegnazione e'
 * illeggibile a chi guarda la pagina cercando un difetto — cioe' a noi.
 *
 * ! E SE NON CI STA NON SI SCRIVE NIENTE. Un attributo troncato a meta' di una
 * dichiarazione sarebbe CSS sbagliato scritto da noi, che e' peggio di una
 * assegnazione persa: la spia `troncato` c'e' apposta per dirlo. */
static void stile_metti(ExDom *D, Legame *L, const char *prop, const char *val)
{
    const char  *st = html_attr(D->doc, L->nodo, "style");
    char         nuovo[STILE_MAX];
    Testo        t;
    unsigned int i = 0, b, n0, n1, v0, v1;
    int          fatto = 0;

    if (!st) st = "";
    t.p = nuovo; t.max = sizeof(nuovo); t.n = 0; t.pieno = 0;

    while (st[i]) {
        b = fine_dich(st, i);
        if (dich_pezzi(st, i, b, &n0, &n1, &v0, &v1)) {
            if (nome_uguale(st, n0, n1, prop)) {
                /* La prima occorrenza prende il valore nuovo; le altre
                 * spariscono, che e' il modo di non lasciare in giro un
                 * doppione che il CSS farebbe vincere sul nostro. */
                if (!fatto && val && val[0])
                    t_dich(&t, prop, lung_u(prop), val, lung_u(val));
                fatto = 1;
            } else {
                t_dich(&t, st + n0, n1 - n0, st + v0, v1 - v0);
            }
        }
        i = st[b] ? b + 1 : b;
    }
    if (!fatto && val && val[0])
        t_dich(&t, prop, lung_u(prop), val, lung_u(val));

    t_chiudi(&t);
    if (t.pieno) { D->troncato = 1; return; }
    html_attr_metti(D->doc, L->nodo, "style", nuovo);
}

/* ! IL GANCIO VIENE PRIMA DEL PROTOTIPO, quindi deve dire «non e' mia» sui
 * nomi dei metodi o li coprirebbe: `el.style.setProperty` diventerebbe la
 * stringa vuota invece di una funzione, e la pagina si fermerebbe li'. E'
 * l'unico elenco di nomi riservati di questa libreria, ed e' corto perche' i
 * metodi di CSSStyleDeclaration sono quattro. */
static int stile_e_metodo(const char *nome)
{
    return ugu(nome, "setProperty") || ugu(nome, "getPropertyValue") ||
           ugu(nome, "removeProperty") || ugu(nome, "item") ||
           ugu(nome, "getPropertyPriority");
}

static int stile_leggi(ExJsCtx *c, void *dato, const char *nome, ExJsVal *fuori)
{
    Legame     *L = (Legame *)dato;
    const char *st;
    char        prop[PROP_MAX], val[VAL_MAX];

    if (stile_e_metodo(nome)) return 0;
    st = html_attr(L->D->doc, L->nodo, "style");

    if (ugu(nome, "cssText")) { *fuori = stringa_c(c, st ? st : ""); return 1; }
    if (ugu(nome, "length")) {
        *fuori = exjs_numero(c, (double)stile_conta(st));
        return 1;
    }
    if (!nome_css(prop, sizeof(prop), nome)) return 0;

    /* ! UNA PROPRIETA' CHE NON C'E' DA "" E NON `undefined`, ed e' quel che fa
     * il DOM: `if (el.style.display)` dev'essere falso, non un errore.
     *
     * ! E QUI C'E' UNA BUGIA DICHIARATA: rendendo "" per QUALUNQUE nome, una
     * pagina che scopre le funzioni con `'grid' in el.style` si sente dire di
     * si' anche per cio' che non sappiamo disegnare. L'alternativa sarebbe un
     * elenco delle proprieta' CSS esistenti dentro il ponte — un secondo
     * elenco da tenere d'accordo con excss, che invecchierebbe da solo. Fra
     * una bugia sola e due elenchi che divergono si e' scelta la bugia, e si
     * scrive qui perche' chi cerchera' quel difetto la trovi. */
    stile_valore(st, prop, val, sizeof(val));
    *fuori = stringa_c(c, val);
    return 1;
}

static int stile_scrivi(ExJsCtx *c, void *dato, const char *nome, ExJsVal v)
{
    Legame *L = (Legame *)dato;
    char    prop[PROP_MAX];
    int     tipo;

    if (stile_e_metodo(nome)) return 0;
    if (ugu(nome, "length")) return 1;      /* si legge e basta */

    if (ugu(nome, "cssText")) {
        html_attr_metti(L->D->doc, L->nodo, "style", exjs_a_stringa(c, v));
        return 1;
    }
    if (!nome_css(prop, sizeof(prop), nome)) return 0;

    /* ! `null` E `undefined` TOLGONO LA DICHIARAZIONE, non scrivono le parole
     * "null" e "undefined". E' quel che fa il DOM, ed e' anche l'unica cosa
     * sensata: `el.style.display = x` con x non impostata deve tornare al
     * valore di prima, non dipingere la pagina con una proprieta' invalida. */
    tipo = exjs_tipo(c, v);
    if (tipo == EXJS_NULLO || tipo == EXJS_INDEFINITO)
        stile_metti(L->D, L, prop, "");
    else
        stile_metti(L->D, L, prop, exjs_a_stringa(c, v));
    return 1;
}

/* ! I SATELLITI PORTANO LO STESSO Legame DELL'ELEMENTO, ed e' come ritrovano
 * il nodo: `el.style` non e' `el`, ma sa di chi e'. Il controllo che li
 * distingue sta in exdom_nodo (l'involucro e' UNO, ed e' `L->val`); qui basta
 * che il puntatore stia nella nostra tabella. */
static Legame *legame_satellite(ExDom *D, ExJsVal v)
{
    Legame *L = (Legame *)exjs_esotico_dato(D->js, v);

    if (!L || L < D->leg || L >= D->leg + D->nodi_max) return 0;
    if (L->nodo < 0 || L->nodo >= (int)D->doc->nodi_n) return 0;
    return L;
}

static ExJsVal m_setProperty(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                             int n_arg, void *dato)
{
    ExDom  *D = (ExDom *)dato;
    Legame *L = legame_satellite(D, questo);
    char    prop[PROP_MAX];

    if (!L || n_arg < 1) return exjs_indefinito();
    if (!nome_css(prop, sizeof(prop), exjs_a_stringa(c, a[0])))
        return exjs_indefinito();
    stile_metti(D, L, prop, arg_str(c, a, n_arg, 1));
    return exjs_indefinito();
}

static ExJsVal m_getPropertyValue(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                                  int n_arg, void *dato)
{
    ExDom  *D = (ExDom *)dato;
    Legame *L = legame_satellite(D, questo);
    char    prop[PROP_MAX], val[VAL_MAX];

    if (!L || n_arg < 1) return stringa_c(c, "");
    if (!nome_css(prop, sizeof(prop), exjs_a_stringa(c, a[0])))
        return stringa_c(c, "");
    stile_valore(html_attr(D->doc, L->nodo, "style"), prop, val, sizeof(val));
    return stringa_c(c, val);
}

static ExJsVal m_removeProperty(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                                int n_arg, void *dato)
{
    ExDom  *D = (ExDom *)dato;
    Legame *L = legame_satellite(D, questo);
    char    prop[PROP_MAX], val[VAL_MAX];

    if (!L || n_arg < 1) return stringa_c(c, "");
    if (!nome_css(prop, sizeof(prop), exjs_a_stringa(c, a[0])))
        return stringa_c(c, "");
    /* Il DOM rende il valore che c'era: serve a chi lo rimette. */
    stile_valore(html_attr(D->doc, L->nodo, "style"), prop, val, sizeof(val));
    stile_metti(D, L, prop, "");
    return stringa_c(c, val);
}

static ExJsVal m_item(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                      int n_arg, void *dato)
{
    ExDom  *D = (ExDom *)dato;
    Legame *L = legame_satellite(D, questo);
    char    nome[PROP_MAX];

    if (!L || n_arg < 1) return stringa_c(c, "");
    stile_nome_k(html_attr(D->doc, L->nodo, "style"),
                 (unsigned int)exjs_a_numero(c, a[0]), nome, sizeof(nome));
    return stringa_c(c, nome);
}


/* Un satellite si costruisce una volta sola, e la memoria e' una proprieta'
 * PROPRIA dell'involucro invece di un campo per nodo: in lettura le proprie
 * vengono prima del gancio, quindi dalla seconda volta in poi il gancio non
 * viene nemmeno chiamato, e un nodo che non chiede mai `style` non paga
 * niente.
 * Il perche' per esteso sta in cima a questo blocco. */
static ExJsVal satellite(ExDom *D, Legame *L, const char *nome,
                         ExJsLeggiProp leggi, ExJsScriviProp scrivi,
                         ExJsVal proto)
{
    ExJsVal s = exjs_esotico(D->js, leggi, scrivi, L);

    if (exjs_tipo(D->js, s) != EXJS_OGGETTO) return exjs_indefinito();
    if (exjs_tipo(D->js, proto) == EXJS_OGGETTO)
        exjs_proto_metti(D->js, s, proto);

    /* ! LA SCRITTURA PASSA DAL GANCIO scrivi_prop, che per un OGGETTO si tira
     * indietro apposta (vedi li'): prendendola, questa riga finirebbe dentro
     * l'attributo invece che nella cache. */
    exjs_metti(D->js, L->val, nome, s);
    return s;
}

/* L'oggetto `style` di un elemento, costruito la prima volta che lo si chiede. */
static ExJsVal stile_oggetto(ExDom *D, Legame *L)
{
    return satellite(D, L, "style", stile_leggi, stile_scrivi, D->proto_stile);
}

/* =============================================================================
 * I SELETTORI — querySelector, querySelectorAll, matches, closest
 *
 * ! NON SI USANO QUELLI DI excss, E LA COSA ERA SCRITTA AL CONTRARIO nella
 * coda dei lavori («excss sa gia' leggere i selettori: ne manca l'uso dal
 * ponte»). Provandolo si e' visto che non torna:
 *
 *   - un CssPezzo conosce TIPO, CLASSE e ID e nient'altro. `[data-x]`, `>`,
 *     `+` e le classi multiple — che sono meta' di quel che scrive una pagina
 *     dentro querySelector — li' non ci sono.
 *   - un selettore di excss vive dentro un CssFoglio: e' fatto di scostamenti
 *     dentro l'arena di QUEL foglio. Per usarne uno al volo bisognerebbe
 *     fabbricare un foglio finto a ogni chiamata, cioe' un'arena e due vettori
 *     per una domanda che dura un istante.
 *   - e exdom prenderebbe una dipendenza da excss, che oggi non ha: le tre
 *     librerie si ignorano apposta, ed e' la decisione da cui viene tutto
 *     questo file.
 *
 * Il pezzo che segue e' un riconoscitore SUO, e sa una cosa in piu' e una in
 * meno di quello di excss. In piu': gli attributi, i combinatori, le classi a
 * ripetizione. In meno: non ha nessuna nozione di peso, perche' qui non c'e'
 * niente da mettere in cascata — si risponde si' o no su un nodo.
 *
 * ! QUEL CHE NON C'E' SI DICHIARA, e un selettore che lo contiene NON TROVA
 * NIENTE invece di trovare la cosa sbagliata: le pseudo-classi (`:hover`,
 * `:nth-child`, `:not`), i namespace e le virgole dentro le parentesi. Una
 * querySelector che rendesse un nodo a caso perche' ha ignorato un pezzo del
 * selettore e' peggio di una che rende null: il chiamante il null lo controlla.
 * ========================================================================== */
#define SEL_COMP_MAX   6        /* «div ul li a» sono quattro                */
#define SEL_TAG_MAX   40
#define SEL_ID_MAX    64
#define SEL_CL_MAX     4        /* `.a.b.c` in un compound                   */
#define SEL_AT_MAX     2

typedef struct {
    char comb;                                  /* 0 (il primo), ' ', >, +, ~ */
    char tag[SEL_TAG_MAX];                      /* "" = qualunque             */
    char id[SEL_ID_MAX];
    char classe[SEL_CL_MAX][SEL_TAG_MAX];
    int  n_classi;
    char attr[SEL_AT_MAX][SEL_TAG_MAX];
    char aval[SEL_AT_MAX][SEL_ID_MAX];
    char aop[SEL_AT_MAX];                       /* 0=presenza, = ~ ^ $ *      */
    int  n_attr;
} SelComp;

static int sel_ident(const char *s, unsigned int *i, char *fuori,
                     unsigned int max, int minusc)
{
    unsigned int k = 0;

    while (s[*i] && (s[*i] == '-' || s[*i] == '_' ||
                     (s[*i] >= '0' && s[*i] <= '9') ||
                     (s[*i] >= 'a' && s[*i] <= 'z') ||
                     (s[*i] >= 'A' && s[*i] <= 'Z') ||
                     (unsigned char)s[*i] >= 0x80)) {
        if (k + 1 >= max) return 0;
        fuori[k++] = minusc ? (char)minuscola((unsigned char)s[*i]) : s[*i];
        (*i)++;
    }
    fuori[k] = '\0';
    return k > 0;
}

/* Legge un compound (`div.a#b[c=d]`) dentro `sc`. Rende 0 se incontra
 * qualcosa che non sa leggere: chi chiama trasforma quello 0 in «non trova
 * niente», che e' la risposta onesta. */
static int sel_compound(const char *s, unsigned int *i, SelComp *sc)
{
    int qualcosa = 0;

    sc->tag[0] = sc->id[0] = '\0';
    sc->n_classi = sc->n_attr = 0;

    for (;;) {
        char ch = s[*i];

        if (ch == '*') { (*i)++; qualcosa = 1; continue; }
        if (ch == '#') {
            (*i)++;
            if (!sel_ident(s, i, sc->id, sizeof(sc->id), 0)) return 0;
            qualcosa = 1;
            continue;
        }
        if (ch == '.') {
            (*i)++;
            if (sc->n_classi >= SEL_CL_MAX) return 0;
            if (!sel_ident(s, i, sc->classe[sc->n_classi], SEL_TAG_MAX, 0))
                return 0;
            sc->n_classi++;
            qualcosa = 1;
            continue;
        }
        if (ch == '[') {
            unsigned int k = 0;
            char         chiudi;

            (*i)++;
            if (sc->n_attr >= SEL_AT_MAX) return 0;
            while (s[*i] == ' ') (*i)++;
            if (!sel_ident(s, i, sc->attr[sc->n_attr], SEL_TAG_MAX, 1)) return 0;
            while (s[*i] == ' ') (*i)++;
            sc->aop[sc->n_attr] = 0;
            if (s[*i] == '~' || s[*i] == '^' || s[*i] == '$' ||
                s[*i] == '*' || s[*i] == '|') {
                sc->aop[sc->n_attr] = s[*i];
                (*i)++;
                if (s[*i] != '=') return 0;
            } else if (s[*i] == '=') {
                sc->aop[sc->n_attr] = '=';
            } else if (s[*i] != ']') {
                return 0;
            }
            if (s[*i] == '=') {
                (*i)++;
                while (s[*i] == ' ') (*i)++;
                chiudi = (s[*i] == '"' || s[*i] == '\'') ? s[*i] : 0;
                if (chiudi) (*i)++;
                while (s[*i] && s[*i] != ']' &&
                       (chiudi ? s[*i] != chiudi : s[*i] != ' ')) {
                    if (k + 1 >= SEL_ID_MAX) return 0;
                    sc->aval[sc->n_attr][k++] = s[*i];
                    (*i)++;
                }
                if (chiudi && s[*i] == chiudi) (*i)++;
            }
            sc->aval[sc->n_attr][k] = '\0';
            while (s[*i] == ' ') (*i)++;
            if (s[*i] != ']') return 0;
            (*i)++;
            sc->n_attr++;
            qualcosa = 1;
            continue;
        }
        if (ch == ':') return 0;            /* le pseudo-classi: vedi sopra */
        if (sc->tag[0] == '\0' && !qualcosa &&
            sel_ident(s, i, sc->tag, sizeof(sc->tag), 1)) {
            qualcosa = 1;
            continue;
        }
        break;
    }
    return qualcosa;
}

/* Legge un selettore intero (senza virgole) in `sc[0..]`, dal primo pezzo
 * all'ultimo. Rende il numero di compound, o 0 se non si sa leggere. */
static int sel_leggi(const char *s, SelComp *sc, int max)
{
    unsigned int i = 0;
    int          n = 0;
    char         comb = 0;

    while (s[i] == ' ') i++;
    while (s[i]) {
        if (n >= max) return 0;
        if (!sel_compound(s, &i, &sc[n])) return 0;
        sc[n].comb = comb;
        n++;

        /* ! LO SPAZIO IN CODA NON E' UN COMBINATORE, e distinguerlo costa una
         * variabile: «div » e' un selettore intero con uno spazio di troppo,
         * «div >» e' un selettore mozzo. Senza la distinzione uno dei due
         * casi si comporta male, e sono tutt'e due comuni. */
        {
            char esplicito = 0;
            int  spazi = 0;

            while (s[i] == ' ' || s[i] == '>' || s[i] == '+' || s[i] == '~') {
                if (s[i] == ' ') spazi = 1;
                else {
                    if (esplicito) return 0;   /* «> >» non vuol dire niente */
                    esplicito = s[i];
                }
                i++;
            }
            if (!s[i]) return esplicito ? 0 : n;
            comb = esplicito ? esplicito : (spazi ? ' ' : 0);
            if (!comb) return 0;
        }
    }
    return n;
}

/* ! LA LIBC QUI NON C'E' (vedi in cima al file): il confronto su n byte se
 * lo scrive da se', come ugu e lung. */
static int uguali_n(const char *a, const char *b, unsigned int n)
{
    unsigned int i;

    for (i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static int attr_confronta(const char *v, char op, const char *atteso)
{
    unsigned int lv, la;

    if (!v) return 0;
    if (!op) return 1;                       /* [attr]: basta che ci sia */
    lv = lung(v); la = lung(atteso);
    switch (op) {
    case '=':  return ugu(v, atteso);
    case '~':  return ha_classe(v, atteso);  /* la stessa regola a parole */
    case '^':  return la <= lv && uguali_n(v, atteso, la);
    case '$':  return la <= lv && uguali_n(v + lv - la, atteso, la);
    case '*': {
        unsigned int k;
        if (!la) return 0;
        for (k = 0; k + la <= lv; k++) if (uguali_n(v + k, atteso, la)) return 1;
        return 0;
    }
    case '|':  return ugu(v, atteso) ||
                      (la < lv && uguali_n(v, atteso, la) && v[la] == '-');
    default:   return 0;
    }
}

static int sel_nodo(const SelComp *sc, HtmlDoc *d, int n)
{
    int i;

    if (sc->tag[0] && !ugu(html_nome(d, n), sc->tag)) return 0;
    if (sc->id[0]) {
        const char *v = html_attr(d, n, "id");
        if (!v || !ugu(v, sc->id)) return 0;
    }
    for (i = 0; i < sc->n_classi; i++)
        if (!ha_classe(html_attr(d, n, "class"), sc->classe[i])) return 0;
    for (i = 0; i < sc->n_attr; i++)
        if (!attr_confronta(html_attr(d, n, sc->attr[i]), sc->aop[i],
                            sc->aval[i])) return 0;
    return 1;
}

static int elemento_prima(const HtmlDoc *d, int n)
{
    int p = fratello_prima(d, n);

    while (p >= 0 && d->nodi[p].tipo != HTML_ELEMENTO) p = fratello_prima(d, p);
    return p;
}

/* Il nodo `n` soddisfa i compound da 0 a k?
 *
 * ! SI GUARDA DA DESTRA A SINISTRA, ed e' il verso che rende la cosa
 * praticabile: da sinistra bisognerebbe cercare tutti i `div` della pagina e
 * poi tutti i loro discendenti; da destra si parte dal nodo che si ha in mano
 * e si sale. E' anche il verso in cui lo fanno i browser veri.
 *
 * ! LA RICORSIONE QUI E' AMMESSA e altrove in questo file no, perche' e'
 * PROFONDA QUANTO IL SELETTORE — al piu' SEL_COMP_MAX — e non quanto l'albero.
 * Le visite dell'albero restano iterative per la ragione scritta piu' su. */
static int sel_risali(const SelComp *sc, int k, HtmlDoc *d, int n)
{
    if (n < 0 || d->nodi[n].tipo != HTML_ELEMENTO) return 0;
    if (!sel_nodo(&sc[k], d, n)) return 0;
    if (k == 0) return 1;

    switch (sc[k].comb) {
    case '>':
        return sel_risali(sc, k - 1, d, d->nodi[n].padre);
    case '+':
        return sel_risali(sc, k - 1, d, elemento_prima(d, n));
    case '~': {
        int p = elemento_prima(d, n);
        while (p >= 0) {
            if (sel_risali(sc, k - 1, d, p)) return 1;
            p = elemento_prima(d, p);
        }
        return 0;
    }
    default: {
        int p = d->nodi[n].padre;
        while (p >= 0) {
            if (sel_risali(sc, k - 1, d, p)) return 1;
            p = d->nodi[p].padre;
        }
        return 0;
    }
    }
}

/* Un selettore con le virgole: `a, b, c` vale se ne vale uno.
 * `fermo_al_primo` fa rendere il primo nodo trovato invece di riempire il
 * vettore — e' la differenza fra querySelector e querySelectorAll, che
 * altrimenti sarebbero due copie della stessa visita. */
static ExJsVal sel_cerca(ExDom *D, int radice, const char *selettore,
                         int fermo_al_primo)
{
    ExJsCtx     *c = D->js;
    ExJsVal      v = fermo_al_primo ? exjs_nullo() : exjs_vettore(c);
    unsigned int k = 0;
    char         pezzo[256];
    int          n;

    if (!selettore || !selettore[0] || radice < 0) return v;

    for (n = prossimo_in_visita(D->doc, radice, radice); n >= 0;
         n = prossimo_in_visita(D->doc, n, radice)) {
        unsigned int i = 0, u;
        int          preso = 0, lungo;

        if (D->doc->nodi[n].tipo != HTML_ELEMENTO) continue;

        /* ! IL SELETTORE SI RILEGGE PER OGNI NODO, ed e' uno spreco
         * dichiarato: analizzarlo una volta sola vorrebbe dire un vettore di
         * liste di compound dimensionato a occhio, e le pagine vere chiamano
         * querySelector con selettori di venti caratteri su alberi di
         * duemila nodi. Se un giorno si vedra' in un profilo, il posto e'
         * questo e la cura e' ovvia. */
        while (selettore[i] && !preso) {
            SelComp sc[SEL_COMP_MAX];
            int     np;

            u = 0; lungo = 0;
            while (selettore[i] && selettore[i] != ',') {
                if (u + 1 < sizeof(pezzo)) pezzo[u++] = selettore[i];
                else lungo = 1;
                i++;
            }
            pezzo[u] = '\0';
            if (selettore[i] == ',') i++;

            /* ! UN PEZZO PIU' LUNGO DEL BUFFER NON COMBACIA, e non combacia a
             * meta': un selettore troncato e' un ALTRO selettore, e piu'
             * largo di quello che era scritto. */
            np = lungo ? 0 : sel_leggi(pezzo, sc, SEL_COMP_MAX);
            if (np > 0 && sel_risali(sc, np - 1, D->doc, n)) preso = 1;
        }

        if (!preso) continue;
        if (fermo_al_primo) return exdom_avvolgi(D, n);
        exjs_indice_metti(c, v, k++, exdom_avvolgi(D, n));
    }
    return v;
}

static int sel_combacia(ExDom *D, int n, const char *selettore)
{
    unsigned int i = 0, u;
    int          lungo;
    char         pezzo[256];

    if (n < 0 || !selettore || D->doc->nodi[n].tipo != HTML_ELEMENTO) return 0;
    while (selettore[i]) {
        SelComp sc[SEL_COMP_MAX];
        int     np;

        u = 0; lungo = 0;
        while (selettore[i] && selettore[i] != ',') {
            if (u + 1 < sizeof(pezzo)) pezzo[u++] = selettore[i];
            else lungo = 1;
            i++;
        }
        pezzo[u] = '\0';
        if (selettore[i] == ',') i++;

        np = lungo ? 0 : sel_leggi(pezzo, sc, SEL_COMP_MAX);
        if (np > 0 && sel_risali(sc, np - 1, D->doc, n)) return 1;
    }
    return 0;
}

static ExJsVal m_querySelector(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                               int n_arg, void *dato)
{
    ExDom *D = (ExDom *)dato;
    int    r = nodo_di(D, questo);

    if (r < 0 || n_arg < 1) return exjs_nullo();
    return sel_cerca(D, r, exjs_a_stringa(c, a[0]), 1);
}

static ExJsVal m_querySelectorAll(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                                  int n_arg, void *dato)
{
    ExDom *D = (ExDom *)dato;
    int    r = nodo_di(D, questo);

    if (r < 0 || n_arg < 1) return exjs_vettore(c);
    return sel_cerca(D, r, exjs_a_stringa(c, a[0]), 0);
}

static ExJsVal m_matches(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                         int n_arg, void *dato)
{
    ExDom *D = (ExDom *)dato;
    int    n = nodo_di(D, questo);

    if (n < 0 || n_arg < 1) return exjs_booleano(0);
    return exjs_booleano(sel_combacia(D, n, exjs_a_stringa(c, a[0])));
}

/* ! closest() PARTE DAL NODO STESSO, non dal padre, e le pagine ci contano:
 * un gestore di clic fa `e.target.closest('a')` e sul collegamento vero deve
 * rendere il collegamento, non il primo antenato. */
static ExJsVal m_closest(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                         int n_arg, void *dato)
{
    ExDom      *D = (ExDom *)dato;
    int         n = nodo_di(D, questo);
    const char *s;

    if (n < 0 || n_arg < 1) return exjs_nullo();
    s = exjs_a_stringa(c, a[0]);
    while (n >= 0) {
        if (D->doc->nodi[n].tipo == HTML_ELEMENTO && sel_combacia(D, n, s))
            return exdom_avvolgi(D, n);
        n = D->doc->nodi[n].padre;
    }
    return exjs_nullo();
}


/* =============================================================================
 * GLI ATTRIBUTI RIFLESSI — `img.src`, `a.href`, `input.value`...
 *
 * ! NON SI RIFLETTE TUTTO SU TUTTO, e la scorciatoia sarebbe stata proprio
 * quella: prendersi ogni nome e cercarlo fra gli attributi. Sarebbe stato
 * sbagliato in un modo che non si vede subito — gli script appendono i propri
 * dati agli elementi in continuazione, e un `div.value = 3` che diventa un
 * attributo `value="3"` rende `3` la volta dopo invece del numero 3. Il DOM
 * dice ESATTAMENTE quali proprieta' esistono su quali elementi, ed e' un
 * elenco: eccolo.
 *
 * ! E QUI SI RIFLETTE L'ATTRIBUTO, NON L'INDIRIZZO RISOLTO. Nel DOM vero
 * `img.src` rende l'indirizzo assoluto («http://sito/a/b.png») anche se
 * l'attributo dice «b.png»: la differenza la fa l'indirizzo DELLA PAGINA, che
 * il ponte non conosce e non deve conoscere — chi lo sa e' il browser, che ha
 * risolvi(). Il giorno che serva, la strada e' passare quell'indirizzo a
 * exdom_apri, non indovinarlo qui. Fino ad allora e' scritto, perche' una
 * pagina che confronta `a.href` con un indirizzo intero trovera' di no.
 * ========================================================================== */
typedef struct {
    const char *prop;       /* il nome in JavaScript                        */
    const char *attr;       /* l'attributo nell'albero                      */
    const char *tag;        /* i tag ammessi, separati da '|'; 0 = tutti    */
} Riflesso;

static const Riflesso RIFLESSI[] = {
    /* comuni a ogni elemento */
    { "title",       "title",       0 },
    { "lang",        "lang",        0 },
    { "dir",         "dir",         0 },
    /* gli indirizzi */
    { "src",         "src",         "img|script|iframe|source|video|audio|embed|input|frame|track" },
    { "href",        "href",        "a|area|link|base" },
    { "alt",         "alt",         "img|area|input" },
    /* i moduli */
    { "value",       "value",       "input|button|option|li|param|progress|meter" },
    { "name",        "name",        "input|select|textarea|button|form|img|iframe|a|meta|param|map|object" },
    { "type",        "type",        "input|button|script|link|style|ol|source|embed|object" },
    { "placeholder", "placeholder", "input|textarea" },
    { "action",      "action",      "form" },
    { "method",      "method",      "form" },
    { "htmlFor",     "for",         "label|output" },
    /* il resto che le pagine leggono davvero */
    { "target",      "target",      "a|area|form|base" },
    { "rel",         "rel",         "a|area|link" },
    { "content",     "content",     "meta" },
    { "width",       "width",       "img|canvas|iframe|video|embed|object|table|td|th" },
    { "height",      "height",      "img|canvas|iframe|video|embed|object" },
    { 0, 0, 0 }
};

/* `tag` sta nell'elenco «a|b|c»? Un elenco vuoto (0) vuol dire «qualunque». */
static int tag_nell_elenco(const char *elenco, const char *tag)
{
    unsigned int i = 0, k;

    if (!elenco) return 1;
    while (elenco[i]) {
        k = 0;
        while (elenco[i + k] && elenco[i + k] != '|' &&
               elenco[i + k] == tag[k]) k++;
        if (tag[k] == '\0' && (elenco[i + k] == '\0' || elenco[i + k] == '|'))
            return 1;
        while (elenco[i] && elenco[i] != '|') i++;
        if (elenco[i] == '|') i++;
    }
    return 0;
}

static const Riflesso *riflesso_di(HtmlDoc *d, int n, const char *nome)
{
    const char *tag = html_nome(d, n);
    int         i;

    for (i = 0; RIFLESSI[i].prop; i++)
        if (ugu(nome, RIFLESSI[i].prop) &&
            tag_nell_elenco(RIFLESSI[i].tag, tag))
            return &RIFLESSI[i];
    return 0;
}

/* =============================================================================
 * `el.dataset` — gli attributi `data-*`
 *
 * ! E' UN SATELLITE COME `el.style`, con lo stesso meccanismo e per la stessa
 * ragione: non possiede niente, legge e scrive gli attributi. Cambia solo la
 * regola del nome — `data-vista-larga` da una parte, `vistaLarga` dall'altra.
 * ========================================================================== */

/* Da `vistaLarga` a `data-vista-larga`. Rende 0 se non ci sta. */
static int nome_data(char *dest, unsigned int max, const char *js)
{
    unsigned int k = 0, i;

    if (max < 8) return 0;
    dest[k++] = 'd'; dest[k++] = 'a'; dest[k++] = 't'; dest[k++] = 'a';
    dest[k++] = '-';
    for (i = 0; js[i]; i++) {
        unsigned char ch = (unsigned char)js[i];

        if (ch >= 'A' && ch <= 'Z') {
            if (k + 2 >= max) return 0;
            dest[k++] = '-';
            dest[k++] = (char)(ch + 32);
        } else {
            if (k + 1 >= max) return 0;
            dest[k++] = (char)ch;
        }
    }
    dest[k] = '\0';
    return i > 0;
}

static int dati_leggi(ExJsCtx *c, void *dato, const char *nome, ExJsVal *fuori)
{
    Legame     *L = (Legame *)dato;
    char        att[PROP_MAX];
    const char *v;

    if (!nome_data(att, sizeof(att), nome)) return 0;
    v = html_attr(L->D->doc, L->nodo, att);

    /* ! UN `data-` CHE NON C'E' DA `undefined`, non "" — al contrario di
     * `el.style.qualcosa`. Non e' un capriccio: e' come il DOM distingue i due
     * casi, e le pagine ci contano. `if (el.dataset.x)` dev'essere falso, ma
     * `'x' in el.dataset` dev'essere falso anche lui, e con "" non lo
     * sarebbe. Dicendo «non e' mia» il motore prosegue e rende undefined. */
    if (!v) return 0;
    *fuori = stringa_c(c, v);
    return 1;
}

static int dati_scrivi(ExJsCtx *c, void *dato, const char *nome, ExJsVal v)
{
    Legame *L = (Legame *)dato;
    char    att[PROP_MAX];

    if (!nome_data(att, sizeof(att), nome)) return 0;
    html_attr_metti(L->D->doc, L->nodo, att, exjs_a_stringa(c, v));
    return 1;
}

/* =============================================================================
 * `el.classList`
 *
 * ! LE CLASSI SI TENGONO NELL'ATTRIBUTO, non in una lista accanto: chi scrive
 * `el.className = 'a b'` e chi scrive `el.classList.add('b')` devono vedere la
 * stessa cosa, e con due depositi bisognerebbe tenerli d'accordo a ogni giro.
 * ========================================================================== */
#define CLASSI_MAX  512

static int classe_e_metodo(const char *nome)
{
    return ugu(nome, "add") || ugu(nome, "remove") || ugu(nome, "toggle") ||
           ugu(nome, "contains") || ugu(nome, "item") ||
           ugu(nome, "replace");
}

static unsigned int classi_conta(const char *el)
{
    unsigned int i = 0, n = 0;

    if (!el) return 0;
    while (el[i]) {
        while (el[i] && bianco(el[i])) i++;
        if (!el[i]) break;
        n++;
        while (el[i] && !bianco(el[i])) i++;
    }
    return n;
}

static int classe_k(const char *el, unsigned int k, char *fuori, unsigned int max)
{
    unsigned int i = 0, n = 0, j;

    fuori[0] = '\0';
    if (!el) return 0;
    while (el[i]) {
        while (el[i] && bianco(el[i])) i++;
        if (!el[i]) break;
        if (n == k) {
            for (j = 0; el[i + j] && !bianco(el[i + j]) && j + 1 < max; j++)
                fuori[j] = el[i + j];
            fuori[j] = '\0';
            return 1;
        }
        n++;
        while (el[i] && !bianco(el[i])) i++;
    }
    return 0;
}

/* Riscrive `class=` con `cl` aggiunta (metti=1) o tolta (metti=0). Rende 1 se
 * l'attributo e' cambiato davvero. */
static int classi_cambia(ExDom *D, Legame *L, const char *cl, int metti)
{
    const char  *el = html_attr(D->doc, L->nodo, "class");
    char         nuovo[CLASSI_MAX];
    Testo        t;
    unsigned int i = 0, j;
    int          c_era;

    if (!cl || !cl[0]) return 0;
    if (!el) el = "";
    c_era = ha_classe(el, cl);
    if (metti == (c_era ? 1 : 0)) return 0;      /* gia' com'e' voluta */

    t.p = nuovo; t.max = sizeof(nuovo); t.n = 0; t.pieno = 0;
    while (el[i]) {
        unsigned int a;

        while (el[i] && bianco(el[i])) i++;
        if (!el[i]) break;
        a = i;
        while (el[i] && !bianco(el[i])) i++;
        /* Togliendo: si salta la parola cercata. Aggiungendo: si copia tutto,
         * e la parola nuova va in fondo — non c'era, o non si sarebbe qui. */
        if (!metti && (i - a) == lung(cl) && uguali_n(el + a, cl, i - a))
            continue;
        if (t.n) t_car(&t, ' ');
        for (j = a; j < i; j++) t_car(&t, el[j]);
    }
    if (metti) {
        if (t.n) t_car(&t, ' ');
        t_stringa(&t, cl);
    }
    t_chiudi(&t);
    if (t.pieno) { D->troncato = 1; return 0; }
    html_attr_metti(D->doc, L->nodo, "class", nuovo);
    return 1;
}

static int classi_leggi(ExJsCtx *c, void *dato, const char *nome, ExJsVal *fuori)
{
    Legame     *L = (Legame *)dato;
    const char *el;
    char        b[128];

    if (classe_e_metodo(nome)) return 0;
    el = html_attr(L->D->doc, L->nodo, "class");

    if (ugu(nome, "length")) {
        *fuori = exjs_numero(c, (double)classi_conta(el));
        return 1;
    }
    if (ugu(nome, "value")) { *fuori = stringa_c(c, el ? el : ""); return 1; }

    /* ! SI RISPONDE ANCHE AGLI INDICI, perche' classList si scorre con un
     * ciclo `for (i = 0; i < l.length; i++) l[i]` almeno quanto con forEach. */
    if (nome[0] >= '0' && nome[0] <= '9') {
        unsigned int k = 0, i;

        for (i = 0; nome[i]; i++) {
            if (nome[i] < '0' || nome[i] > '9') return 0;
            k = k * 10 + (unsigned int)(nome[i] - '0');
        }
        if (!classe_k(el, k, b, sizeof(b))) return 0;
        *fuori = stringa_c(c, b);
        return 1;
    }
    return 0;
}

/* ! IL GANCIO IN SCRITTURA C'E' ANCHE SE HA UNA COSA SOLA DA FARE, e non si
 * puo' passare 0 al suo posto: un esotico senza gancio di scrittura, su
 * QuickJS, e' un oggetto su cui NON SI SCRIVE (l'assegnazione si perde in
 * silenzio), mentre su ExJs la scrittura passa e diventa una proprieta'
 * normale. Due motori, due comportamenti, nessun errore da nessuna parte: e'
 * esattamente il genere di differenza che le novantadue prove uguali per
 * tutt'e due esistono per non far nascere. */
static int classi_scrivi(ExJsCtx *c, void *dato, const char *nome, ExJsVal v)
{
    Legame *L = (Legame *)dato;

    /* `classList.value = 'a b'` e' l'unica scrittura che il DOM ammette, ed e'
     * la stessa cosa di `className`. */
    if (ugu(nome, "value")) {
        html_attr_metti(L->D->doc, L->nodo, "class", exjs_a_stringa(c, v));
        return 1;
    }
    return 0;
}

static ExJsVal m_cl_add(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                        int n_arg, void *dato)
{
    ExDom  *D = (ExDom *)dato;
    Legame *L = legame_satellite(D, questo);
    int     i;

    if (!L) return exjs_indefinito();
    /* Il DOM ne accetta quante gliene si danno: `classList.add('a', 'b')`. */
    for (i = 0; i < n_arg; i++) classi_cambia(D, L, exjs_a_stringa(c, a[i]), 1);
    return exjs_indefinito();
}

static ExJsVal m_cl_remove(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                           int n_arg, void *dato)
{
    ExDom  *D = (ExDom *)dato;
    Legame *L = legame_satellite(D, questo);
    int     i;

    if (!L) return exjs_indefinito();
    for (i = 0; i < n_arg; i++) classi_cambia(D, L, exjs_a_stringa(c, a[i]), 0);
    return exjs_indefinito();
}

static ExJsVal m_cl_contains(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                             int n_arg, void *dato)
{
    ExDom  *D = (ExDom *)dato;
    Legame *L = legame_satellite(D, questo);

    if (!L || n_arg < 1) return exjs_booleano(0);
    return exjs_booleano(ha_classe(html_attr(D->doc, L->nodo, "class"),
                                   exjs_a_stringa(c, a[0])));
}

/* ! toggle CON IL SECONDO ARGOMENTO NON E' UN toggle: `toggle('x', cond)`
 * vuol dire «mettila se cond, toglila se no», ed e' il modo in cui mezzo web
 * accende e spegne una classe senza scrivere un if. */
static ExJsVal m_cl_toggle(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                           int n_arg, void *dato)
{
    ExDom      *D = (ExDom *)dato;
    Legame     *L = legame_satellite(D, questo);
    const char *cl;
    int         voluta;

    if (!L || n_arg < 1) return exjs_booleano(0);
    cl = exjs_a_stringa(c, a[0]);
    if (n_arg >= 2) voluta = exjs_a_booleano(c, a[1]);
    else voluta = !ha_classe(html_attr(D->doc, L->nodo, "class"), cl);
    classi_cambia(D, L, cl, voluta);
    return exjs_booleano(voluta);
}

static ExJsVal m_cl_item(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                         int n_arg, void *dato)
{
    ExDom  *D = (ExDom *)dato;
    Legame *L = legame_satellite(D, questo);
    char    b[128];

    if (!L || n_arg < 1) return exjs_nullo();
    if (!classe_k(html_attr(D->doc, L->nodo, "class"),
                  (unsigned int)exjs_a_numero(c, a[0]), b, sizeof(b)))
        return exjs_nullo();
    return stringa_c(c, b);
}

static ExJsVal m_cl_replace(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                            int n_arg, void *dato)
{
    ExDom  *D = (ExDom *)dato;
    Legame *L = legame_satellite(D, questo);

    if (!L || n_arg < 2) return exjs_booleano(0);
    if (!ha_classe(html_attr(D->doc, L->nodo, "class"), exjs_a_stringa(c, a[0])))
        return exjs_booleano(0);
    classi_cambia(D, L, exjs_a_stringa(c, a[0]), 0);
    classi_cambia(D, L, exjs_a_stringa(c, a[1]), 1);
    return exjs_booleano(1);
}

static ExJsVal dati_oggetto(ExDom *D, Legame *L)
{
    return satellite(D, L, "dataset", dati_leggi, dati_scrivi,
                     exjs_indefinito());
}

static ExJsVal classi_oggetto(ExDom *D, Legame *L)
{
    return satellite(D, L, "classList", classi_leggi, classi_scrivi,
                     D->proto_classi);
}

/* ! IL PONTE FRA leggi_prop E LA TABELLA STA QUI E NON LA', perche' leggi_prop
 * viene prima nel file e non puo' vedere il tipo Riflesso. Due righe di
 * involucro costano meno di spostare mezza libreria per far tornare l'ordine
 * delle dichiarazioni. */
static int riflesso_leggi(ExJsCtx *c, HtmlDoc *d, int n, const char *nome,
                          ExJsVal *fuori)
{
    const Riflesso *R = riflesso_di(d, n, nome);

    if (!R) return 0;
    /* ! L'ATTRIBUTO CHE NON C'E' DA "", come `id` e `className` e al
     * contrario di getAttribute: e' quel che fa il DOM per le proprieta'
     * riflesse, ed e' il motivo per cui `if (img.alt)` funziona. */
    *fuori = stringa_c(c, html_attr(d, n, R->attr));
    return 1;
}

static int riflesso_scrivi(ExJsCtx *c, HtmlDoc *d, int n, const char *nome,
                           ExJsVal v)
{
    const Riflesso *R = riflesso_di(d, n, nome);

    if (!R) return 0;
    html_attr_metti(d, n, R->attr, exjs_a_stringa(c, v));
    return 1;
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


/* =============================================================================
 * GLI EVENTI DELLA FINESTRA — `window.addEventListener`
 *
 * ! `window` E' L'OGGETTO GLOBALE (vedi in fondo, dove lo si dichiara), e
 * l'oggetto globale non e' un nodo: `nodo_di` su di lui rende -1, e i metodi
 * del prototipo degli elementi non ci arrivano. Servono tre involucri, e ogni
 * involucro fa una cosa sola: prende la RADICE al posto di `questo`.
 *
 * ! QUINDI GLI ASCOLTATORI DELLA FINESTRA E QUELLI DEL DOCUMENTO SONO GLI
 * STESSI, ed e' una differenza vera dal DOM, dove window e document sono due
 * bersagli distinti. Qui il documento E' la radice e la finestra E' il
 * globale: tenerli separati vorrebbe dire una seconda tabella di ascolti con
 * una seconda propagazione, per una distinzione che si vede solo in
 * `event.currentTarget`. Sta scritto qui perche' il giorno che una pagina ci
 * caschi si sappia dove guardare.
 * ========================================================================== */
static ExJsVal m_win_addEventListener(ExJsCtx *c, ExJsVal questo,
                                      const ExJsVal *a, int n_arg, void *dato)
{
    ExDom *D = (ExDom *)dato;
    char   tipo[TIPO_MAX];

    (void)questo;
    /* ! LE STESSE TRE PORTE DI m_addEventListener, NELLO STESSO ORDINE, e non
     * e' pignoleria: la spia `perso` vuol dire «un ascoltatore non ha trovato
     * posto», non «qualcuno ha chiamato male». Una chiamata con un argomento
     * solo, o con qualcosa che non e' una funzione, e' uno sbaglio della
     * pagina e si lascia cadere in silenzio come fa il DOM; solo un nome
     * troppo lungo per la tabella accende la spia. Se le due funzioni si
     * comportassero diversamente, `window.addEventListener` farebbe apparire
     * spie che il gemello non fa apparire, e nessuno capirebbe perche'. */
    if (n_arg < 2) return exjs_indefinito();
    if (exjs_tipo(c, a[1]) != EXJS_FUNZIONE) return exjs_indefinito();
    if (!tipo_copia(tipo, exjs_a_stringa(c, a[0]))) {
        D->perso = 1;
        return exjs_indefinito();
    }
    asc_aggiungi(D, D->doc->radice, tipo, a[1],
                 (n_arg >= 3) ? exjs_a_booleano(c, a[2]) : 0, 0);
    return exjs_indefinito();
}

static ExJsVal m_win_removeEventListener(ExJsCtx *c, ExJsVal questo,
                                         const ExJsVal *a, int n_arg,
                                         void *dato)
{
    ExDom *D = (ExDom *)dato;

    (void)questo;
    if (n_arg < 2) return exjs_indefinito();
    asc_togli(D, asc_trova(D, D->doc->radice, exjs_a_stringa(c, a[0]), a[1],
                           (n_arg >= 3) ? exjs_a_booleano(c, a[2]) : 0, 0));
    return exjs_indefinito();
}

/* =============================================================================
 * `document.fonts` — LA PROMESSA CHE NON HA NIENTE DA ASPETTARE
 *
 * ! LA PROMESSA E' QUELLA DI fetch, non una seconda scritta apposta. Ce n'era
 * una sua, con un `then` che ACCODAVA il gestore invece di chiamarlo: due
 * oggetti che si chiamano promessa e si comportano in due modi dentro la
 * stessa libreria sono un difetto che aspetta, non un'ottimizzazione. Adesso
 * la forma e' una sola — sta in fondo a questo file, insieme a fetch — e
 * questa e' un'istanza gia' risolta, con `undefined` dentro.
 *
 * ! E CHIAMARE SUBITO IL GESTORE, QUI, E' GIUSTO — mentre in un browser vero
 * non lo sarebbe. Il pericolo classico e' `document.fonts.ready.then(parti)`
 * che fa partire `parti` prima che il <body> esista; qui non puo' succedere,
 * perche' gli script girano DOPO che il documento e' stato analizzato per
 * intero. E' lo stesso motivo per cui `document.readyState` dice «complete».
 *
 * ! LA PROMESSA E' UNA SOLA per tutto il documento, e non una per chiamata:
 * non ha stato — non c'e' niente da aspettare e non c'e' errore possibile —
 * e ExJs non ha un raccoglitore di memoria, quindi una pagina che chiedesse
 * cinquecento caratteri ne lascerebbe cinquecento in giro.
 * ========================================================================== */
static ExJsVal m_falso(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                       int n_arg, void *dato)
{
    (void)questo; (void)a; (void)n_arg; (void)dato; (void)c;
    return exjs_booleano(0);
}

static ExJsVal m_vero(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                      int n_arg, void *dato)
{
    (void)questo; (void)a; (void)n_arg; (void)dato; (void)c;
    return exjs_booleano(1);
}

static ExJsVal m_niente(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                        int n_arg, void *dato)
{
    (void)c; (void)questo; (void)a; (void)n_arg; (void)dato;
    return exjs_indefinito();
}

static ExJsVal m_fonts_load(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                            int n_arg, void *dato)
{
    ExDom *D = (ExDom *)dato;

    (void)c; (void)questo; (void)a; (void)n_arg;
    return D->promessa;
}


/* =============================================================================
 * `location` — L'INDIRIZZO DELLA PAGINA
 *
 * ! L'INDIRIZZO ARRIVA DA FUORI, come il tempo in ExJs e come gli eventi in
 * questa stessa libreria. Il ponte non apre connessioni e non sa dove si trovi
 * la pagina che ha in mano: glielo dice chi l'ha caricata, con
 * exdom_indirizzo(). Una libreria che se lo andasse a cercare non si potrebbe
 * provare senza una rete.
 *
 * ! E LA NAVIGAZIONE NON LA FA LEI. `location.href = "..."` non apre niente:
 * mette da parte l'indirizzo, e il browser lo raccoglie con
 * exdom_dove_andare() quando lo script ha finito. E' la stessa forma di
 * preventDefault — il ponte dice che cosa e' successo, chi ha lo schermo
 * decide che cosa farne — ed e' anche l'unica che funziona: una navigazione
 * fatta in mezzo a uno script butterebbe via l'albero che quello script sta
 * ancora usando.
 *
 * ! LE PARTI SI CALCOLANO A OGNI LETTURA e non si tengono in dieci campi.
 * `location.search` di una pagina e' una fetta di `location.href`: due
 * depositi vorrebbero dire tenerli d'accordo, e sarebbe la solita storia.
 * ========================================================================== */

/* Le fette di un indirizzo, calcolate al volo:
 *
 *     https://ex.os:8080/a/b?c=d#e
 *     ~~~~~                          protocol  «https:»
 *             ~~~~~~~~~~             host      «ex.os:8080»
 *             ~~~~~                  hostname  «ex.os»
 *                   ~~~~             port      «8080»
 *                       ~~~~         pathname  «/a/b»
 *                           ~~~~     search    «?c=d»
 *                                ~~  hash      «#e»
 */
static int url_fetta(const char *u, const char *quale, char *fuori,
                     unsigned int max)
{
    unsigned int i = 0, a, b, k;

    fuori[0] = '\0';
    if (!u || !u[0]) return 1;

    /* lo schema, fino ai «://» (o ai soli «:» per «file:») */
    while (u[i] && u[i] != ':' && u[i] != '/' && u[i] != '?' && u[i] != '#') i++;
    if (u[i] != ':') { i = 0; }                  /* indirizzo senza schema */

    if (ugu(quale, "protocol")) { a = 0; b = (u[i] == ':') ? i + 1 : 0; }
    else {
        unsigned int p = (u[i] == ':') ? i + 1 : 0;
        unsigned int h0, h1, c0;

        if (u[p] == '/' && u[p + 1] == '/') p += 2;
        h0 = p;
        while (u[p] && u[p] != '/' && u[p] != '?' && u[p] != '#') p++;
        h1 = p;
        c0 = h0;
        while (c0 < h1 && u[c0] != ':') c0++;

        if (ugu(quale, "host"))          { a = h0; b = h1; }
        else if (ugu(quale, "hostname")) { a = h0; b = c0; }
        else if (ugu(quale, "port"))     { a = (c0 < h1) ? c0 + 1 : h1; b = h1; }
        else if (ugu(quale, "origin"))   { a = 0;  b = h1; }
        else {
            unsigned int q = p, f;

            while (u[q] && u[q] != '?' && u[q] != '#') q++;
            f = q;
            while (u[f] && u[f] != '#') f++;

            if (ugu(quale, "pathname"))    { a = h1; b = q; }
            else if (ugu(quale, "search")) { a = q;  b = f; }
            else if (ugu(quale, "hash"))   { a = f;  b = f + lung(u + f); }
            else return 0;                       /* non e' una parte nostra */
        }
    }

    for (k = 0; a + k < b && k + 1 < max; k++) fuori[k] = u[a + k];
    fuori[k] = '\0';
    return 1;
}

static int luogo_e_metodo(const char *nome)
{
    return ugu(nome, "assign") || ugu(nome, "replace") ||
           ugu(nome, "reload") || ugu(nome, "toString");
}

static int luogo_leggi(ExJsCtx *c, void *dato, const char *nome, ExJsVal *fuori)
{
    ExDom *D = (ExDom *)dato;
    char   b[EXDOM_URL_MAX];

    if (luogo_e_metodo(nome)) return 0;
    if (ugu(nome, "href")) { *fuori = stringa_c(c, D->url); return 1; }
    if (!url_fetta(D->url, nome, b, sizeof(b))) return 0;
    *fuori = stringa_c(c, b);
    return 1;
}

/* Mette da parte dove si vuole andare. Non apre niente: vedi in cima. */
static void luogo_vai(ExDom *D, const char *dove)
{
    unsigned int i;

    if (!dove) return;
    for (i = 0; dove[i] && i + 1 < sizeof(D->vai); i++) D->vai[i] = dove[i];
    D->vai[i] = '\0';
}

static int luogo_scrivi(ExJsCtx *c, void *dato, const char *nome, ExJsVal v)
{
    ExDom *D = (ExDom *)dato;

    if (luogo_e_metodo(nome)) return 0;

    /* ! `location.href = "..."` SI PRENDE; `location = "..."` NO, ed e' una
     * differenza che si vede. Il secondo assegna a una proprieta' del globale,
     * e il globale non ha ganci: intercettarlo vorrebbe dire mettere un gancio
     * su TUTTE le scritture globali di ogni pagina, per un caso solo. La forma
     * lunga e i due metodi assign/replace coprono quel che si legge in giro;
     * se un giorno una pagina vera cadesse sulla forma corta, e' scritto qui
     * che cosa costa prenderla. */
    if (ugu(nome, "href")) { luogo_vai(D, exjs_a_stringa(c, v)); return 1; }

    /* ! LE ALTRE PARTI SI LEGGONO E BASTA, per adesso. Nel DOM sono
     * scrivibili — `location.hash = '#x'` e' comune — e per farle bisogna
     * RICOMPORRE l'indirizzo, non solo tagliarlo. Si dichiara invece di
     * accettarle e non fare niente: una scrittura che sparisce in silenzio e'
     * peggio di una che non c'e'. */
    { char b[EXDOM_URL_MAX];
      if (url_fetta(D->url, nome, b, sizeof(b))) return 1; }
    return 0;
}

static ExJsVal m_loc_assign(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                            int n_arg, void *dato)
{
    ExDom *D = (ExDom *)dato;

    (void)questo;
    if (n_arg >= 1) luogo_vai(D, exjs_a_stringa(c, a[0]));
    return exjs_indefinito();
}

/* ! reload() RIMANDA DOVE SI E' GIA': e' quel che vuol dire ricaricare. */
static ExJsVal m_loc_reload(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                            int n_arg, void *dato)
{
    ExDom *D = (ExDom *)dato;

    (void)c; (void)questo; (void)a; (void)n_arg;
    luogo_vai(D, D->url);
    return exjs_indefinito();
}

static ExJsVal m_loc_stringa(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                             int n_arg, void *dato)
{
    ExDom *D = (ExDom *)dato;

    (void)questo; (void)a; (void)n_arg;
    return stringa_c(c, D->url);
}

void exdom_indirizzo(ExDom *D, const char *url)
{
    unsigned int i = 0;

    if (!D) return;
    if (url) while (url[i] && i + 1 < sizeof(D->url)) { D->url[i] = url[i]; i++; }
    D->url[i] = '\0';
    /* ! L'INDIRIZZO NUOVO CANCELLA QUELLO VOLUTO. Si arriva qui perche' una
     * pagina e' stata caricata: un `location.href` rimasto in sospeso dalla
     * pagina di PRIMA farebbe partire una seconda navigazione che nessuno ha
     * chiesto, e sarebbe impossibile da capire guardando lo schermo. */
    D->vai[0] = '\0';
}

/* ! SI COPIA E SI DIMENTICA, in una chiamata sola. Rendere un puntatore
 * dentro il ponte e lasciare a chi legge il compito di azzerarlo vorrebbe dire
 * che una navigazione fallita resta in coda e riparte al giro dopo: il browser
 * tornerebbe su una pagina che non ha caricato, per sempre, e nessuno
 * capirebbe da dove viene. */
int exdom_dove_andare(ExDom *D, char *fuori, unsigned int max)
{
    unsigned int i = 0;

    if (!D || !fuori || max == 0) return 0;
    fuori[0] = '\0';
    if (!D->vai[0]) return 0;
    while (D->vai[i] && i + 1 < max) { fuori[i] = D->vai[i]; i++; }
    fuori[i] = '\0';
    D->vai[0] = '\0';
    return 1;
}


/* =============================================================================
 * `document.cookie` — I BISCOTTI, LA META' CHE TOCCA AL PONTE
 *
 * ! QUESTA E' LA META' JAVASCRIPT, E L'ALTRA NON C'E' ANCORA. Un biscotto vero
 * fa due giri: uno script lo scrive e lo rilegge (questo), e il browser lo
 * manda al server in un'intestazione `Cookie:` e ne raccoglie i `Set-Cookie:`
 * che tornano (quello, che vuole exhttp e una dispensa per dominio nel
 * browser). Sta scritto qui perche' la differenza si vede: una pagina che
 * scrive un biscotto e RICARICA se stessa aspettandoselo indietro dal server
 * non lo trovera'.
 *
 * ! E ALLORA PERCHE' FARNE META'? Perche' senza, mezza pagina moderna non
 * arriva in fondo alla prima funzione: `document.cookie.length` su
 * `undefined` e' un errore, e da li' in poi non gira piu' niente. Con la
 * dispensa in memoria lo script prosegue e — e' il caso di google.com —
 * SCEGLIE DA SE' la strada senza biscotti, che e' quella che sappiamo
 * percorrere. Rispondere «non ne ho» in modo corretto vale piu' che non
 * rispondere.
 *
 * ! GLI ATTRIBUTI SI BUTTANO. `nome=valore; expires=...; path=/; secure` porta
 * dentro la dispensa solo `nome=valore`: scadenze, percorsi e domini sono
 * decisioni che spettano a chi tiene la dispensa vera, e fingere di rispettarli
 * qui vorrebbe dire scriverli due volte e in due modi diversi.
 * ========================================================================== */

/* Copia in `fuori` il pezzo `nome=valore` di una scrittura, cioe' tutto quel
 * che sta prima del primo «;», senza gli spazi ai bordi. Rende 0 se non c'e'
 * un nome. */
static int biscotto_pezzo(const char *s, char *fuori, unsigned int max)
{
    unsigned int a = 0, b, k;

    fuori[0] = '\0';
    if (!s) return 0;
    while (s[a] && bianco(s[a])) a++;
    b = a;
    while (s[b] && s[b] != ';') b++;
    while (b > a && bianco(s[b - 1])) b--;
    if (b == a) return 0;
    for (k = 0; a + k < b && k + 1 < max; k++) fuori[k] = s[a + k];
    fuori[k] = '\0';
    /* Senza «=» non e' un biscotto: e' un attributo scritto da solo. */
    for (k = 0; fuori[k]; k++) if (fuori[k] == '=') return 1;
    return 0;
}

/* Quanto e' lungo il NOME di «nome=valore» (fino al primo «=»). */
static unsigned int nome_biscotto(const char *s)
{
    unsigned int k = 0;

    while (s[k] && s[k] != '=') k++;
    return k;
}

static void biscotto_aggiungi(ExDom *D, const char *nuovo)
{
    char         fatto[EXDOM_BISCOTTI_MAX];
    Testo        t;
    unsigned int nl = nome_biscotto(nuovo), i = 0;
    int          sostituito = 0;

    t.p = fatto; t.max = sizeof(fatto); t.n = 0; t.pieno = 0;

    /* Si ricopiano quelli che restano, saltando l'omonimo: un biscotto
     * riscritto sostituisce, non si affianca. */
    while (D->biscotti[i]) {
        unsigned int a = i, b;

        while (D->biscotti[i] && D->biscotti[i] != ';') i++;
        b = i;
        if (D->biscotti[i] == ';') i++;
        while (D->biscotti[i] == ' ') i++;
        while (a < b && bianco(D->biscotti[a])) a++;
        while (b > a && bianco(D->biscotti[b - 1])) b--;
        if (b == a) continue;

        if (b - a > nl && nome_biscotto(D->biscotti + a) == nl &&
            uguali_n(D->biscotti + a, nuovo, nl)) {
            if (t.n) { t_car(&t, ';'); t_car(&t, ' '); }
            t_stringa(&t, nuovo);
            sostituito = 1;
            continue;
        }
        if (t.n) { t_car(&t, ';'); t_car(&t, ' '); }
        { unsigned int k; for (k = a; k < b; k++) t_car(&t, D->biscotti[k]); }
    }
    if (!sostituito) {
        if (t.n) { t_car(&t, ';'); t_car(&t, ' '); }
        t_stringa(&t, nuovo);
    }
    t_chiudi(&t);

    /* ! SE NON CI STA NON SI SCRIVE NIENTE, come per lo stile: una dispensa
     * troncata a meta' di un valore e' un biscotto sbagliato, che e' peggio di
     * un biscotto mancante. */
    if (t.pieno) { D->troncato = 1; return; }
    for (i = 0; fatto[i]; i++) D->biscotti[i] = fatto[i];
    D->biscotti[i] = '\0';
}

void exdom_biscotti_metti(ExDom *D, const char *tutti)
{
    unsigned int i = 0;

    if (!D) return;
    if (tutti) while (tutti[i] && i + 1 < sizeof(D->biscotti)) {
        D->biscotti[i] = tutti[i]; i++;
    }
    D->biscotti[i] = '\0';
}

const char *exdom_biscotti(ExDom *D)
{
    return D ? D->biscotti : "";
}


/* =============================================================================
 * LA RETE — XMLHttpRequest E fetch
 *
 * ! IL PONTE NON APRE CONNESSIONI, E NON DEVE. exhttp esiste, TLS compreso, e
 * chiamarlo da qui sarebbe stato di gran lunga il modo piu' corto: sarebbe
 * anche stato il primo posto in cui questa libreria smette di essere un ponte.
 * exdom sa due cose — che esiste un albero e che esiste un motore — e ogni
 * terza cosa che impara e' una cosa che il giorno dopo non si puo' piu'
 * provare senza. Il tempo arriva da fuori (exjs_pompa), gli eventi arrivano da
 * fuori (exdom_evento), l'indirizzo arriva da fuori (exdom_indirizzo): la rete
 * arriva da fuori come loro, con un gancio che il browser registra.
 *
 * ! E IL BANCO NE APPROFITTA. Con un gancio, provare XMLHttpRequest non vuole
 * ne' una rete ne' un server: la prova registra un gancio finto che risponde
 * quel che vuole lei — 200, 404, o «non e' partita» — e guarda che cosa fa la
 * pagina. Con una chiamata a exhttp qui dentro, quelle prove non si potrebbero
 * scrivere affatto.
 *
 * -----------------------------------------------------------------------------
 * ! LE RICHIESTE SONO SINCRONE, TUTTE, ANCHE QUELLE DICHIARATE ASINCRONE.
 * `xhr.open(m, u, true)` e' la forma normale sul web e qui si comporta come
 * `false`: send() va in rete, aspetta, riempie i campi e SUBITO DOPO chiama i
 * gestori. La differenza si vede in un caso solo — il codice che sta dopo
 * `send()` gira DOPO `onload` invece che prima — ed e' scritto qui perche' chi
 * lo incontrera' lo riconosca. La strada per l'asincrono vero non e' in questa
 * libreria: e' un browser che sappia aspettare una connessione senza smettere
 * di rispondere al mouse, e quello e' un lavoro suo.
 *
 * ! E I GESTORI SI CHIAMANO CON `this` GIUSTO, che e' il motivo per cui si
 * chiamano subito invece di accodarli. La coda dei lavori di ExJs porta una
 * funzione e nient'altro: niente `this`, niente argomenti. Un `onload` che
 * facesse `this.responseText` — e sono tanti — troverebbe il globale.
 * ========================================================================== */

static int rete_chiedi(ExDom *D, ExDomRichiesta *r)
{
    r->risposta = 0;
    r->byte     = 0;
    r->codice   = 0;
    r->tipo     = 0;

    /* ! SENZA GANCIO SI RISPONDE DI NO, non si finge un 200 vuoto. Un browser
     * che non ha registrato la rete non e' un server che ha risposto male. */
    if (!D->rete || !r->url || !r->url[0]) return 0;
    return D->rete(D->rete_dato, r) ? 1 : 0;
}

/* Il testo della risposta come stringa del motore. */
static ExJsVal rete_testo(ExDom *D, const ExDomRichiesta *r)
{
    if (!r->risposta || r->byte == 0) return stringa_c(D->js, "");
    return exjs_stringa(D->js, r->risposta, (int)r->byte);
}

/* =============================================================================
 * LE PROMESSE, SCRITTE A MANO
 *
 * ! NON SONO Promise VERE, ED E' DICHIARATO. ExJs e' un ES3 e le promesse non
 * ce le ha; QuickJS si'. Un `fetch` che rendesse una Promise vera sotto un
 * motore e un oggetto finto sotto l'altro sarebbe la cosa peggiore di tutte —
 * la stessa pagina, due comportamenti, nessun errore. Qui ce n'e' UNA sola,
 * scritta qui, e fa quel che serve: `then`, `catch`, `finally`, e si incatena.
 *
 * ! SI RISOLVONO SUBITO, e in questo browser e' giusto. La richiesta e' gia'
 * finita quando `then` viene chiamato — le richieste sono sincrone, vedi sopra
 * — quindi non c'e' niente da aspettare. E gli script girano DOPO che il
 * documento e' stato analizzato per intero (e' anche il motivo per cui
 * `document.readyState` dice «complete»), quindi non c'e' nemmeno il pericolo
 * classico: far partire il seguito di una pagina mentre il <body> non c'e'
 * ancora.
 *
 * ! E `await` FUNZIONA LO STESSO, con QuickJS. `await x` non vuole una
 * Promise: vuole un oggetto con un `then`, e chiama `then(risolvi, rifiuta)`.
 * Il nostro lo chiama subito, QuickJS accoda la ripresa come lavoro, e la
 * pompa dei tempi la esegue. Con ExJs `await` non esiste, ma li' non esiste
 * nemmeno la parola.
 * ========================================================================== */

/* Il segno che un oggetto e' una delle nostre promesse: serve a `then` per
 * riconoscere che il gestore ne ha resa un'altra e non incartarla di nuovo. */
#define SEGNO_PROMESSA  "__exos_promessa"

static ExJsVal promessa_nuova(ExDom *D, ExJsVal valore, int ok)
{
    ExJsCtx *c = D->js;
    ExJsVal  p = exjs_oggetto(c);

    if (exjs_tipo(c, p) != EXJS_OGGETTO) return exjs_indefinito();
    exjs_proto_metti(c, p, D->proto_promessa);
    exjs_metti(c, p, SEGNO_PROMESSA, exjs_booleano(1));
    exjs_metti(c, p, "__valore", valore);
    exjs_metti(c, p, "__ok", exjs_booleano(ok != 0));
    return p;
}

static int e_promessa(ExJsCtx *c, ExJsVal v)
{
    if (exjs_tipo(c, v) != EXJS_OGGETTO) return 0;
    return exjs_a_booleano(c, exjs_prendi(c, v, SEGNO_PROMESSA));
}

/* Un motivo di rifiuto che somigli a un errore: mezza pagina legge `.message`,
 * e una stringa nuda non ce l'ha. */
static ExJsVal motivo(ExDom *D, const char *testo)
{
    ExJsCtx *c = D->js;
    ExJsVal  e = exjs_oggetto(c);

    exjs_metti(c, e, "name", stringa_c(c, "TypeError"));
    exjs_metti(c, e, "message", stringa_c(c, testo));
    return e;
}

static ExJsVal m_pr_then(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                         int n_arg, void *dato)
{
    ExDom  *D = (ExDom *)dato;
    ExJsVal v = exjs_prendi(c, questo, "__valore");
    int     ok = exjs_a_booleano(c, exjs_prendi(c, questo, "__ok"));
    ExJsVal f = (n_arg >= 1) ? a[0] : exjs_indefinito();
    ExJsVal g = (n_arg >= 2) ? a[1] : exjs_indefinito();
    ExJsVal r;

    if (ok) {
        if (exjs_tipo(c, f) != EXJS_FUNZIONE) return questo;
        r = exjs_invoca(c, f, exjs_indefinito(), &v, 1, 0);
    } else {
        /* Senza gestore dell'errore il rifiuto prosegue lungo la catena: e'
         * cosi' che `.then(a).then(b).catch(c)` fa arrivare l'errore a `c`. */
        if (exjs_tipo(c, g) != EXJS_FUNZIONE) return questo;
        r = exjs_invoca(c, g, exjs_indefinito(), &v, 1, 0);
        ok = 1;                     /* gestito: da qui in poi si e' a posto */
    }

    /* ! SE IL GESTORE NE HA RESA UN'ALTRA SI RENDE QUELLA, e non la si
     * incarta. E' cio' che fa funzionare
     * `fetch(u).then(function (r) { return r.text(); }).then(...)`: il primo
     * gestore rende una promessa, e il secondo `then` deve vedere il TESTO,
     * non una promessa dentro una promessa. */
    if (e_promessa(c, r)) return r;
    return promessa_nuova(D, r, ok);
}

static ExJsVal m_pr_catch(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                          int n_arg, void *dato)
{
    ExJsVal arg[2];

    arg[0] = exjs_indefinito();
    arg[1] = (n_arg >= 1) ? a[0] : exjs_indefinito();
    return m_pr_then(c, questo, arg, 2, dato);
}

/* ! `finally` CHIAMA E BASTA, e NON cambia il valore: e' la sua regola nel
 * linguaggio, ed e' anche il motivo per cui esiste separato da `then`. */
static ExJsVal m_pr_finally(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                            int n_arg, void *dato)
{
    (void)dato;
    if (n_arg >= 1 && exjs_tipo(c, a[0]) == EXJS_FUNZIONE)
        exjs_invoca(c, a[0], exjs_indefinito(), 0, 0, 0);
    return questo;
}

/* =============================================================================
 * LA RISPOSTA DI fetch
 * ========================================================================== */
static ExJsVal m_rp_text(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                         int n_arg, void *dato)
{
    ExDom *D = (ExDom *)dato;

    (void)a; (void)n_arg;
    return promessa_nuova(D, exjs_prendi(c, questo, "__testo"), 1);
}

static ExJsVal m_rp_json(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                         int n_arg, void *dato)
{
    ExDom  *D = (ExDom *)dato;
    ExJsVal t = exjs_prendi(c, questo, "__testo");
    ExJsVal J = exjs_prendi(c, exjs_globale(c), "JSON");
    ExJsVal p;

    (void)a; (void)n_arg;
    /* ! JSON.parse SI CHIEDE AL MOTORE, non si riscrive qui. Ce l'hanno
     * tutt'e due, ed e' il posto giusto: un secondo analizzatore di JSON
     * dentro il ponte sarebbe una seconda idea di che cosa sia un numero. */
    if (exjs_tipo(c, J) != EXJS_OGGETTO) return promessa_nuova(D, t, 1);
    p = exjs_prendi(c, J, "parse");
    if (exjs_tipo(c, p) != EXJS_FUNZIONE) return promessa_nuova(D, t, 1);
    return promessa_nuova(D, exjs_invoca(c, p, J, &t, 1, 0), 1);
}

static ExJsVal m_hd_get(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                        int n_arg, void *dato)
{
    const char *n;

    (void)dato;
    if (n_arg < 1) return exjs_nullo();
    n = exjs_a_stringa(c, a[0]);
    /* ! DI INTESTAZIONI SE NE CONOSCE UNA, e si dice quale. exhttp oggi
     * riporta il solo Content-Type; per tutte le altre la risposta onesta e'
     * `null`, che nel DOM vuol dire «non c'e'» — e non "" , che vorrebbe dire
     * «c'e' ed e' vuota». */
    if ((n[0] | 32) == 'c' && (n[1] | 32) == 'o' && (n[2] | 32) == 'n')
        return exjs_prendi(c, questo, "__tipo");
    return exjs_nullo();
}

static ExJsVal risposta_nuova(ExDom *D, const ExDomRichiesta *r,
                              const char *url)
{
    ExJsCtx *c = D->js;
    ExJsVal  o = exjs_oggetto(c);
    ExJsVal  h;

    if (exjs_tipo(c, o) != EXJS_OGGETTO) return exjs_indefinito();
    exjs_proto_metti(c, o, D->proto_risposta);
    exjs_metti(c, o, "__testo", rete_testo(D, r));
    exjs_metti(c, o, "__tipo",
               r->tipo ? stringa_c(c, r->tipo) : exjs_nullo());
    /* ! `ok` E' 200..299 E NON «e' arrivata qualcosa». Una pagina che
     * controlla `if (!resp.ok)` sta chiedendo del CODICE, e un 404 arrivato
     * benissimo dalla rete e' una risposta riuscita e un `ok` falso. */
    exjs_metti(c, o, "ok",
               exjs_booleano(r->codice >= 200 && r->codice <= 299));
    exjs_metti(c, o, "status", exjs_numero(c, (double)r->codice));
    exjs_metti(c, o, "statusText",
               stringa_c(c, r->codice == 200 ? "OK" : ""));
    exjs_metti(c, o, "url", stringa_c(c, url));
    exjs_metti(c, o, "redirected", exjs_booleano(0));
    exjs_metti(c, o, "type", stringa_c(c, "basic"));
    exjs_metti(c, o, "bodyUsed", exjs_booleano(0));

    h = exjs_oggetto(c);
    exjs_metti(c, h, "__tipo", exjs_prendi(c, o, "__tipo"));
    metti_metodo(D, h, "get", m_hd_get);
    exjs_metti(c, o, "headers", h);
    return o;
}

static ExJsVal m_fetch(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                       int n_arg, void *dato)
{
    ExDom          *D = (ExDom *)dato;
    ExDomRichiesta  r;
    const char     *url;

    (void)questo;
    if (n_arg < 1) return promessa_nuova(D, motivo(D, "fetch senza indirizzo"), 0);

    url = exjs_a_stringa(c, a[0]);
    r.metodo     = "GET";
    r.url        = url;
    r.corpo      = 0;
    r.tipo_corpo = 0;

    if (n_arg >= 2 && exjs_tipo(c, a[1]) == EXJS_OGGETTO) {
        ExJsVal m = exjs_prendi(c, a[1], "method");
        ExJsVal b = exjs_prendi(c, a[1], "body");

        if (exjs_tipo(c, m) == EXJS_STRINGA) r.metodo = exjs_a_stringa(c, m);
        if (exjs_tipo(c, b) == EXJS_STRINGA) r.corpo  = exjs_a_stringa(c, b);
    }

    /* ! LA RETE CHE NON RISPONDE E' UN RIFIUTO, un 404 NO. E' la regola di
     * fetch, ed e' quella giusta: «il server ha detto di no» e «non sono
     * riuscito a chiedere» sono due cose diverse, e una pagina le tratta in
     * due posti diversi. */
    if (!rete_chiedi(D, &r) || r.codice == 0)
        return promessa_nuova(D, motivo(D, "la richiesta non e' partita"), 0);

    return promessa_nuova(D, risposta_nuova(D, &r, url), 1);
}

/* =============================================================================
 * XMLHttpRequest
 *
 * ! LO STATO STA IN PROPRIETA' DELL'OGGETTO, non in una tabella del ponte, e
 * per una volta la strada corta e' anche quella giusta: un XHR e' un oggetto
 * che vive quanto lo script vuole, e una tabella avrebbe voluto un tetto —
 * cioe' un numero da indovinare e una pagina che smette di funzionare quando
 * lo supera. Le proprieta' di servizio cominciano con due trattini bassi e
 * sono visibili a chi le cerca: nasconderle vorrebbe dire un secondo
 * meccanismo di oggetti esotici per non far vedere tre stringhe.
 *
 * ! I METODI STANNO SUL PROTOTIPO, come per i nodi: una pagina che apre venti
 * richieste altrimenti pagherebbe centoquaranta funzioni native.
 * ========================================================================== */

/* Chiama i gestori registrati per `tipo`: prima `on<tipo>`, poi quelli messi
 * con addEventListener. */
static void xhr_manda(ExDom *D, ExJsVal x, const char *tipo)
{
    ExJsCtx     *c = D->js;
    char         nome[TIPO_MAX + 8];
    unsigned int i = 0, k;
    ExJsVal      f, lista, ev;

    if (lung(tipo) + 5 >= sizeof(nome)) return;

    ev = exjs_oggetto(c);
    exjs_metti(c, ev, "type", stringa_c(c, tipo));
    exjs_metti(c, ev, "target", x);

    nome[0] = 'o'; nome[1] = 'n'; i = 2;
    for (k = 0; tipo[k]; k++) nome[i++] = tipo[k];
    nome[i] = '\0';
    f = exjs_prendi(c, x, nome);
    if (exjs_tipo(c, f) == EXJS_FUNZIONE) exjs_invoca(c, f, x, &ev, 1, 0);

    nome[0] = '_'; nome[1] = '_'; i = 2;
    for (k = 0; tipo[k]; k++) nome[i++] = tipo[k];
    nome[i] = '\0';
    lista = exjs_prendi(c, x, nome);
    if (exjs_tipo(c, lista) != EXJS_OGGETTO) return;
    {
        unsigned int n = exjs_lunghezza(c, lista);

        for (k = 0; k < n; k++) {
            ExJsVal g = exjs_indice_prendi(c, lista, k);

            if (exjs_tipo(c, g) == EXJS_FUNZIONE) exjs_invoca(c, g, x, &ev, 1, 0);
        }
    }
}

static ExJsVal m_xhr_open(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                          int n_arg, void *dato)
{
    ExDom *D = (ExDom *)dato;

    (void)D;
    if (n_arg < 2) return exjs_indefinito();
    exjs_metti(c, questo, "__metodo", a[0]);
    exjs_metti(c, questo, "__url", a[1]);
    /* ! IL TERZO ARGOMENTO SI LEGGE E NON SI USA, ed e' dichiarato in cima:
     * qui la richiesta e' sincrona comunque. Rifiutarlo sarebbe peggio — la
     * forma con `true` e' quella che si scrive sempre. */
    exjs_metti(c, questo, "readyState", exjs_numero(c, 1.0));
    return exjs_indefinito();
}

/* ! LE INTESTAZIONI SI PRENDONO E SI BUTTANO, e va detto qui e nella guida.
 * exhttp costruisce la richiesta da se' e non ha un posto dove infilarne una
 * in piu': accettarle e non mandarle e' una bugia, ma RIFIUTARLE fermerebbe
 * ogni pagina che ne mette una — e sono quasi tutte. Fra le due si e' scelta
 * quella che lascia la pagina viva, e la si e' scritta in tre posti. */
static ExJsVal m_xhr_header(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                            int n_arg, void *dato)
{
    (void)c; (void)questo; (void)a; (void)n_arg; (void)dato;
    return exjs_indefinito();
}

static ExJsVal m_xhr_send(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                          int n_arg, void *dato)
{
    ExDom          *D = (ExDom *)dato;
    ExDomRichiesta  r;
    ExJsVal         m = exjs_prendi(c, questo, "__metodo");
    ExJsVal         u = exjs_prendi(c, questo, "__url");

    if (exjs_tipo(c, u) != EXJS_STRINGA) {
        xhr_manda(D, questo, "error");
        return exjs_indefinito();
    }

    r.metodo     = (exjs_tipo(c, m) == EXJS_STRINGA) ? exjs_a_stringa(c, m) : "GET";
    r.url        = exjs_a_stringa(c, u);
    r.corpo      = (n_arg >= 1 && exjs_tipo(c, a[0]) == EXJS_STRINGA)
                   ? exjs_a_stringa(c, a[0]) : 0;
    r.tipo_corpo = 0;

    if (!rete_chiedi(D, &r) || r.codice == 0) {
        /* ! UNA RICHIESTA CHE NON PARTE LASCIA status A ZERO, ed e' quel che
         * fa il DOM: e' cosi' che una pagina distingue «il server ha detto
         * 404» da «non sono nemmeno riuscito a chiedere». */
        exjs_metti(c, questo, "readyState", exjs_numero(c, 4.0));
        exjs_metti(c, questo, "status", exjs_numero(c, 0.0));
        xhr_manda(D, questo, "readystatechange");
        xhr_manda(D, questo, "error");
        xhr_manda(D, questo, "loadend");
        return exjs_indefinito();
    }

    {
        ExJsVal t = rete_testo(D, &r);

        exjs_metti(c, questo, "responseText", t);
        exjs_metti(c, questo, "response", t);
    }
    exjs_metti(c, questo, "status", exjs_numero(c, (double)r.codice));
    exjs_metti(c, questo, "statusText",
               stringa_c(c, r.codice == 200 ? "OK" : ""));
    exjs_metti(c, questo, "__tipo",
               r.tipo ? stringa_c(c, r.tipo) : exjs_nullo());
    exjs_metti(c, questo, "responseURL", stringa_c(c, r.url));
    exjs_metti(c, questo, "readyState", exjs_numero(c, 4.0));

    xhr_manda(D, questo, "readystatechange");
    xhr_manda(D, questo, "load");
    xhr_manda(D, questo, "loadend");
    return exjs_indefinito();
}

static ExJsVal m_xhr_abort(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                           int n_arg, void *dato)
{
    (void)a; (void)n_arg; (void)dato;
    /* ! NON C'E' NIENTE DA INTERROMPERE: quando questa si puo' chiamare, la
     * richiesta e' gia' finita. Si riporta lo stato a zero perche' e' quel
     * che una pagina si aspetta di leggere dopo, e non si manda `abort`:
     * l'evento direbbe che si e' fermato qualcosa che era gia' fermo. */
    exjs_metti(c, questo, "readyState", exjs_numero(c, 0.0));
    exjs_metti(c, questo, "status", exjs_numero(c, 0.0));
    return exjs_indefinito();
}

static ExJsVal m_xhr_tutte(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                           int n_arg, void *dato)
{
    ExJsVal t = exjs_prendi(c, questo, "__tipo");
    char    b[256];
    Testo   s;

    (void)a; (void)n_arg; (void)dato;
    s.p = b; s.max = sizeof(b); s.n = 0; s.pieno = 0;
    if (exjs_tipo(c, t) == EXJS_STRINGA) {
        t_stringa(&s, "content-type: ");
        t_stringa(&s, exjs_a_stringa(c, t));
        t_stringa(&s, "\r\n");
    }
    t_chiudi(&s);
    return stringa_c(c, b);
}

static ExJsVal m_xhr_una(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                         int n_arg, void *dato)
{
    (void)dato;
    if (n_arg < 1) return exjs_nullo();
    {
        const char *n = exjs_a_stringa(c, a[0]);

        if ((n[0] | 32) == 'c' && (n[1] | 32) == 'o' && (n[2] | 32) == 'n')
            return exjs_prendi(c, questo, "__tipo");
    }
    return exjs_nullo();
}

static ExJsVal m_xhr_ascolta(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                             int n_arg, void *dato)
{
    char         nome[TIPO_MAX + 8];
    unsigned int i = 2, k;
    const char  *tipo;
    ExJsVal      lista;

    (void)dato;
    if (n_arg < 2 || exjs_tipo(c, a[1]) != EXJS_FUNZIONE) return exjs_indefinito();
    tipo = exjs_a_stringa(c, a[0]);
    if (lung(tipo) + 3 >= sizeof(nome)) return exjs_indefinito();

    /* ! LA LISTA STA SULL'OGGETTO, non nella tabella degli ascolti del ponte:
     * quella tabella e' indicizzata per NODO, e un XMLHttpRequest non e' un
     * nodo dell'albero. Sono due meccanismi perche' sono due cose. */
    nome[0] = '_'; nome[1] = '_';
    for (k = 0; tipo[k]; k++) nome[i++] = tipo[k];
    nome[i] = '\0';

    lista = exjs_prendi(c, questo, nome);
    if (exjs_tipo(c, lista) != EXJS_OGGETTO) {
        lista = exjs_vettore(c);
        exjs_metti(c, questo, nome, lista);
    }
    exjs_indice_metti(c, lista, exjs_lunghezza(c, lista), a[1]);
    return exjs_indefinito();
}

static ExJsVal m_xhr_nuovo(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                           int n_arg, void *dato)
{
    ExDom  *D = (ExDom *)dato;
    ExJsVal o = exjs_oggetto(c);

    (void)questo; (void)a; (void)n_arg;
    if (exjs_tipo(c, o) != EXJS_OGGETTO) return exjs_indefinito();
    exjs_proto_metti(c, o, D->proto_xhr);

    /* ! I CAMPI CI SONO DA SUBITO, anche prima di `open`. Una pagina che
     * legge `xhr.readyState` appena costruito deve trovare 0, non
     * `undefined`: il primo e' uno stato del DOM, il secondo e' un oggetto
     * che non sa di essere un XMLHttpRequest. */
    exjs_metti(c, o, "readyState",   exjs_numero(c, 0.0));
    exjs_metti(c, o, "status",       exjs_numero(c, 0.0));
    exjs_metti(c, o, "statusText",   stringa_c(c, ""));
    exjs_metti(c, o, "responseText", stringa_c(c, ""));
    exjs_metti(c, o, "response",     stringa_c(c, ""));
    exjs_metti(c, o, "responseType", stringa_c(c, ""));
    exjs_metti(c, o, "responseURL",  stringa_c(c, ""));
    exjs_metti(c, o, "timeout",      exjs_numero(c, 0.0));
    exjs_metti(c, o, "withCredentials", exjs_booleano(0));
    exjs_metti(c, o, "__tipo",       exjs_nullo());
    return o;
}

void exdom_rete_metti(ExDom *D, ExDomRete f, void *dato)
{
    if (!D) return;
    D->rete      = f;
    D->rete_dato = dato;
}

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
    D->url[0]      = '\0';
    D->vai[0]      = '\0';
    D->biscotti[0] = '\0';
    D->rete        = 0;
    D->rete_dato   = 0;

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
    /* ! I SELETTORI STANNO SUL PROTOTIPO E VALGONO ANCHE PER IL DOCUMENTO, che
     * quel prototipo ce l'ha come tutti gli altri nodi. Nel DOM vero
     * querySelector e' su Document E su Element — al contrario di
     * getElementById, che e' del documento soltanto — quindi qui il posto
     * giusto e' uno solo. */
    metti_metodo(D, D->proto, "querySelector",          m_querySelector);
    metti_metodo(D, D->proto, "querySelectorAll",       m_querySelectorAll);
    metti_metodo(D, D->proto, "matches",                m_matches);
    metti_metodo(D, D->proto, "closest",                m_closest);
    metti_metodo(D, D->proto, "addEventListener",       m_addEventListener);
    metti_metodo(D, D->proto, "removeEventListener",    m_removeEventListener);
    metti_metodo(D, D->proto, "dispatchEvent",          m_dispatchEvent);

    D->proto_stile = exjs_oggetto(js);
    metti_metodo(D, D->proto_stile, "setProperty",      m_setProperty);
    metti_metodo(D, D->proto_stile, "getPropertyValue", m_getPropertyValue);
    metti_metodo(D, D->proto_stile, "removeProperty",   m_removeProperty);
    metti_metodo(D, D->proto_stile, "item",             m_item);

    D->proto_classi = exjs_oggetto(js);
    metti_metodo(D, D->proto_classi, "add",      m_cl_add);
    metti_metodo(D, D->proto_classi, "remove",   m_cl_remove);
    metti_metodo(D, D->proto_classi, "contains", m_cl_contains);
    metti_metodo(D, D->proto_classi, "toggle",   m_cl_toggle);
    metti_metodo(D, D->proto_classi, "item",     m_cl_item);
    metti_metodo(D, D->proto_classi, "replace",  m_cl_replace);

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

    /* ! GLI ASCOLTATORI DELLA FINESTRA VANNO SUL GLOBALE, non sul prototipo
     * dei nodi: `window` E' il globale, e il globale non e' un nodo. Il perche'
     * per esteso sta accanto a m_win_addEventListener. */
    metti_metodo(D, exjs_globale(js), "addEventListener",
                 m_win_addEventListener);
    metti_metodo(D, exjs_globale(js), "removeEventListener",
                 m_win_removeEventListener);

    /* =====================================================================
     * `navigator`
     *
     * ! NON E' UN VEZZO: senza, la pagina dei risultati di google.com muore
     * alla terza riga con «navigator is not defined» e mostra il contenuto
     * del <noscript>. Mezzo web moderno guarda `navigator.userAgent` prima
     * di decidere che cosa disegnare, e chi non risponde non e' un browser
     * sconosciuto: e' un errore.
     *
     * ! E SI DICE LA VERITA', anche quando conviene mentire. `userAgent` e'
     * la stessa stringa che exhttp mette nell'intestazione (vedi
     * lib/exhttp/exhttp.c, dove e' scritta a mano): se le due divergessero,
     * un sito servirebbe una pagina pensata per un browser e la pagina ne
     * troverebbe un altro. `cookieEnabled` e' falso perche' i biscotti non
     * ci sono davvero — dire di si' farebbe fare a un sito un giro che non
     * puo' finire.
     * ================================================================== */
    {
        ExJsVal nv = exjs_oggetto(js);

        exjs_metti(js, nv, "userAgent",  exjs_stringa(js, "EX-OS", -1));
        exjs_metti(js, nv, "appName",    exjs_stringa(js, "Netscape", -1));
        exjs_metti(js, nv, "appVersion", exjs_stringa(js, "5.0 (EX-OS)", -1));
        exjs_metti(js, nv, "appCodeName",exjs_stringa(js, "Mozilla", -1));
        exjs_metti(js, nv, "platform",   exjs_stringa(js, "EX-OS", -1));
        exjs_metti(js, nv, "product",    exjs_stringa(js, "Gecko", -1));
        exjs_metti(js, nv, "vendor",     exjs_stringa(js, "", -1));
        /* ! LA LINGUA E' QUELLA E BASTA, per ora. Il sistema una lingua ce
         * l'ha — sta in kernel.cfg e la rende getenv("lingua") — ma questa
         * libreria gira anche sull'ospite, dove quella chiamata non esiste.
         * Il giorno che serva, la strada e' che il browser la passi, come
         * dovra' passare l'indirizzo della pagina per `location`. */
        exjs_metti(js, nv, "language",   exjs_stringa(js, "en", -1));
        exjs_metti(js, nv, "cookieEnabled", exjs_booleano(0));
        exjs_metti(js, nv, "onLine",        exjs_booleano(1));
        exjs_metti(js, nv, "doNotTrack",    exjs_nullo());
        exjs_metti(js, nv, "maxTouchPoints", exjs_numero(js, 0.0));
        exjs_metti(js, nv, "hardwareConcurrency", exjs_numero(js, 1.0));
        metti_metodo(D, nv, "javaEnabled", m_falso);
        /* ! sendBeacon RENDE FALSO E NON MANDA NIENTE, ed e' la risposta
         * giusta due volte: non sappiamo mandare una richiesta in sottofondo,
         * e chi la usa manda statistiche. Rendere `true` vorrebbe dire dire
         * a una pagina che il messaggio e' partito. */
        metti_metodo(D, nv, "sendBeacon", m_falso);
        exjs_metti(js, exjs_globale(js), "navigator", nv);
    }

    /* `location`, su window E su document: nel DOM e' lo stesso oggetto. */
    D->luogo = exjs_esotico(js, luogo_leggi, luogo_scrivi, D);
    if (exjs_tipo(js, D->luogo) != EXJS_OGGETTO) return 0;
    metti_metodo(D, D->luogo, "assign",   m_loc_assign);
    metti_metodo(D, D->luogo, "replace",  m_loc_assign);
    metti_metodo(D, D->luogo, "reload",   m_loc_reload);
    metti_metodo(D, D->luogo, "toString", m_loc_stringa);
    exjs_metti(js, exjs_globale(js), "location", D->luogo);
    exjs_metti(js, D->documento,     "location", D->luogo);

    /* =====================================================================
     * LE PROMESSE, LE RISPOSTE E XMLHttpRequest
     *
     * ! TRE PROTOTIPI E NON TRE COPIE PER OGGETTO. Una pagina che apre venti
     * richieste altrimenti pagherebbe centoquaranta funzioni native, e in un
     * motore senza raccoglitore di memoria quelle non tornano indietro.
     * ================================================================== */
    D->proto_promessa = exjs_oggetto(js);
    metti_metodo(D, D->proto_promessa, "then",    m_pr_then);
    metti_metodo(D, D->proto_promessa, "catch",   m_pr_catch);
    metti_metodo(D, D->proto_promessa, "finally", m_pr_finally);

    D->proto_risposta = exjs_oggetto(js);
    metti_metodo(D, D->proto_risposta, "text", m_rp_text);
    metti_metodo(D, D->proto_risposta, "json", m_rp_json);

    D->proto_xhr = exjs_oggetto(js);
    metti_metodo(D, D->proto_xhr, "open",                m_xhr_open);
    metti_metodo(D, D->proto_xhr, "send",                m_xhr_send);
    metti_metodo(D, D->proto_xhr, "abort",               m_xhr_abort);
    metti_metodo(D, D->proto_xhr, "setRequestHeader",    m_xhr_header);
    metti_metodo(D, D->proto_xhr, "overrideMimeType",    m_xhr_header);
    metti_metodo(D, D->proto_xhr, "getAllResponseHeaders", m_xhr_tutte);
    metti_metodo(D, D->proto_xhr, "getResponseHeader",   m_xhr_una);
    metti_metodo(D, D->proto_xhr, "addEventListener",    m_xhr_ascolta);

    /* ! IL COSTRUTTORE VUOLE exjs_costruttore E NON exjs_nativa, o sotto
     * QuickJS `new XMLHttpRequest()` e' «not a constructor» mentre sotto ExJs
     * funziona: il perche' sta accanto alla dichiarazione in exjs.h. */
    exjs_metti(js, exjs_globale(js), "XMLHttpRequest",
               exjs_costruttore(js, m_xhr_nuovo, D, "XMLHttpRequest"));
    metti_metodo(D, exjs_globale(js), "fetch", m_fetch);

    D->promessa = promessa_nuova(D, exjs_indefinito(), 1);

    {
        ExJsVal f = exjs_oggetto(js);

        metti_metodo(D, f, "load",    m_fonts_load);
        metti_metodo(D, f, "check",   m_vero);
        metti_metodo(D, f, "add",     m_niente);
        metti_metodo(D, f, "delete",  m_niente);
        metti_metodo(D, f, "clear",   m_niente);
        metti_metodo(D, f, "forEach", m_niente);
        exjs_metti(js, f, "ready",  D->promessa);
        exjs_metti(js, f, "size",   exjs_numero(js, 0.0));
        /* ! «loaded» E NON «loading»: i caratteri che questo browser sa
         * disegnare li ha gia' tutti in memoria, e non ne arriveranno altri.
         * Dire «sto caricando» sarebbe una promessa che non si mantiene. */
        exjs_metti(js, f, "status", exjs_stringa(js, "loaded", -1));
        exjs_metti(js, D->documento, "fonts", f);
    }
    return D;
}
