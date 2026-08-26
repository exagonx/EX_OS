/* =============================================================================
 * tools/prove/domprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il banco di prova del ponte fra l'albero e il motore, che gira SULL'HOST.
 *
 * ! LE PROVE CHE CONTANO SONO QUELLE CHE GUARDANO IL DOCUMENTO, non quelle che
 * guardano il valore reso dallo script. Un ponte rotto rende benissimo dei
 * valori: e' proprio il difetto che gli oggetti esotici sono nati per evitare
 * — `elemento.innerHTML = '<b>x</b>'` che scrive una proprieta' JavaScript,
 * rende la stringa che gli hai dato se la rileggi, e lascia la pagina com'era.
 * Percio' meta' di questo file rimette in marcatore il documento dopo lo
 * script e lo confronta lettera per lettera.
 *
 *     cc -o /tmp/domprova tools/prove/domprova.c lib/exdom/exdom.c \
 *        lib/exhtml/html.c lib/exjs/lex.c lib/exjs/parse.c lib/exjs/val.c \
 *        lib/exjs/run.c lib/exjs/base.c -I lib/exdom -I lib/exjs -I lib/exhtml
 * ============================================================================= */

#include <stdio.h>
#include <string.h>
#include "exdom.h"

#define OGG        4096
#define ARENA      (192 * 1024)
#define NODI       2048
#define ATTR       1024
#define TESTO      (64 * 1024)

static HtmlNodo g_nodi[NODI];
static HtmlAttr g_attr[ATTR];
static char     g_arena[256 * 1024];
static HtmlDoc  g_doc;

static unsigned char g_mem_js[4 << 20];
static unsigned char g_mem_dom[NODI * 32 + TESTO + 4096];
static char          g_ser[TESTO];

static int fatte = 0, sbagliate = 0;

static char g_console[4096];
static unsigned int g_console_n;

static void raccogli(const char *t, unsigned int n, void *dato)
{
    (void)dato;
    if (g_console_n + n < sizeof(g_console)) {
        memcpy(g_console + g_console_n, t, n);
        g_console_n += n;
        g_console[g_console_n] = '\0';
    }
}

/* Apre tutto da capo: documento, motore, ponte. Ogni prova parte pulita,
 * perche' una prova che eredita lo stato della precedente prima o poi passa
 * per il motivo sbagliato. */
static ExDom *apparecchia(const char *html, ExJsCtx **fuori)
{
    ExJsCtx *c;
    ExDom   *D;

    html_prepara(&g_doc, g_nodi, NODI, g_attr, ATTR, g_arena, sizeof(g_arena));
    html_analizza(&g_doc, html, (unsigned int)strlen(html));

    c = exjs_apri(g_mem_js, sizeof(g_mem_js), OGG, ARENA);
    if (!c) return 0;
    g_console_n = 0; g_console[0] = '\0';
    exjs_uscita_metti(c, raccogli, 0);

    D = exdom_apri(g_mem_dom, sizeof(g_mem_dom), c, &g_doc, NODI, TESTO);
    *fuori = c;
    return D;
}

static int gira(ExJsCtx *c, const char *script, const char *nome)
{
    ExJsErrore err;
    ExJsVal    r;

    memset(&err, 0, sizeof(err));
    if (!exjs_esegui(c, script, (unsigned int)strlen(script), &r, &err)) {
        printf("NO   %-38s riga %d: %s\n", nome, err.riga, err.messaggio);
        return 0;
    }
    return 1;
}

/* Guarda il valore dell'ultima espressione. */
static void prova_val(const char *nome, const char *html, const char *script,
                      const char *atteso)
{
    ExJsCtx    *c;
    ExDom      *D;
    ExJsErrore  err;
    ExJsVal     r;
    const char *s;

    fatte++;
    D = apparecchia(html, &c);
    if (!D) { printf("NO   %-38s il ponte non si apre\n", nome); sbagliate++; return; }

    memset(&err, 0, sizeof(err));
    if (!exjs_esegui(c, script, (unsigned int)strlen(script), &r, &err)) {
        printf("NO   %-38s riga %d: %s\n", nome, err.riga, err.messaggio);
        sbagliate++;
        return;
    }
    s = exjs_a_stringa(c, r);
    if (strcmp(s, atteso) != 0) {
        printf("NO   %-38s atteso \"%s\", trovato \"%s\"\n", nome, atteso, s);
        sbagliate++;
        return;
    }
    printf("ok   %-38s %s\n", nome, s);
}

/* Guarda il DOCUMENTO dopo lo script: e' la prova che il ponte tocca davvero
 * l'albero e non un oggetto JavaScript che gli somiglia. */
