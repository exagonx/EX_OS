/* =============================================================================
 * lib/exwin/exwin_stub.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Lo STUB di ExWin: cio' che si collega dentro l'applicazione al posto della
 * libreria intera
 *
 * Un'applicazione grafica si collegava con `exwin.c` dentro: 13 KB di toolkit
 * piu' 4 KB di font in OGNI programma. Adesso si collega con questo file, che
 * definisce gli stessi nomi e li fa arrivare alla libreria condivisa.
 *
 * ! IL SORGENTE DELL'APPLICAZIONE NON CAMBIA DI UNA RIGA. Stesso `exwin.h`,
 * stesse chiamate. Cambia solo cosa si collega, e lo decide il Makefile: e'
 * questa la ragione per cui lo stub definisce esattamente i nomi di exwin.h
 * invece di offrirne di nuovi.
 *
 * ! LA RISOLUZIONE E' PIGRA, E NON C'E' NIENTE DA CHIAMARE PRIMA DI main().
 * Un `ex_avvia()` da mettere in cima a ogni main sarebbe una riga in piu' in
 * ogni applicazione — e una riga che, dimenticata, da' un salto a zero invece
 * di un messaggio. Qui la prima chiamata a una qualunque funzione di ExWin
 * risolve TUTTI i nomi in un colpo, e il costo e' un confronto per chiamata.
 *
 * ! SI CERCA IN DUE POSTI, E L'ORDINE CONTA: su un sistema installato la
 * libreria sta in /exwin/lib, avviando dal CD sta sotto /cdrom. E' la stessa
 * regola del program manager e del file manager, per la stessa ragione.
 * ============================================================================= */

#include "exwin.h"
#include "exlib.h"

/* Il minimo per lamentarsi e morire senza tirarsi dietro la libc: se ExWin non
 * si trova, printf potrebbe non essere l'unica cosa che manca. */
#define SYS_WRITE   4
#define SYS_EXIT    1

static void grida_e_muori(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    __asm__ volatile ("int $0x80" :: "a"(SYS_WRITE), "b"(2), "c"(s), "d"(n) : "memory");
    __asm__ volatile ("int $0x80" :: "a"(SYS_EXIT), "b"(1) : "memory");
    for (;;) { }
}

static const char *const g_dove[] = {
    "/exwin/lib/exwin.so",
    "/cdrom/exwin/lib/exwin.so"
};

/* I puntatori risolti. Uno solo `pronto`: o ci sono tutti o non si parte. */
static struct {
    int pronto;
    ExWindow  (*crea)(const char *, const char *, unsigned int,
                        int, int, int, int, ExWindow, unsigned int, ExWindowProc);
    void        (*distruggi)(ExWindow);
    void        (*titolo)(ExWindow, const char *);
    void        (*sposta)(ExWindow, int, int);
    void        (*misura)(ExWindow, int, int);
    void        (*mostra)(ExWindow, int);
    void        (*fuoco)(ExWindow);
    void        (*fuoco_via)(ExWindow);
    ExWindow  (*fuoco_chi)(ExWindow);
    void        (*spegni)(void);
    unsigned int (*app_metti)(const char *, unsigned int);
    unsigned int (*app_prendi)(char *, unsigned int);
    void        (*testo_metti)(ExWindow, const char *);
    const char *(*testo_prendi)(ExWindow);
    int         (*prendi_msg)(ExMsg *);
    int         (*msg_ora)(ExMsg *);
    void        (*smista)(const ExMsg *);
    void        (*esci)(int);
    long        (*procedura_base)(ExWindow, unsigned int, unsigned int, long);
    void        (*riempi)(ExWindow, int, int, int, int, unsigned int);
    void        (*riquadro)(ExWindow, int, int, int, int, unsigned int);
    void        (*scrivi)(ExWindow, int, int, const char *, unsigned int);
    void        (*aggiorna)(ExWindow);
    void        (*pixmap)(ExWindow, int, int, int, int,
                          const unsigned int *, unsigned int);
    int         (*immagine)(ExWindow, const char *, int, int);
    void         (*l_svuota)(ExWindow);
    int          (*l_aggiungi)(ExWindow, const char *);
    unsigned int (*l_quante)(ExWindow);
    unsigned int (*l_scelta)(ExWindow);
    void         (*l_scegli)(ExWindow, unsigned int);
    const char  *(*l_testo)(ExWindow, unsigned int);
    void         (*a_svuota)(ExWindow);
    int          (*a_aggiungi)(ExWindow, const char *);
    unsigned int (*a_righe)(ExWindow);
    const char  *(*a_riga)(ExWindow, unsigned int);
    int          (*a_modificato)(ExWindow);
    void         (*a_pulita)(ExWindow);
    void         (*a_cursore)(ExWindow, unsigned int *, unsigned int *);
    void         (*a_sel_tutto)(ExWindow);
    int          (*a_copia)(ExWindow);
    int          (*a_taglia)(ExWindow);
    int          (*a_incolla)(ExWindow);
    int          (*a_cancella)(ExWindow);
    ExWindow  (*menu)(ExWindow);
    int         (*menu_voce)(ExWindow, const char *, const char *, unsigned int);
    void        (*rilievo)(ExWindow, int, int, int, int);
    void        (*incavo)(ExWindow, int, int, int, int);
    void        (*schermo)(unsigned int *, unsigned int *);
    /* I font. In fondo, come vuole la regola dell'elenco esportato. */
    unsigned int (*font_apri)(const char *, int);
    unsigned int (*font_trova)(int, int, int, int);
    const char  *(*font_nome)(int, int, int);
    void         (*font_chiudi)(unsigned int);
    int          (*font_altezza)(unsigned int);
    int          (*font_base)(unsigned int);
    int          (*larghezza_testo)(unsigned int, const char *);
    void         (*scrivi_con)(ExWindow, unsigned int, int, int,
                               const char *, unsigned int);
    void         (*sveglia)(ExWindow, unsigned int);

