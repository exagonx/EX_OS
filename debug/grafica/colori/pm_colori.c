/* =============================================================================
 * debug/grafica/colori/pm_colori.c  ->  exwin/bin/pm/pm_colori.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * IMPOSTAZIONI > COLORI — la scheda di pm che sceglie e personalizza i colori
 *
 * Disposizione (finestra Impostazioni portata a 640 x 500, vedi
 * implementazione_COLORI001.txt passo 4; l'immagine pm_pannello_colori.png
 * e' disegnata con le stesse coordinate):
 *
 *   Schema: [ Classico         v]  [Salva come...] [Elimina]
 *   +-------------------------------+  +-----------------------------+
 *   |  anteprima viva (clic =       |  | Elementi (lista)            |
 *   |  scegli l'elemento)           |  |  Scrivania (sopra)          |
 *   |                               |  |  ...                        |
 *   +-------------------------------+  +-----------------------------+
 *   [16 tinte rapide]                  [##] [#3A6EA5] [Chiaro] [Scuro]
 *                                      R ===========|======  58
 *                                      G ===========|======  110
 *                                      B ===========|======  165
 *   La spiegazione dell'elemento scelto.
 *   (avviso di contrasto, se serve)
 *   [x] Barra sfumata [x] Titolo grassetto [ ] Ombra [ ] Angoli arrotondati
 *   [Predefinito]                         [Annulla modifiche] [Applica]
 *
 * Come si comporta:
 *   - ogni modifica (barra, casella, tinta, clic) cambia lo schema IN
 *     MODIFICA e ridisegna l'anteprima subito: nessun "Applica" per vedere;
 *   - Applica scrive $HOME/.exwin/config/colori.cfg e chiede al server di
 *     far rileggere il tema a tutte le applicazioni (ex_tema_ricarica);
 *   - Annulla modifiche torna all'ultimo applicato; Predefinito allo schema
 *     del tema (Classico);
 *   - Salva come... scrive $HOME/.exwin/colori/<nome>.colori e lo aggiunge
 *     all'elenco; Elimina toglie solo quelli dell'utente (quelli di sistema
 *     in /exwin/lib/colori non si toccano: il pulsante si spegne);
 *   - lo schema modificato a partire da uno salvato si chiama
 *     "<nome> (modificato)" finche' non lo si salva.
 *
 * DIPENDE da implementazione_TEMA001 (extema in exwin.so) e da tre funzioni
 * nuove del toolkit, descritte in implementazione_COLORI001 passo 3:
 *     int  ex_tela_di(ExWindow f, ExTela *t);
 *     void ex_tema_ricarica(void);
 *     const ExTema *ex_tema_forma(const char *nome);
 * ============================================================================= */
#include "libc.h"
#include "exwin.h"
#include "exdlg.h"
#include "extema.h"
#include "excolori.h"
#include "colori_anteprima.h"
#include "pm_colori.h"

/* in lib/libc.c ma non dichiarata in libc.h (da aggiungere li', vedi
 * implementazione_COLORI001 passo 3) */
int fsync(int fd);

/* gli id: pm usa 100..199 e 900+; questi stanno a 300..329 */
#define ID_C_SCHEMA     300
#define ID_C_SALVA      301
#define ID_C_ELIMINA    302
#define ID_C_ELEMENTI   303
#define ID_C_ESA        304
#define ID_C_CHIARO     305
#define ID_C_SCURO      306
#define ID_C_R          307
#define ID_C_G          308
#define ID_C_B          309
#define ID_C_SFUMATA    310
#define ID_C_GRASSETTO  311
#define ID_C_OMBRA      312
#define ID_C_ANGOLI     313
#define ID_C_PREDEF     314
#define ID_C_RIPRISTINA 315
#define ID_C_APPLICA    316

/* le aree disegnate da pm (non sono controlli) */
#define AN_X   12
#define AN_Y   70
#define AN_W  300
#define AN_H  210
#define RAP_X  14
#define RAP_Y 290
#define CAMP_X 324                   /* il campione del colore scelto */
#define CAMP_Y 230
#define CAMP_L  44

#define SCHEMI_MAX 64
#define DIR_SISTEMA "/exwin/lib/colori"

