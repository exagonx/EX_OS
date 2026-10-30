/* =============================================================================
 * exwin/bin/finestreprova/finestreprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Many windows at once: the test of wserver 0.013 and exwin.so 0.020 (voce 42,
 * point 1: no fixed number of windows).
 *
 *     finestreprova [N] [secondi]     (default 40 finestre, 5 secondi)
 *
 * Opens N windows with a title longer than the 48 bytes of old, follows the
 * list of windows (ex_track_windows) and, when the list is whole, counts its
 * own. Up to wserver 0.012 the 17th window was refused, and the list said
 * sixteen at most. It writes on the serial line:
 *
 *     FINESTRE aperte A di N          the windows the server gave
 *     FINESTRE elenco T mie M         the list, whole (T in all, M ours)
 *     FINESTRE fine 0                 all N created and all N in the list
 *     FINESTRE fine 1                 something is missing (or 60 s passed)
 *     FINESTRE chiudo A / chiuse A    around closing them all in a row
 *
 * then keeps them open the given seconds (for a photograph), closes them all
 * and exits.
 *
 *     finestreprova -c [secondi]
 *
 * The controls past their old ceilings (exwin.so 0.020), in one window: a
 * combo of 100 entries (were 32), a menu of 10 titles of 40 entries with 8
 * side drop-downs (were 6, 16, 4), a text area of 5000 lines of 600
 * characters (were 512 x 200), a code area of 4000 x 300 (were 3000 x 240),
 * a text box of 5000 characters (were 1023), a list of 70000 rows of 150
 * characters (were 65536 x 63). One line each, then:
 *
 *     CONTROLLI fine 0                every one holds all it was given
 *
 *     finestreprova -t [secondi]
 *
 * The keys of voce 42 point 2 (kbd.drv 0.007, wserver 0.014, exwin.so
 * 0.021): a window that asked for presses and releases (ex_track_keys), with
 * a text box that has the focus and a text area under it (Tab goes there).
 * Every key message is written as it comes:
 *
 *     TASTI giu  c=<code> u=<unicode> f=<physical> m=<mods> r=<repeat>
 *     TASTI mod  ...                  a key without a code (EXM_KEY_DOWN)
 *     TASTI su   ...                  any release (EXM_KEY_UP)
 *
 * and at the end what the two controls hold, in hex, to see the UTF-8:
 *
 *     TASTI casella <hex>  /  TASTI area <hex>  /  TASTI fine
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"

#define VERSIONE_APP "0.003"
EX_VERSIONE("finestreprova", VERSIONE_APP);

static ExWindow    *g_fin = 0;
static int          g_n = 0, g_aperte = 0, g_esito = -1, g_tieni_ms = 5000;
static unsigned int g_t0 = 0;

static void riga(const char *fmt, int a, int b)
{
    char r[96];

    snprintf(r, sizeof(r), fmt, a, b);
    log_seriale(r);
    printf("%s\n", r);
}

static void chiudi_tutte(int codice)
{
    int i;

    riga("FINESTRE chiudo %d (dopo %d ms)", g_aperte, (int)(uptime_ms() - g_t0));
    for (i = 0; i < g_n; i++) if (g_fin[i]) ex_destroy(g_fin[i]);
    riga("FINESTRE chiuse %d", g_aperte, 0);
    ex_quit(codice);
}

static void conta(void)
{
    ExWindowEntry *v;
    int n = ex_list_windows(0, 0), i, mie = 0;
    unsigned int io = (unsigned int)getpid();

    v = (ExWindowEntry *)malloc((unsigned int)(n ? n : 1) * sizeof(ExWindowEntry));
    if (!v) return;
    n = ex_list_windows(v, n);
    for (i = 0; i < n; i++) if (v[i].pid == io) mie++;
    free(v);

    riga("FINESTRE elenco %d mie %d", n, mie);
    if (g_esito < 0 && mie == g_n && g_aperte == g_n) {
        g_esito = 0;
        riga("FINESTRE fine %d (in %d ms)", 0, (int)(uptime_ms() - g_t0));
        ex_set_timer(g_fin[0], (unsigned int)g_tieni_ms);
    }
}

static long proc(ExWindow f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_WINDOW_LIST:
        conta();
        return 0;

    case EXM_TIMER:
        if (g_esito < 0) {
            g_esito = 1;
            riga("FINESTRE fine %d (dopo %d ms)", 1, (int)(uptime_ms() - g_t0));
        }
        chiudi_tutte(g_esito);
        return 0;

    case EXM_CLOSE:
        chiudi_tutte(g_esito < 0 ? 1 : g_esito);
        return 0;

    case EXM_PAINT:
        ex_fill_rect(f, 0, 0, 2000, 2000, 0x00C0C0C0);
        ex_draw_text(f, 6, 6, "molte finestre", 0x00000000);
        ex_update(f);
        return 0;

    default:
        return ex_default_proc(f, msg, wp, lp);
    }
}

static int g_errori = 0;

static void esito(const char *cosa, unsigned int avuti, unsigned int voluti, int ok)
{
    char r[120];

    if (!ok || avuti != voluti) { ok = 0; g_errori++; }
    snprintf(r, sizeof(r), "CONTROLLI %s %u/%u %s", cosa, avuti, voluti, ok ? "ok" : "NO");
    log_seriale(r);
    printf("%s\n", r);
}

/* A line of `n` characters, all `c` but the last, which says which line. */
static char *riga_lunga(unsigned int n, unsigned int quale)
{
    static char *b = 0;
    static unsigned int posti = 0;

    if (n + 1 > posti) {
        char *nb = (char *)realloc(b, n + 1);
        if (!nb) return 0;
        b = nb;
        posti = n + 1;
    }
    memset(b, 'a' + (int)(quale % 26), n);
    b[n] = '\0';
    return b;
}