    /* La spunta, il radio, la barra, le voci di un combo o di una barra di
     * linguette: aggiunti il 3 settembre 2026. */
    int          (*acceso)(ExWindow);
    void         (*accendi)(ExWindow, int);
    void         (*scorri_limiti)(ExWindow, unsigned int, unsigned int);
    unsigned int (*scorri_dove)(ExWindow);
    void         (*scorri_vai)(ExWindow, unsigned int);
    void         (*voci_svuota)(ExWindow);
    int          (*voce_aggiungi)(ExWindow, const char *);
    unsigned int (*voci_quante)(ExWindow);
    unsigned int (*voce_scelta)(ExWindow);
    void         (*voce_scegli)(ExWindow, unsigned int);
    const char  *(*voce_testo)(ExWindow, unsigned int);
    void         (*area_vai)(ExWindow, unsigned int, unsigned int);
    void         (*area_riga_metti)(ExWindow, unsigned int, const char *);
    void         (*area_colora)(ExWindow, ExHighlighter, void *);
    unsigned int (*colora_c)(void *, const char *, unsigned char *, unsigned int);
    ExWindow   (*mdi_attivo)(ExWindow);
    void         (*mdi_attiva)(ExWindow);

    void         (*lista_icona)(ExWindow, unsigned int, ExIcon);
    unsigned int (*lista_margine)(ExWindow);
    ExIcon      (*icona_apri)(const char *);
    unsigned int (*icona_lato)(ExIcon);
    int          (*icona_disegna)(ExWindow, ExIcon, int, int,
                                  unsigned int, unsigned int);
    void         (*icona_metti)(ExWindow, ExIcon, unsigned int);
    void         (*icona_chiudi)(ExIcon);
    unsigned int (*a_vista)(ExWindow, unsigned int *);
    void         (*a_mostra_da)(ExWindow, unsigned int);
    int          (*guarda_fd)(int, ExWatchProc, void *);
    void         (*tab_contenuto)(ExWindow, int);
    void         (*finestre_segui)(ExWindow);
    int          (*finestre_elenco)(ExWindowEntry *, int);
    void         (*finestra_attiva)(unsigned int);
    void         (*attiva)(ExWindow);          /* optional: 28 September 2026 */
    void         (*scrivi_in)(unsigned int *, int, int, ExFont, int, int, const char *, unsigned int);
    int          (*menu_comparsa)(ExWindow, int, int, const char *const *, int);
    int          (*apri_file)(const char *, char *, unsigned int);
    void         (*lista_multipla)(ExWindow, int);
    int          (*lista_scelte)(ExWindow, unsigned int *, int);
    int          (*lista_riga_a)(ExWindow, int);
    void         (*voci_schede)(ExWindow, int);
    int          (*voce_togli)(ExWindow, unsigned int);
    void         (*voce_rinomina)(ExWindow, unsigned int, const char *);
    void         (*mouse_passaggio)(ExWindow, int);
    int          (*ridisegna)(ExWindow);
    void         (*menu_spunta)(ExWindow, unsigned int, int);
    void         (*ritaglio)(ExWindow, int, int, int, int);
    void         (*pixmap_fuso)(ExWindow, int, int, int, int, const unsigned int *, unsigned int);
    void         (*finestra_riduci)(unsigned int);
    void         (*chiudi_altre)(void);
    void         (*area_seleziona)(ExWindow, unsigned int, unsigned int, unsigned int);
    int          (*imm_disponi)(ExWindow, const char *, int, int, int, int, int, unsigned int);
    const char  *(*versione)(void);
    void         (*abilita)(ExWindow, int);
} P;

static void *chiedi(const ExLibTesta *t, const char *nome)
{
    void *p = exlib_simbolo(t, nome);

    /* ! UN NOME CHE MANCA SI DICE, E SI DICE QUALE. E' il modo in cui ci si
     * accorge che la libreria installata e' piu' vecchia del programma —
     * l'unico guaio che il patto per nome non impedisce. Proseguire con un
     * puntatore a zero darebbe un salto nel vuoto, e il fault indicherebbe
     * l'applicazione invece della libreria. */
    if (p == 0) {
        grida_e_muori("exwin: la libreria condivisa non esporta un nome che "
                      "serve a questo programma:\n        ");
    }
    return p;
}