static ExWindow  g_f;                /* la finestra Impostazioni          */
static int       g_vis;              /* la scheda Colori e' quella visibile */
static ExWindow  g_schema, g_salva, g_elimina, g_el, g_esa, g_r, g_g, g_b;
static ExWindow  g_lr, g_lg, g_lb, g_spiega, g_avviso;
static ExWindow  g_sfumata, g_grassetto, g_ombra, g_angoli;

static ExColori  g_mod;              /* lo schema che si sta modificando */
static ExColori  g_app;              /* l'ultimo applicato (Annulla modifiche) */
static ExTema    g_vista;            /* il tema ricavato da g_mod, per l'anteprima */
static int       g_scelto = EXC_FINESTRA;
static ColZone   g_zone;
static int       g_tocca = 0;        /* 1 mentre pm stesso aggiorna i controlli:
                                        i loro EXM_COMMAND non sono dell'utente */

static struct { char nome[EXC_NOME_MAX]; char file[160]; int sistema; } g_sc[SCHEMI_MAX];
static int g_sc_n;

/* -----------------------------------------------------------------------------
 * File e percorsi
 * --------------------------------------------------------------------------- */
static const char *casa(void)
{
    const char *h = getenv("HOME");
    if (!h || !h[0] || !strcmp(h, "/")) return getuid() == 0 ? "/root" : "";
    return h;
}

static int leggi_file(const char *p, char *buf, int max)
{
    int fd = open(p, O_RDONLY, 0), n;
    if (fd < 0) return -1;
    n = (int)read(fd, buf, (unsigned int)max - 1);
    close(fd);
    if (n < 0) return -1;
    buf[n] = 0;
    return n;
}

/* scrittura sicura: prima <file>.nuovo, poi rename sopra (bug_UNIX/voce 49:
 * rename non e' atomica sopra un file esistente su ext2 oggi, ma il file
 * vecchio non resta mai a meta') */
static int scrivi_file(const char *p, const char *buf, int n)
{
    char tmp[200];
    int  fd;

    snprintf(tmp, sizeof(tmp), "%s.nuovo", p);
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return 0;
    if (write(fd, buf, (unsigned int)n) != n) { close(fd); unlink(tmp); return 0; }
    fsync(fd);
    close(fd);
    unlink(p);
    return rename(tmp, p) == 0;
}

static void cfg_percorso(char *out, int max)
{
    snprintf(out, (size_t)max, "%s/.exwin/config/colori.cfg", casa());
}

/* -----------------------------------------------------------------------------
 * Lo schema in modifica -> controlli e anteprima
 * --------------------------------------------------------------------------- */
static void ridisegna(void)
{
    ExTela t;

    if (!g_vis || !ex_tela_di(g_f, &t)) return;
    excol_applica(&g_mod, ex_tema_forma(g_mod.tema), &g_vista);
    colori_anteprima(&t, &g_vista, AN_X, AN_Y, AN_W, AN_H, g_scelto, &g_zone);
    {
        ColZone r; r.n = 0;
        colori_rapidi(&t, RAP_X, RAP_Y, &r);
    }
    /* il campione del colore scelto, con la cornice incavata */
    ext_riempi(&t, CAMP_X, CAMP_Y, CAMP_L, CAMP_L, g_mod.c[g_scelto]);
    ext_rilievo(&t, CAMP_X - 1, CAMP_Y - 1, CAMP_L + 2, CAMP_L + 2, 0x00808080u, 0x00FFFFFFu);
    ex_update(g_f);
}

