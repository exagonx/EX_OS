/* Banco di prova di excss, sull'host. Come htmlprova e httpprova:
 * i difetti che contano stanno nei fogli MALFATTI, e quelli si scrivono. */
#include <stdio.h>
#include <string.h>
#include "html.h"
#include "css.h"

static HtmlNodo  g_nodi[512];
static HtmlAttr  g_attr[256];
static char      g_arena[16384];
static HtmlDoc   g_doc;

static CssRegola g_reg[128];
static CssDich   g_dich[512];
static char      g_carena[8192];
static CssFoglio g_fog;

static int falliti = 0, fatti = 0;

static void ok(const char *t, int cond)
{
    fatti++;
    if (!cond) { printf("  *** FALLITO: %s\n", t); falliti++; }
}

/* Trova il primo elemento con quel nome. */
static int trova(const char *nome)
{
    unsigned int i;
    for (i = 0; i < g_doc.nodi_n; i++)
        if (g_doc.nodi[i].tipo == HTML_ELEMENTO &&
            strcmp(html_nome(&g_doc, (int)i), nome) == 0) return (int)i;
    return -1;
}

/* Trova l'ennesimo elemento con quel nome (0 = il primo). */
static int trova_n(const char *nome, int quale)
{
    unsigned int i;
    for (i = 0; i < g_doc.nodi_n; i++)
        if (g_doc.nodi[i].tipo == HTML_ELEMENTO &&
            strcmp(html_nome(&g_doc, (int)i), nome) == 0 && quale-- == 0)
            return (int)i;
    return -1;
}

static void carica(const char *html, const char *css)
{
    html_prepara(&g_doc, g_nodi, 512, g_attr, 256, g_arena, sizeof(g_arena));
    html_analizza(&g_doc, html, (unsigned int)strlen(html));
    css_prepara(&g_fog, g_reg, 128, g_dich, 512, g_carena, sizeof(g_carena));
    if (css) css_analizza(&g_fog, css, (unsigned int)strlen(css), CSS_ORIGINE_FOGLIO);
}

/* Lo stile di un elemento, ereditando lungo tutta la catena dei padri. */
static void stile_di(int nodo, CssStile *out)
{
    int catena[32], n = 0, i;
    CssStile s;

    while (nodo >= 0 && n < 32) { catena[n++] = nodo; nodo = g_doc.nodi[nodo].padre; }

    css_stile_vuoto(&s);
    for (i = n - 1; i >= 0; i--) {
        CssStile q;
        css_calcola(&g_fog, &g_doc, catena[i], &s, &q);
        s = q;
    }
    *out = s;
}