static void assicura(void)
{
    const ExLibTesta *t;

    if (P.pronto) return;

    t = exlib_apri_fra(g_dove, (int)(sizeof g_dove / sizeof g_dove[0]));
    if (t == 0) {
        grida_e_muori("exwin: non trovo la libreria condivisa.\n"
                      "       Cercata in /exwin/lib/exwin.so e "
                      "/cdrom/exwin/lib/exwin.so\n");
    }

    P.crea           = (ExWindow (*)(const char *, const char *, unsigned int,
                                       int, int, int, int, ExWindow,
                                       unsigned int, ExWindowProc))
                       chiedi(t, "ex_create");
    P.distruggi      = (void (*)(ExWindow))              chiedi(t, "ex_destroy");
    P.titolo         = (void (*)(ExWindow, const char *))chiedi(t, "ex_set_title");
    P.sposta         = (void (*)(ExWindow, int, int))    chiedi(t, "ex_move");
    P.misura         = (void (*)(ExWindow, int, int))    chiedi(t, "ex_resize");
    P.mostra         = (void (*)(ExWindow, int))         chiedi(t, "ex_show");
    P.fuoco          = (void (*)(ExWindow))              chiedi(t, "ex_set_focus");
    P.fuoco_via      = (void (*)(ExWindow))              chiedi(t, "ex_clear_focus");
    P.fuoco_chi      = (ExWindow (*)(ExWindow))        chiedi(t, "ex_get_focus");
    P.spegni         = (void (*)(void))                    chiedi(t, "ex_shutdown_desktop");
    P.app_metti      = (unsigned int (*)(const char *, unsigned int)) chiedi(t, "ex_clipboard_set");
    P.app_prendi     = (unsigned int (*)(char *, unsigned int))       chiedi(t, "ex_clipboard_get");
    P.testo_metti    = (void (*)(ExWindow, const char *))chiedi(t, "ex_set_text");
    P.testo_prendi   = (const char *(*)(ExWindow))       chiedi(t, "ex_get_text");
    P.prendi_msg     = (int (*)(ExMsg *))                  chiedi(t, "ex_get_message");
    P.msg_ora        = (int (*)(ExMsg *))                  chiedi(t, "ex_peek_message");
    P.smista         = (void (*)(const ExMsg *))           chiedi(t, "ex_dispatch");
    P.esci           = (void (*)(int))                     chiedi(t, "ex_quit");
    P.procedura_base = (long (*)(ExWindow, unsigned int, unsigned int, long))
                       chiedi(t, "ex_default_proc");
    P.riempi         = (void (*)(ExWindow, int, int, int, int, unsigned int))
                       chiedi(t, "ex_fill_rect");
    P.riquadro       = (void (*)(ExWindow, int, int, int, int, unsigned int))
                       chiedi(t, "ex_draw_rect");
    P.scrivi         = (void (*)(ExWindow, int, int, const char *, unsigned int))
                       chiedi(t, "ex_draw_text");
    P.aggiorna       = (void (*)(ExWindow))              chiedi(t, "ex_update");
    P.pixmap         = (void (*)(ExWindow, int, int, int, int,
                                 const unsigned int *, unsigned int))
                       chiedi(t, "ex_pixmap");
    P.immagine       = (int (*)(ExWindow, const char *, int, int))
                       chiedi(t, "ex_draw_image");
    P.l_svuota   = (void (*)(ExWindow))                     chiedi(t, "ex_list_clear");
    P.l_aggiungi = (int (*)(ExWindow, const char *))        chiedi(t, "ex_list_add");
    P.l_quante   = (unsigned int (*)(ExWindow))             chiedi(t, "ex_list_count");
    P.l_scelta   = (unsigned int (*)(ExWindow))             chiedi(t, "ex_list_get_selected");
    P.l_scegli   = (void (*)(ExWindow, unsigned int))       chiedi(t, "ex_list_select");
    P.l_testo    = (const char *(*)(ExWindow, unsigned int))chiedi(t, "ex_list_text");

    P.a_svuota     = (void (*)(ExWindow))                  chiedi(t, "ex_textarea_clear");
    P.a_aggiungi   = (int (*)(ExWindow, const char *))     chiedi(t, "ex_textarea_add_line");
    P.a_righe      = (unsigned int (*)(ExWindow))          chiedi(t, "ex_textarea_line_count");
    P.a_riga       = (const char *(*)(ExWindow, unsigned int)) chiedi(t, "ex_textarea_line");
    P.a_modificato = (int (*)(ExWindow))                   chiedi(t, "ex_textarea_is_modified");
    P.a_pulita     = (void (*)(ExWindow))                  chiedi(t, "ex_textarea_set_unmodified");
    P.a_cursore    = (void (*)(ExWindow, unsigned int *, unsigned int *))
                     chiedi(t, "ex_textarea_get_cursor");

    P.a_sel_tutto = (void (*)(ExWindow))chiedi(t, "ex_textarea_select_all");
    P.a_copia     = (int (*)(ExWindow)) chiedi(t, "ex_textarea_copy");
    P.a_taglia    = (int (*)(ExWindow)) chiedi(t, "ex_textarea_cut");
    P.a_incolla   = (int (*)(ExWindow)) chiedi(t, "ex_textarea_paste");
    P.a_cancella  = (int (*)(ExWindow)) chiedi(t, "ex_textarea_delete");
    P.menu           = (ExWindow (*)(ExWindow))        chiedi(t, "ex_menu_bar");
    P.menu_voce      = (int (*)(ExWindow, const char *, const char *, unsigned int))
                       chiedi(t, "ex_menu_add_item");
    P.rilievo        = (void (*)(ExWindow, int, int, int, int))
                       chiedi(t, "ex_draw_raised");
    P.incavo         = (void (*)(ExWindow, int, int, int, int))
                       chiedi(t, "ex_draw_sunken");
    P.schermo        = (void (*)(unsigned int *, unsigned int *))
                       chiedi(t, "ex_screen_size");

    P.font_apri       = (unsigned int (*)(const char *, int))
                        chiedi(t, "ex_font_open");
    P.font_trova      = (unsigned int (*)(int, int, int, int))
                        chiedi(t, "ex_font_find");
    P.font_nome       = (const char *(*)(int, int, int))
                        chiedi(t, "ex_font_name");
    P.font_chiudi     = (void (*)(unsigned int))chiedi(t, "ex_font_close");
    P.font_altezza    = (int (*)(unsigned int))chiedi(t, "ex_font_height");
    P.font_base       = (int (*)(unsigned int))chiedi(t, "ex_font_baseline");
    P.larghezza_testo = (int (*)(unsigned int, const char *))
                        chiedi(t, "ex_text_width");
    P.scrivi_con      = (void (*)(ExWindow, unsigned int, int, int,
                                  const char *, unsigned int))
                        chiedi(t, "ex_draw_text_font");
    P.sveglia         = (void (*)(ExWindow, unsigned int))
                        chiedi(t, "ex_set_timer");

    P.acceso        = (int (*)(ExWindow))            chiedi(t, "ex_is_checked");
    P.accendi       = (void (*)(ExWindow, int))      chiedi(t, "ex_set_checked");
    P.scorri_limiti = (void (*)(ExWindow, unsigned int, unsigned int))
                      chiedi(t, "ex_scroll_set_range");
    P.scorri_dove   = (unsigned int (*)(ExWindow))   chiedi(t, "ex_scroll_get_pos");
    P.scorri_vai    = (void (*)(ExWindow, unsigned int))
                      chiedi(t, "ex_scroll_set_pos");
    P.voci_svuota   = (void (*)(ExWindow))           chiedi(t, "ex_items_clear");
    P.voce_aggiungi = (int (*)(ExWindow, const char *))
                      chiedi(t, "ex_item_add");
    P.voci_quante   = (unsigned int (*)(ExWindow))   chiedi(t, "ex_items_count");
    P.voce_scelta   = (unsigned int (*)(ExWindow))   chiedi(t, "ex_item_get_selected");
    P.voce_scegli   = (void (*)(ExWindow, unsigned int))
                      chiedi(t, "ex_item_select");
    P.voce_testo    = (const char *(*)(ExWindow, unsigned int))
                      chiedi(t, "ex_item_text");
    P.area_vai      = (void (*)(ExWindow, unsigned int, unsigned int))
                      chiedi(t, "ex_textarea_set_cursor");
    P.area_riga_metti = (void (*)(ExWindow, unsigned int, const char *))
                        chiedi(t, "ex_textarea_set_line");
    P.area_colora   = (void (*)(ExWindow, ExHighlighter, void *))
                      chiedi(t, "ex_textarea_set_highlighter");
    P.colora_c      = (unsigned int (*)(void *, const char *, unsigned char *,
                                        unsigned int))
                      chiedi(t, "ex_highlight_c");
    P.mdi_attivo    = (ExWindow (*)(ExWindow)) chiedi(t, "ex_mdi_get_active");
    P.mdi_attiva    = (void (*)(ExWindow))       chiedi(t, "ex_mdi_activate");

    P.lista_icona   = (void (*)(ExWindow, unsigned int, ExIcon))
                      chiedi(t, "ex_list_set_icon");
    P.lista_margine = (unsigned int (*)(ExWindow))
                      chiedi(t, "ex_list_margin");
    P.icona_apri    = (ExIcon (*)(const char *))  chiedi(t, "ex_icon_open");
    P.icona_lato    = (unsigned int (*)(ExIcon))  chiedi(t, "ex_icon_size");
    P.icona_disegna = (int (*)(ExWindow, ExIcon, int, int, unsigned int,
                               unsigned int))      chiedi(t, "ex_icon_draw");
    P.icona_metti   = (void (*)(ExWindow, ExIcon, unsigned int))
                      chiedi(t, "ex_set_icon");
    P.icona_chiudi  = (void (*)(ExIcon))          chiedi(t, "ex_icon_close");
    P.a_vista       = (unsigned int (*)(ExWindow, unsigned int *))
                      chiedi(t, "ex_textarea_get_view");
    P.a_mostra_da   = (void (*)(ExWindow, unsigned int))
                      chiedi(t, "ex_textarea_scroll_to");
    /* ! FACOLTATIVA: chi la usa (i download di exdlg) sa dire che manca. */
    P.guarda_fd     = (int (*)(int, ExWatchProc, void *))
                      exlib_simbolo(t, "ex_watch_fd");
    P.tab_contenuto = (void (*)(ExWindow, int))
                      exlib_simbolo(t, "ex_tab_content");
    /* ! OPTIONAL, like the two above: a new taskbar over an old exwin.so
     * starts, and simply shows no entries. */
    P.finestre_segui  = (void (*)(ExWindow))
                        exlib_simbolo(t, "ex_track_windows");
    P.finestre_elenco = (int (*)(ExWindowEntry *, int))
                        exlib_simbolo(t, "ex_list_windows");
    P.finestra_attiva = (void (*)(unsigned int))
                        exlib_simbolo(t, "ex_activate_window_id");
    P.attiva          = (void (*)(ExWindow))exlib_simbolo(t, "ex_activate");
    P.scrivi_in       = (void (*)(unsigned int *, int, int, ExFont, int, int, const char *, unsigned int))
                        exlib_simbolo(t, "ex_draw_text_buffer");
    P.menu_comparsa   = (int (*)(ExWindow, int, int, const char *const *, int))
                        exlib_simbolo(t, "ex_popup_menu");
    P.apri_file       = (int (*)(const char *, char *, unsigned int))exlib_simbolo(t, "ex_open_file");
    P.lista_multipla  = (void (*)(ExWindow, int))exlib_simbolo(t, "ex_list_set_multiselect");
    P.lista_scelte    = (int (*)(ExWindow, unsigned int *, int))exlib_simbolo(t, "ex_list_get_selection");
    P.lista_riga_a    = (int (*)(ExWindow, int))exlib_simbolo(t, "ex_list_row_at");
    P.voci_schede     = (void (*)(ExWindow, int))exlib_simbolo(t, "ex_items_as_tabs");
    P.voce_togli      = (int (*)(ExWindow, unsigned int))exlib_simbolo(t, "ex_item_remove");
    P.voce_rinomina   = (void (*)(ExWindow, unsigned int, const char *))
                        exlib_simbolo(t, "ex_item_rename");
    P.mouse_passaggio = (void (*)(ExWindow, int))
                        exlib_simbolo(t, "ex_track_mouse_hover");
    P.ridisegna       = (int (*)(ExWindow))exlib_simbolo(t, "ex_redraw");
    P.menu_spunta     = (void (*)(ExWindow, unsigned int, int))
                        exlib_simbolo(t, "ex_menu_check");
    P.ritaglio        = (void (*)(ExWindow, int, int, int, int))
                        exlib_simbolo(t, "ex_set_clip");
    P.pixmap_fuso     = (void (*)(ExWindow, int, int, int, int, const unsigned int *, unsigned int))
                        exlib_simbolo(t, "ex_pixmap_blend");
    P.finestra_riduci = (void (*)(unsigned int))
                        exlib_simbolo(t, "ex_minimize_window_id");
    P.chiudi_altre    = (void (*)(void))
                        exlib_simbolo(t, "ex_close_others");
    P.area_seleziona  = (void (*)(ExWindow, unsigned int, unsigned int, unsigned int))
                        exlib_simbolo(t, "ex_textarea_select");
    P.imm_disponi     = (int (*)(ExWindow, const char *, int, int, int, int, int, unsigned int))
                        exlib_simbolo(t, "ex_draw_image_mode");
    P.versione        = (const char *(*)(void)) exlib_simbolo(t, "ex_version");
    P.abilita         = (void (*)(ExWindow, int)) exlib_simbolo(t, "ex_enable");

    P.pronto = 1;
}