/* The window of the controls draws nothing of its own: the toolkit draws the
 * controls. Only the timer and the close box matter. */
static long proc_controlli(ExWindow f, unsigned int msg, unsigned int wp, long lp)
{
    if (msg == EXM_TIMER || msg == EXM_CLOSE) { chiudi_tutte(g_esito); return 0; }
    return ex_default_proc(f, msg, wp, lp);
}

static int controlli(int tieni_ms)
{
    ExWindow     f, c, m, a, k, s, l;
    unsigned int i, j, n;
    int          ok;
    char         t[64];
    char        *r;

    f = ex_create("window", "Controlli oltre i vecchi tetti",
                  EX_CAPTION | EX_BORDER | EX_CLOSEBOX, EX_AUTO, EX_AUTO,
                  640, 440, 0, 0, proc_controlli);
    if (!f) { riga("CONTROLLI fine %d (nessuna finestra)", 1, 0); return 1; }

    /* the combo: 100 entries */
    c = ex_create("combo", "", EX_CHILD, 8, 30, 200, 20, f, 0, 0);
    ok = c != 0;
    for (i = 0; ok && i < 100; i++) {
        snprintf(t, sizeof(t), "voce %u", i);
        ok = ex_item_add(c, t);
    }
    snprintf(t, sizeof(t), "voce %u", 99u);
    esito("combo", c ? ex_items_count(c) : 0, 100, ok && strcmp(ex_item_text(c, 99), t) == 0);

    /* the menu: 10 titles of 40 entries, 8 side drop-downs of 3 */
    m = ex_menu_bar(f);
    ok = m != 0;
    n = 0;
    for (i = 0; ok && i < 10; i++)
        for (j = 0; ok && j < 40; j++) {
            char tit[16];

            snprintf(tit, sizeof(tit), "M%u", i);
            snprintf(t, sizeof(t), "voce %u.%u", i, j);
            ok = ex_menu_add_item(m, tit, t, 1000 + i * 100 + j);
            if (ok) n++;
        }
    for (i = 0; ok && i < 8; i++)
        for (j = 0; ok && j < 3; j++) {
            char tit[24];

            snprintf(tit, sizeof(tit), "M0/laterale %u", i);
            snprintf(t, sizeof(t), "sotto %u.%u", i, j);
            ok = ex_menu_add_item(m, tit, t, 5000 + i * 10 + j);
            if (ok) n++;
        }
    esito("menu", n, 10 * 40 + 8 * 3, ok);

    /* the text area: 5000 lines of 600 */
    a = ex_create("textarea", "", EX_CHILD, 8, 60, 300, 160, f, 0, 0);
    ok = a != 0;
    for (i = 0; ok && i < 5000; i++) {
        r = riga_lunga(600, i);
        ok = r && ex_textarea_add_line(a, r);
    }
    esito("areatesto", a ? ex_textarea_line_count(a) : 0, 5000,
          ok && strlen(ex_textarea_line(a, 4999)) == 600 &&
          ex_textarea_line(a, 4999)[599] == 'a' + (4999 % 26));

    /* the code area: 4000 lines of 300 */
    k = ex_create("codearea", "", EX_CHILD, 316, 60, 300, 160, f, 0, 0);
    ok = k != 0;
    for (i = 0; ok && i < 4000; i++) {
        r = riga_lunga(300, i);
        ok = r && ex_textarea_add_line(k, r);
    }
    esito("areacodice", k ? ex_textarea_line_count(k) : 0, 4000,
          ok && strlen(ex_textarea_line(k, 3999)) == 300);

    /* the text box: 5000 characters */
    s = ex_create("textbox", "", EX_CHILD, 8, 230, 600, 22, f, 0, 0);
    r = riga_lunga(5000, 7);
    if (s && r) ex_set_title(s, r);
    esito("casella", s ? (unsigned int)strlen(ex_get_text(s)) : 0, 5000, s != 0);

    /* the list: 70000 rows of 150 */
    l = ex_create("list", "", EX_CHILD, 8, 260, 600, 160, f, 0, 0);
    ok = l != 0;
    for (i = 0; ok && i < 70000; i++) {
        r = riga_lunga(150, i);
        ok = r && ex_list_add(l, r);
    }
    esito("lista", l ? ex_list_count(l) : 0, 70000,
          ok && strlen(ex_list_text(l, 69999)) == 150 &&
          ex_list_text(l, 69999)[0] == 'a' + (69999 % 26));

    ex_default_proc(f, EXM_PAINT, 0, 0);    /* the background and the controls */
    riga("CONTROLLI fine %d (errori %d)", g_errori ? 1 : 0, g_errori);
    g_esito = g_errori ? 1 : 0;
    g_n = 1;
    g_fin = &f;
    ex_set_timer(f, (unsigned int)tieni_ms);
    {
        ExMsg msg;

        while (ex_get_message(&msg)) ex_dispatch(&msg);
    }
    return g_esito;
}