/* ! NELLE ATTESE NON C'E' `<html><body>` SE IL DOCUMENTO NON CE L'AVEVA, e la
 * prima stesura di questo file lo dava per scontato — sbagliando quattordici
 * prove su un motore che funzionava. L'analizzatore di exhtml non inventa gli
 * elementi impliciti che il browser vero costruisce; costruisce l'albero di
 * quel che gli e' arrivato. Il posto per rimediare, il giorno che serva, e'
 * l'analizzatore, e allora saranno queste attese a cambiare. */
static void prova_doc(const char *nome, const char *html, const char *script,
                      const char *atteso)
{
    ExJsCtx *c;
    ExDom   *D;

    fatte++;
    D = apparecchia(html, &c);
    if (!D) { printf("NO   %-38s il ponte non si apre\n", nome); sbagliate++; return; }
    if (!gira(c, script, nome)) { sbagliate++; return; }

    html_serializza(&g_doc, g_doc.radice, 0, g_ser, sizeof(g_ser));
    if (strcmp(g_ser, atteso) != 0) {
        printf("NO   %-38s atteso \"%s\"\n%-43s trovato \"%s\"\n",
               nome, atteso, "", g_ser);
        sbagliate++;
        return;
    }
    printf("ok   %-38s %s\n", nome, g_ser);
}

/* Guarda cio' che lo script ha stampato. */
static void prova_stampa(const char *nome, const char *html, const char *script,
                         const char *atteso)
{
    ExJsCtx *c;
    ExDom   *D;

    fatte++;
    D = apparecchia(html, &c);
    if (!D) { printf("NO   %-38s il ponte non si apre\n", nome); sbagliate++; return; }
    if (!gira(c, script, nome)) { sbagliate++; return; }

    if (strcmp(g_console, atteso) != 0) {
        printf("NO   %-38s atteso \"%s\", stampato \"%s\"\n",
               nome, atteso, g_console);
        sbagliate++;
        return;
    }
    printf("ok   %-38s %s\n", nome, g_console);
}

static void ok(const char *nome, int cond, const char *dettaglio)
{
    fatte++;
    printf("%s   %-38s %s\n", cond ? "ok" : "NO", nome, dettaglio);
    if (!cond) sbagliate++;
}

/* -----------------------------------------------------------------------------
 * I documenti di prova
 * --------------------------------------------------------------------------- */
static const char *PAG =
    "<html><head><title>Titolo</title></head>"
    "<body><div id=\"uno\" class=\"grosso rosso\">ciao</div>"
    "<p class=\"rosso\">testo</p><span>fine</span></body></html>";

