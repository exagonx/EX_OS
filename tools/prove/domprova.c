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

/* ! QUESTI NUMERI SONO QUELLI DEL BROWSER, E NON PER ELEGANZA.
 * Erano piu' piccoli — 2048 nodi, un'arena da 256 KB — e su una pagina vera
 * il banco RACCONTAVA UNA COSA CHE NEL BROWSER NON SUCCEDE: google.com pesa
 * 280 KB, l'arena finiva a meta' documento, e l'ultimo <script> arrivava al
 * motore TRONCATO. L'errore che si leggeva era «SyntaxError: unexpected end
 * of string» — cioe' un difetto del motore, che non c'era. Un banco con meno
 * memoria del programma che imita non prova il programma: ne prova uno piu'
 * povero, e i guasti che inventa costano piu' di quelli che trova.
 * I nomi accanto sono le costanti di exwin/bin/browser/browser.c: se una
 * cresce li', cresce anche qui. */
#define OGG        4096                 /* >= JS_OGGETTI  (2000)   */
#define ARENA      (192 * 1024)         /* >= JS_ARENA    (96 KB)  */
#define NODI       24000                /* == NODI_MAX             */
#define ATTR       16000                /* == ATTR_MAX             */
#define TESTO      (64 * 1024)          /* == JS_TESTO             */
#define ASCOLTI    256                  /* == JS_ASCOLTI           */
#define ARENA_DOC  (1024u * 1024u)      /* == ARENA_MAX            */

static HtmlNodo g_nodi[NODI];
static HtmlAttr g_attr[ATTR];
static char     g_arena[ARENA_DOC];
static HtmlDoc  g_doc;

static unsigned char g_mem_js[8 << 20];
static unsigned char g_mem_dom[NODI * 64 + ASCOLTI * 64 + TESTO + 65536];
static char          g_ser[2 * 1024 * 1024];

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
/* =============================================================================
 * LA RETE FINTA
 *
 * ! IL GANCIO DELLA RETE ESISTE ANCHE PER QUESTO. Provare XMLHttpRequest e
 * fetch con una rete vera vorrebbe dire un server, una porta e una macchina
 * accesa: tre cose che non ci sono quando si lancia `make prova-exdom`, e che
 * renderebbero le prove diverse a seconda di dove girano. Qui la rete e' una
 * funzione di venti righe che risponde secondo l'indirizzo, e le risposte
 * scomode — il 404, la richiesta che non parte — sono facili quanto le altre.
 * Con una chiamata a exhttp dentro exdom, niente di tutto questo si potrebbe
 * scrivere.
 * ========================================================================== */
static int   g_rete_n;              /* quante richieste sono passate di qui */
static char  g_rete_ultima[256];    /* metodo, spazio, indirizzo            */
static char  g_rete_corpo[256];     /* il corpo dell'ultima POST            */

static int rete_finta(void *dato, ExDomRichiesta *r)
{
    (void)dato;
    g_rete_n++;
    snprintf(g_rete_ultima, sizeof(g_rete_ultima), "%s %s",
             r->metodo ? r->metodo : "?", r->url ? r->url : "?");
    g_rete_corpo[0] = '\0';
    if (r->corpo) snprintf(g_rete_corpo, sizeof(g_rete_corpo), "%s", r->corpo);

    if (strcmp(r->url, "/ciao") == 0) {
        r->risposta = "buongiorno";
        r->byte     = 10;
        r->codice   = 200;
        r->tipo     = "text/plain";
        return 1;
    }
    if (strcmp(r->url, "/dati.json") == 0) {
        r->risposta = "{\"n\":7,\"s\":\"ciao\"}";
        r->byte     = 18;
        r->codice   = 200;
        r->tipo     = "application/json";
        return 1;
    }
    if (strcmp(r->url, "/manca") == 0) {
        r->risposta = "non c'e'";
        r->byte     = 8;
        r->codice   = 404;
        r->tipo     = "text/plain";
        return 1;
    }
    if (strcmp(r->url, "/eco") == 0) {
        r->risposta = g_rete_corpo;
        r->byte     = (unsigned int)strlen(g_rete_corpo);
        r->codice   = 200;
        r->tipo     = "text/plain";
        return 1;
    }
    /* ! «NON E' PARTITA» NON E' UN CODICE, ed e' il caso che distingue fetch
     * da XMLHttpRequest: fetch RIFIUTA la promessa, XHR lascia status a zero. */
    return 0;
}

/* =============================================================================
 * LA RETE DEL DISCO — per il modo «una pagina vera»
 *
 * ! QUANDO SI APRE UN FILE, LA RETE E' IL DISCO, ed e' quel che fa anche il
 * browser: `fetch('altro.html')` da una pagina `file:` legge da disco, perche'
 * exhttp di «file:» non sa niente. Senza questo gancio, la stessa pagina si
 * comporterebbe in due modi qui e li' — che e' l'unica cosa che un banco non
 * deve mai fare.
 * ========================================================================== */
static char g_disco_buf[512 * 1024];

static int rete_da_disco(void *dato, ExDomRichiesta *r)
{
    const char *p = r->url;
    FILE       *fp;
    size_t      n;

    (void)dato;
    if (!p) return 0;
    if (strncmp(p, "file://", 7) == 0) p += 7;

    fp = fopen(p, "rb");
    /* ! E SI RIPROVA SENZA LA BARRA IN TESTA. Il banco dichiara alla pagina un
     * indirizzo `file:///qualcosa` costruito da un percorso RELATIVO, quindi
     * il percorso assoluto che ne esce non esiste. E' una stortura del banco e
     * non del ponte: si ripara qui, dove sta. */
    if (!fp && p[0] == '/') fp = fopen(p + 1, "rb");
    if (!fp) return 0;

    n = fread(g_disco_buf, 1, sizeof(g_disco_buf), fp);
    fclose(fp);

    r->risposta = g_disco_buf;
    r->byte     = (unsigned int)n;
    r->codice   = 200;
    r->tipo     = 0;
    return 1;
}

/* Con `rete` a 0 il ponte resta senza gancio: serve a provare che senza
 * browser sotto XMLHttpRequest e fetch dicono di no invece di fingere. */
static ExDom *apparecchia_rete(const char *html, ExJsCtx **fuori, int rete);

static ExDom *apparecchia(const char *html, ExJsCtx **fuori)
{
    return apparecchia_rete(html, fuori, 1);
}

static ExDom *apparecchia_rete(const char *html, ExJsCtx **fuori, int rete)
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
    if (D && rete) exdom_rete_metti(D, rete_finta, 0);
    g_rete_n = 0;
    g_rete_ultima[0] = '\0';
    g_rete_corpo[0]  = '\0';
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