/* l'avviso di contrasto: guarda le coppie che si leggono una sull'altra */
static void contrasto_controlla(void)
{
    static const int coppie[][2] = {
        { EXC_TESTO, EXC_FINESTRA }, { EXC_CAMPO_TESTO, EXC_CAMPO },
        { EXC_SELEZIONE_TESTO, EXC_SELEZIONE }, { EXC_TITOLO, EXC_BARRA },
        { EXC_TITOLO_INA, EXC_BARRA_INA }, { EXC_SUGGERIMENTO_TESTO, EXC_SUGGERIMENTO },
    };
    int i;
    char t[160];

    for (i = 0; i < (int)(sizeof(coppie) / sizeof(coppie[0])); i++) {
        int c = excol_contrasto(g_mod.c[coppie[i][0]], g_mod.c[coppie[i][1]]);
        if (c < 450) {
            snprintf(t, sizeof(t), "Attenzione: \"%s\" su \"%s\" si legge male (contrasto %d.%d:1, "
                     "ne serve almeno 4.5).", excol_etichetta(coppie[i][0]),
                     excol_etichetta(coppie[i][1]), c / 100, (c % 100) / 10);
            ex_set_text(g_avviso, t);
            return;
        }
    }
    ex_set_text(g_avviso, "");
}

/* i controlli dell'elemento scelto: barre, casella, spiegazione */
static void elemento_mostra(void)
{
    unsigned int c = g_mod.c[g_scelto];
    char esa[8], n[8];

    g_tocca = 1;
    excol_esa(c, esa);
    ex_set_text(g_esa, esa);
    ex_scroll_set_pos(g_r, (c >> 16) & 255u);
    ex_scroll_set_pos(g_g, (c >> 8) & 255u);
    ex_scroll_set_pos(g_b, c & 255u);
    snprintf(n, sizeof(n), "%u", (c >> 16) & 255u); ex_set_text(g_lr, n);
    snprintf(n, sizeof(n), "%u", (c >> 8) & 255u);  ex_set_text(g_lg, n);
    snprintf(n, sizeof(n), "%u", c & 255u);         ex_set_text(g_lb, n);
    ex_set_text(g_spiega, excol_spiega(g_scelto));
    if ((int)ex_list_get_selected(g_el) != g_scelto) ex_list_select(g_el, (unsigned int)g_scelto);
    ex_set_checked(g_sfumata, g_mod.sfumata);
    ex_set_checked(g_grassetto, g_mod.grassetto);
    ex_set_checked(g_ombra, g_mod.ombra > 0);
    ex_set_checked(g_angoli, g_mod.raggio > 0);
    g_tocca = 0;
    contrasto_controlla();
    ridisegna();
}

/* una modifica dell'utente: il nome dice che non e' piu' lo schema salvato */
static void modificato(void)
{
    int l = (int)strlen(g_mod.schema);
    const char *s = " (modificato)";

    if (l >= 13 && !strcmp(g_mod.schema + l - 13, s)) return;
    if (l + 13 < EXC_NOME_MAX) strcat(g_mod.schema, s);
}

static void colore_metti(unsigned int c)
{
    g_mod.c[g_scelto] = c & 0xFFFFFFu;
    modificato();
    elemento_mostra();
}

/* -----------------------------------------------------------------------------
 * Gli schemi: sistema + utente
 * --------------------------------------------------------------------------- */
static void schemi_da(const char *dir, int sistema)
{
    DIR *d = opendir(dir);
    struct dirent *e;

    if (!d) return;
    while ((e = readdir(d)) != NULL && g_sc_n < SCHEMI_MAX) {
        int l = (int)strlen(e->d_name);
        char buf[2048];
        ExColori s;

        if (l < 8 || strcmp(e->d_name + l - 7, ".colori")) continue;
        snprintf(g_sc[g_sc_n].file, sizeof(g_sc[0].file), "%s/%s", dir, e->d_name);
        if (leggi_file(g_sc[g_sc_n].file, buf, sizeof(buf)) < 0) continue;
        excol_da_tema(&s, ex_tema_forma("classico"));
        excol_leggi(&s, buf, (int)strlen(buf));
        strncpy(g_sc[g_sc_n].nome, s.schema, EXC_NOME_MAX - 1);
        g_sc[g_sc_n].sistema = sistema;
        g_sc_n++;
    }
    closedir(d);
}

