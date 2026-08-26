/* =============================================================================
 * tools/prove/htmlprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il banco di prova del lettore HTML, che gira SULL'HOST.
 *
 * ! I CASI CHE CONTANO SONO QUELLI MALFATTI. Un documento ben formato lo
 * leggerebbe anche un parser XML; quello che distingue un lettore HTML e' cosa
 * fa con «<b><i>x</b>», con i <li> mai chiusi, con un «a < b» dentro il
 * JavaScript. Su una pagina vera quei casi non sono l'eccezione.
 *
 *     cc -o /tmp/htmlprova tools/prove/htmlprova.c lib/exhtml/html.c \
 *        -I lib/exhtml
 *     /tmp/htmlprova            i casi
 *     /tmp/htmlprova <file>     l'albero di un file vero
 * ============================================================================= */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "html.h"

static HtmlNodo g_nodi[4096];
static HtmlAttr g_attr[2048];
static char     g_arena[256 * 1024];
static HtmlDoc  g_doc;

static int errori = 0;

static void ok(int cond, const char *cosa)
{
    printf("  [%s] %s\n", cond ? " ok " : "NO  ", cosa);
    if (!cond) errori++;
}

static void leggi(const char *s)
{
    html_prepara(&g_doc, g_nodi, 4096, g_attr, 2048, g_arena, sizeof(g_arena));
    html_analizza(&g_doc, s, (unsigned int)strlen(s));
}

/* Cerca il primo elemento con quel nome; -1 se non c'e'. */
static int trova(const char *nome)
{
    unsigned int i;

    for (i = 0; i < g_doc.nodi_n; i++)
        if (g_doc.nodi[i].tipo == HTML_ELEMENTO &&
            strcmp(html_nome(&g_doc, (int)i), nome) == 0) return (int)i;
    return -1;
}

/* Concatena tutto il testo sotto un nodo. */
static void testo_sotto(int nodo, char *out, unsigned int max)
{
    int f;

    if (nodo < 0) return;
    if (g_doc.nodi[nodo].tipo == HTML_TESTO) {
        const char *t = html_testo(&g_doc, nodo);

        if (strlen(out) + strlen(t) + 1 < max) strcat(out, t);
        return;
    }
    for (f = g_doc.nodi[nodo].primo_figlio; f >= 0; f = g_doc.nodi[f].prossimo)
        testo_sotto(f, out, max);
}

static int profondita(int nodo)
{
    int p = 0;

    while (nodo > 0) { nodo = g_doc.nodi[nodo].padre; p++; }
    return p;
}

static void stampa(int nodo, int liv)
{
    int f, i;

    for (i = 0; i < liv; i++) printf("  ");
    if (g_doc.nodi[nodo].tipo == HTML_TESTO) {
        const char *t = html_testo(&g_doc, nodo);
        printf("\"%.60s\"%s\n", t, strlen(t) > 60 ? "..." : "");
    } else {
        const char *h = html_attr(&g_doc, nodo, "href");
        printf("<%s>%s%s\n", html_nome(&g_doc, nodo), h ? " href=" : "",
               h ? h : "");
    }
    for (f = g_doc.nodi[nodo].primo_figlio; f >= 0; f = g_doc.nodi[f].prossimo)
        stampa(f, liv + 1);
}

