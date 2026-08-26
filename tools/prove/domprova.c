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
#include <stdlib.h>
#include "exdom.h"

#define OGG        4096
#define ARENA      (192 * 1024)
#define NODI       2048
#define ATTR       1024
#define TESTO      (64 * 1024)
#define ASCOLTI    64

static HtmlNodo g_nodi[NODI];
static HtmlAttr g_attr[ATTR];
static char     g_arena[256 * 1024];
static HtmlDoc  g_doc;

static unsigned char g_mem_js[4 << 20];
static unsigned char g_mem_dom[NODI * 32 + ASCOLTI * 64 + TESTO + 4096];
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

    D = exdom_apri(g_mem_dom, sizeof(g_mem_dom), c, &g_doc, NODI, TESTO,
                   ASCOLTI);
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

/* L'indice di un nodo per id: il banco ne ha bisogno perche' exdom_evento
 * parla di NODI, non di involucri — e' il browser che chiama, e il browser sa
 * dove ha cliccato. */
static int trova_id(HtmlDoc *d, const char *id)
{
    unsigned int i;

    for (i = 0; i < d->nodi_n; i++) {
        const char *v = html_attr(d, (int)i, "id");
        if (v && strcmp(v, id) == 0) return (int)i;
    }
    return -1;
}

/* Registra i gestori con `script`, poi fa partire `tipo` sul nodo con
 * quell'id, DA FUORI, e confronta cio' che e' stato stampato e la risposta di
 * exdom_evento (1 = si puo' procedere, 0 = preventDefault). */