int main(void)
{
    printf("\n--- il documento e la navigazione ---\n");

    prova_val("document c'e'", PAG, "typeof document", "object");
    prova_val("e' un nodo",    PAG, "document.nodeType", "1");
    prova_val("window e' il globale", PAG,
              "window.document === document", "true");
    prova_val("il padre della radice e' null", PAG,
              "document.parentNode === null", "true");
    prova_val("documentElement", PAG, "document.documentElement.nodeName", "HTML");
    prova_val("body", PAG, "document.body.nodeName", "BODY");
    prova_val("head", PAG, "document.head.nodeName", "HEAD");
    prova_val("il titolo si legge", PAG, "document.title", "Titolo");

    prova_val("getElementById", PAG,
              "document.getElementById('uno').tagName", "DIV");
    prova_val("un id che non c'e' e' null", PAG,
              "document.getElementById('zero') === null", "true");
    prova_val("lo stesso nodo, lo stesso oggetto", PAG,
              "document.getElementById('uno') === document.getElementById('uno')",
              "true");
    prova_val("un elemento tiene le sue cose", PAG,
              "var a = document.getElementById('uno'); a.mioStato = 7;"
              "document.getElementById('uno').mioStato", "7");

    prova_val("getElementsByTagName", PAG,
              "document.getElementsByTagName('p').length", "1");
    prova_val("la stella li prende tutti", PAG,
              "document.getElementsByTagName('*').length >= 6", "true");
    prova_val("il nome del tag non guarda le maiuscole", PAG,
              "document.getElementsByTagName('DIV').length", "1");
    prova_val("getElementsByClassName", PAG,
              "document.getElementsByClassName('rosso').length", "2");
    prova_val("una classe in mezzo alle altre", PAG,
              "document.getElementsByClassName('grosso')[0].id", "uno");
    prova_val("una classe che non c'e'", PAG,
              "document.getElementsByClassName('ross').length", "0");

    printf("\n--- i fratelli e i figli ---\n");

    prova_val("firstChild del body", PAG,
              "document.body.firstChild.tagName", "DIV");
    prova_val("nextSibling", PAG,
              "document.getElementById('uno').nextSibling.tagName", "P");
    prova_val("previousSibling", PAG,
              "document.body.lastChild.previousSibling.tagName", "P");
    prova_val("previousSibling del primo e' null", PAG,
              "document.body.firstChild.previousSibling === null", "true");
    prova_val("childNodes", PAG, "document.body.childNodes.length", "3");
    prova_val("il testo e' un nodo", PAG,
              "document.getElementById('uno').firstChild.nodeType", "3");
    prova_val("nodeName di un testo", PAG,
              "document.getElementById('uno').firstChild.nodeName", "#text");
    prova_val("il dato di un testo", PAG,
              "document.getElementById('uno').firstChild.data", "ciao");
    prova_val("children salta i testi", PAG,
              "document.getElementById('uno').children.length", "0");
    prova_val("hasChildNodes", PAG,
              "document.getElementById('uno').hasChildNodes()", "true");

    printf("\n--- gli attributi ---\n");

    prova_val("getAttribute", PAG,
              "document.getElementById('uno').getAttribute('class')",
              "grosso rosso");
    prova_val("un attributo che non c'e' e' null", PAG,
              "document.getElementById('uno').getAttribute('href') === null",
              "true");
    prova_val("ma .id assente e' la stringa vuota", PAG,
              "document.body.id === ''", "true");
    prova_val("hasAttribute", PAG,
              "document.getElementById('uno').hasAttribute('id')", "true");
    prova_val("className", PAG,
              "document.getElementById('uno').className", "grosso rosso");

    prova_doc("setAttribute cambia l'albero",
              "<p>x</p>",
              "document.getElementsByTagName('p')[0].setAttribute('id','q')",
              "<p id=\"q\">x</p>");
    prova_doc("il nome dell'attributo si abbassa",
              "<p>x</p>",
              "document.getElementsByTagName('p')[0].setAttribute('ID','q')",
              "<p id=\"q\">x</p>");
    prova_doc("removeAttribute",
              "<p id=\"q\">x</p>",
              "document.getElementsByTagName('p')[0].removeAttribute('id')",
              "<p>x</p>");
    prova_doc("scrivere .id e' scrivere l'attributo",
              "<p>x</p>",
              "document.getElementsByTagName('p')[0].id = 'q'",
              "<p id=\"q\">x</p>");
    prova_doc("scrivere .className",
              "<p>x</p>",
              "document.getElementsByTagName('p')[0].className = 'a b'",
              "<p class=\"a b\">x</p>");

    printf("\n--- innerHTML: la prova che il ponte e' vero ---\n");

    prova_val("innerHTML si legge", PAG,
              "document.getElementById('uno').innerHTML", "ciao");
    prova_val("e legge anche i tag", "<div><b>x</b>y</div>",
              "document.getElementsByTagName('div')[0].innerHTML", "<b>x</b>y");
    prova_val("outerHTML", "<div><b>x</b></div>",
              "document.getElementsByTagName('div')[0].outerHTML",
              "<div><b>x</b></div>");

    prova_doc("innerHTML cambia il DOCUMENTO",
              "<div>vecchio</div>",
              "document.getElementsByTagName('div')[0].innerHTML = '<b>nuovo</b>'",
              "<div><b>nuovo</b></div>");
    prova_doc("innerHTML svuota prima",
              "<div><i>a</i><i>b</i></div>",
              "document.getElementsByTagName('div')[0].innerHTML = 'solo'",
              "<div>solo</div>");
    prova_val("e rileggendolo si trova il nuovo", "<div>vecchio</div>",
              "var d = document.getElementsByTagName('div')[0];"
              "d.innerHTML = '<b>nuovo</b>'; d.innerHTML", "<b>nuovo</b>");
    prova_val("il nuovo albero e' navigabile", "<div>vecchio</div>",
              "var d = document.getElementsByTagName('div')[0];"
              "d.innerHTML = '<b>nuovo</b>'; d.firstChild.tagName", "B");

    printf("\n--- textContent NON analizza, ed e' il punto ---\n");

    prova_val("textContent raccoglie tutto", "<div><b>ci</b>ao</div>",
              "document.getElementsByTagName('div')[0].textContent", "ciao");
    prova_doc("scriverlo mette del TESTO",
              "<div><b>x</b></div>",
              "document.getElementsByTagName('div')[0].textContent = 'ciao'",
              "<div>ciao</div>");
    prova_doc("e un tag scritto li' resta testo",
              "<div>x</div>",
              "document.getElementsByTagName('div')[0].textContent = '<script>'",
              "<div>&lt;script&gt;</div>");
    prova_val("infatti non nasce nessun elemento", "<div>x</div>",
              "var d = document.getElementsByTagName('div')[0];"
              "d.textContent = '<b>x</b>'; d.children.length", "0");
    prova_doc("il testo di un nodo di testo",
              "<p>vecchio</p>",
              "document.getElementsByTagName('p')[0].firstChild.data = 'nuovo'",
              "<p>nuovo</p>");

    printf("\n--- costruire e spostare ---\n");

    prova_doc("createElement + appendChild",
              "<div></div>",
              "var e = document.createElement('span');"
              "e.appendChild(document.createTextNode('ciao'));"
              "document.getElementsByTagName('div')[0].appendChild(e)",
              "<div><span>ciao</span></div>");
    prova_val("createElement abbassa il nome", "<div></div>",
              "document.createElement('SPAN').tagName", "SPAN");
    prova_val("appendChild rende il figlio", "<div></div>",
              "var e = document.createElement('b');"
              "document.getElementsByTagName('div')[0].appendChild(e) === e",
              "true");
    prova_doc("insertBefore",
              "<div><i>b</i></div>",
              "var d = document.getElementsByTagName('div')[0];"
              "d.insertBefore(document.createElement('u'), d.firstChild)",
              "<div><u></u><i>b</i></div>");
    prova_doc("removeChild",
              "<div><i>a</i><u>b</u></div>",
              "var d = document.getElementsByTagName('div')[0];"
              "d.removeChild(d.firstChild)",
              "<div><u>b</u></div>");
    prova_val("removeChild col padre sbagliato rende null",
              "<div><i>a</i></div><p></p>",
              "var p = document.getElementsByTagName('p')[0];"
              "var i = document.getElementsByTagName('i')[0];"
              "p.removeChild(i) === null", "true");
    prova_doc("appendChild SPOSTA",
              "<div><i>a</i></div><p></p>",
              "var p = document.getElementsByTagName('p')[0];"
              "p.appendChild(document.getElementsByTagName('i')[0])",
              "<div></div><p><i>a</i></p>");
    prova_val("un ciclo si rifiuta", "<div><i>a</i></div>",
              "var d = document.getElementsByTagName('div')[0];"
              "var i = document.getElementsByTagName('i')[0];"
              "i.appendChild(d) === null", "true");

    printf("\n--- il titolo, gli elenchi, la versione ---\n");

    prova_doc("scrivere document.title", PAG,
              "document.title = 'Nuovo'",
              "<html><head><title>Nuovo</title></head>"
              "<body><div id=\"uno\" class=\"grosso rosso\">ciao</div>"
              "<p class=\"rosso\">testo</p><span>fine</span></body></html>");
    prova_stampa("ciclare su un elenco", PAG,
                 "var v = document.getElementsByClassName('rosso');"
                 "for (var i = 0; i < v.length; i++) console.log(v[i].tagName);",
                 "DIV\nP\n");
    prova_stampa("costruire una lista", "<ul></ul>",
                 "var u = document.getElementsByTagName('ul')[0];"
                 "for (var i = 1; i <= 3; i++) {"
                 "  var li = document.createElement('li');"
                 "  li.textContent = 'riga ' + i;"
                 "  u.appendChild(li);"
                 "}"
                 "console.log(u.children.length + ':' + u.innerHTML);",
                 "3:<li>riga 1</li><li>riga 2</li><li>riga 3</li>\n");

    /* ! LA VERSIONE E' IL MOTIVO PER CUI IL BROWSER PUO' SAPERE SE
     * RIMPAGINARE, e va provata di qui: dall'albero si vedeva gia' salire
     * chiamando le funzioni a mano, ma la domanda vera e' se sale quando a
     * toccare il documento e' uno script. */
    {
        ExJsCtx     *c;
        ExDom       *D;
        unsigned int prima, dopo, ferma;

        D = apparecchia("<div>x</div>", &c);
        prima = html_versione(&g_doc);
        gira(c, "document.getElementsByTagName('div')[0].innerHTML = 'y'", "versione");
        dopo = html_versione(&g_doc);
        ok("uno script alza la versione", dopo > prima, "");

        gira(c, "var q = document.getElementsByTagName('div')[0].innerHTML", "versione");
        ferma = html_versione(&g_doc);
        ok("leggere non la alza", ferma == dopo, "");
        (void)D;
    }

    printf("\n%d prove, %d sbagliate\n\n", fatte, sbagliate);
    return sbagliate ? 1 : 0;
}