/* -----------------------------------------------------------------------------
 * I ponti. Uno per funzione, e non c'e' altro modo di scriverli: una macro che
 * li generasse renderebbe illeggibile l'unico posto in cui si vede, in chiaro,
 * che cosa questo stub promette.
 * --------------------------------------------------------------------------- */
ExWindow ex_create(const char *classe, const char *titolo, unsigned int stile,
                   int x, int y, int w, int h,
                   ExWindow padre, unsigned int id, ExWindowProc proc)
{
    assicura();
    return P.crea(classe, titolo, stile, x, y, w, h, padre, id, proc);
}

void ex_destroy(ExWindow f)            { assicura(); P.distruggi(f); }
void ex_set_title(ExWindow f, const char *s){ assicura(); P.titolo(f, s); }
void ex_move(ExWindow f, int x, int y) { assicura(); P.sposta(f, x, y); }
void ex_resize(ExWindow f, int w, int h) { assicura(); P.misura(f, w, h); }

void ex_textarea_select_all(ExWindow f) { assicura(); P.a_sel_tutto(f); }
int  ex_textarea_copy(ExWindow f)    { assicura(); return P.a_copia(f); }
int  ex_textarea_cut(ExWindow f)   { assicura(); return P.a_taglia(f); }
int  ex_textarea_paste(ExWindow f)  { assicura(); return P.a_incolla(f); }
int  ex_textarea_delete(ExWindow f) { assicura(); return P.a_cancella(f); }