static void schemi_elenca(void)
{
    char dir[160];
    int  i;

    g_sc_n = 0;
    schemi_da(DIR_SISTEMA, 1);
    schemi_da("/cdrom" DIR_SISTEMA, 1);
    snprintf(dir, sizeof(dir), "%s/.exwin/colori", casa());
    schemi_da(dir, 0);
    /* in ordine di nome, quelli di sistema prima */
    for (i = 1; i < g_sc_n; i++) {
        int j = i;
        while (j > 0 && (g_sc[j - 1].sistema < g_sc[j].sistema ||
               (g_sc[j - 1].sistema == g_sc[j].sistema &&
                strcmp(g_sc[j - 1].nome, g_sc[j].nome) > 0))) {
            char tmp[sizeof(g_sc[0])];
            memcpy(tmp, &g_sc[j], sizeof(tmp));
            memcpy(&g_sc[j], &g_sc[j - 1], sizeof(tmp));
            memcpy(&g_sc[j - 1], tmp, sizeof(tmp));
            j--;
        }
    }
    g_tocca = 1;
    while (ex_item_remove(g_schema, 0) == 0) ;
    for (i = 0; i < g_sc_n; i++) {
        char r[EXC_NOME_MAX + 12];
        snprintf(r, sizeof(r), "%s%s", g_sc[i].nome, g_sc[i].sistema ? "" : "  (tuo)");
        ex_item_add(g_schema, r);
    }
    g_tocca = 0;
}

static void schema_carica(int i)
{
    char buf[2048];

    if (i < 0 || i >= g_sc_n || leggi_file(g_sc[i].file, buf, sizeof(buf)) < 0) return;
    excol_da_tema(&g_mod, ex_tema_forma("classico"));
    excol_leggi(&g_mod, buf, (int)strlen(buf));
    ex_set_enabled(g_elimina, !g_sc[i].sistema);
    elemento_mostra();
}

static void schema_salva_come(void)
{
    char nome[EXC_NOME_MAX], dir[160], p[220], buf[2048];
    int  i, n;

    strncpy(nome, g_mod.schema, sizeof(nome) - 1);
    nome[sizeof(nome) - 1] = 0;
    if (strstr(nome, " (modificato)")) *strstr(nome, " (modificato)") = 0;
    if (!ex_dlg_riga("Salva lo schema", "Nome dello schema:", nome, sizeof(nome)) || !nome[0])
        return;
    /* il nome del file: lettere e cifre, il resto diventa '_' */
    snprintf(dir, sizeof(dir), "%s/.exwin/colori", casa());
    mkdir(dir, 0755);
    n = snprintf(p, sizeof(p), "%s/", dir);
    for (i = 0; nome[i] && n < (int)sizeof(p) - 9; i++) {
        char c = nome[i];
        p[n++] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
                 ? (char)(c | 0x20) : '_';
    }
    strcpy(p + n, ".colori");
    strncpy(g_mod.schema, nome, EXC_NOME_MAX - 1);
    n = excol_scrivi(&g_mod, buf, sizeof(buf));
    if (n < 0 || !scrivi_file(p, buf, n)) {
        ex_dlg_avviso("Colori", "Non riesco a scrivere lo schema.");
        return;
    }
    schemi_elenca();
    for (i = 0; i < g_sc_n; i++)
        if (!strcmp(g_sc[i].file, p)) { g_tocca = 1; ex_item_select(g_schema, (unsigned int)i); g_tocca = 0; }
    elemento_mostra();
}

static void schema_elimina(void)
{
    int i = (int)ex_item_get_selected(g_schema);
    char t[120];

    if (i < 0 || i >= g_sc_n || g_sc[i].sistema) return;
    snprintf(t, sizeof(t), "Eliminare lo schema \"%s\"?", g_sc[i].nome);
    if (!ex_dlg_conferma("Colori", t, "Elimina", "Annulla")) return;
    unlink(g_sc[i].file);
    schemi_elenca();
}

/* -----------------------------------------------------------------------------
 * Applicare
 * --------------------------------------------------------------------------- */
static void applica(void)
{
    char p[200], buf[2048];
    int  n = excol_scrivi(&g_mod, buf, sizeof(buf));

    cfg_percorso(p, sizeof(p));
    if (n < 0 || !scrivi_file(p, buf, n)) {
        ex_dlg_avviso("Colori", "Non riesco a scrivere colori.cfg nel tuo profilo.");
        return;
    }
    g_app = g_mod;
    ex_tema_ricarica();          /* server + ogni applicazione, pm compreso */
}

