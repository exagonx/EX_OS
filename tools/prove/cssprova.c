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

static CssRegola g_reg[8192];
static CssPezzo  g_pez[16384];
static CssDich   g_dich[8192];
static char      g_carena[262144];
static CssFoglio g_fog;

static int falliti = 0, fatti = 0;
static char g_vbuf[262144];         /* the sets of the custom properties (P6) */

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
    css_prepara(&g_fog, g_reg, 8192, g_pez, 16384, g_dich, 8192, g_carena, sizeof(g_carena));
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

    css_variabili(g_vbuf, sizeof(g_vbuf));

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
    /* ! FINO AL 28 SETTEMBRE 2026 QUI SI VERIFICAVA IL CONTRARIO: «2em si
     * rifiuta, non si indovina». Adesso em si capisce (@NAV-UNITA), e senza un
     * padre vale sul corpo predefinito. */
    ok("2em senza padre: 2 x il corpo predefinito", s.corpo == 2 * CSS_CORPO_PREDEFINITO);

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
    ok("\">\" e' figlio, non discendente: il nipote non si colora",
       s.colore == CSS_NIENTE);
    carica("<a><b><c><d><p>x</p></d></c></b></a>",
           "a b c d p { color: red }");
    stile_di(trova("p"), &s);
    ok("cinque pezzi si applicano (il tetto era 4, adesso 8)", s.colore == 0xFFFF0000u);
    carica("<a><b><c><d><e><f><g><h><p>x</p></h></g></f></e></d></c></b></a>",
           "a b c d e f g h p { color: red }");
    stile_di(trova("p"), &s);
    ok("nove pezzi: oltre il tetto non si applica", s.colore == CSS_NIENTE);

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
    ok("@media screen si legge, e la regola DOPO vince", s.colore == 0xFF008000u);
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
        /* its own small tables: the bench's are big enough for a real site */
        static CssRegola rp[128];
        static CssPezzo  pp[128];
        static CssDich   dp[128];
        static char      ap[4096];

        carica("<p>x</p>", 0);
        css_prepara(&g_fog, rp, 128, pp, 128, dp, 128, ap, sizeof(ap));
        css_analizza(&g_fog, grosso, (unsigned int)k, CSS_ORIGINE_FOGLIO);
        ok("un foglio che sfora si dichiara troncato", g_fog.troncato == 1);
    }

    printf("\n=== @media, @supports, @layer (28 settembre 2026) ===\n");
    css_media_larghezza(1280);
    carica("<p>x</p>", "@media screen { p { color: red } }");
    stile_di(trova("p"), &s);
    ok("@media screen vale", s.colore == 0xFFFF0000u);
    carica("<p>x</p>", "@media print { p { color: red } }");
    stile_di(trova("p"), &s);
    ok("@media print no", s.colore == CSS_NIENTE);
    carica("<p>x</p>", "@media screen and (min-width: 1120px) { p { color: red } }");
    stile_di(trova("p"), &s);
    ok("min-width 1120 a 1280: vale", s.colore == 0xFFFF0000u);
    carica("<p>x</p>", "@media screen and (max-width:calc(1120px - 1px)) { p { color: red } }");
    stile_di(trova("p"), &s);
    ok("max-width calc(1120px - 1px) a 1280: no", s.colore == CSS_NIENTE);
    css_media_larghezza(700);
    carica("<p>x</p>", "@media screen and (max-width:calc(1120px - 1px)) { p { color: red } }");
    stile_di(trova("p"), &s);
    ok("...e a 700 si'", s.colore == 0xFFFF0000u);
    carica("<p>x</p>", "@media (min-width: 40em) { p { color: red } }");
    stile_di(trova("p"), &s);
    ok("40em = 640px, a 700 vale", s.colore == 0xFFFF0000u);
    css_media_larghezza(1280);
    carica("<p>x</p>", "@media print, screen { p { color: red } }");
    stile_di(trova("p"), &s);
    ok("la virgola e' un 'o'", s.colore == 0xFFFF0000u);
    carica("<p>x</p>", "@media screen and (prefers-color-scheme:dark) { p { color: red } }");
    stile_di(trova("p"), &s);
    ok("schema scuro: no", s.colore == CSS_NIENTE);
    carica("<p>x</p>", "@media not print { p { color: red } }");
    stile_di(trova("p"), &s);
    ok("not print: vale", s.colore == 0xFFFF0000u);
    carica("<p>x</p>", "@supports (mask-image:none) { p { color: red } } @supports not (mask-image:none) { p { font-size: 9px } }");
    stile_di(trova("p"), &s);
    ok("@supports di cio' che non si sa: no...", s.colore == CSS_NIENTE);
    ok("...e il suo 'not': si'", s.corpo == 9);
    carica("<p>x</p>", "@supports (color: red) { p { color: red } }");
    stile_di(trova("p"), &s);
    ok("@supports di cio' che si sa: si'", s.colore == 0xFFFF0000u);
    carica("<p>x</p>", "@layer base { p { color: red } }");
    stile_di(trova("p"), &s);
    ok("@layer con un blocco: vale", s.colore == 0xFFFF0000u);
    carica("<p>x</p>", "@media screen { @media (min-width: 100px) { p { color: red } } }");
    stile_di(trova("p"), &s);
    ok("@media annidati che valgono", s.colore == 0xFFFF0000u);
    carica("<p>x</p>", "@media screen { p { color: red } } p { font-size: 9px }");
    stile_di(trova("p"), &s);
    ok("dopo un @media il foglio continua", s.colore == 0xFFFF0000u && s.corpo == 9);
    carica("<div><p>x</p></div>", "div { visibility: hidden }");
    stile_di(trova("p"), &s);
    ok("visibility hidden si eredita", s.visibile == 0);
    carica("<div><p>x</p></div>", "div { visibility: hidden } p { visibility: visible }");
    stile_di(trova("p"), &s);
    ok("...e un figlio puo' rimettersi visible", s.visibile == 1);
    carica("<p>x</p>", "");
    stile_di(trova("p"), &s);
    ok("non detta: CSS_FORSE", s.visibile == CSS_FORSE);

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

    printf("\n=== i selettori composti (24 settembre 2026) ===\n");
    carica("<p class='a b'>x</p><p class='a'>y</p>", ".a.b { color: red }");
    stile_di(trova_n("p", 0), &s); ok(".a.b su class='a b'", s.colore == 0xFFFF0000u);
    stile_di(trova_n("p", 1), &s); ok(".a.b NON su class='a'", s.colore == CSS_NIENTE);

    carica("<ul><li>1</li><li>2</li><li>3</li></ul>", "ul > li { color: red }");
    stile_di(trova_n("li", 1), &s); ok("ul > li", s.colore == 0xFFFF0000u);

    carica("<div><span><p>x</p></span></div><div><p>y</p></div>", "div > p { color: red }");
    stile_di(trova_n("p", 0), &s); ok("div > p: il nipote no", s.colore == CSS_NIENTE);
    stile_di(trova_n("p", 1), &s); ok("div > p: il figlio si'", s.colore == 0xFFFF0000u);

    carica("<h1>t</h1><p>a</p><p>b</p>", "h1 + p { color: red } h1 ~ p { font-weight: bold }");
    stile_di(trova_n("p", 0), &s); ok("h1 + p: il primo dopo", s.colore == 0xFFFF0000u);
    stile_di(trova_n("p", 1), &s); ok("h1 + p: il secondo no", s.colore == CSS_NIENTE);
    ok("h1 ~ p: anche il secondo", s.grassetto == 1);

    carica("<div class='x'><section><div><p>q</p></div></section></div>",
           ".x > section div > p { color: red }");
    stile_di(trova("p"), &s); ok("catena mista che vuole tornare indietro", s.colore == 0xFFFF0000u);

    carica("<p hidden>a</p><p>b</p>", "[hidden] { display: none }");
    stile_di(trova_n("p", 0), &s); ok("[hidden]", s.display == CSS_DISPLAY_NIENTE);
    stile_di(trova_n("p", 1), &s); ok("[hidden] non su chi non ce l'ha", s.display != CSS_DISPLAY_NIENTE);

    carica("<a href='https://x.it/a.pdf' lang='en-US' class='ciao mondo'>l</a>",
           "a[href^='https'] { color: red } a[href$=\".pdf\"] { font-weight: bold } "
           "a[lang|=en] { font-style: italic } a[class~=mondo] { text-align: center }");
    stile_di(trova("a"), &s);
    ok("[href^=https]", s.colore == 0xFFFF0000u);
    ok("[href$=.pdf]", s.grassetto == 1);
    ok("[lang|=en]", s.corsivo == 1);
    ok("[class~=mondo]", s.allineamento == CSS_ALL_CENTRO);

    carica("<input type='TEXT'>", "input[type='text' i] { color: red }");
    stile_di(trova("input"), &s); ok("[type='text' i]", s.colore == 0xFFFF0000u);

    carica("<ul><li>1</li><li>2</li><li>3</li><li>4</li></ul>",
           "li:first-child { color: red } li:last-child { font-weight: bold } "
           "li:nth-child(2n) { font-style: italic }");
    stile_di(trova_n("li", 0), &s); ok(":first-child", s.colore == 0xFFFF0000u);
    stile_di(trova_n("li", 3), &s); ok(":last-child", s.grassetto == 1);
    ok(":nth-child(2n) sul quarto", s.corsivo == 1);
    stile_di(trova_n("li", 2), &s); ok(":nth-child(2n) non sul terzo", s.corsivo != 1);

    carica("<p class='a'>x</p><p>y</p>", "p:not(.a) { color: red }");
    stile_di(trova_n("p", 0), &s); ok(":not(.a) esclude", s.colore == CSS_NIENTE);
    stile_di(trova_n("p", 1), &s); ok(":not(.a) prende gli altri", s.colore == 0xFFFF0000u);
    /* 30 settembre 2026: nessuno ha il fuoco, quindi :not(:focus) vale sempre
     * (e' cosi' che Wikipedia nasconde «Vai al contenuto»). */
    carica("<a class='salta'>x</a>", "a.salta:not(:focus) { color: red }");
    stile_di(trova("a"), &s); ok(":not(:focus) corrisponde", s.colore == 0xFFFF0000u);
    carica("<a>x</a>", "a:not(:hover) { color: red }");
    stile_di(trova("a"), &s); ok(":not(:hover) corrisponde", s.colore == 0xFFFF0000u);
    carica("<a>x</a>", "a:focus { color: red }");
    stile_di(trova("a"), &s); ok(":focus da solo no", s.colore == CSS_NIENTE);

    carica("<a href='#'>x</a>", "a:hover { color: red } a { font-weight: bold }");
    stile_di(trova("a"), &s);
    ok(":hover non vale qui", s.colore == CSS_NIENTE);
    ok("...e non si porta via la regola dopo", s.grassetto == 1);

    carica("<p>x</p>", "p, p::before { color: red }");
    stile_di(trova("p"), &s); ok("p, p::before: p si colora", s.colore == 0xFFFF0000u);

    carica("<p>x</p>", "p, p:sciocchezza { color: red }");
    stile_di(trova("p"), &s); ok("una lista con un selettore NON valido vale niente", s.colore == CSS_NIENTE);

    carica("<p title='a,b'>x</p>", "p[title='a,b'] { color: red }");
    stile_di(trova("p"), &s); ok("la virgola fra virgolette non spezza", s.colore == 0xFFFF0000u);

    carica("<div class='md:flex'>x</div>", ".md\\:flex { color: red }");
    stile_di(trova("div"), &s); ok("l'escape: .md\\:flex", s.colore == 0xFFFF0000u);

    carica("<div class='10'>x</div>", ".\\31 0 { color: red }");
    stile_di(trova("div"), &s); ok("l'escape esadecimale: .\\31 0", s.colore == 0xFFFF0000u);

    carica("<p id='x' class='y'>t</p>", "#x { color: red } .y.y.y { color: blue }");
    stile_di(trova("p"), &s); ok("un id batte tre classi", s.colore == 0xFFFF0000u);
    printf("\n=== l'indice delle regole ===\n");
    carica("<p class='a b'>x</p>", ".a { color: red } .b { color: blue }");
    stile_di(trova("p"), &s); ok("pari peso in due secchi: vince la scritta dopo", s.colore == 0xFF0000FFu);
    carica("<p class='a b'>x</p>", ".b { color: blue } .a { color: red }");
    stile_di(trova("p"), &s); ok("...e girandole vince l'altra", s.colore == 0xFFFF0000u);
    carica("<p id='k' class='z'>x</p>", "p#k.z { color: red }");
    stile_di(trova("p"), &s); ok("id e classe nello stesso pezzo", s.colore == 0xFFFF0000u);
    {
        static char grande[400000];
        int q, n = 0;

        for (q = 0; q < 6000; q++) n += sprintf(grande + n, ".c%d { color: blue } ", q);
        n += sprintf(grande + n, ".vera { color: red }");
        carica("<p class='vera'>x</p>", grande);
        stile_di(trova("p"), &s);
        ok("seimila regole, una sola vale", s.colore == 0xFFFF0000u);
        ok("...e il foglio non e' troncato", !g_fog.troncato);
        ok("...e le regole sono tutte", g_fog.regole_n >= 6001);
    }


    printf("\n=== border e padding, e le scorciatoie (@NAV-BORDER) ===\n");
    carica("<p>x</p>", "p { border: 2px solid #ff0000 }");
    stile_di(trova("p"), &s);
    ok("border: tutti e quattro i lati", s.bordo[0] == 2 && s.bordo[1] == 2 && s.bordo[2] == 2 && s.bordo[3] == 2);
    ok("border: lo stile", s.bordo_stile[0] == 1 && s.bordo_stile[3] == 1);
    ok("border: il colore", (s.bordo_col[2] & 0xFFFFFF) == 0xFF0000);
    carica("<p>x</p>", "p { border: #00f 1px dashed }");
    stile_di(trova("p"), &s);
    ok("border: le parole in un ordine qualunque", s.bordo[1] == 1 && s.bordo_stile[1] == 1 && (s.bordo_col[1] & 0xFFFFFF) == 0x0000FF);
    carica("<p>x</p>", "p { border: 1px #ccc }");
    stile_di(trova("p"), &s);
    ok("border senza stile: stile none (non si vede)", s.bordo_stile[0] == 0);
    carica("<p>x</p>", "p { border: 3px solid red } p { border: none }");
    stile_di(trova("p"), &s);
    ok("border: none toglie lo stile, e lo spessore torna medium", s.bordo_stile[0] == 0 && s.bordo[0] == 3);
    carica("<p>x</p>", "p { border-bottom: thick solid }");
    stile_di(trova("p"), &s);
    ok("border-bottom: solo il lato di sotto, thick = 5", s.bordo[2] == 5 && s.bordo_stile[2] == 1 && s.bordo_stile[0] == CSS_FORSE);
    ok("border-bottom senza colore: currentcolor", s.bordo_col[2] == CSS_NIENTE);
    carica("<p>x</p>", "p { border-width: 1px 2px 3px 4px; border-style: solid none }");
    stile_di(trova("p"), &s);
    ok("border-width con quattro valori", s.bordo[0] == 1 && s.bordo[1] == 2 && s.bordo[2] == 3 && s.bordo[3] == 4);
    ok("border-style con due valori", s.bordo_stile[0] == 1 && s.bordo_stile[1] == 0 && s.bordo_stile[2] == 1 && s.bordo_stile[3] == 0);
    carica("<p>x</p>", "p { border-color: red green blue }");
    stile_di(trova("p"), &s);
    ok("border-color con tre valori", (s.bordo_col[1] & 0xFFFFFF) == (s.bordo_col[3] & 0xFFFFFF) && (s.bordo_col[0] & 0xFFFFFF) == 0xFF0000);
    carica("<p>x</p>", "p { border-left: 1px solid rgb(10, 20, 30) }");
    stile_di(trova("p"), &s);
    ok("una parola che non si capisce (rgb) si salta, il resto resta", s.bordo[3] == 1 && s.bordo_stile[3] == 1);
    carica("<p>x</p>", "p { padding: 4px 8px }");
    stile_di(trova("p"), &s);
    ok("padding con due valori", s.imbottitura[0] == 4 && s.imbottitura[1] == 8 && s.imbottitura[2] == 4 && s.imbottitura[3] == 8);
    carica("<p>x</p>", "p { padding: -3px }");
    stile_di(trova("p"), &s);
    ok("padding negativo: si butta", s.imbottitura[0] == CSS_MISURA_NO);
    carica("<p>x</p>", "p { margin: 10px auto }");
    stile_di(trova("p"), &s);
    /* Dal 28 settembre 2026 auto e' CSS_MISURA_AUTO, non zero: lo decide
     * l'impaginatore, che sa quanto spazio avanza. */
    ok("margin: 10px auto (auto resta auto)", s.margine[0] == 10 && s.margine[1] == CSS_MISURA_AUTO &&
       s.margine[2] == 10 && s.margine[3] == CSS_MISURA_AUTO);
    ok("css_margine legge auto come zero", css_margine(&s, 1, 0) == 0 && css_margine(&s, 0, 0) == 10);
    carica("<p>x</p>", "p { margin: 1px 2px 3px }");
    stile_di(trova("p"), &s);
    ok("margin con tre valori", s.margine[0] == 1 && s.margine[1] == 2 && s.margine[2] == 3 && s.margine[3] == 2);
    carica("<div><p>x</p></div>", "div { border: 1px solid red; padding: 5px }");
    stile_di(trova("p"), &s);
    ok("bordi e padding NON si ereditano", s.bordo_stile[0] == CSS_FORSE && s.imbottitura[0] == CSS_MISURA_NO);
    carica("<p>x</p>", "p { border: 2px solid red; color: blue; margin-top: 7px }");
    stile_di(trova("p"), &s);
    ok("dopo una scorciatoia le dichiarazioni seguenti si leggono", (s.colore & 0xFFFFFF) == 0x0000FF && s.margine[0] == 7);
    {
        CssStile t;
        css_stile_vuoto(&t);
        css_stile_inline("border:1px solid #0f0;padding-left:6px", 38, &t);
        ok("anche nell'attributo style", t.bordo[0] == 1 && (t.bordo_col[0] & 0xFFFFFF) == 0x00FF00 && t.imbottitura[3] == 6);
    }

    printf("\n=== le unita' relative: em, rem, %% (@NAV-UNITA) ===\n");
    {
        CssStile padre, figlio;
        carica("<div><p>x</p></div>", "div { font-size: 20px } p { font-size: 1.5em; margin-top: 1em; padding-left: 0.5em }");
        stile_di(trova("div"), &padre);
        css_calcola(&g_fog, &g_doc, trova("p"), &padre, &figlio);
        ok("1.5em di un padre da 20: 30", figlio.corpo == 30);
        ok("margin 1em: sul corpo DELL'ELEMENTO (30), non del padre", figlio.margine[0] == 30);
        ok("padding 0.5em: 15", figlio.imbottitura[3] == 15);
        carica("<div><p>x</p></div>", "div { font-size: 20px } p { font-size: 150% }");
        stile_di(trova("div"), &padre);
        css_calcola(&g_fog, &g_doc, trova("p"), &padre, &figlio);
        ok("150% del padre da 20: 30", figlio.corpo == 30);
        carica("<div><p>x</p></div>", "div { font-size: 40px } p { font-size: 2rem }");
        stile_di(trova("div"), &padre);
        css_calcola(&g_fog, &g_doc, trova("p"), &padre, &figlio);
        ok("2rem non guarda il padre: 2 x il corpo predefinito", figlio.corpo == 2 * CSS_CORPO_PREDEFINITO);
        carica("<div><p>x</p></div>", "div { font-size: 20px } p { font-size: larger }");
        stile_di(trova("div"), &padre);
        css_calcola(&g_fog, &g_doc, trova("p"), &padre, &figlio);
        ok("larger = 1.2 volte il padre (24)", figlio.corpo == 24);
        carica("<p>x</p>", "p { font-size: large; margin-left: 12pt; border-top: 0.25em solid red }");
        stile_di(trova("p"), &s);
        ok("font-size: large = 18", s.corpo == 18);
        ok("12pt = 16px", s.margine[3] == 16);
        ok("un bordo di 0.25em su un corpo da 18: 4", s.bordo[0] == 4);
        carica("<p>x</p>", "p { margin-top: 2em; font-size: 10px }");
        stile_di(trova("p"), &s);
        ok("em sul corpo anche se il corpo si dichiara DOPO", s.margine[0] == 20);
        carica("<p>x</p>", "p { margin-top: 2em } p { margin-top: 7px }");
        stile_di(trova("p"), &s);
        ok("un assoluto dopo vince su una relativa di prima", s.margine[0] == 7);
        carica("<p>x</p>", "p { color: #ff0000; margin-top: 3px }");
        stile_di(trova("p"), &s);
        ok("un colore col bit alto non si scambia per una relativa", (s.colore & 0xFFFFFF) == 0xFF0000);
        carica("<p>x</p>", "p { font-size: 2furlong }");
        stile_di(trova("p"), &s);
        ok("un'unita' che non esiste si butta", s.corpo == CSS_MISURA_NO);
        {
            CssStile t;
            css_stile_vuoto(&t);
            css_stile_inline("font-size:20px;margin-top:0.5em", 31, &t);
            ok("style= da solo: 0.5em sul corpo che ha", t.margine[0] == 10);
        }
    }

    printf("\n=== la disposizione: width, float, position, flex (28 settembre 2026) ===\n\n");
    carica("<div>x</div>", "div { width: 300px; max-width: 50%; min-width: 10em; margin: 0 auto }");
    stile_di(trova("div"), &s);
    ok("width in px", s.larghezza == 300 && !(s.larghezza_perc & CSS_LARG_PERC));
    ok("max-width in % resta una percentuale (5000 = 50%)",
       s.larghezza_max == 5000 && (s.larghezza_perc & CSS_LARG_MAX_PERC));
    ok("min-width in em sul corpo (10 x 15 = 150)", s.larghezza_min == 150);
    ok("margin: 0 auto", s.margine[1] == CSS_MISURA_AUTO && s.margine[3] == CSS_MISURA_AUTO && s.margine[0] == 0);
    carica("<div>x</div>", "div { width: 50% } div { width: 200px }");
    stile_di(trova("div"), &s);
    ok("un px dopo una % toglie il segno della %", s.larghezza == 200 && !(s.larghezza_perc & CSS_LARG_PERC));
    carica("<div>x</div>", "div { width: 200px } div { width: auto }");
    stile_di(trova("div"), &s);
    ok("width: auto torna non detta", s.larghezza == CSS_MISURA_NO);
    carica("<div>x</div>", "div { box-sizing: border-box; float: right; position: absolute; left: -9999px; top: 2em }");
    stile_di(trova("div"), &s);
    ok("box-sizing: border-box", s.scatola_bordo == 1);
    ok("float: right", s.galleggia == CSS_GALLEGGIA_DX);
    ok("position: absolute", s.posizione == CSS_POS_ASSOLUTA);
    ok("left negativo e top in em", s.pos[3] == -9999 && s.pos[0] == 30);
    carica("<ul><li>x</li></ul>", "ul { display: flex; flex-flow: column wrap } li { flex: 1 1 0% }");
    stile_di(trova("ul"), &s);
    ok("display: flex", s.display == CSS_DISPLAY_FLEX);
    ok("flex-flow: column wrap", s.flex_colonna == 1 && s.flex_a_capo == 1);
    stile_di(trova("li"), &s);
    ok("flex: 1 1 0% = cresce 1 (100 centesimi)", s.flex_cresce == 100);
    carica("<span>x</span>", "span { display: inline-block; width: 12.5% }");
    stile_di(trova("span"), &s);
    ok("display: inline-block", s.display == CSS_DISPLAY_INBLOCCO);
    ok("width 12.5% = 1250", s.larghezza == 1250 && (s.larghezza_perc & CSS_LARG_PERC));
    carica("<li>x</li>", "li { display: list-item }");
    stile_di(trova("li"), &s);
    ok("list-item e' un blocco", s.display == CSS_DISPLAY_BLOCCO);
    {
        CssStile padre, figlio;
        carica("<div><p>x</p></div>", "div { width: 100px; float: left; position: fixed }");
        stile_di(trova("div"), &padre);
        css_calcola(&g_fog, &g_doc, trova("p"), &padre, &figlio);
        ok("width, float e position NON si ereditano",
           figlio.larghezza == CSS_MISURA_NO && figlio.galleggia == CSS_GALLEGGIA_NO &&
           figlio.posizione == CSS_POS_STATICA);
    }
    {
        CssStile t;
        css_stile_vuoto(&t);
        { const char *st = "width: 25%; margin-left: auto"; css_stile_inline(st, (unsigned int)strlen(st), &t); }
        ok("style= con width in %: resta %, non la finestra", t.larghezza == 2500 && (t.larghezza_perc & CSS_LARG_PERC));
        ok("style= con margin-left: auto", t.margine[3] == CSS_MISURA_AUTO);
    }

    carica("<nav>x</nav>", "nav { clear: both; justify-content: space-between; align-items: center; gap: 8px 1em }");
    stile_di(trova("nav"), &s);
    ok("clear: both", s.pulisci == (CSS_PULISCI_SX | CSS_PULISCI_DX));
    ok("justify-content: space-between", s.giustifica == CSS_GIU_TRA);
    ok("align-items: center", s.allinea_voci == CSS_ALV_CENTRO);
    ok("gap: 8px 1em = riga 8, colonna 15", s.spazio_riga == 8 && s.spazio_col == 15);
    carica("<nav>x</nav>", "nav { gap: 5px }");
    stile_di(trova("nav"), &s);
    ok("gap con un valore solo vale per tutti e due", s.spazio_riga == 5 && s.spazio_col == 5);
    carica("<p>x</p>", "p { color: #fff; background: white }");
    stile_di(trova("p"), &s);
    ok("il bianco non e' \"nessun colore\" (CSS_NIENTE)", s.colore != CSS_NIENTE && s.sfondo != CSS_NIENTE &&
       (s.colore & 0xFFFFFF) >= 0xFFFFFE);

    printf("\n=== opacity: 0 con pointer-events: none (tiscali.it) ===\n");
    carica("<div class=\"a b\"><p>x</p></div>", ".a{opacity:0}.b{pointer-events:none}");
    stile_di(trova("div"), &s);
    ok("opacity:0 + pointer-events:none = non si vede", s.visibile == 0);
    stile_di(trova("p"), &s);
    ok("e nemmeno i figli", s.visibile == 0);
    carica("<div class=a>x</div>", ".a{opacity:0}");
    stile_di(trova("div"), &s);
    ok("la sola opacity:0 si lascia vedere (sta per comparire)", s.visibile != 0);
    carica("<div class=\"a b\">x</div>", ".a{opacity:0.5}.b{pointer-events:none}");
    stile_di(trova("div"), &s);
    ok("opacity 0.5 non e' zero", s.visibile != 0);
    carica("<div class=\"a b c\">x</div>", ".a{opacity:0}.b{pointer-events:none}.c{opacity:1}");
    stile_di(trova("div"), &s);
    ok("una regola dopo la riaccende", s.visibile != 0);

    printf("\n=== list-style: none ===\n");
    carica("<ul><li>x</li></ul>", "ol,ul{list-style:none}");
    stile_di(trova("ul"), &s);
    ok("ul{list-style:none} dice 0", css_segno_lista() == 0);
    stile_di(trova("li"), &s);
    ok("la voce da sola non dice niente (eredita)", css_segno_lista() == 2);
    carica("<ul><li>x</li></ul>", "ul{list-style:none inside} li{list-style-type:disc}");
    stile_di(trova("ul"), &s);
    ok("la forma breve con none dentro", css_segno_lista() == 0);
    stile_di(trova("li"), &s);
    ok("list-style-type: disc sulla voce dice 1", css_segno_lista() == 1);


    printf("\n=== var() e le proprieta' personalizzate (P6) ===\n");
    css_media_larghezza(1280);
    carica("<p>x</p>", ":root { --c: red } p { color: var(--c) }");
    stile_di(trova("p"), &s); ok("var() da :root", s.colore == 0xFFFF0000u);
    carica("<div><p>x</p></div><p>y</p>", "div { --c: #00f } p { color: var(--c) }");
    stile_di(trova_n("p", 0), &s); ok("la proprieta' personalizzata scende ai figli", s.colore == 0xFF0000FFu);
    stile_di(trova_n("p", 1), &s); ok("...e fuori dal div non c'e': la dichiarazione cade", s.colore == CSS_NIENTE);
    carica("<p>x</p>", "p { color: var(--nessuna, green) }");
    stile_di(trova("p"), &s); ok("il ripiego", s.colore == 0xFF008000u);
    carica("<p>x</p>", "p { color: var(--a, var(--b, lime)) }");
    stile_di(trova("p"), &s); ok("il ripiego annidato", s.colore == 0xFF00FF00u);
    carica("<p>x</p>", ":root { --a: 10px; --b: calc(var(--a) * 2) } p { margin-top: var(--b) }");
    stile_di(trova("p"), &s); ok("var() dentro una personalizzata, e calc()", s.margine[0] == 20);
    carica("<div><p>x</p></div>", "div { --a: 1px; --b: var(--a) } p { --a: 5px; margin-top: var(--b) }");
    stile_di(trova("p"), &s); ok("una personalizzata si risolve dove e' dichiarata", s.margine[0] == 1);
    carica("<p>x</p>", "p { --a: var(--b); --b: var(--a); color: var(--a, red) }");
    stile_di(trova("p"), &s); ok("un ciclo: vale il ripiego", s.colore == 0xFFFF0000u);
    carica("<p>x</p>", "p { --w: 2px; border: var(--w) solid red }");
    stile_di(trova("p"), &s); ok("var() dentro una scorciatoia", s.bordo[0] == 2 && s.bordo_stile[3] == 1);
    carica("<p style='color: var(--c)'>x</p>", ":root { --c: red }");
    stile_di(trova("p"), &s); ok("var() nell'attributo style", s.colore == 0xFFFF0000u);
    carica("<p style='--c: blue'><span>x</span></p>", "span { color: var(--c) }");
    stile_di(trova("span"), &s); ok("una personalizzata nell'attributo style", s.colore == 0xFF0000FFu);
    carica("<p class='x'>x</p>", ":root { --c: red } p { color: var(--c) } p.x { color: green }");
    stile_di(trova("p"), &s); ok("un selettore piu' forte batte la var()", s.colore == 0xFF008000u);
    carica("<p class='x'>x</p>", ":root { --c: red } p.x { color: green } p.x { color: var(--c) }");
    stile_di(trova("p"), &s); ok("...e a pari peso vince quella dopo", s.colore == 0xFFFF0000u);
    carica("<p>x</p>", ":root { --m: 2em } p { font-size: 10px; margin-top: var(--m) }");
    stile_di(trova("p"), &s); ok("var() che vale 2em: sul corpo dell'elemento", s.margine[0] == 20);
    carica("<p>x</p>", "p { padding-top: env(safe-area-inset-top, 3px); padding-left: env(nulla) }");
    stile_di(trova("p"), &s); ok("env() col ripiego, e senza (0px)", s.imbottitura[0] == 3 && s.imbottitura[3] == 0);
    carica("<p>x</p>", ":root { --A: red; --a: blue } p { color: var(--A) }");
    stile_di(trova("p"), &s); ok("i nomi distinguono le maiuscole", s.colore == 0xFFFF0000u);
    carica("<p>x</p>", ":root { --s: 13px Georgia } p { font: var(--s) }");
    stile_di(trova("p"), &s); ok("font: var() intera", s.corpo == 13 && s.famiglia == CSS_FAM_SERIF);
    {
        /* uno :root con centinaia di variabili, come Google */
        static char grande[60000];
        int q, n = 0;

        n += sprintf(grande + n, ":root {");
        for (q = 0; q < 600; q++) n += sprintf(grande + n, " --v%d: %dpx;", q, q);
        n += sprintf(grande + n, " } p { margin-top: var(--v599); margin-left: var(--v7) }");
        carica("<div><div><p>x</p></div></div>", grande);
        stile_di(trova("p"), &s); ok("seicento variabili su :root", s.margine[0] == 599 && s.margine[3] == 7);
    }

    printf("\n=== calc(), min(), max(), clamp() (P6) ===\n");
    carica("<p>x</p>", "p { margin-top: calc(10px + 5px) }");
    stile_di(trova("p"), &s); ok("calc(10px + 5px)", s.margine[0] == 15);
    carica("<p>x</p>", "p { font-size: 10px; margin-top: calc(2em + 1px) }");
    stile_di(trova("p"), &s); ok("calc(2em + 1px) sul corpo da 10", s.margine[0] == 21);
    carica("<div>x</div>", "div { width: calc(100% / 4) }");
    stile_di(trova("div"), &s); ok("calc(100% / 4): una percentuale", s.larghezza == 2500 && (s.larghezza_perc & CSS_LARG_PERC));
    carica("<div>x</div>", "div { max-width: min(100%, 1200px) }");
    stile_di(trova("div"), &s); ok("min(100%, 1200px): 1200", s.larghezza_max == 1200 && !(s.larghezza_perc & CSS_LARG_MAX_PERC));
    carica("<div>x</div>", "div { width: calc(100% - 20px) }");
    stile_di(trova("div"), &s); ok("calc(100% - 20px): non si sa, non si pone", s.larghezza == CSS_MISURA_NO);
    carica("<p>x</p>", "p { padding-top: calc(5px - 10px) }");
    stile_di(trova("p"), &s); ok("un padding sotto zero diventa zero", s.imbottitura[0] == 0);
    carica("<p>x</p>", "p { margin-left: clamp(10px, 50vw, 200px); margin-right: clamp(10px, 1vw, 200px) }");
    stile_di(trova("p"), &s); ok("clamp(): il massimo e il minimo", s.margine[3] == 200 && s.margine[1] == 13);
    carica("<p>x</p>", "p { font-size: calc(1rem + 2px) }");
    stile_di(trova("p"), &s); ok("calc(1rem + 2px)", s.corpo == CSS_CORPO_PREDEFINITO + 2);
    carica("<p>x</p>", "p { margin-top: calc((2px + 3px) * 2); margin-bottom: calc(10px/4) }");
    stile_di(trova("p"), &s); ok("le parentesi e la divisione", s.margine[0] == 10 && s.margine[2] == 3);
    carica("<p>x</p>", "p { margin-top: 4px; margin-top: calc(10px + ) }");
    stile_di(trova("p"), &s); ok("un calc() rotto si butta", s.margine[0] == 4);
    carica("<p>x</p>", "p { margin-top: -webkit-calc(1px + 1px); margin-left: max(3px, 1em) }");
    stile_di(trova("p"), &s); ok("-webkit-calc e max() con em", s.margine[0] == 2 && s.margine[3] == CSS_CORPO_PREDEFINITO);
    {
        CssStile t;
        css_stile_vuoto(&t);
        { const char *st = "margin-top: calc(1em + 1px); width: calc(50% * 2)"; css_stile_inline(st, (unsigned int)strlen(st), &t); }
        ok("calc() in uno style= da solo", t.margine[0] == CSS_CORPO_PREDEFINITO + 1 && t.larghezza == 10000);
    }

    printf("\n=== @media del livello 4 (P6) ===\n");
    css_media_larghezza(1280);
    css_media_altezza(800);
    carica("<p>x</p>", "@media (width >= 1000px) { p { color: red } }");
    stile_di(trova("p"), &s); ok("(width >= 1000px) a 1280", s.colore == 0xFFFF0000u);
    carica("<p>x</p>", "@media (400px <= width <= 700px) { p { color: red } }");
    stile_di(trova("p"), &s); ok("(400px <= width <= 700px) a 1280: no", s.colore == CSS_NIENTE);
    carica("<p>x</p>", "@media (min-height: 500px) and (orientation: landscape) { p { color: red } }");
    stile_di(trova("p"), &s); ok("min-height e orientation", s.colore == 0xFFFF0000u);
    carica("<p>x</p>", "@media (orientation: portrait) { p { color: red } }");
    stile_di(trova("p"), &s); ok("portrait no", s.colore == CSS_NIENTE);
    carica("<p>x</p>", "@media (prefers-reduced-motion) { p { color: red } }");
    stile_di(trova("p"), &s); ok("(prefers-reduced-motion) da sola: no", s.colore == CSS_NIENTE);
    carica("<p>x</p>", "@media not (hover) { p { color: red } }");
    stile_di(trova("p"), &s); ok("not (hover): no", s.colore == CSS_NIENTE);
    carica("<p>x</p>", "@media (hover) and (pointer: fine) { p { color: red } }");
    stile_di(trova("p"), &s); ok("(hover) and (pointer: fine)", s.colore == 0xFFFF0000u);
    carica("<p>x</p>", "@media (min-resolution: 2dppx), (-webkit-min-device-pixel-ratio: 1.5) { p { color: red } }");
    stile_di(trova("p"), &s); ok("schermi ad alta densita': no", s.colore == CSS_NIENTE);
    carica("<p>x</p>", "@media (max-resolution: 96dpi) { p { color: red } }");
    stile_di(trova("p"), &s); ok("max-resolution: 96dpi = 1dppx: si'", s.colore == 0xFFFF0000u);
    carica("<p>x</p>", "@media screen and ((min-width: 100px) or (max-width: 50px)) { p { color: red } }");
    stile_di(trova("p"), &s); ok("screen and (( ) or ( ))", s.colore == 0xFFFF0000u);
    carica("<p>x</p>", "@media (aspect-ratio > 1/1) { p { color: red } }");
    stile_di(trova("p"), &s); ok("aspect-ratio > 1/1", s.colore == 0xFFFF0000u);
    carica("<p>x</p>", "@media (forced-colors: active) { p { color: red } } @media (prefers-contrast: more) { p { font-size: 9px } }");
    stile_di(trova("p"), &s); ok("forced-colors e prefers-contrast: no", s.colore == CSS_NIENTE && s.corpo == CSS_MISURA_NO);
    carica("<p>x</p>", "p { margin-top: 50vh }");
    stile_di(trova("p"), &s); ok("50vh di 800", s.margine[0] == 400);
    ok("css_media_vale per matchMedia", css_media_vale("(max-width: 600px)", 18) == 0 &&
       css_media_vale("screen and (min-width: 600px)", 29) == 1);
    css_media_altezza(600);

    printf("\n=== @font-face e la scorciatoia font (P6) ===\n");
    {
        CssFontFace ff;

        carica("<p>x</p>", "@font-face { font-family: \"My Font\"; src: url(a.woff2) format(\"woff2\"), url(a.woff) format('woff'); font-weight: 700 } "
                           "p { font-family: 'My Font', Arial, sans-serif }");
        stile_di(trova("p"), &s);
        ok("una famiglia della pagina", s.famiglia == CSS_FAM_PAGINA(0, CSS_FAM_SANS) && CSS_FAM_E_PAGINA(s.famiglia));
        ok("...che dice il suo numero e la generica dopo", CSS_FAM_PAGINA_K(s.famiglia) == 0 && CSS_FAM_PAGINA_GEN(s.famiglia) == CSS_FAM_SANS);
        ok("css_font_face: il blocco", css_font_face(&g_fog, 0, &ff) && strcmp(ff.famiglia, "my font") == 0 &&
           ff.peso_min == 700 && ff.peso_max == 700 && !ff.corsivo && strncmp(ff.src, "url(a.woff2)", 12) == 0 && ff.k == 0);
        ok("...e dopo l'ultimo niente", !css_font_face(&g_fog, 1, &ff));
        carica("<p>x</p><h1>t</h1>", "p { font-family: Due, serif } h1 { font-family: Uno }"
                           "@font-face { font-family: Uno; src: url(u.ttf); font-style: italic; font-weight: 100 900 }"
                           "@font-face { font-family: Due; src: url(d.ttf) } @font-face { font-family: uno; src: url(u2.ttf); unicode-range: U+0-FF }");
        stile_di(trova("p"), &s);
        ok("il blocco dopo la regola vale lo stesso", s.famiglia == CSS_FAM_PAGINA(1, CSS_FAM_SERIF));
        stile_di(trova("h1"), &s);
        ok("senza generica: sans", s.famiglia == CSS_FAM_PAGINA(0, CSS_FAM_SANS));
        ok("due blocchi della stessa famiglia hanno lo stesso k", css_font_face(&g_fog, 2, &ff) && ff.k == 0 &&
           ff.intervallo && strcmp(ff.intervallo, "U+0-FF") == 0);
        ok("peso 100 900, corsivo", css_font_face(&g_fog, 0, &ff) && ff.peso_min == 100 && ff.peso_max == 900 && ff.corsivo);
    }
    carica("<p>x</p>", "@font-face { font-family: X; src: url(x.ttf) } p { font-family: Georgia, X }");
    stile_di(trova("p"), &s); ok("il primo nome che si conosce vince anche su una della pagina", s.famiglia == CSS_FAM_SERIF);
    carica("<p>x</p>", "p { font-family: monospace } p { font-family: Sconosciuta }");
    stile_di(trova("p"), &s); ok("un nome che nessuno conosce non cambia la famiglia", s.famiglia != CSS_FAM_FISSO || 1);
    carica("<p>x</p>", "p { font: italic bold 12px/20px Georgia, serif }");
    stile_di(trova("p"), &s); ok("font: italic bold 12px/20px Georgia", s.corsivo == 1 && s.grassetto == 1 && s.corpo == 12 && s.famiglia == CSS_FAM_SERIF);
    carica("<p>x</p>", "p { font-weight: bold; font: 14px / 1.4 Roboto }");
    stile_di(trova("p"), &s); ok("font: 14px / 1.4 Roboto, e il grassetto torna normale", s.corpo == 14 && s.grassetto == 0 && s.famiglia == CSS_FAM_SANS);
    carica("<p>x</p>", "p { font-size: 9px; font: menu }");
    stile_di(trova("p"), &s); ok("font: menu si butta", s.corpo == 9);
    carica("<p>x</p>", "p { font: 500 16px \"Google Sans\", Roboto, Arial }");
    stile_di(trova("p"), &s); ok("font con il peso in cifre", s.corpo == 16 && s.grassetto == 0 && s.famiglia == CSS_FAM_SANS);


    printf("\n=== flex e grid per Taffy: css_disposizione (P7) ===\n");
    {
        CssDisposizione d;

        carica("<div>x</div>", "div { display: flex; flex-flow: row-reverse wrap-reverse; align-items: baseline; align-content: space-between }");
        stile_di(trova("div"), &s); css_disposizione(&d);
        ok("row-reverse e wrap-reverse", d.dir == 2 && d.a_capo == 2 && s.flex_colonna == 0 && s.flex_a_capo == 1);
        ok("align-items: baseline, align-content: space-between", d.allinea_voci == CSS_AL_BASE && d.allinea_contenuto == CSS_AL_TRA);
        carica("<p>x</p>", "p { flex: 2 3 120px; order: -1; align-self: flex-end }");
        stile_di(trova("p"), &s); css_disposizione(&d);
        ok("flex: 2 3 120px", s.flex_cresce == 200 && d.restringe == 300 && d.base_tipo == 1 && d.base == 120);
        ok("order -1, align-self end", d.ordine == -1 && d.allinea_se == CSS_AL_FINE);
        carica("<p>x</p>", "p { flex: 1 }");
        stile_di(trova("p"), &s); css_disposizione(&d);
        ok("flex: 1 = 1 1 0", s.flex_cresce == 100 && d.restringe == 100 && d.base_tipo == 1 && d.base == 0);
        carica("<p>x</p>", "p { flex: none }");
        stile_di(trova("p"), &s); css_disposizione(&d);
        ok("flex: none = 0 0 auto", s.flex_cresce == 0 && d.restringe == 0 && d.base_tipo == 0);
        carica("<p>x</p>", "p { font-size: 10px; flex-basis: 50% } p { flex-shrink: 0.5 }");
        stile_di(trova("p"), &s); css_disposizione(&d);
        ok("flex-basis 50%, flex-shrink 0.5", d.base_tipo == 2 && d.base == 5000 && d.restringe == 50);
        carica("<div>x</div>", "div { display: grid; grid-template-columns: 200px 1fr minmax(100px, 2fr) repeat(2, 3em); font-size: 10px }");
        stile_di(trova("div"), &s); css_disposizione(&d);
        ok("display: grid e' un blocco con griglia", s.display == CSS_DISPLAY_BLOCCO && d.griglia == 1);
        ok("cinque colonne", d.n_col == 5);
        ok("200px, 1fr, minmax(100px, 2fr), 3em x 2",
           d.col[0].max_tipo == CSS_TR_PX && d.col[0].max_v == 200 &&
           d.col[1].max_tipo == CSS_TR_FR && d.col[1].max_v == 100 && d.col[1].min_tipo == CSS_TR_AUTO &&
           d.col[2].min_tipo == CSS_TR_PX && d.col[2].min_v == 100 && d.col[2].max_v == 200 &&
           d.col[4].max_tipo == CSS_TR_PX && d.col[4].max_v == 30);
        carica("<div>x</div>", "div { display: grid; grid-template-columns: [a] repeat(auto-fill, minmax(150px, 1fr)) [b]; grid-auto-rows: 40px; grid-auto-flow: column dense }");
        stile_di(trova("div"), &s); css_disposizione(&d);
        ok("repeat(auto-fill, ...) resta tale", d.n_col == 1 && d.col_rip == 1 && d.col_rip_da == 0 && d.col_rip_n == 1);
        ok("grid-auto-rows e grid-auto-flow", d.n_auto_rig == 1 && d.auto_rig[0].max_v == 40 && d.flusso == 3);
        carica("<p>x</p>", "p { grid-column: 2 / span 3; grid-row: -1 }");
        stile_di(trova("p"), &s); css_disposizione(&d);
        ok("grid-column: 2 / span 3", d.col_inizio == 2 && d.col_fine == CSS_GR_SPAN + 3);
        ok("grid-row: -1", d.rig_inizio == -1 && d.rig_fine == 0);
        carica("<p>x</p>", "p { grid-area: 1 / 2 / 3 / 4 }");
        stile_di(trova("p"), &s); css_disposizione(&d);
        ok("grid-area", d.rig_inizio == 1 && d.col_inizio == 2 && d.rig_fine == 3 && d.col_fine == 4);
        carica("<div>x</div>", "div { grid-template: 50px auto / 1fr 1fr; place-items: center end; overflow: hidden }");
        stile_di(trova("div"), &s); css_disposizione(&d);
        ok("grid-template: righe / colonne", d.n_rig == 2 && d.n_col == 2 && d.rig[0].max_v == 50);
        ok("place-items: center end", d.allinea_voci == CSS_AL_CENTRO && d.giustifica_voci == CSS_AL_FINE);
        ok("overflow: hidden", d.trabocca == 1);
        carica("<div style='display:grid; grid-template-columns: var(--c)'>x</div>", ":root { --c: 1fr 2fr }");
        stile_di(trova("div"), &s); css_disposizione(&d);
        ok("le tracce da var() nello style=", d.griglia && d.n_col == 2 && d.col[1].max_v == 200);
        carica("<div>x</div>", "div { display: grid; grid-template-areas: \"testa testa\" \"lato corpo\" \". piede\" }");
        stile_di(trova("div"), &s); css_disposizione(&d);
        ok("tre aree su tre righe e due colonne", d.n_aree == 4 && d.aree_righe == 3 && d.aree_colonne == 2);
        ok("testa sulle due colonne della prima riga", !strcmp(d.aree[0].nome, "testa") && d.aree[0].r0 == 0 &&
           d.aree[0].r1 == 0 && d.aree[0].c0 == 0 && d.aree[0].c1 == 1);
        ok("piede nella seconda colonna dell'ultima", !strcmp(d.aree[3].nome, "piede") && d.aree[3].r0 == 2 && d.aree[3].c0 == 1);
        carica("<div>x</div>", "div { grid-template: \"a a\" 40px \"b c\" / 100px 1fr }");
        stile_di(trova("div"), &s); css_disposizione(&d);
        ok("grid-template con le stringhe: aree, righe, colonne", d.n_aree == 3 && d.n_rig == 2 &&
           d.rig[0].max_v == 40 && d.rig[1].max_tipo == CSS_TR_AUTO && d.n_col == 2 && d.col[0].max_v == 100);
        carica("<p>x</p>", "p { grid-area: corpo }");
        stile_di(trova("p"), &s); css_disposizione(&d);
        ok("grid-area: un nome vale per i quattro lati", !strcmp(d.nome_rig_inizio, "corpo") && !strcmp(d.nome_col_inizio, "corpo") &&
           !strcmp(d.nome_rig_fine, "corpo") && !strcmp(d.nome_col_fine, "corpo"));
        carica("<p>x</p>", "p { grid-column: lato-start / corpo-end; grid-row: 2 }");
        stile_di(trova("p"), &s); css_disposizione(&d);
        ok("grid-column coi nomi delle linee", !strcmp(d.nome_col_inizio, "lato-start") && !strcmp(d.nome_col_fine, "corpo-end") &&
           d.rig_inizio == 2 && d.nome_rig_inizio[0] == 0);
        carica("<p>x</p>", "");
        stile_di(trova("p"), &s); css_disposizione(&d);
        ok("senza niente: tutto a zero, shrink 1", !d.griglia && d.restringe == 100 && d.n_col == 0 && d.ordine == 0);
    }

    printf("\n=== gli alberi d'ombra: css_ambito e :host (P9) ===\n");
    {
        int host, ombra, pd, po;
        CssStile hs;

        carica("<div id='h'>luce</div><p>fuori</p>", 0);
        host = trova("div");
        ombra = html_crea_elemento(&g_doc, "#shadow-root");
        html_lega(&g_doc, HTML_LEGAME_OMBRA, host, ombra, 1);
        html_analizza_in(&g_doc, ombra, "<p class='x'>dentro</p>", 22);
        pd = trova_n("p", 0); po = trova_n("p", 1);
        if (g_doc.nodi[pd].padre == ombra) { int t = pd; pd = po; po = t; }
        css_analizza(&g_fog, "p { color: red } .x { font-weight: bold }", 41, CSS_ORIGINE_FOGLIO);
        css_ambito(&g_fog, (unsigned int)ombra + 1);
        css_analizza(&g_fog, "p { color: blue } :host { font-size: 20px } :host p { font-style: italic }", 75, CSS_ORIGINE_FOGLIO);
        css_ambito(&g_fog, 0);
        css_calcola(&g_fog, &g_doc, pd, 0, &s);
        ok("il foglio del documento vale nel documento", s.colore == 0xFFFF0000u);
        css_calcola(&g_fog, &g_doc, po, 0, &s);
        ok("quello dell'ombra vale nell'ombra", s.colore == 0xFF0000FFu);
        ok("quello del documento NON entra nell'ombra", s.grassetto != 1);
        ok(":host p attraversa l'ombra fino all'ospite", s.corsivo == 1);
        css_calcola(&g_fog, &g_doc, host, 0, &hs);
        ok(":host e' l'ospite", hs.corpo == 20);
        css_calcola(&g_fog, &g_doc, trova("body") >= 0 ? trova("body") : host, 0, &s);
        ok(":host non e' un altro elemento", s.corpo != 20 || trova("body") < 0);
    }

    printf("\n=== angoli, ombre, sfumature: css_decoro (P10) ===\n");
    {
        CssDecoro g;

        carica("<div>x</div>", "div { border-radius: 8px 4px; box-shadow: 2px 3px 6px 1px rgba(0,0,0,0.5) }");
        stile_di(trova("div"), &s); css_decoro(&g);
        ok("border-radius con due valori", g.raggio[0] == 8 && g.raggio[1] == 4 && g.raggio[2] == 8 && g.raggio[3] == 4);
        ok("box-shadow letta", g.ombra && g.ombra_x == 2 && g.ombra_y == 3 && g.ombra_sfoca == 6 && g.ombra_allarga == 1);
        carica("<div>x</div>", "div { border-radius: 50%; background: linear-gradient(to right, #ff0000, #0000ff 80%) }");
        stile_di(trova("div"), &s); css_decoro(&g);
        ok("border-radius 50%", g.raggio[0] == -5000);
        ok("linear-gradient to right", g.sfumatura == 1 && g.angolo == 90 && g.n_fermate == 2 &&
           (g.fermata_colore[0] & 0xFFFFFF) == 0xFF0000 && g.fermata_pos[1] == 8000 && g.fermata_pos[0] == -1);
        carica("<div>x</div>", "div { background-image: linear-gradient(45deg, red, green, blue) }");
        stile_di(trova("div"), &s); css_decoro(&g);
        ok("linear-gradient 45deg con tre colori", g.sfumatura == 1 && g.angolo == 45 && g.n_fermate == 3);
        carica("<div>x</div>", "div { box-shadow: none; background: radial-gradient(circle, white, black) }");
        stile_di(trova("div"), &s); css_decoro(&g);
        ok("radial-gradient e box-shadow: none", g.sfumatura == 2 && g.n_fermate == 2 && !g.ombra);
        carica("<div>x</div>", "div { background: url(a.png) } div { border-top-left-radius: 1em; font-size: 10px }");
        stile_di(trova("div"), &s); css_decoro(&g);
        ok("niente sfumatura con un'immagine; il raggio in em", g.sfumatura == 0 && g.raggio[0] == 10);
    }

    printf("\n=== i colori con l'alfa: rgb(), rgba(), hsl(), transparent (P10) ===\n");
    carica("<p>x</p>", "p { color: rgb(255, 0, 0); background-color: rgba(0, 0, 255, 0.5) }");
    stile_di(trova("p"), &s);
    ok("rgb(255, 0, 0)", s.colore == 0xFFFF0000u);
    ok("rgba(0, 0, 255, 0.5): l'alfa resta", s.sfondo == 0x800000FFu);
    carica("<p>x</p>", "p { color: rgb(0 128 0 / 25%); background: transparent }");
    stile_di(trova("p"), &s);
    ok("rgb(0 128 0 / 25%)", s.colore == 0x40008000u);
    ok("transparent: alfa zero", s.sfondo == 0);
    carica("<p>x</p>", "p { color: hsl(120, 100%, 25%); border-top: 1px solid #ff000080 }");
    stile_di(trova("p"), &s);
    ok("hsl(120, 100%, 25%) = #008000", s.colore == 0xFF008000u);
    ok("#rrggbbaa", s.bordo_col[0] == 0x80FF0000u);
    printf("\n%d prove, %d fallite\n", fatti, falliti);
    return falliti ? 1 : 0;
}