ExWindow ex_menu_bar(ExWindow f) { assicura(); return P.menu(f); }
int ex_menu_add_item(ExWindow m, const char *t, const char *v, unsigned int id)
{ assicura(); return P.menu_voce(m, t, v, id); }

void ex_draw_raised(ExWindow f, int x, int y, int w, int h)
{ assicura(); P.rilievo(f, x, y, w, h); }
void ex_draw_sunken(ExWindow f, int x, int y, int w, int h)
{ assicura(); P.incavo(f, x, y, w, h); }
void ex_show(ExWindow f, int v)        { assicura(); P.mostra(f, v); }
void ex_set_focus(ExWindow f)                { assicura(); P.fuoco(f); }
void ex_clear_focus(ExWindow f)            { assicura(); P.fuoco_via(f); }
ExWindow ex_get_focus(ExWindow f)      { assicura(); return P.fuoco_chi(f); }
void ex_shutdown_desktop(void)             { assicura(); P.spegni(); }
unsigned int ex_clipboard_set(const char *t, unsigned int n)
                                           { assicura(); return P.app_metti(t, n); }
unsigned int ex_clipboard_get(char *o, unsigned int m)
                                           { assicura(); return P.app_prendi(o, m); }

void ex_set_text(ExWindow f, const char *s) { assicura(); P.testo_metti(f, s); }
const char *ex_get_text(ExWindow f)        { assicura(); return P.testo_prendi(f); }