static void prova_evento(const char *nome, const char *html, const char *id,
                         const char *tipo, const char *script,
                         const char *atteso, int proseguire)
{
    ExJsCtx *c;
    ExDom   *D;
    int      n, r;

    fatte++;
    D = apparecchia(html, &c);
    if (!D) { printf("NO   %-38s il ponte non si apre\n", nome); sbagliate++; return; }
    if (script && script[0] && !gira(c, script, nome)) { sbagliate++; return; }

    n = trova_id(&g_doc, id);
    if (n < 0) { printf("NO   %-38s nessun nodo con id %s\n", nome, id); sbagliate++; return; }

    g_console_n = 0; g_console[0] = '\0';
    r = exdom_evento(D, n, tipo, 0);

    if (strcmp(g_console, atteso) != 0) {
        printf("NO   %-38s atteso \"%s\", stampato \"%s\"\n",
               nome, atteso, g_console);
        sbagliate++;
        return;
    }
    if (r != proseguire) {
        printf("NO   %-38s exdom_evento ha reso %d invece di %d\n",
               nome, r, proseguire);
        sbagliate++;
        return;
    }
    printf("ok   %-38s %s\n", nome, atteso[0] ? atteso : "(muto)");
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

/* -----------------------------------------------------------------------------
 * La pagina vera
 *
 * ! SI FA GIRARE UNA PAGINA DEL SITO DI PROVA SENZA ACCENDERE QEMU, e non e'
 * pigrizia: un giro dentro la macchina virtuale costa minuti e mostra una
 * schermata, questo costa un istante e dice quale nodo e' venuto storto. Il
 * browser fa esattamente queste tre cose in questo ordine — apre il ponte,
 * esegue gli <script> nell'ordine del documento, poi impagina — quindi cio'
 * che si vede qui e' cio' che si vedra' li'.
 * --------------------------------------------------------------------------- */
static int pagina(const char *nomefile)
{
    static char  testo[512 * 1024];
    FILE        *fp = fopen(nomefile, "rb");
    unsigned int n;
    ExJsCtx     *c;
    ExDom       *D;
    int          i, fatti = 0;

    if (!fp) { printf("non trovo %s\n", nomefile); return 1; }
    n = (unsigned int)fread(testo, 1, sizeof(testo) - 1, fp);
    testo[n] = '\0';
    fclose(fp);

    html_prepara(&g_doc, g_nodi, NODI, g_attr, ATTR, g_arena, sizeof(g_arena));
    html_analizza(&g_doc, testo, n);

    c = exjs_apri(g_mem_js, sizeof(g_mem_js), OGG, ARENA);
    if (!c) { printf("il motore non si apre\n"); return 1; }
    g_console_n = 0; g_console[0] = '\0';
    exjs_uscita_metti(c, raccogli, 0);

    D = exdom_apri(g_mem_dom, sizeof(g_mem_dom), c, &g_doc, NODI, TESTO, ASCOLTI);
    if (!D) { printf("il ponte non si apre\n"); return 1; }

    for (i = 0; i < (int)g_doc.nodi_n; i++) {
        int f;

        if (g_doc.nodi[i].tipo != HTML_ELEMENTO) continue;
        if (strcmp(html_nome(&g_doc, i), "script") != 0) continue;

        for (f = g_doc.nodi[i].primo_figlio; f >= 0; f = g_doc.nodi[f].prossimo) {
            ExJsErrore err;
            ExJsVal    r;
            const char *t;

            if (g_doc.nodi[f].tipo != HTML_TESTO) continue;
            t = html_testo(&g_doc, f);
            if (!t[0]) continue;

            memset(&err, 0, sizeof(err));
            if (!exjs_esegui(c, t, (unsigned int)strlen(t), &r, &err))
                printf("!! script, riga %d: %s\n", err.riga, err.messaggio);
            fatti++;
        }
    }

    printf("script eseguiti: %d\n", fatti);
    if (g_console[0]) printf("console: %s", g_console);

    /* I tempi: si pompano come farebbe il ciclo dei messaggi del browser. */
    if (exjs_lavori_in_attesa(c)) {
        exjs_pompa(c, 5000);
        printf("dopo i tempi, versione %u\n", html_versione(&g_doc));
    }

    /* ! E SI PREME QUEL CHE C'E' DA PREMERE. Una pagina di prova che non venga
     * mai toccata non dice niente sui gestori — e i gestori sono meta' del
     * motivo per cui il JavaScript esiste. Si cercano gli elementi che hanno
     * un `id` e un gestore, e si fa partire il clic da fuori, come fa il
     * browser dal suo ciclo di messaggi. */
    for (i = 0; i < (int)g_doc.nodi_n; i++) {
        const char *id = html_attr(&g_doc, (int)i, "id");
        int         seguire;

        if (g_doc.nodi[i].tipo != HTML_ELEMENTO || !id) continue;
        if (!html_attr(&g_doc, i, "onclick") &&
            strcmp(html_nome(&g_doc, i), "a") != 0) continue;

        g_console_n = 0; g_console[0] = '\0';
        seguire = exdom_evento(D, i, "click", 0);
        printf("clic su #%-10s  %s\n", id,
               seguire ? "il browser prosegue" : "preventDefault: fermo");
    }

    printf("\n--- il documento dopo gli script ---\n");
    html_serializza(&g_doc, g_doc.radice, 0, g_ser, sizeof(g_ser));
    printf("%s\n", g_ser);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2) return pagina(argv[1]);

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

    printf("\n--- gli eventi ---\n");

    /* ! GLI EVENTI SI PROVANO DA FUORI, come li fa partire il browser: prima
     * uno script registra i gestori, poi il banco chiama exdom_evento() con
     * nessuno script in corso. E' proprio quel momento — motore fermo, evento
     * che arriva — che ha fatto nascere exjs_invoca, e provarlo facendo
     * partire l'evento da dentro uno script non avrebbe dimostrato niente. */
    prova_evento("un gestore gira", PAG, "uno", "click",
                 "document.getElementById('uno').addEventListener('click',"
                 "  function () { console.log('preso'); });",
                 "preso\n", 1);
    prova_evento("onclick gira", PAG, "uno", "click",
                 "document.getElementById('uno').onclick ="
                 "  function () { console.log('preso'); };",
                 "preso\n", 1);
    prova_evento("onclick si sostituisce", PAG, "uno", "click",
                 "var e = document.getElementById('uno');"
                 "e.onclick = function () { console.log('vecchio'); };"
                 "e.onclick = function () { console.log('nuovo'); };",
                 "nuovo\n", 1);
    prova_evento("onclick = null lo toglie", PAG, "uno", "click",
                 "var e = document.getElementById('uno');"
                 "e.onclick = function () { console.log('c\\'e\\''); };"
                 "e.onclick = null;",
                 "", 1);
    prova_val("onclick si rilegge", PAG,
              "var e = document.getElementById('uno');"
              "e.onclick = function () { return 1; };"
              "typeof e.onclick", "function");
    prova_val("senza gestore e' null", PAG,
              "document.getElementById('uno').onclick === null", "true");
    prova_val("un nome che non e' un gestore resta suo", PAG,
              "var e = document.getElementById('uno'); e.onda = 3; e.onda", "3");

    prova_evento("due gestori, in ordine", PAG, "uno", "click",
                 "var e = document.getElementById('uno');"
                 "e.addEventListener('click', function () { console.log('a'); });"
                 "e.addEventListener('click', function () { console.log('b'); });",
                 "a\nb\n", 1);
    prova_evento("lo stesso gestore due volte gira una volta", PAG, "uno", "click",
                 "function f() { console.log('x'); }"
                 "var e = document.getElementById('uno');"
                 "e.addEventListener('click', f);"
                 "e.addEventListener('click', f);",
                 "x\n", 1);
    prova_evento("removeEventListener", PAG, "uno", "click",
                 "function f() { console.log('x'); }"
                 "var e = document.getElementById('uno');"
                 "e.addEventListener('click', f);"
                 "e.removeEventListener('click', f);",
                 "", 1);
    prova_evento("un altro tipo non risponde", PAG, "uno", "mousedown",
                 "document.getElementById('uno').addEventListener('click',"
                 "  function () { console.log('x'); });",
                 "", 1);

    printf("\n--- la propagazione ---\n");

    prova_evento("l'evento risale", PAG, "uno", "click",
                 "document.body.addEventListener('click',"
                 "  function () { console.log('body'); });",
                 "body\n", 1);
    prova_evento("prima il bersaglio, poi i padri", PAG, "uno", "click",
                 "document.getElementById('uno').addEventListener('click',"
                 "  function () { console.log('div'); });"
                 "document.body.addEventListener('click',"
                 "  function () { console.log('body'); });",
                 "div\nbody\n", 1);
    prova_evento("la cattura scende prima", PAG, "uno", "click",
                 "document.body.addEventListener('click',"
                 "  function () { console.log('giu'); }, true);"
                 "document.getElementById('uno').addEventListener('click',"
                 "  function () { console.log('bersaglio'); });"
                 "document.body.addEventListener('click',"
                 "  function () { console.log('su'); });",
                 "giu\nbersaglio\nsu\n", 1);
    prova_evento("stopPropagation ferma la risalita", PAG, "uno", "click",
                 "document.getElementById('uno').addEventListener('click',"
                 "  function (ev) { console.log('div'); ev.stopPropagation(); });"
                 "document.body.addEventListener('click',"
                 "  function () { console.log('body'); });",
                 "div\n", 1);
    prova_evento("preventDefault si vede da fuori", PAG, "uno", "click",
                 "document.getElementById('uno').addEventListener('click',"
                 "  function (ev) { ev.preventDefault(); });",
                 "", 0);
    prova_evento("target e currentTarget", PAG, "uno", "click",
                 "document.body.addEventListener('click', function (ev) {"
                 "  console.log(ev.target.tagName + ' ' + ev.currentTarget.tagName"
                 "              + ' ' + ev.type); });",
                 "DIV BODY click\n", 1);
    prova_evento("this e' l'elemento del gestore", PAG, "uno", "click",
                 "document.body.addEventListener('click',"
                 "  function () { console.log(this.tagName); });",
                 "BODY\n", 1);

    printf("\n--- i gestori scritti nell'attributo ---\n");

    prova_evento("onclick nel marcatore",
                 "<div id=\"uno\" onclick=\"console.log('attributo')\">x</div>",
                 "uno", "click", "", "attributo\n", 1);
    prova_evento("e vede window.event",
                 "<div id=\"uno\" onclick=\"console.log(event.type)\">x</div>",
                 "uno", "click", "", "click\n", 1);
    prova_evento("l'attributo gira prima dei registrati",
                 "<div id=\"uno\" onclick=\"console.log('attr')\">x</div>",
                 "uno", "click",
                 "document.getElementById('uno').addEventListener('click',"
                 "  function () { console.log('reg'); });",
                 "attr\nreg\n", 1);
    prova_evento("puo' anche lui annullare il seguito",
                 "<div id=\"uno\" onclick=\"event.preventDefault()\">x</div>",
                 "uno", "click", "", "", 0);

    printf("\n--- quel che uno script rotto non deve rompere ---\n");

    prova_evento("un gestore rotto non ferma gli altri", PAG, "uno", "click",
                 "var e = document.getElementById('uno');"
                 "e.addEventListener('click', function () { nonEsiste(); });"
                 "e.addEventListener('click', function () { console.log('dopo'); });",
                 "dopo\n", 1);
    prova_evento("un gestore che cambia la pagina", PAG, "uno", "click",
                 "document.getElementById('uno').addEventListener('click',"
                 "  function (ev) { ev.target.innerHTML = '<b>fatto</b>'; });",
                 "", 1);
    prova_val("dispatchEvent da dentro uno script", PAG,
              "var n = 0;"
              "var e = document.getElementById('uno');"
              "e.addEventListener('pippo', function () { n = n + 1; });"
              "e.dispatchEvent('pippo'); n", "1");

    {
        ExJsCtx *c;
        ExDom   *D;

        D = apparecchia(PAG, &c);
        gira(c, "document.getElementById('uno').addEventListener('click',"
                "  function (ev) { ev.target.innerHTML = 'nuovo'; });", "muta");
        exdom_evento(D, 0, "click", 0);
        exdom_evento(D, trova_id(&g_doc, "uno"), "click", 0);
        html_serializza(&g_doc, trova_id(&g_doc, "uno"), 0, g_ser, sizeof(g_ser));
        ok("il gestore ha cambiato l'albero", strcmp(g_ser, "nuovo") == 0, g_ser);
        ok("e nessun ascoltatore e' andato perso", exdom_perso(D) == 0, "");
    }

    /* ! LA SPIA SI PROVA, altrimenti marcisce. Un `exdom_perso` che non
     * diventa mai 1 nemmeno quando il posto finisce e' peggio di non averlo:
     * il browser lo guarderebbe e si fiderebbe. */
    {
        ExJsCtx *c;
        ExDom   *D;
        char     codice[256];
        int      i;

        D = apparecchia(PAG, &c);
        gira(c, "var e = document.getElementById('uno'); var v = [];", "pieno");
        for (i = 0; i < ASCOLTI + 4; i++) {
            sprintf(codice, "v[%d] = function () { }; "
                            "e.addEventListener('click', v[%d]);", i, i);
            gira(c, codice, "pieno");
        }
        ok("il posto finito si dice", exdom_perso(D) == 1, "");

        D = apparecchia(PAG, &c);
        gira(c, "document.getElementById('uno').addEventListener("
                "'unNomeDiEventoMoltoMaMoltoLungoDavvero', function () { });",
                "lungo");
        ok("e un nome troppo lungo si rifiuta", exdom_perso(D) == 1, "");
    }

    printf("\n%d prove, %d sbagliate\n\n", fatte, sbagliate);
    return sbagliate ? 1 : 0;
}