/* --- -t: the keys ---------------------------------------------------------- */
static ExWindow g_t_casella = 0, g_t_area = 0;

static void tasto_riga(const char *cosa, unsigned int wp)
{
    ExKeyInfo ki;
    char      r[128];

    if (ex_key_info(&ki))
        snprintf(r, sizeof(r), "TASTI %s c=%x u=%x f=%x m=%x r=%d", cosa,
                 ki.code, ki.unicode, ki.physical, ki.mods, ki.repeat);
    else
        snprintf(r, sizeof(r), "TASTI %s c=%x (senza dettagli)", cosa, wp);
    log_seriale(r);
}

static void esadecimale(const char *cosa, const char *t)
{
    char         r[256];
    unsigned int n;

    n = (unsigned int)snprintf(r, sizeof(r), "TASTI %s ", cosa);
    while (t && *t && n + 3 < sizeof(r)) {
        snprintf(r + n, sizeof(r) - n, "%02x", (unsigned char)*t++);
        n += 2;
    }
    log_seriale(r);
}

static long proc_tasti(ExWindow f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_KEY:      tasto_riga("giu", wp); break;
    case EXM_KEY_DOWN: tasto_riga("mod", wp); return EX_NO_REDRAW;
    case EXM_KEY_UP:   tasto_riga("su", wp);  return EX_NO_REDRAW;
    case EXM_TIMER:
    case EXM_CLOSE:
        esadecimale("casella", ex_get_text(g_t_casella));
        esadecimale("area", ex_textarea_line(g_t_area, 0));
        log_seriale("TASTI fine");
        chiudi_tutte(0);
        return 0;
    }
    return ex_default_proc(f, msg, wp, lp);
}