/* =============================================================================
 * ! LE RICHIESTE NON SI FANNO PIU' DENTRO `send()`, e il banco ha dovuto
 * imparare a fare quel che fa il browser: mandare avanti la coda.
 *
 * exdom_rete_pompa() ne fa UNA per chiamata — cosi' chi ospita puo' ridisegnare
 * in mezzo — quindi qui si gira finche' non ne resta nessuna. Il tetto ai giri
 * c'e' perche' un gestore puo' fare un'altra richiesta, e due gestori che se le
 * rimbalzano non devono bloccare il banco: sessantaquattro sono piu' di quante
 * ne faccia qualunque prova, e un ciclo infinito si vede subito.
 * ========================================================================== */
static void pompa_rete(ExDom *D)
{
    int giri = 0;

    while (giri++ < 64 && exdom_rete_pompa(D)) { }
}

/* Come prova_val, ma GUARDA DOPO: esegue lo script, manda avanti la coda delle
 * richieste, e solo allora valuta l'espressione. E' la forma che serve a tutto
 * cio' che e' asincrono — il valore che conta non c'e' ancora quando lo script
 * finisce, ed e' esattamente quel che «asincrono» vuol dire. */
static void prova_dopo(const char *nome, const char *html, const char *script,
                       const char *espressione, const char *atteso)
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

    pompa_rete(D);

    memset(&err, 0, sizeof(err));
    if (!exjs_esegui(c, espressione, (unsigned int)strlen(espressione), &r, &err)) {
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

/* ! COME prova_val MA SU UN PONTE GIA' APERTO. `location` vuole che qualcuno
 * abbia gia' detto dove siamo — exdom_indirizzo() — e prova_val apparecchia
 * da se': servirebbe un quinto argomento su cinquanta chiamate per un caso. */
static void prova_gia(const char *nome, ExJsCtx *c, const char *script,
                      const char *atteso)
{
    ExJsErrore  err;
    ExJsVal     r;
    const char *s;

    fatte++;
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

/* Per lo stile: uno senza attributo, uno con due dichiarazioni, e uno con la
 * stessa proprieta' due volte — che nel CSS non e' un errore e ha un vincitore
 * preciso, l'ultima. */
static const char *STL =
    "<p id=\"uno\"></p>"
    "<p id=\"due\" style=\"color: red; margin: 0px\"></p>"
    "<p id=\"tre\" style=\"color: red; color: blue\"></p>";

/* Per i selettori. E' costruito perche' `div p` e `div > p` diano risposte
 * DIVERSE: con tutti i `p` figli diretti del `div` le due prove passerebbero
 * anche con il combinatore ignorato, cioe' non proverebbero niente. */
/* Per le proprieta' riflesse: un <title> vero (che NON e' l'attributo `title`
 * della radice), un elemento per ogni famiglia di attributi, e un `div` che
 * fa da cassetto — quello che uno script usa per appenderci i propri dati. */
static const char *RFL =
    "<html><head><title>Prova</title></head><body>"
    "<img src=\"gatto.png\">"
    "<a href=\"/via.html\">via</a>"
    "<input id=\"campo\" type=\"text\" value=\"ciao\">"
    "<div id=\"cassetto\" class=\"rosso grosso\""
    " data-ruolo=\"capo\" data-vista-larga=\"si\">roba</div>"
    "</body></html>";

static const char *SEL =
    "<body><div>"
    "<p id=\"uno\" class=\"x y\"><b>forte</b></p>"
    "<span data-ruolo=\"capo\">a</span>"
    "<section><p class=\"x\"><i>corsivo</i></p></section>"
    "<span>b</span>"
    "</div>"
    "<p class=\"x\" data-ruolo=\"gregario\">fuori</p>"
    "<a href=\"http://ex/os.html\">via</a></body>";

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
/* L'indirizzo da dichiarare alla pagina, se chi prova ne da' uno. */
static char g_url_finto[EXDOM_URL_MAX] = "";

static int pagina(const char *nomefile)
{
    static char  testo[2 * 1024 * 1024];
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
    exdom_rete_metti(D, rete_da_disco, 0);

    /* ! ANCHE L'INDIRIZZO, come fa il browser. Senza, `location` risponde
     * stringhe vuote e una pagina vera si comporta diversamente qui e la':
     * il banco imita il browser o non serve a niente. Con un secondo
     * argomento si passa l'indirizzo VERO da cui la pagina e' stata scaricata,
     * che e' quel che serve a provare un sito salvato su disco. */
    {
        char url[EXDOM_URL_MAX];

        if (g_url_finto[0]) exdom_indirizzo(D, g_url_finto);
        else {
            /* Tre barre e non due: dopo «file://» viene l'HOST, e con due un
             * percorso relativo diventerebbe un nome di macchina. */
            snprintf(url, sizeof(url), "file://%s%s",
                     nomefile[0] == '/' ? "" : "/", nomefile);
            exdom_indirizzo(D, url);
        }
    }

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
            if (!exjs_esegui(c, t, (unsigned int)strlen(t), &r, &err)) {
                /* ! E SI DICE QUALE, non solo che ce n'e' uno. Su una pagina
                 * vera gli <script> sono quindici e il messaggio da solo —
                 * «TypeError: not a function» — non dice ne' quale riga del
                 * web manca ne' dove guardare. Il numero, il nodo, la
                 * LUNGHEZZA (che smaschera un testo troncato) e le prime
                 * lettere bastano a ritrovarlo dentro il documento. */
                char        capo[97];
                unsigned int k, len = (unsigned int)strlen(t);

                for (k = 0; k < sizeof(capo) - 1 && t[k]; k++)
                    capo[k] = (t[k] == '\n' || t[k] == '\r') ? ' ' : t[k];
                capo[k] = '\0';
                printf("!! script #%d (nodo %d, %u byte), riga %d: %s\n"
                       "   [%s]\n",
                       fatti, i, len, err.riga, err.messaggio, capo);
            }
            fatti++;
        }
    }

    printf("script eseguiti: %d\n", fatti);

    /* ! E SI MANDA AVANTI LA CODA DELLE RICHIESTE, come fa il browser dal suo
     * ciclo dei messaggi: da quando `send()` mette in coda e torna, una pagina
     * che chiede qualcosa alla rete non ha ancora la risposta quando l'ultimo
     * <script> finisce. Senza queste righe il banco mostrerebbe la pagina a
     * meta' e sembrerebbe un difetto del ponte. */
    pompa_rete(D);

    if (g_console[0]) printf("console: %s", g_console);

    /* ! E SE UNO SCRIPT HA CHIESTO DI ANDARE ALTROVE LO SI DICE, perche' su
     * una pagina vera e' spesso TUTTO quello che quella pagina fa: la
     * redirezione scritta in JavaScript e' il modo normale di mandare altrove
     * un browser che non ha i biscotti. Senza questa riga il banco direbbe
     * «zero errori» su una pagina che non ha fatto niente di visibile. */
    {
        char dove[EXDOM_URL_MAX];

        if (exdom_dove_andare(D, dove, sizeof(dove)))
            printf("uno script vuole andare a: %s\n", dove);
    }

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
    /* ! CON UN SECONDO ARGOMENTO SI DICHIARA L'INDIRIZZO VERO della pagina.
     * Un sito salvato su disco e provato come `file://…` non e' lo stesso
     * sito: mezzo web guarda `location.hostname` prima di decidere che fare. */
    if (argc >= 3) {
        unsigned int k = 0;

        while (argv[2][k] && k + 1 < sizeof(g_url_finto)) {
            g_url_finto[k] = argv[2][k]; k++;
        }
        g_url_finto[k] = '\0';
    }
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

    /* =========================================================================
     * `el.style`
     *
     * ! LE PROVE GUARDANO L'ATTRIBUTO, non solo il valore riletto. Uno `style`
     * finto che si ricorda quel che gli si scrive e lo rende indietro passa
     * qualunque prova fatta con `el.style.color` da sola — e lascia la pagina
     * esattamente com'era. E' lo stesso motivo per cui questo banco guarda il
     * documento invece del valore reso dallo script.
     * ====================================================================== */
    printf("\n--- lo stile inline ---\n");

    prova_val("scrivere una proprieta' tocca l'attributo", STL,
              "var e = document.getElementById('uno');"
              "e.style.color = 'red';"
              "e.getAttribute('style')", "color: red");
    prova_val("e si rilegge", STL,
              "var e = document.getElementById('uno');"
              "e.style.color = 'red'; e.style.color", "red");
    prova_val("il maiuscolo diventa un trattino", STL,
              "var e = document.getElementById('uno');"
              "e.style.backgroundColor = 'blue';"
              "e.getAttribute('style')", "background-color: blue");
    prova_val("una proprieta' che non c'e' da \"\"", STL,
              "document.getElementById('uno').style.display", "");
    prova_val("quel che c'era resta, e al suo posto", STL,
              "var e = document.getElementById('due');"
              "e.style.color = 'blue';"
              "e.getAttribute('style')", "color: blue; margin: 0px");
    /* ! UNA PROPRIETA' CHE excss NON CONOSCE DEV'ESSERE CONSERVATA LO STESSO,
     * ed e' il motivo per cui lo stile non passa da li'. */
    prova_val("una proprieta' sconosciuta al CSS nostro", STL,
              "var e = document.getElementById('uno');"
              "e.style.zIndex = '7'; e.style.zIndex", "7");
    prova_val("la stringa vuota toglie la dichiarazione", STL,
              "var e = document.getElementById('due');"
              "e.style.margin = ''; e.getAttribute('style')", "color: red");
    prova_val("e null la toglie come la stringa vuota", STL,
              "var e = document.getElementById('due');"
              "e.style.color = null; e.getAttribute('style')", "margin: 0px");
    prova_val("l'ultima dichiarazione vince, come nel CSS", STL,
              "document.getElementById('tre').style.color", "blue");
    prova_val("cssText si legge", STL,
              "document.getElementById('due').style.cssText",
              "color: red; margin: 0px");
    prova_val("cssText si scrive", STL,
              "var e = document.getElementById('uno');"
              "e.style.cssText = 'color: green';"
              "e.getAttribute('style')", "color: green");
    prova_val("setProperty e getPropertyValue", STL,
              "var e = document.getElementById('uno');"
              "e.style.setProperty('font-size', '12px');"
              "e.style.getPropertyValue('font-size')", "12px");
    prova_val("e il nome col trattino arriva anche da JS", STL,
              "var e = document.getElementById('uno');"
              "e.style.setProperty('font-size', '12px');"
              "e.style.fontSize", "12px");
    prova_val("removeProperty rende quel che c'era", STL,
              "document.getElementById('due').style.removeProperty('color')",
              "red");
    prova_val("length conta le dichiarazioni", STL,
              "document.getElementById('due').style.length", "2");
    prova_val("item da' il nome della k-esima", STL,
              "document.getElementById('due').style.item(1)", "margin");
    /* ! L'OGGETTO E' SEMPRE LO STESSO, come l'involucro del nodo: senza,
     * `var s = el.style; s.color = 'x'` scriverebbe su una copia. */
    prova_val("`el.style` e' sempre lo stesso oggetto", STL,
              "var e = document.getElementById('uno');"
              "e.style === e.style", "true");
    /* ! IL GANCIO VIENE PRIMA DEL PROTOTIPO: se non dicesse «non e' mia» sui
     * nomi dei metodi, questa sarebbe la stringa vuota. */
    prova_val("i metodi non li copre il gancio", STL,
              "typeof document.getElementById('uno').style.setProperty",
              "function");
    prova_val("assegnare una stringa a style vale cssText", STL,
              "var e = document.getElementById('uno');"
              "e.style = 'color: teal';"
              "e.getAttribute('style')", "color: teal");
    /* ! UN SATELLITE NON E' IL SUO ELEMENTO. Prima di questo controllo
     * `appendChild(el.style)` avrebbe spostato `el`: lo stile porta lo stesso
     * Legame, ed e' l'involucro — non il legame — a dire chi e' un nodo. */
    prova_val("`el.style` non e' un nodo", STL,
              "var e = document.getElementById('uno');"
              "String(document.getElementById('due').appendChild(e.style))",
              "null");
    /* ! NON CI SONO DUE POSTI DA TENERE D'ACCORDO: `setAttribute('style')` e
     * `el.style.x` scrivono e leggono lo stesso attributo, e questa prova
     * cadrebbe il giorno che qualcuno mettesse lo stile in una struttura
     * accanto «per andare piu' svelti». */
    prova_val("setAttribute e style, la stessa cosa", STL,
              "var e = document.getElementById('uno');"
              "e.setAttribute('style', 'color: red');"
              "e.style.color = 'blue';"
              "e.getAttribute('style')", "color: blue");

    /* =========================================================================
     * I SELETTORI
     * ====================================================================== */
    printf("\n--- i selettori ---\n");

    prova_val("querySelector per tipo", SEL,
              "document.querySelector('b').textContent", "forte");
    prova_val("per id", SEL,
              "document.querySelector('#uno').tagName", "P");
    prova_val("per classe", SEL,
              "document.querySelector('.x').tagName", "P");
    prova_val("querySelectorAll conta", SEL,
              "document.querySelectorAll('.x').length", "3");
    prova_val("le classi si sommano nello stesso pezzo", SEL,
              "document.querySelectorAll('.x.y').length", "1");
    prova_val("il discendente", SEL,
              "document.querySelectorAll('div p').length", "2");
    prova_val("il figlio non e' il discendente", SEL,
              "document.querySelectorAll('div > p').length", "1");
    prova_val("il fratello che viene subito dopo", SEL,
              "document.querySelectorAll('p + span').length", "1");
    prova_val("e il fratello che viene dopo, comunque", SEL,
              "document.querySelectorAll('p ~ span').length", "2");
    prova_val("un attributo che c'e'", SEL,
              "document.querySelectorAll('[data-ruolo]').length", "2");
    prova_val("un attributo con un valore", SEL,
              "document.querySelector('[data-ruolo=capo]').tagName", "SPAN");
    prova_val("le virgolette dentro le quadre", SEL,
              "document.querySelector('[data-ruolo=\"capo\"]').tagName", "SPAN");
    prova_val("il prefisso di un attributo", SEL,
              "document.querySelectorAll('a[href^=http]').length", "1");
    prova_val("e la coda", SEL,
              "document.querySelectorAll('a[href$=\".html\"]').length", "1");
    prova_val("l'elenco separato dalle virgole", SEL,
              "document.querySelectorAll('b, i').length", "2");
    prova_val("la stella prende tutto", SEL,
              "document.querySelectorAll('div *').length", "7");
    prova_val("matches dice si' e no", SEL,
              "document.querySelector('#uno').matches('p.x') + ' ' +"
              "document.querySelector('#uno').matches('div')", "true false");
    /* ! closest PARTE DA SE STESSO: e' cosi' che lo usano i gestori di clic. */
    prova_val("closest parte dal nodo stesso", SEL,
              "document.querySelector('b').closest('div').tagName", "DIV");
    prova_val("e se il nodo gia' combacia rende lui", SEL,
              "document.querySelector('#uno').closest('p').id", "uno");
    prova_val("closest che non trova niente rende null", SEL,
              "String(document.querySelector('b').closest('table'))", "null");
    /* ! DA UN ELEMENTO SI GUARDANO I DISCENDENTI, non tutto il documento. */
    prova_val("da un elemento si cercano i discendenti", SEL,
              "document.querySelector('div').querySelectorAll('p').length", "2");
    prova_val("ma il selettore puo' risalire fuori", SEL,
              "document.querySelector('div').querySelectorAll('body p').length",
              "2");
    /* ! QUEL CHE NON SI SA LEGGERE NON TROVA NIENTE, e non trova la cosa
     * sbagliata: una pseudo-classe ignorata avrebbe reso il primo `p` della
     * pagina a chi chiedeva `p:hover`. */
    prova_val("una pseudo-classe non trova niente", SEL,
              "String(document.querySelector('p:hover'))", "null");
    prova_val("e nemmeno dentro un elenco", SEL,
              "document.querySelectorAll('b, i:first-child').length", "1");
    prova_val("un selettore vuoto non trova niente", SEL,
              "String(document.querySelector(''))", "null");
    /* ! I NODI RESI SONO GLI INVOLUCRI DI SEMPRE, non copie nuove. */
    prova_val("rende l'involucro di sempre", SEL,
              "document.querySelector('#uno') === document.getElementById('uno')",
              "true");
    /* ! E IL NODO TROVATO E' TOCCABILE, che e' tutto il punto: un
     * querySelector che rendesse una fotografia sarebbe una funzione di
     * lettura, non un pezzo di DOM. */
    prova_val("e da li' si tocca la pagina", SEL,
              "document.querySelector('#uno').style.display = 'none';"
              "document.getElementById('uno').getAttribute('style')",
              "display: none");

    /* =========================================================================
     * GLI ATTRIBUTI RIFLESSI, `dataset`, `classList`
     * ====================================================================== */
    printf("\n--- le proprieta' degli elementi ---\n");

    prova_val("img.src rende l'attributo", RFL,
              "document.getElementsByTagName('img')[0].src", "gatto.png");
    prova_val("a.href pure", RFL,
              "document.getElementsByTagName('a')[0].href", "/via.html");
    prova_val("e si scrivono", RFL,
              "var i = document.getElementsByTagName('img')[0];"
              "i.src = 'cane.png'; i.getAttribute('src')", "cane.png");
    prova_val("quel che non c'e' da \"\", non null", RFL,
              "document.getElementsByTagName('img')[0].alt", "");
    prova_val("input.value e input.type", RFL,
              "var e = document.getElementById('campo');"
              "e.value + ' ' + e.type", "ciao text");
    /* ! LA TABELLA DICE ANCHE SU QUALE ELEMENTO, e questa prova e' il motivo:
     * un `div` usato come cassetto da uno script non deve trovarsi le sue
     * proprieta' trasformate in attributi. */
    prova_val("su un div, `value` resta roba dello script", RFL,
              "var d = document.getElementById('cassetto');"
              "d.value = 3; d.value + ' ' + String(d.getAttribute('value'))",
              "3 null");
    prova_val("e `src` su un div nemmeno", RFL,
              "var d = document.getElementById('cassetto');"
              "d.src = 'x'; String(d.getAttribute('src'))", "null");
    /* ! `document.title` NON E' L'ATTRIBUTO `title` DELLA RADICE. La radice e'
     * un elemento come gli altri, e senza il controllo apposta la tabella
     * l'avrebbe presa lei. */
    prova_val("document.title resta il <title>", RFL,
              "document.title", "Prova");
    prova_val("ma su un elemento `title` e' l'attributo", RFL,
              "var d = document.getElementById('cassetto');"
              "d.title = 'spiegazione'; d.getAttribute('title')", "spiegazione");

    prova_val("dataset legge un data-", RFL,
              "document.getElementById('cassetto').dataset.ruolo", "capo");
    prova_val("e il maiuscolo e' un trattino", RFL,
              "document.getElementById('cassetto').dataset.vistaLarga", "si");
    prova_val("dataset scrive", RFL,
              "var d = document.getElementById('cassetto');"
              "d.dataset.nuovoDato = '7'; d.getAttribute('data-nuovo-dato')",
              "7");
    /* ! UN data- CHE NON C'E' DA undefined, non "": e' come il DOM distingue
     * «non c'e'» da «c'e' ed e' vuoto», e le pagine ci contano. */
    prova_val("un data- che non c'e' da' undefined", RFL,
              "String(document.getElementById('cassetto').dataset.mai)",
              "undefined");

    prova_val("classList.contains", RFL,
              "var d = document.getElementById('cassetto');"
              "d.classList.contains('rosso') + ' ' + d.classList.contains('blu')",
              "true false");
    prova_val("classList.add non duplica", RFL,
              "var d = document.getElementById('cassetto');"
              "d.classList.add('rosso'); d.className", "rosso grosso");
    prova_val("classList.add aggiunge in fondo", RFL,
              "var d = document.getElementById('cassetto');"
              "d.classList.add('blu'); d.className", "rosso grosso blu");
    prova_val("classList.remove toglie la parola giusta", RFL,
              "var d = document.getElementById('cassetto');"
              "d.classList.remove('rosso'); d.className", "grosso");
    prova_val("toggle senza argomento gira", RFL,
              "var d = document.getElementById('cassetto');"
              "d.classList.toggle('rosso') + ' ' + d.className", "false grosso");
    /* ! toggle CON IL SECONDO ARGOMENTO NON GIRA: mette o toglie. */
    prova_val("toggle con la condizione non gira", RFL,
              "var d = document.getElementById('cassetto');"
              "d.classList.toggle('rosso', true); d.className", "rosso grosso");
    prova_val("classList si scorre per indice", RFL,
              "var l = document.getElementById('cassetto').classList;"
              "l.length + ' ' + l[0] + ' ' + l[1]", "2 rosso grosso");
    prova_val("classList.replace", RFL,
              "var d = document.getElementById('cassetto');"
              "d.classList.replace('rosso', 'verde'); d.className",
              "grosso verde");
    /* ! className E classList GUARDANO LA STESSA COSA: se fossero due depositi
     * questa prova cadrebbe, ed e' l'unico modo di accorgersene. */
    prova_val("className e classList, un deposito solo", RFL,
              "var d = document.getElementById('cassetto');"
              "d.className = 'a b'; d.classList.contains('b') + ' ' +"
              "d.classList.length", "true 2");
    /* ! IL GANCIO IN SCRITTURA DEV'ESSERCI ANCHE SE FA UNA COSA SOLA, o i due
     * motori si comportano in modo diverso: vedi la nota accanto a
     * classi_scrivi. Questa prova gira su tutt'e due. */
    prova_val("scrivere roba propria sul classList non si perde", RFL,
              "var l = document.getElementById('cassetto').classList;"
              "l.mioStato = 3; l.mioStato", "3");
    prova_val("classList.value scrive le classi", RFL,
              "var d = document.getElementById('cassetto');"
              "d.classList.value = 'x y'; d.getAttribute('class')", "x y");

    /* =========================================================================
     * `location` E `navigator`
     *
     * ! L'INDIRIZZO SI DA' DA FUORI, e queste prove lo fanno come lo fa il
     * browser: exdom_indirizzo() prima, gli script dopo. Senza la chiamata
     * `location` risponde stringhe vuote — che e' la verita', non un errore.
     * ====================================================================== */
    printf("\n--- location e navigator ---\n");
    {
        ExJsCtx    *c;
        ExDom      *D;
        char        dove[EXDOM_URL_MAX];
        const char *URL = "https://ex.os:8080/a/b.html?q=1&r=2#in-fondo";

        D = apparecchia(PAG, &c);
        exdom_indirizzo(D, URL);
        prova_gia("href e' l'indirizzo intero", c, "location.href", URL);
        prova_gia("protocol", c, "location.protocol", "https:");
        prova_gia("host col numero di porta", c, "location.host", "ex.os:8080");
        prova_gia("hostname senza", c, "location.hostname", "ex.os");
        prova_gia("port", c, "location.port", "8080");
        prova_gia("pathname", c, "location.pathname", "/a/b.html");
        prova_gia("search comincia col punto interrogativo", c,
                  "location.search", "?q=1&r=2");
        prova_gia("hash comincia col cancelletto", c, "location.hash",
                  "#in-fondo");
        prova_gia("origin e' schema piu' host", c, "location.origin",
                  "https://ex.os:8080");
        /* ! `location.toString()` E NON `String(location)`, E LA DIFFERENZA
         * E' DEI MOTORI, NON DEL PONTE. QuickJS, portando un oggetto in
         * stringa, ne chiama `toString`; ExJs rende "[object Object]" senza
         * guardare (val.c, exjs_a_stringa). Il metodo c'e' ed e' giusto in
         * tutt'e due: e' la CONVERSIONE implicita che manca da una parte.
         * La prova chiede quel che i due motori devono fare uguale; la
         * differenza sta scritta qui e nella coda dei lavori, perche' vale
         * per QUALUNQUE oggetto del DOM messo dentro una stringa — e allora
         * il posto per rimediare e' exjs_a_stringa, non questo file. */
        prova_gia("in stringa e' l'indirizzo", c, "location.toString()", URL);
        /* ! window.location E document.location SONO LO STESSO OGGETTO, come
         * nel DOM: due oggetti distinti vorrebbero dire due verita'. */
        prova_gia("window.location e document.location", c,
                  "window.location === document.location", "true");

        /* Senza porta, senza query e senza frammento: le fette vuote devono
         * essere vuote, non l'ultima cosa che c'era. */
        D = apparecchia(PAG, &c);
        exdom_indirizzo(D, "http://ex.os/");
        prova_gia("niente porta da' \"\"", c, "location.port", "");
        prova_gia("niente search da' \"\"", c, "location.search", "");
        prova_gia("niente hash da' \"\"", c, "location.hash", "");
        prova_gia("il percorso resta la barra", c, "location.pathname", "/");

        /* ! UN INDIRIZZO LOCALE HA LO SCHEMA E NON HA HOST, e il taglio
         * dev'essere quello giusto lo stesso: «file:///a/b.html». */
        D = apparecchia(PAG, &c);
        exdom_indirizzo(D, "file:///exwin/doc/index.html");
        prova_gia("file: lo schema", c, "location.protocol", "file:");
        prova_gia("file: il percorso", c, "location.pathname",
                  "/exwin/doc/index.html");
        prova_gia("file: nessun host", c, "location.host", "");

        /* --- la navigazione, che il ponte NON fa ------------------------- */
        D = apparecchia(PAG, &c);
        exdom_indirizzo(D, "http://ex.os/uno");
        ok("nessuno vuole andare da nessuna parte",
           exdom_dove_andare(D, dove, sizeof(dove)) == 0, "");
        gira(c, "location.href = 'http://ex.os/due';", "href");
        ok("location.href dice dove andare",
           exdom_dove_andare(D, dove, sizeof(dove)) == 1 &&
           strcmp(dove, "http://ex.os/due") == 0, dove);
        /* ! E SE LO DIMENTICA: una navigazione fallita non deve ripartire da
         * sola al giro dopo, per sempre. */
        ok("e se lo dimentica",
           exdom_dove_andare(D, dove, sizeof(dove)) == 0, "");
        /* ! L'INDIRIZZO NON CAMBIA DA SE': location.href resta quello di
         * prima finche' il browser non carica davvero. Un ponte che si
         * aggiornasse da solo direbbe di essere su una pagina mai aperta. */
        prova_gia("ma l'indirizzo e' ancora quello", c, "location.href",
                  "http://ex.os/uno");

        gira(c, "location.assign('/tre');", "assign");
        ok("assign fa lo stesso",
           exdom_dove_andare(D, dove, sizeof(dove)) == 1 &&
           strcmp(dove, "/tre") == 0, dove);
        gira(c, "location.replace('/quattro');", "replace");
        ok("replace pure",
           exdom_dove_andare(D, dove, sizeof(dove)) == 1 &&
           strcmp(dove, "/quattro") == 0, dove);
        gira(c, "location.reload();", "reload");
        ok("reload rimanda dove si e' gia'",
           exdom_dove_andare(D, dove, sizeof(dove)) == 1 &&
           strcmp(dove, "http://ex.os/uno") == 0, dove);
        /* ! UNA PAGINA NUOVA CANCELLA LA VOGLIA DI ANDARE ALTROVE, o un
         * `location.href` rimasto in sospeso dalla pagina di PRIMA farebbe
         * partire una navigazione che nessuno ha chiesto. */
        gira(c, "location.href = '/cinque';", "sospeso");
        exdom_indirizzo(D, "http://ex.os/sei");
        ok("caricare una pagina cancella il sospeso",
           exdom_dove_andare(D, dove, sizeof(dove)) == 0, "");

        /* --- gli ascoltatori della finestra ------------------------------ */
        /* ! window E' IL GLOBALE, e il globale non e' un nodo: senza i tre
         * involucri apposta, `window.addEventListener` e' «not a function» —
         * ed e' cosi' che una pagina vera se ne accorge. */
        D = apparecchia(PAG, &c);
        prova_gia("window.addEventListener c'e'", c,
                  "typeof window.addEventListener", "function");
        gira(c, "var visto = 0;"
                "window.addEventListener('pippo', function () { visto = 1; });",
                "win");
        exdom_evento(D, g_doc.radice, "pippo", 0);
        prova_gia("e un evento sulla radice lo chiama", c, "visto", "1");
        /* ! LA SPIA `perso` NON SI ACCENDE PER UNA CHIAMATA SBAGLIATA, come
         * per il gemello sugli elementi: vuol dire «non c'era posto», non
         * «qualcuno ha chiamato male». */
        gira(c, "window.addEventListener('x');", "corta");
        gira(c, "window.addEventListener('y', 3);", "nonfunz");
        ok("una chiamata sbagliata non accende la spia",
           exdom_perso(D) == 0, "");

        /* --- navigator --------------------------------------------------- */
        D = apparecchia(PAG, &c);
        /* ! LA STRINGA E' QUELLA CHE exhttp MANDA DAVVERO (lib/exhttp/exhttp.c):
         * se le due divergessero, un sito servirebbe la pagina per un browser
         * e la pagina ne troverebbe un altro. */
        prova_gia("navigator.userAgent", c, "navigator.userAgent", "EX-OS");
        prova_gia("i biscotti non ci sono, e lo si dice", c,
                  "navigator.cookieEnabled", "false");
        prova_gia("sendBeacon non manda niente e lo dice", c,
                  "navigator.sendBeacon('/x', '')", "false");
    }

    /* =========================================================================
     * XMLHttpRequest E fetch
     *
     * ! LE PROVE SONO SCRITTE IN ES3, senza funzioni a freccia e senza
     * try/catch: girano su tutt'e due i motori, e ExJs quelle cose non le ha.
     * ====================================================================== */
    printf("\n--- XMLHttpRequest ---\n");

    /* ! `new` SU UNA NATIVA NON E' SCONTATO: QuickJS tiene un bit sulla
     * funzione e senza quello e' «not a constructor». Questa prova esiste
     * perche' il difetto si vedeva su UN motore solo. */
    prova_val("si costruisce con new", PAG,
              "var x = new XMLHttpRequest(); typeof x", "object");
    prova_val("appena nato e' allo stato zero", PAG,
              "var x = new XMLHttpRequest();"
              "x.readyState + ' ' + x.status + ' ' + x.responseText",
              "0 0 ");
    prova_val("open porta allo stato uno", PAG,
              "var x = new XMLHttpRequest(); x.open('GET', '/ciao');"
              "x.readyState", "1");
    prova_dopo("send prende la risposta", PAG,
               "var x = new XMLHttpRequest(); x.open('GET', '/ciao'); x.send();",
               "x.status + ' ' + x.responseText + ' ' + x.readyState",
               "200 buongiorno 4");
    prova_dopo("un 404 e' una risposta, non un errore", PAG,
               "var x = new XMLHttpRequest(); x.open('GET', '/manca'); x.send();",
               "x.status + ' ' + x.responseText", "404 non c'e'");
    /* ! LA RICHIESTA CHE NON PARTE LASCIA status A ZERO, ed e' cosi' che una
     * pagina distingue «il server ha detto 404» da «non ho potuto chiedere». */
    prova_dopo("quella che non parte lascia zero", PAG,
               "var x = new XMLHttpRequest(); x.open('GET', '/altrove'); x.send();",
               "x.status + ' ' + x.readyState", "0 4");
    prova_dopo("onload si chiama, e `this` e' la richiesta", PAG,
               "var v = '';"
               "var x = new XMLHttpRequest();"
               "x.onload = function () { v = this.status + ':' + this.responseText; };"
               "x.open('GET', '/ciao'); x.send();", "v", "200:buongiorno");
    prova_dopo("l'evento porta il tipo e il bersaglio", PAG,
               "var v = '';"
               "var x = new XMLHttpRequest();"
               "x.onload = function (e) { v = e.type + ' ' + (e.target === x); };"
               "x.open('GET', '/ciao'); x.send();", "v", "load true");
    prova_dopo("onerror per quella che non parte", PAG,
               "var v = 'niente';"
               "var x = new XMLHttpRequest();"
               "x.onerror = function () { v = 'errore'; };"
               "x.onload  = function () { v = 'carico'; };"
               "x.open('GET', '/altrove'); x.send();", "v", "errore");
    prova_dopo("onreadystatechange vede lo stato quattro", PAG,
               "var v = 0;"
               "var x = new XMLHttpRequest();"
               "x.onreadystatechange = function () { v = this.readyState; };"
               "x.open('GET', '/ciao'); x.send();", "v", "4");
    /* ! addEventListener SU UN XHR NON PASSA DALLA TABELLA DEGLI ASCOLTI del
     * ponte: quella e' indicizzata per NODO, e una richiesta non e' un nodo. */
    prova_dopo("addEventListener, e piu' di uno", PAG,
               "var v = '';"
               "var x = new XMLHttpRequest();"
               "x.addEventListener('load', function () { v = v + 'a'; });"
               "x.addEventListener('load', function () { v = v + 'b'; });"
               "x.open('GET', '/ciao'); x.send();", "v", "ab");
    prova_dopo("POST manda il corpo", PAG,
               "var x = new XMLHttpRequest(); x.open('POST', '/eco');"
               "x.send('ciao mondo');", "x.responseText", "ciao mondo");
    prova_dopo("getResponseHeader conosce il tipo", PAG,
               "var x = new XMLHttpRequest(); x.open('GET', '/ciao'); x.send();",
               "x.getResponseHeader('Content-Type')", "text/plain");
    /* ! DI UN'ALTRA INTESTAZIONE SI RENDE null, non "": nel DOM la differenza
     * e' fra «non c'e'» e «c'e' ed e' vuota», e le pagine ci contano. */
    prova_dopo("di un'altra rende null", PAG,
               "var x = new XMLHttpRequest(); x.open('GET', '/ciao'); x.send();",
               "String(x.getResponseHeader('X-Qualcosa'))", "null");
    prova_dopo("setRequestHeader si accetta e non ferma niente", PAG,
               "var x = new XMLHttpRequest(); x.open('GET', '/ciao');"
               "x.setRequestHeader('X-Prova', '1'); x.send();", "x.status", "200");

    printf("\n--- fetch ---\n");

    prova_val("fetch rende qualcosa con then", PAG,
              "typeof fetch('/ciao').then", "function");
    prova_dopo("la catena arriva al testo", PAG,
               "var v = '';"
               "fetch('/ciao').then(function (r) { return r.text(); })"
               "              .then(function (t) { v = t; });", "v", "buongiorno");
    prova_dopo("ok, status e url", PAG,
               "var v = '';"
               "fetch('/ciao').then(function (r) {"
               "  v = r.ok + ' ' + r.status + ' ' + r.url; });", "v",
               "true 200 /ciao");
    /* ! UN 404 NON RIFIUTA LA PROMESSA, ed e' la regola di fetch: la rete ha
     * risposto benissimo, e' il server che ha detto di no. */
    prova_dopo("un 404 non rifiuta, e ok e' falso", PAG,
               "var v = '';"
               "fetch('/manca').then(function (r) { v = r.ok + ' ' + r.status; },"
               "                     function ()  { v = 'rifiutata'; });", "v",
               "false 404");
    prova_dopo("la rete che non risponde rifiuta", PAG,
               "var v = 'niente';"
               "fetch('/altrove').then(function () { v = 'arrivata'; })"
               "                 .catch(function (e) { v = 'no: ' + e.message; });",
               "v", "no: la richiesta non e' partita");
    /* ! IL RIFIUTO SCENDE LUNGO LA CATENA fino a chi lo prende: e' cosi' che
     * `.then(a).then(b).catch(c)` fa arrivare l'errore a `c` e non ad `a`. */
    prova_dopo("il rifiuto scavalca i then e arriva al catch", PAG,
               "var v = '';"
               "fetch('/altrove').then(function () { v = v + 'a'; })"
               "                 .then(function () { v = v + 'b'; })"
               "                 .catch(function () { v = v + 'c'; });", "v", "c");
    prova_dopo("json passa da JSON.parse del motore", PAG,
               "var v = '';"
               "fetch('/dati.json').then(function (r) { return r.json(); })"
               "                   .then(function (d) { v = d.n + ' ' + d.s; });",
               "v", "7 ciao");
    prova_dopo("headers.get", PAG,
               "var v = '';"
               "fetch('/dati.json').then(function (r) {"
               "  v = r.headers.get('content-type'); });", "v", "application/json");
    prova_dopo("fetch con metodo e corpo", PAG,
               "var v = '';"
               "fetch('/eco', { method: 'POST', body: 'roba' })"
               "  .then(function (r) { return r.text(); })"
               "  .then(function (t) { v = t; });", "v", "roba");
    prova_dopo("finally passa e non cambia il valore", PAG,
               "var v = '';"
               "fetch('/ciao').finally(function () { v = v + 'f'; })"
               "              .then(function (r) { v = v + r.status; });", "v",
               "f200");

    /* ! SENZA GANCIO NON SI FINGE UN 200 VUOTO. Un browser che non ha
     * registrato la rete non e' un server che ha risposto male, e una pagina
     * che ricevesse «200, zero byte» concluderebbe la cosa sbagliata. */
    {
        ExJsCtx *c;
        ExDom   *D;

        D = apparecchia_rete(PAG, &c, 0);
        prova_gia("senza gancio, XHR lascia zero", c,
                  "var x = new XMLHttpRequest(); x.open('GET', '/ciao');"
                  "x.send(); x.status", "0");
        gira(c, "var v = '';"
                "fetch('/ciao').catch(function (e) { v = e.name; });", "senza");
        pompa_rete(D);
        prova_gia("senza gancio, fetch rifiuta", c, "v", "TypeError");
    }

    /* Il gancio riceve metodo e indirizzo com'e' scritto nella pagina: e'
     * chi lo registra a doverlo risolvere, e la prova lo fissa. */
    {
        ExJsCtx *c;
        ExDom   *D;

        D = apparecchia(PAG, &c);
        gira(c, "var x = new XMLHttpRequest();"
                "x.open('POST', '/eco'); x.send('abc');", "gancio");
        /* ! IL GANCIO NON L'HA ANCORA VISTO: `send` mette in coda. Questa riga
         * e' la prova che l'asincrono e' asincrono. */
        ok("prima della pompa il gancio non ha visto niente", g_rete_n == 0, "");
        pompa_rete(D);
        ok("il gancio ha visto metodo e indirizzo",
           strcmp(g_rete_ultima, "POST /eco") == 0, g_rete_ultima);
        ok("e il corpo", strcmp(g_rete_corpo, "abc") == 0, g_rete_corpo);
        ok("una richiesta sola", g_rete_n == 1, "");
    }

    /* =========================================================================
     * L'ASINCRONO
     *
     * ! LA PROVA CHE CONTA E' L'ORDINE, non il risultato. Che `onload` prima o
     * poi si chiami lo diceva anche la versione sincrona; quel che distingue
     * l'asincrono e' che il codice DOPO `send()` gira PRIMA del gestore. Un
     * contatore di lettere e' il modo piu' corto di scriverlo.
     * ====================================================================== */
    printf("\n--- l'asincrono ---\n");

    prova_val("send torna subito, e lo stato resta 1", PAG,
              "var x = new XMLHttpRequest();"
              "x.open('GET', '/ciao'); x.send();"
              "x.readyState + ' ' + x.status + ' ' + x.responseText", "1 0 ");
    /* ! QUESTA E' LA RIGA CHE DEFINISCE L'ASINCRONO. Con la versione di prima
     * il risultato era «BA»: il gestore girava dentro send(). */
    prova_dopo("il codice dopo send gira PRIMA di onload", PAG,
               "var v = '';"
               "var x = new XMLHttpRequest();"
               "x.onload = function () { v = v + 'B'; };"
               "x.open('GET', '/ciao'); x.send();"
               "v = v + 'A';", "v", "AB");
    /* ! E LA FORMA SINCRONA RESTA SINCRONA, che e' quel che promette:
     * `open(m, u, false)` esiste apposta, e c'e' del codice che ci conta. */
    prova_val("con async=false il gestore gira dentro send", PAG,
              "var v = '';"
              "var x = new XMLHttpRequest();"
              "x.onload = function () { v = v + 'B'; };"
              "x.open('GET', '/ciao', false); x.send();"
              "v = v + 'A'; v", "BA");
    prova_val("e la risposta c'e' gia' quando send torna", PAG,
              "var x = new XMLHttpRequest();"
              "x.open('GET', '/ciao', false); x.send();"
              "x.readyState + ' ' + x.responseText", "4 buongiorno");

    /* ! PIU' RICHIESTE INSIEME: prima non si poteva nemmeno chiedere, perche'
     * la prima non tornava finche' non era finita. */
    prova_dopo("due richieste insieme arrivano tutt'e due", PAG,
               "var v = '';"
               "var a = new XMLHttpRequest();"
               "var b = new XMLHttpRequest();"
               "a.onload = function () { v = v + 'a'; };"
               "b.onload = function () { v = v + 'b'; };"
               "a.open('GET', '/ciao'); a.send();"
               "b.open('GET', '/manca'); b.send();", "v", "ab");

    /* --- le promesse che sanno aspettare ---------------------------------- */
    prova_val("fetch rende una promessa ancora in sospeso", PAG,
              "var p = fetch('/ciao');"
              "typeof p.then + ' ' + p.__stato", "function 0");
    prova_dopo("e si risolve quando la risposta arriva", PAG,
               "var p = fetch('/ciao');", "p.__stato", "1");
    /* ! UN `then` MESSO PRIMA DELLA RISPOSTA DEVE SCATTARE LO STESSO: e' tutta
     * la differenza fra una promessa e un valore. */
    prova_dopo("un then messo prima scatta dopo", PAG,
               "var v = 'niente';"
               "fetch('/ciao').then(function (r) { v = 'stato ' + r.status; });",
               "v", "stato 200");
    /* ! E UNO MESSO DOPO, SU UNA PROMESSA GIA' RISOLTA, SCATTA SUBITO. Sono le
     * due strade di `then`, e vanno provate tutt'e due. */
    prova_dopo("e uno messo dopo scatta subito", PAG,
               "var v = 'niente';"
               "var p = fetch('/ciao');", 
               "p.then(function (r) { v = 'poi ' + r.status; }); v", "poi 200");
    prova_dopo("la catena si costruisce prima che ci sia il valore", PAG,
               "var v = '';"
               "fetch('/ciao').then(function (r) { return r.text(); })"
               "              .then(function (t) { return t.length; })"
               "              .then(function (n) { v = 'lungo ' + n; });",
               "v", "lungo 10");
    prova_dopo("finally scatta anche sulla sospesa", PAG,
               "var v = '';"
               "fetch('/ciao').finally(function () { v = v + 'f'; })"
               "              .then(function (r) { v = v + r.status; });",
               "v", "f200");
    prova_dopo("e su una rifiutata finally scatta lo stesso", PAG,
               "var v = '';"
               "fetch('/altrove').finally(function () { v = v + 'f'; })"
               "                 .catch(function () { v = v + '!'; });",
               "v", "f!");
    /* ! UNA PROMESSA SI RISOLVE UNA VOLTA SOLA, e il gestore si chiama una
     * volta sola: senza, un `then` su una promessa gia' risolta e poi ripompata
     * scatterebbe due volte. */
    prova_dopo("il gestore si chiama una volta sola", PAG,
               "var n = 0;"
               "fetch('/ciao').then(function () { n = n + 1; });", "n", "1");

    /* --- la coda ---------------------------------------------------------- */
    {
        ExJsCtx *c;
        ExDom   *D;
        char     codice[256];
        int      i;

        D = apparecchia(PAG, &c);
        gira(c, "var v = 0; var s = [];", "coda");
        for (i = 0; i < 12; i++) {
            sprintf(codice,
                    "s[%d] = new XMLHttpRequest();"
                    "s[%d].onload = function () { v = v + 1; };"
                    "s[%d].open('GET', '/ciao'); s[%d].send();", i, i, i, i);
            gira(c, codice, "coda");
        }
        /* ! LA CODA PIENA SI DICE, e chi non ci sta fallisce SUBITO invece di
         * aspettare una risposta che non arrivera' mai. */
        ok("la coda piena si dice", exdom_rete_persa(D) == 1, "");
        ok("e quante ne aspettano si sa",
           exdom_rete_in_attesa(D) == 8, "");
        pompa_rete(D);
        ok("dopo la pompa non ne aspetta piu' nessuna",
           exdom_rete_in_attesa(D) == 0, "");
        prova_gia("e le otto che ci stavano sono arrivate", c, "v", "8");
    }

    printf("\n%d prove, %d sbagliate\n\n", fatte, sbagliate);
    return sbagliate ? 1 : 0;
}