int main(void)
{
    CssStile s;

    printf("\n=== il minimo ===\n");
    carica("<p>ciao</p>", "p { color: red }");
    stile_di(trova("p"), &s);
    ok("p { color: red }", s.colore == 0xFFFF0000u);

    printf("\n=== i colori ===\n");
    carica("<p>x</p>", "p { color: #abc }");
    stile_di(trova("p"), &s);
    ok("#abc vale #aabbcc", s.colore == 0xFFAABBCCu);
    carica("<p>x</p>", "p { color: #12ef56 }");
    stile_di(trova("p"), &s);
    ok("#12ef56", s.colore == 0xFF12EF56u);
    carica("<p>x</p>", "p { color: NAVY }");
    stile_di(trova("p"), &s);
    ok("i nomi non distinguono maiuscole", s.colore == 0xFF000080u);
    carica("<p>x</p>", "p { color: verdolino }");
    stile_di(trova("p"), &s);
    ok("un colore inventato non si applica", s.colore == CSS_NIENTE);

    printf("\n=== le misure ===\n");
    carica("<p>x</p>", "p { font-size: 20px }");
    stile_di(trova("p"), &s);
    ok("20px", s.corpo == 20);
    carica("<p>x</p>", "p { font-size: 20 }");
    stile_di(trova("p"), &s);
    ok("20 senza unita'", s.corpo == 20);
    carica("<p>x</p>", "p { font-size: 2em }");
    stile_di(trova("p"), &s);
    ok("2em si RIFIUTA, non si indovina", s.corpo == CSS_MISURA_NO);

    printf("\n=== la specificita' ===\n");
    carica("<p class='c' id='i'>x</p>",
           "p { color: red } .c { color: green } #i { color: blue }");
    stile_di(trova("p"), &s);
    ok("id batte classe batte tipo", s.colore == 0xFF0000FFu);
    carica("<p class='c'>x</p>", ".c { color: green } p { color: red }");
    stile_di(trova("p"), &s);
    ok("la classe batte il tipo anche se viene prima", s.colore == 0xFF008000u);
    carica("<p>x</p>", "p { color: red } p { color: green }");
    stile_di(trova("p"), &s);
    ok("a parita' di peso vince l'ultima", s.colore == 0xFF008000u);

    printf("\n=== la discendenza ===\n");
    carica("<div><p>x</p></div><p>y</p>", "div p { color: red }");
    stile_di(trova_n("p", 0), &s);
    ok("il p dentro il div si colora", s.colore == 0xFFFF0000u);
    stile_di(trova_n("p", 1), &s);
    ok("il p fuori no", s.colore == CSS_NIENTE);
    carica("<div><span><p>x</p></span></div>", "div p { color: red }");
    stile_di(trova("p"), &s);
    ok("la discendenza salta i livelli in mezzo", s.colore == 0xFFFF0000u);

    printf("\n=== quello che si SCARTA invece di indovinare ===\n");
    carica("<div><span><p>x</p></span></div>", "div > p { color: red }");
    stile_di(trova("p"), &s);
    ok("«>» scarta la regola, non la tratta da discendenza",
       s.colore == CSS_NIENTE);
    carica("<a><b><c><d><p>x</p></d></c></b></a>",
           "a b c d p { color: red }");
    stile_di(trova("p"), &s);
    ok("un selettore piu' lungo del tetto si scarta", s.colore == CSS_NIENTE);

    printf("\n=== l'ereditarieta' ===\n");
    carica("<div><p>x</p></div>", "div { color: red; background-color: blue }");
    stile_di(trova("p"), &s);
    ok("il colore scende", s.colore == 0xFFFF0000u);
    ok("lo sfondo NON scende", s.sfondo == CSS_NIENTE);

    printf("\n=== lo style= vince ===\n");
    carica("<p id='i' style='color: lime'>x</p>", "#i { color: red }");
    stile_di(trova("p"), &s);
    ok("style batte anche un id", s.colore == 0xFF00FF00u);

    printf("\n=== i fogli malfatti non fermano il resto ===\n");
    carica("<p>x</p>", "@media screen { p { color: red } } p { color: green }");
    stile_di(trova("p"), &s);
    ok("@media si salta INTERO, la regola dopo si legge", s.colore == 0xFF008000u);
    carica("<p>x</p>", "/* commento */ p /* qui */ { color: red }");
    stile_di(trova("p"), &s);
    ok("i commenti stanno dove capita", s.colore == 0xFFFF0000u);
    carica("<p>x</p>", "p { color red; font-size: 9px }");
    stile_di(trova("p"), &s);
    ok("dichiarazione senza ':' saltata...", s.colore == CSS_NIENTE);
    ok("...ma quella dopo si legge", s.corpo == 9);
    carica("<p>x</p>", "p { color: red !important }");
    stile_di(trova("p"), &s);
    ok("!important si toglie e il valore resta", s.colore == 0xFFFF0000u);
    carica("<p>x</p>", "p { color: red");
    stile_di(trova("p"), &s);
    ok("graffa mai chiusa: si legge lo stesso", s.colore == 0xFFFF0000u);
    carica("<p>x</p>", "");
    stile_di(trova("p"), &s);
    ok("foglio vuoto", s.colore == CSS_NIENTE);
    carica("<p>x</p>", "}}}{{{;;;");
    ok("solo spazzatura: non si schianta", 1);

    carica("<p>x</p>", "p { color: /* qui */ red }");
    stile_di(trova("p"), &s);
    ok("un commento dentro le graffe", s.colore == 0xFFFF0000u);
    carica("<p>x</p>", "@media a { @media b { p{color:red} } } p { color: green }");
    stile_di(trova("p"), &s);
    ok("@media annidati si saltano tutti", s.colore == 0xFF008000u);
    {
        /* Un foglio piu' grande dei buffer: si tronca e LO DICE. */
        static char grosso[40000];
        int i, k = 0;
        for (i = 0; i < 400; i++)
            k += sprintf(grosso + k, ".c%d { color: red } ", i);
        carica("<p>x</p>", grosso);
        ok("un foglio che sfora si dichiara troncato", g_fog.troncato == 1);
    }

    printf("\n=== l'elenco di selettori ===\n");
    carica("<h1>a</h1><h2>b</h2>", "h1, h2 { color: red }");
    stile_di(trova("h1"), &s);
    ok("h1 dell'elenco", s.colore == 0xFFFF0000u);
    stile_di(trova("h2"), &s);
    ok("h2 dell'elenco", s.colore == 0xFFFF0000u);

    printf("\n=== le altre proprieta' ===\n");
    carica("<p>x</p>", "p { font-weight: bold; font-style: italic;"
                       " text-align: center; display: none; margin-top: 7px }");
    stile_di(trova("p"), &s);
    ok("font-weight", s.grassetto == 1);
    ok("font-style",  s.corsivo == 1);
    ok("text-align",  s.allineamento == CSS_ALL_CENTRO);
    ok("display",     s.display == CSS_DISPLAY_NIENTE);
    ok("margin-top",  s.margine[0] == 7);
    carica("<p>x</p>", "p { font-weight: 700 }");
    stile_di(trova("p"), &s);
    ok("font-weight numerico", s.grassetto == 1);

    printf("\n=== le classi sono un ELENCO ===\n");
    carica("<p class='uno due tre'>x</p>", ".due { color: red }");
    stile_di(trova("p"), &s);
    ok("la classe in mezzo all'elenco", s.colore == 0xFFFF0000u);
    carica("<p class='duetto'>x</p>", ".due { color: red }");
    stile_di(trova("p"), &s);
    ok("non basta il prefisso", s.colore == CSS_NIENTE);

    printf("\n%d prove, %d fallite\n", fatti, falliti);
    return falliti ? 1 : 0;
}