int main(int argc, char **argv)
{
    char buf[8192];

    /* Un file vero: si stampa l'albero e basta. */
    if (argc > 1) {
        static char d[2 * 1024 * 1024];
        FILE *f = fopen(argv[1], "rb");
        size_t n;

        if (!f) { fprintf(stderr, "non leggo %s\n", argv[1]); return 2; }
        n = fread(d, 1, sizeof(d) - 1, f);
        fclose(f);
        d[n] = '\0';

        html_prepara(&g_doc, g_nodi, 4096, g_attr, 2048, g_arena, sizeof(g_arena));
        html_analizza(&g_doc, d, (unsigned int)n);
        printf("%u nodi, %u attributi, %u byte di arena%s\n",
               g_doc.nodi_n, g_doc.attr_n, g_doc.arena_n,
               g_doc.troncato ? "  (TRONCATO)" : "");
        stampa(g_doc.radice, 0);
        return 0;
    }

    printf("BASE\n");
    leggi("<html><body><p>Ciao</p></body></html>");
    ok(trova("html") > 0 && trova("body") > 0 && trova("p") > 0,
       "html, body, p ci sono");
    buf[0] = '\0'; testo_sotto(trova("p"), buf, sizeof(buf));
    ok(strcmp(buf, "Ciao") == 0, "il testo del p");

    leggi("<DIV CLASS=x>A</DIV>");
    ok(trova("div") > 0, "il nome del tag va in minuscolo");
    ok(html_attr(&g_doc, trova("div"), "CLASS") != 0 &&
       strcmp(html_attr(&g_doc, trova("div"), "class"), "x") == 0,
       "l'attributo si trova comunque lo si scriva");

    printf("SPAZI E <pre>\n");
    leggi("<p>a   \n\t b</p>");
    buf[0] = '\0'; testo_sotto(trova("p"), buf, sizeof(buf));
    ok(strcmp(buf, "a b") == 0, "fuori da <pre> gli spazi si riducono a uno");

    leggi("<pre>a   \n\tb</pre>");
    buf[0] = '\0'; testo_sotto(trova("pre"), buf, sizeof(buf));
    ok(strcmp(buf, "a   \n\tb") == 0, "dentro <pre> restano tali e quali");

    leggi("<pre>a\nb</pre><p>c   d</p>");
    buf[0] = '\0'; testo_sotto(trova("p"), buf, sizeof(buf));
    ok(strcmp(buf, "c d") == 0, "e dopo il </pre> si torna a ridurli");

    leggi("<pre>x  <code>y  z</code>  w</pre>");
    buf[0] = '\0'; testo_sotto(trova("pre"), buf, sizeof(buf));
    ok(strcmp(buf, "x  y  z  w") == 0, "vale anche dentro i figli del <pre>");

    printf("ATTRIBUTI\n");
    leggi("<a href=\"/x?a=1&amp;b=2\" title='c\"d' hidden>t</a>");
    ok(strcmp(html_attr(&g_doc, trova("a"), "href"), "/x?a=1&b=2") == 0,
       "entita' sciolte dentro un attributo");
    ok(strcmp(html_attr(&g_doc, trova("a"), "title"), "c\"d") == 0,
       "apici singoli, con doppi dentro");
    ok(html_attr(&g_doc, trova("a"), "hidden") != 0 &&
       html_attr(&g_doc, trova("a"), "hidden")[0] == '\0',
       "un attributo senza valore c'e' e vale stringa vuota");

    leggi("<td width=100 bgcolor=#fff>x</td>");
    ok(strcmp(html_attr(&g_doc, trova("td"), "width"), "100") == 0 &&
       strcmp(html_attr(&g_doc, trova("td"), "bgcolor"), "#fff") == 0,
       "valori SENZA virgolette");

    printf("ENTITA'\n");
    leggi("<p>&lt;tag&gt; &amp; &#65;&#x42; &nbsp;fine &boh;</p>");
    buf[0] = '\0'; testo_sotto(trova("p"), buf, sizeof(buf));
    /* ! DUE SPAZI SONO GIUSTI: uno e' quello scritto, l'altro e' &nbsp;, che
     * e' uno spazio UNIFICATORE e non va fuso con quelli accanto — e' tutta la
     * ragione per cui esiste. L'attesa sbagliata era la mia. */
    ok(strcmp(buf, "<tag> & AB  fine &boh;") == 0,
       "nominali, numeriche, e cio' che non si riconosce resta com'e'");

    printf("SPAZI\n");
    leggi("<p>a   \n\t  b</p>");
    buf[0] = '\0'; testo_sotto(trova("p"), buf, sizeof(buf));
    ok(strcmp(buf, "a b") == 0, "gli spazi di seguito diventano uno");

    printf("HTML CHE NON E' XML\n");
    leggi("<ul><li>uno<li>due<li>tre</ul>");
    {
        int u = trova("ul"), n = 0, f;

        for (f = g_doc.nodi[u].primo_figlio; f >= 0; f = g_doc.nodi[f].prossimo)
            if (g_doc.nodi[f].tipo == HTML_ELEMENTO) n++;
        ok(n == 3, "tre <li> mai chiusi sono tre FRATELLI, non una scala");
    }

    leggi("<p>uno<p>due<p>tre");
    {
        int n = 0; unsigned int i;

        for (i = 0; i < g_doc.nodi_n; i++)
            if (g_doc.nodi[i].tipo == HTML_ELEMENTO &&
                strcmp(html_nome(&g_doc, (int)i), "p") == 0 &&
                profondita((int)i) == 1) n++;
        ok(n == 3, "tre <p> mai chiusi sono tutti allo stesso livello");
    }

    leggi("<b><i>x</b>y");
    ok(profondita(trova("i")) == profondita(trova("b")) + 1,
       "<b><i>x</b>: la i sta dentro la b");
    {
        /* Dopo </b> il testo non deve piu' stare dentro la b. */
        int b = trova("b");
        buf[0] = '\0'; testo_sotto(b, buf, sizeof(buf));
        ok(strcmp(buf, "x") == 0,
           "dopo </b> il testo esce, e non resta tutto in grassetto");
    }

    leggi("<div>a</span>b</div>");
    buf[0] = '\0'; testo_sotto(trova("div"), buf, sizeof(buf));
    ok(strcmp(buf, "ab") == 0,
       "una chiusura che non ha mai avuto apertura si ignora");

    printf("ELEMENTI VUOTI\n");
    leggi("<p>a<br>b<img src=\"x.png\">c</p>");
    {
        int p = trova("p"), n = 0, f;

        for (f = g_doc.nodi[p].primo_figlio; f >= 0; f = g_doc.nodi[f].prossimo) n++;
        ok(n == 5, "<br> e <img> non aprono niente: cinque figli in fila");
    }
    ok(strcmp(html_attr(&g_doc, trova("img"), "src"), "x.png") == 0,
       "l'attributo di un elemento vuoto c'e'");

    leggi("<p>a<br/>b</p>");
    ok(trova("br") > 0, "anche <br/> va bene");

    printf("SCRIPT E STYLE: DENTRO NON C'E' MARKUP\n");
    leggi("<p>prima</p><script>if (a < b && c > d) { x(\"</p>\"); }</script><p>dopo</p>");
    {
        int n = 0; unsigned int i;

        for (i = 0; i < g_doc.nodi_n; i++)
            if (g_doc.nodi[i].tipo == HTML_ELEMENTO &&
                strcmp(html_nome(&g_doc, (int)i), "p") == 0) n++;
        ok(n == 2, "il «<» del JavaScript non crea tag: restano due <p>");
    }
    buf[0] = '\0'; testo_sotto(trova("script"), buf, sizeof(buf));
    ok(strstr(buf, "a < b && c > d") != 0,
       "il contenuto dello script arriva intero, entita' comprese");

    leggi("<style>p { color: red } /* < > */</style><p>x</p>");
    ok(trova("p") > 0 && trova("style") > 0, "lo stesso per <style>");

    printf("COMMENTI E DICHIARAZIONI\n");
    leggi("<!DOCTYPE html><!-- <p>finto</p> --><p>vero</p>");
    {
        int n = 0; unsigned int i;

        for (i = 0; i < g_doc.nodi_n; i++)
            if (g_doc.nodi[i].tipo == HTML_ELEMENTO &&
                strcmp(html_nome(&g_doc, (int)i), "p") == 0) n++;
        ok(n == 1, "il DOCTYPE si salta e il commento non crea nodi");
    }

    printf("ROBUSTEZZA\n");
    leggi("<p>a < b</p>");
    buf[0] = '\0'; testo_sotto(g_doc.radice, buf, sizeof(buf));
    ok(strstr(buf, "a <") != 0, "un «<» seguito da spazio e' testo, non un tag");

    leggi("<div><div><div>");
    ok(g_doc.nodi_n >= 4, "tag aperti e mai chiusi non fanno danni");

    leggi("");
    ok(g_doc.radice >= 0 && g_doc.nodi_n == 1, "documento vuoto: solo la radice");

    leggi("<a href=");
    ok(g_doc.nodi_n >= 2, "un tag tagliato a meta' non fa danni");

    {
        /* Spazio finito: si deve dire, non fingere. */
        static HtmlNodo pochi[4];
        HtmlDoc         p;

        html_prepara(&p, pochi, 4, g_attr, 2048, g_arena, sizeof(g_arena));
        html_analizza(&p, "<a><b><c><d><e>", 15);
        ok(p.troncato, "finiti i nodi, `troncato` lo dice");
    }

    printf("MUTAZIONI\n");
    {
        int corpo, p1, p2, t;

        leggi("<body><p id=uno>A</p></body>");
        corpo = trova("body");
        p1    = trova("p");

        /* ! IL CONTATORE DELLE MODIFICHE E' LA COSA PIU' IMPORTANTE: senza,
         * chi impagina o rifa' tutto a ogni script o non rifa' mai niente. */
        {
            unsigned int prima = html_versione(&g_doc);
            p2 = html_crea_elemento(&g_doc, "SPAN");
            ok(p2 > 0, "createElement rende un nodo");
            ok(strcmp(html_nome(&g_doc, p2), "span") == 0,
               "il nome va in minuscolo, come nell'analizzatore");
            ok(html_versione(&g_doc) > prima, "creare alza la versione");
        }

        ok(html_aggiungi(&g_doc, corpo, p2), "appendChild attacca");
        ok(g_doc.nodi[corpo].ultimo_figlio == p2, "e va in fondo");
        ok(g_doc.nodi[p2].padre == corpo, "e il padre e' quello giusto");

        t = html_crea_testo(&g_doc, "&amp; resta com'e'");
        html_aggiungi(&g_doc, p2, t);
        /* ! LE ENTITA' NON SI SCIOLGONO in createTextNode: il testo arriva da
         * chi lo ha scritto, non da un documento. */
        ok(strcmp(html_testo(&g_doc, t), "&amp; resta com'e'") == 0,
           "createTextNode non tocca le entita'");

        /* ! UN NODO STA IN UN POSTO SOLO: appendChild SPOSTA. */
        ok(html_aggiungi(&g_doc, p1, p2), "riattaccare altrove funziona");
        ok(g_doc.nodi[p2].padre == p1, "il padre e' cambiato");
        ok(g_doc.nodi[corpo].ultimo_figlio == p1,
           "e il vecchio padre non lo tiene piu'");

        /* ! IL CICLO SI RIFIUTA: senza, l'impaginatore girerebbe per sempre. */
        ok(!html_aggiungi(&g_doc, p2, p1),
           "attaccare un avo dentro un discendente si rifiuta");
        ok(!html_aggiungi(&g_doc, p1, p1), "e nemmeno dentro se stesso");

        /* Togliere, e riattaccare. */
        {
            unsigned int prima = html_versione(&g_doc);
            ok(html_togli(&g_doc, p2), "removeChild stacca");
            ok(g_doc.nodi[p2].padre == -1, "e il padre non c'e' piu'");
            ok(!html_togli(&g_doc, p2), "toglierlo due volte rende 0");
            ok(html_versione(&g_doc) > prima, "togliere alza la versione");
            ok(html_aggiungi(&g_doc, corpo, p2), "e si puo' riattaccare");
        }

        /* Inserire in mezzo. */
        {
            int a = html_crea_elemento(&g_doc, "i");
            int b = html_crea_elemento(&g_doc, "b");

            html_aggiungi(&g_doc, corpo, a);
            ok(html_inserisci_prima(&g_doc, corpo, b, a),
               "insertBefore mette prima del riferimento");
            ok(g_doc.nodi[b].prossimo == a, "e l'ordine e' quello");
        }

        /* Gli attributi. */
        ok(html_attr_metti(&g_doc, p2, "CLASS", "rosso"),
           "setAttribute mette");
        ok(strcmp(html_attr(&g_doc, p2, "class"), "rosso") == 0,
           "e il nome va in minuscolo");
        ok(html_attr_metti(&g_doc, p2, "class", "blu"),
           "riscriverlo funziona");
        ok(strcmp(html_attr(&g_doc, p2, "class"), "blu") == 0,
           "e vale il valore nuovo");
        ok(html_attr_togli(&g_doc, p2, "class"), "removeAttribute toglie");
        ok(html_attr(&g_doc, p2, "class") == 0, "e poi non c'e' piu'");
        ok(!html_attr_togli(&g_doc, p2, "class"), "toglierlo due volte rende 0");

        /* Il testo. */
        ok(html_testo_metti(&g_doc, t, "altro"), "il testo si cambia");
        ok(strcmp(html_testo(&g_doc, t), "altro") == 0, "e vale quello nuovo");
        ok(!html_testo_metti(&g_doc, p2, "no"),
           "ma non su un elemento: rende 0 invece di fingere");

        /* ! L'ALBERO DEVE RESTARE PERCORRIBILE dopo tutto questo: e' la prova
         * che conta, perche' un elenco di figli rotto non da' errore — da' un
         * ciclo infinito nell'impaginatore. */
        {
            int  f, n = 0;
            for (f = g_doc.nodi[corpo].primo_figlio; f >= 0 && n < 1000;
                 f = g_doc.nodi[f].prossimo) {
                ok(g_doc.nodi[f].padre == corpo, "ogni figlio ha il padre giusto");
                n++;
            }
            ok(n < 1000, "l'elenco dei figli finisce");
        }
    }

    printf("\n%s\n", errori ? "CI SONO ERRORI" : "tutto a posto");
    return errori ? 1 : 0;
}