int  ex_get_message(ExMsg *m)        { assicura(); return P.prendi_msg(m); }
int  ex_peek_message(ExMsg *m)           { assicura(); return P.msg_ora(m); }
void ex_dispatch(const ExMsg *m)      { assicura(); P.smista(m); }
void ex_quit(int codice)            { assicura(); P.esci(codice); }

long ex_default_proc(ExWindow f, unsigned int msg, unsigned int wp, long lp)
{
    assicura();
    return P.procedura_base(f, msg, wp, lp);
}

void ex_fill_rect(ExWindow f, int x, int y, int w, int h, unsigned int c)
{
    assicura();
    P.riempi(f, x, y, w, h, c);
}

void ex_draw_rect(ExWindow f, int x, int y, int w, int h, unsigned int c)
{
    assicura();
    P.riquadro(f, x, y, w, h, c);
}

void ex_draw_text(ExWindow f, int x, int y, const char *s, unsigned int c)
{
    assicura();
    P.scrivi(f, x, y, s, c);
}

void ex_update(ExWindow f) { assicura(); P.aggiorna(f); }

void ex_pixmap(ExWindow f, int x, int y, int w, int h,
               const unsigned int *px, unsigned int passo)
{
    assicura();
    P.pixmap(f, x, y, w, h, px, passo);
}

int ex_draw_image(ExWindow f, const char *percorso, int x, int y)
{
    assicura();
    return P.immagine(f, percorso, x, y);
}

void ex_list_clear(ExWindow f)  { assicura(); P.l_svuota(f); }
unsigned int ex_list_count(ExWindow f)  { assicura(); return P.l_quante(f); }
unsigned int ex_list_get_selected(ExWindow f)  { assicura(); return P.l_scelta(f); }
void ex_list_select(ExWindow f, unsigned int i) { assicura(); P.l_scegli(f, i); }

int ex_list_add(ExWindow f, const char *s)
{
    assicura();
    return P.l_aggiungi(f, s);
}

const char *ex_list_text(ExWindow f, unsigned int i)
{
    assicura();
    return P.l_testo(f, i);
}

void ex_textarea_clear(ExWindow f)          { assicura(); P.a_svuota(f); }
unsigned int ex_textarea_line_count(ExWindow f)   { assicura(); return P.a_righe(f); }
int  ex_textarea_is_modified(ExWindow f)      { assicura(); return P.a_modificato(f); }
void ex_textarea_set_unmodified(ExWindow f)          { assicura(); P.a_pulita(f); }

int ex_textarea_add_line(ExWindow f, const char *r) { assicura(); return P.a_aggiungi(f, r); }
const char *ex_textarea_line(ExWindow f, unsigned int i) { assicura(); return P.a_riga(f, i); }

void ex_textarea_get_cursor(ExWindow f, unsigned int *r, unsigned int *c)
{
    assicura();
    P.a_cursore(f, r, c);
}

void ex_screen_size(unsigned int *l, unsigned int *a) { assicura(); P.schermo(l, a); }