/* -----------------------------------------------------------------------------
 * L'interfaccia verso pm.c
 * --------------------------------------------------------------------------- */
void colori_crea(ExWindow f, int scheda, void (*metti)(int, ExWindow), int iw, int ih)
{
    char buf[2048], p[200];
    int  e;

    g_f = f;
    /* lo schema in uso: quello del tema, poi colori.cfg sopra */
    excol_da_tema(&g_mod, ex_tema_forma("classico"));
    cfg_percorso(p, sizeof(p));
    if (leggi_file(p, buf, sizeof(buf)) > 0) excol_leggi(&g_mod, buf, (int)strlen(buf));
    g_app = g_mod;

    metti(scheda, ex_create("label", "Schema:", EX_CHILD, 12, 42, 60, 18, f, 0, 0));
    metti(scheda, g_schema = ex_create("combo", "", EX_CHILD, 70, 38, 220, 24, f, ID_C_SCHEMA, 0));
    metti(scheda, g_salva = ex_create("button", "Salva come...", EX_CHILD, 300, 38, 110, 24, f, ID_C_SALVA, 0));
    metti(scheda, g_elimina = ex_create("button", "Elimina", EX_CHILD, 416, 38, 80, 24, f, ID_C_ELIMINA, 0));

    metti(scheda, g_el = ex_create("list", "", EX_CHILD, 324, 70, iw - 336, 150, f, ID_C_ELEMENTI, 0));
    for (e = 0; e < EXC_N; e++) ex_list_add(g_el, excol_etichetta(e));

    metti(scheda, g_esa = ex_create("textbox", "", EX_CHILD, 376, 230, 90, 24, f, ID_C_ESA, 0));
    metti(scheda, ex_create("button", "Chiaro", EX_CHILD, 472, 230, 76, 24, f, ID_C_CHIARO, 0));
    metti(scheda, ex_create("button", "Scuro", EX_CHILD, 552, 230, 76, 24, f, ID_C_SCURO, 0));
    metti(scheda, ex_create("label", "R", EX_CHILD, 376, 262, 10, 16, f, 0, 0));
    metti(scheda, ex_create("label", "G", EX_CHILD, 376, 284, 10, 16, f, 0, 0));
    metti(scheda, ex_create("label", "B", EX_CHILD, 376, 306, 10, 16, f, 0, 0));
    metti(scheda, g_r = ex_create("scrollbar", "", EX_CHILD, 392, 262, 196, 16, f, ID_C_R, 0));
    metti(scheda, g_g = ex_create("scrollbar", "", EX_CHILD, 392, 284, 196, 16, f, ID_C_G, 0));
    metti(scheda, g_b = ex_create("scrollbar", "", EX_CHILD, 392, 306, 196, 16, f, ID_C_B, 0));
    ex_scroll_set_range(g_r, 255, 16);
    ex_scroll_set_range(g_g, 255, 16);
    ex_scroll_set_range(g_b, 255, 16);
    metti(scheda, g_lr = ex_create("label", "", EX_CHILD, 594, 262, 34, 16, f, 0, 0));
    metti(scheda, g_lg = ex_create("label", "", EX_CHILD, 594, 284, 34, 16, f, 0, 0));
    metti(scheda, g_lb = ex_create("label", "", EX_CHILD, 594, 306, 34, 16, f, 0, 0));

    metti(scheda, g_spiega = ex_create("label", "", EX_CHILD, 12, 342, iw - 24, 16, f, 0, 0));
    metti(scheda, g_avviso = ex_create("label", "", EX_CHILD, 12, 362, iw - 24, 16, f, 0, 0));

    metti(scheda, g_sfumata = ex_create("checkbox", "Barra sfumata", EX_CHILD, 12, 392, 150, 20, f, ID_C_SFUMATA, 0));
    metti(scheda, g_grassetto = ex_create("checkbox", "Titolo in grassetto", EX_CHILD, 166, 392, 170, 20, f, ID_C_GRASSETTO, 0));
    metti(scheda, g_ombra = ex_create("checkbox", "Ombra sotto le finestre", EX_CHILD, 364, 392, 200, 20, f, ID_C_OMBRA, 0));
    metti(scheda, g_angoli = ex_create("checkbox", "Angoli arrotondati", EX_CHILD, 12, 416, 200, 20, f, ID_C_ANGOLI, 0));

    metti(scheda, ex_create("button", "Predefinito", EX_CHILD, 12, ih - 44, 110, 28, f, ID_C_PREDEF, 0));
    metti(scheda, ex_create("button", "Annulla modifiche", EX_CHILD, iw - 284, ih - 44, 152, 28, f, ID_C_RIPRISTINA, 0));
    metti(scheda, ex_create("button", "Applica", EX_CHILD, iw - 124, ih - 44, 112, 28, f, ID_C_APPLICA, 0));

    schemi_elenca();
    ex_set_enabled(g_elimina, 0);
}