static int tasti(int tieni_ms)
{
    static ExWindow f;
    ExMsg           msg;

    f = ex_create("window", "Tasti: pressioni e rilasci",
                  EX_CAPTION | EX_BORDER | EX_CLOSEBOX, EX_AUTO, EX_AUTO,
                  420, 220, 0, 0, proc_tasti);
    if (!f) { log_seriale("TASTI fine (nessuna finestra)"); return 1; }
    g_t_casella = ex_create("textbox", "", EX_CHILD, 8, 10, 380, 22, f, 0, 0);
    g_t_area    = ex_create("textarea", "", EX_CHILD, 8, 40, 380, 120, f, 0, 0);
    ex_track_keys(f, 1);
    ex_set_focus(g_t_casella);
    ex_default_proc(f, EXM_PAINT, 0, 0);
    log_seriale("TASTI pronto");
    g_n = 1;
    g_fin = &f;
    ex_set_timer(f, (unsigned int)tieni_ms);
    while (ex_get_message(&msg)) ex_dispatch(&msg);
    return 0;
}

int main(int argc, char **argv)
{
    ExMsg        m;
    unsigned int sw = 0, sh = 0;
    int          i;
    char         titolo[160];

    if (argc > 1 && (strcmp(argv[1], "-c") == 0 || strcmp(argv[1], "-t") == 0)) {
        unsigned int w0 = 0, h0 = 0;
        int          ms = (argc > 2 && atoi(argv[2]) > 0) ? atoi(argv[2]) * 1000 : 5000;

        ex_screen_size(&w0, &h0);
        if (w0 == 0) {
            printf("finestreprova: il server a finestre non risponde (exwin).\n");
            return 1;
        }
        return argv[1][1] == 'c' ? controlli(ms) : tasti(ms);
    }

    g_n = (argc > 1) ? atoi(argv[1]) : 40;
    if (g_n <= 0) g_n = 40;
    if (argc > 2 && atoi(argv[2]) > 0) g_tieni_ms = atoi(argv[2]) * 1000;

    ex_screen_size(&sw, &sh);
    if (sw == 0) {
        printf("finestreprova: il server a finestre non risponde (exwin).\n");
        return 1;
    }

    g_fin = (ExWindow *)calloc((unsigned int)g_n, sizeof(ExWindow));
    if (!g_fin) return 1;
    g_t0 = uptime_ms();

    for (i = 0; i < g_n; i++) {
        snprintf(titolo, sizeof(titolo),
                 "Finestra %d di %d, con un titolo piu' lungo dei 48 byte di una volta",
                 i + 1, g_n);
        g_fin[i] = ex_create("window", titolo, EX_CAPTION | EX_BORDER | EX_CLOSEBOX,
                             EX_AUTO, EX_AUTO, 420, 60, 0, 0, proc);
        if (g_fin[i]) {
            g_aperte++;
            proc(g_fin[i], EXM_PAINT, 0, 0);
        }
    }
    riga("FINESTRE aperte %d di %d", g_aperte, g_n);
    if (g_aperte == 0) return 1;

    /* the list comes when it changes; and a timer as a safety net */
    ex_track_windows(g_fin[0]);
    ex_set_timer(g_fin[0], 60000);

    while (ex_get_message(&m)) ex_dispatch(&m);
    return g_esito < 0 ? 1 : g_esito;
}