ExFont ex_font_open(const char *p, int corpo) { assicura(); return P.font_apri(p, corpo); }
ExFont ex_font_find(int fam, int corpo, int g, int c) { assicura(); return P.font_trova(fam, corpo, g, c); }
const char *ex_font_name(int fam, int g, int c) { assicura(); return P.font_nome(fam, g, c); }
void   ex_font_close(ExFont f)               { assicura(); P.font_chiudi(f); }
int    ex_font_height(ExFont f)              { assicura(); return P.font_altezza(f); }
int    ex_font_baseline(ExFont f)                 { assicura(); return P.font_base(f); }

int ex_text_width(ExFont f, const char *s)
{ assicura(); return P.larghezza_testo(f, s); }

void ex_draw_text_font(ExWindow w, ExFont f, int x, int y,
                   const char *s, unsigned int c)
{ assicura(); P.scrivi_con(w, f, x, y, s, c); }

void ex_set_timer(ExWindow f, unsigned int ms) { assicura(); P.sveglia(f, ms); }

int  ex_is_checked(ExWindow c)             { assicura(); return P.acceso(c); }
void ex_set_checked(ExWindow c, int a)     { assicura(); P.accendi(c, a); }

void ex_scroll_set_range(ExWindow c, unsigned int massimo, unsigned int pagina)
{ assicura(); P.scorri_limiti(c, massimo, pagina); }
unsigned int ex_scroll_get_pos(ExWindow c) { assicura(); return P.scorri_dove(c); }
void ex_scroll_set_pos(ExWindow c, unsigned int d) { assicura(); P.scorri_vai(c, d); }

void ex_items_clear(ExWindow c)         { assicura(); P.voci_svuota(c); }
int  ex_item_add(ExWindow c, const char *t)
{ assicura(); return P.voce_aggiungi(c, t); }
unsigned int ex_items_count(ExWindow c) { assicura(); return P.voci_quante(c); }
unsigned int ex_item_get_selected(ExWindow c) { assicura(); return P.voce_scelta(c); }
void ex_item_select(ExWindow c, unsigned int i) { assicura(); P.voce_scegli(c, i); }
const char *ex_item_text(ExWindow c, unsigned int i)
{ assicura(); return P.voce_testo(c, i); }

void ex_textarea_set_cursor(ExWindow c, unsigned int riga, unsigned int col)
{ assicura(); P.area_vai(c, riga, col); }

void ex_textarea_set_line(ExWindow c, unsigned int riga, const char *testo)
{ assicura(); P.area_riga_metti(c, riga, testo); }
void ex_textarea_set_highlighter(ExWindow c, ExHighlighter fn, void *dato)
{ assicura(); P.area_colora(c, fn, dato); }

/* ! CHI SCRIVE `ex_textarea_set_highlighter(a, ex_highlight_c, 0)` PASSA QUESTO PONTE, non la
 * funzione dentro la libreria: l'indirizzo che il programma conosce e' questo.
 * Funziona — la libreria chiama qui e questo rimbalza di la' — e costa un salto
 * in piu' per riga disegnata, cioe' una trentina per ridisegno. Si dice perche'
 * chi misurera' i tempi lo trovera' nel mezzo e non deve stupirsi. */
unsigned int ex_highlight_c(void *dato, const char *riga, unsigned char *ruoli,
                         unsigned int stato)
{ assicura(); return P.colora_c(dato, riga, ruoli, stato); }

ExWindow ex_mdi_get_active(ExWindow c) { assicura(); return P.mdi_attivo(c); }
void       ex_mdi_activate(ExWindow c) { assicura(); P.mdi_attiva(c); }

ExIcon      ex_icon_open(const char *p)  { assicura(); return P.icona_apri(p); }

void ex_list_set_icon(ExWindow f, unsigned int riga, ExIcon ic)
{
    assicura();
    P.lista_icona(f, riga, ic);
}

unsigned int ex_list_margin(ExWindow f)
{
    assicura();
    return P.lista_margine(f);
}
unsigned int ex_icon_size(ExIcon ic)     { assicura(); return P.icona_lato(ic); }
void         ex_icon_close(ExIcon ic)   { assicura(); P.icona_chiudi(ic); }
unsigned int ex_textarea_get_view(ExWindow f, unsigned int *v) { assicura(); return P.a_vista(f, v); }
void         ex_textarea_scroll_to(ExWindow f, unsigned int r) { assicura(); P.a_mostra_da(f, r); }
int          ex_watch_fd(int fd, ExWatchProc fn, void *dato)
{ assicura(); return P.guarda_fd ? P.guarda_fd(fd, fn, dato) : -2; }

/* Optional: an older exwin.so without it just keeps Tab to itself. */
void         ex_tab_content(ExWindow f, int si)
{ assicura(); if (P.tab_contenuto) P.tab_contenuto(f, si); }

void         ex_track_windows(ExWindow f)
{ assicura(); if (P.finestre_segui) P.finestre_segui(f); }