void colori_visibile(int si) { g_vis = si; if (si) elemento_mostra(); }

void colori_chiudi(void) { g_f = 0; g_vis = 0; }

int colori_comando(unsigned int wp, long lp)
{
    if (g_tocca) return wp >= 300 && wp < 330;      /* aggiornamenti di pm stesso */
    switch (wp) {
    case ID_C_SCHEMA:    schema_carica((int)lp); return 1;
    case ID_C_SALVA:     schema_salva_come(); return 1;
    case ID_C_ELIMINA:   schema_elimina(); return 1;
    case ID_C_ELEMENTI:  g_scelto = (int)ex_list_get_selected(g_el); elemento_mostra(); return 1;
    case ID_C_ESA: {
        unsigned int c;
        const char *s = ex_get_text(g_esa);
        if (s && excol_colore(s, (int)strlen(s), &c)) colore_metti(c);
        return 1;
    }
    case ID_C_CHIARO:    colore_metti(ext_schiarisci(g_mod.c[g_scelto], 24)); return 1;
    case ID_C_SCURO:     colore_metti(ext_schiarisci(g_mod.c[g_scelto], -24)); return 1;
    case ID_C_R: case ID_C_G: case ID_C_B: {
        unsigned int c = g_mod.c[g_scelto], v = (unsigned int)lp & 255u;
        if (wp == ID_C_R) c = (c & 0x00FFFFu) | (v << 16);
        if (wp == ID_C_G) c = (c & 0xFF00FFu) | (v << 8);
        if (wp == ID_C_B) c = (c & 0xFFFF00u) | v;
        colore_metti(c);
        return 1;
    }
    case ID_C_SFUMATA:   g_mod.sfumata = ex_is_checked(g_sfumata); modificato(); elemento_mostra(); return 1;
    case ID_C_GRASSETTO: g_mod.grassetto = ex_is_checked(g_grassetto); modificato(); elemento_mostra(); return 1;
    case ID_C_OMBRA:     g_mod.ombra = ex_is_checked(g_ombra) ? 10 : 0; modificato(); elemento_mostra(); return 1;
    case ID_C_ANGOLI:    g_mod.raggio = ex_is_checked(g_angoli) ? 3 : 0; modificato(); elemento_mostra(); return 1;
    case ID_C_PREDEF:    excol_da_tema(&g_mod, ex_tema_forma("classico")); elemento_mostra(); return 1;
    case ID_C_RIPRISTINA: g_mod = g_app; elemento_mostra(); return 1;
    case ID_C_APPLICA:   applica(); return 1;
    default: return 0;
    }
}

/* un clic nella finestra Impostazioni: se cade sull'anteprima o sulle
 * tinte rapide e' nostro */
int colori_mouse(int x, int y)
{
    int el;

    if (!g_vis) return 0;
    el = colori_zona_a(&g_zone, x, y);
    if (el >= 0 && el < EXC_N) { g_scelto = el; elemento_mostra(); return 1; }
    if (x >= RAP_X && y >= RAP_Y && x < RAP_X + 8 * 26 && y < RAP_Y + 2 * 22) {
        int i = (x - RAP_X) / 26 + 8 * ((y - RAP_Y) / 22);
        if ((x - RAP_X) % 26 < 22 && (y - RAP_Y) % 22 < 18 && i < 16) {
            colore_metti(g_colori_rapidi[i]);
            return 1;
        }
    }
    return 0;
}

void colori_disegna(void) { ridisegna(); }