int          ex_list_windows(ExWindowEntry *v, int max)
{ assicura(); return P.finestre_elenco ? P.finestre_elenco(v, max) : 0; }

void         ex_activate_window_id(unsigned int id)
{ assicura(); if (P.finestra_attiva) P.finestra_attiva(id); }
void         ex_activate(ExWindow f)
{ assicura(); if (P.attiva) P.attiva(f); }
void ex_draw_text_buffer(unsigned int *px, int w, int h, ExFont f, int x, int y,
                  const char *s, unsigned int c)
{ assicura(); if (P.scrivi_in) P.scrivi_in(px, w, h, f, x, y, s, c); }
int ex_popup_menu(ExWindow f, int x, int y, const char *const *voci, int n)
{ assicura(); return P.menu_comparsa ? P.menu_comparsa(f, x, y, voci, n) : -1; }
int ex_open_file(const char *percorso, char *prog, unsigned int max)
{ assicura(); return P.apri_file ? P.apri_file(percorso, prog, max) : -1; }
void ex_list_set_multiselect(ExWindow f, int si)
{ assicura(); if (P.lista_multipla) P.lista_multipla(f, si); }
int ex_list_get_selection(ExWindow f, unsigned int *righe, int max)
{ assicura(); if (P.lista_scelte) return P.lista_scelte(f, righe, max);
  if (righe && max > 0) { righe[0] = ex_list_get_selected(f); return 1; } return 0; }
int ex_list_row_at(ExWindow f, int y)
{ assicura(); return P.lista_riga_a ? P.lista_riga_a(f, y) : -1; }
/* Su una exwin.so vecchia le schede sono una barra di linguette qualunque:
 * niente X e niente Ctrl+Tab, e togliere o rinominare non si puo'. */
void ex_items_as_tabs(ExWindow c, int si)
{ assicura(); if (P.voci_schede) P.voci_schede(c, si); }
int ex_item_remove(ExWindow c, unsigned int i)
{ assicura(); return P.voce_togli ? P.voce_togli(c, i) : 0; }
void ex_item_rename(ExWindow c, unsigned int i, const char *testo)
{ assicura(); if (P.voce_rinomina) P.voce_rinomina(c, i, testo); }
/* Over an older exwin.so the window simply gets no passing messages. */
void ex_track_mouse_hover(ExWindow f, int si)
{ assicura(); if (P.mouse_passaggio) P.mouse_passaggio(f, si); }
int ex_redraw(ExWindow c)
{ assicura(); return P.ridisegna ? P.ridisegna(c) : 0; }
void ex_menu_check(ExWindow menu, unsigned int id, int acceso)
{ assicura(); if (P.menu_spunta) P.menu_spunta(menu, id, acceso); }
/* Over an older exwin.so nothing is clipped: the drawing is as before. */
void ex_set_clip(ExWindow f, int x, int y, int w, int h)
{ assicura(); if (P.ritaglio) P.ritaglio(f, x, y, w, h); }
/* Over an older exwin.so the pixels are laid down as they are. */
void ex_pixmap_blend(ExWindow f, int x, int y, int w, int h,
                    const unsigned int *px, unsigned int passo)
{
    assicura();
    if (P.pixmap_fuso) P.pixmap_fuso(f, x, y, w, h, px, passo);
    else ex_pixmap(f, x, y, w, h, px, passo);
}

void         ex_minimize_window_id(unsigned int id)
{ assicura(); if (P.finestra_riduci) P.finestra_riduci(id); }

void         ex_close_others(void)
{ assicura(); if (P.chiudi_altre) P.chiudi_altre(); }

/* Over an older exwin.so: the cursor goes there, without the selection. */
void         ex_textarea_select(ExWindow a, unsigned int riga, unsigned int da,
                               unsigned int fino)
{
    assicura();
    if (P.area_seleziona) P.area_seleziona(a, riga, da, fino);
    else                  ex_textarea_set_cursor(a, riga, fino);
}

/* Over an exwin.so from before 27 September 2026 there is no version. */
const char  *ex_version(void)
{
    assicura();
    return P.versione ? P.versione() : "(di prima delle versioni)";
}

/* Over an older exwin.so the control simply stays usable. */
void         ex_enable(ExWindow c, int si)
{
    assicura();
    if (P.abilita) P.abilita(c, si);
}

/* Over an older exwin.so: the image as it is, in the corner. */
int          ex_draw_image_mode(ExWindow f, const char *percorso, int x, int y,
                                 int w, int h, int modo, unsigned int sfondo)
{
    assicura();
    if (P.imm_disponi) return P.imm_disponi(f, percorso, x, y, w, h, modo, sfondo);
    return P.immagine(f, percorso, x, y);
}

void ex_set_icon(ExWindow c, ExIcon ic, unsigned int lato)
{
    assicura();
    P.icona_metti(c, ic, lato);
}

int ex_icon_draw(ExWindow f, ExIcon ic, int x, int y,
                     unsigned int lato, unsigned int sfondo)
{
    assicura();
    return P.icona_disegna(f, ic, x, y, lato, sfondo);
}
