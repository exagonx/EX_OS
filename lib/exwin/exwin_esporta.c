/* =============================================================================
 * lib/exwin/exwin_esporta.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Cio' che exwin.so mette a disposizione — l'unico file da toccare per
 * aggiungere una funzione alla libreria
 *
 * ! LE VOCI SI AGGIUNGONO IN FONDO PER ABITUDINE, NON PER OBBLIGO. La
 * risoluzione e' per nome (vedi lib/include/exlib.h): l'ordine di questo
 * elenco non e' parte dell'ABI e riordinarlo non rompe niente. Lo si tiene
 * ordinato per comodita' di chi legge, non perche' serva.
 *
 * ! QUELLO CHE ROMPE E' TOGLIERE UN NOME. Un'applicazione gia' compilata lo
 * cerchera' comunque, non lo trovera', e si fermera' dicendolo. E' il patto di
 * una DLL: si aggiunge quanto si vuole, non si toglie.
 *
 * ! I DUE ELENCHI DEVONO AVERE LO STESSO ORDINE, ed e' l'unica cosa che si
 * puo' sbagliare qui dentro. L'asserzione in fondo controlla che siano lunghi
 * uguale — che non e' la stessa cosa, ma prende il caso in cui se ne aggiunge
 * uno solo dei due, che e' come si sbaglia davvero.
 * ============================================================================= */

#include "exlib.h"
#include "exwin.h"

/* ! exwin.so USA LA LIBC CONDIVISA, quindi i suoi ponti vanno riempiti prima
 * che una qualunque delle sue funzioni venga chiamata. Una libreria non ha un
 * _start in cui farlo: lo fa exlib_apri() di chi la apre, cercando proprio
 * questo nome. Vedi lib/exlib/exlib.c. */
void __libc_ponti_avvia(void);

static const char *const g_nomi[] = {
    /* creare e distruggere */
    "ex_create",
    "ex_destroy",
    "ex_set_title",
    "ex_move",
    "ex_resize",
    "ex_show",
    "ex_set_focus",

    /* il testo di un controllo */
    "ex_set_text",
    "ex_get_text",

    /* il ciclo dei messaggi */
    "ex_get_message",
    "ex_peek_message",
    "ex_dispatch",
    "ex_quit",
    "ex_default_proc",

    /* disegnare */
    "ex_fill_rect",
    "ex_draw_rect",
    "ex_draw_text",
    "ex_update",
    "ex_pixmap",
    "ex_draw_image",

    /* la lista a scorrimento */
    "ex_list_clear",
    "ex_list_add",
    "ex_list_count",
    "ex_list_get_selected",
    "ex_list_select",
    "ex_list_text",

    /* l'area di testo multiriga */
    "ex_textarea_clear",
    "ex_textarea_add_line",
    "ex_textarea_line_count",
    "ex_textarea_line",
    "ex_textarea_is_modified",
    "ex_textarea_set_unmodified",
    "ex_textarea_get_cursor",

    /* lo schermo */
    "ex_textarea_select_all",
    "ex_textarea_copy",
    "ex_textarea_cut",
    "ex_textarea_paste",
    "ex_textarea_delete",
    "ex_menu_bar",
    "ex_menu_add_item",
    "ex_draw_raised",
    "ex_draw_sunken",
    "ex_screen_size",

    /* I font. In fondo, come vuole la regola: si aggiunge, non si riordina. */
    "ex_font_open",
    "ex_font_close",
    "ex_font_height",
    "ex_font_baseline",
    "ex_text_width",
    "ex_draw_text_font",
    "ex_set_timer",

    /* Aggiunte il 25 agosto 2026: trovare un carattere per famiglia. */
    "ex_font_find",
    "ex_font_name",

    "ex_clear_focus",
    /* Aggiunta il 4 settembre 2026: due caselle nella stessa barra vogliono
     * sapere quale ha il fuoco — vedi la casella «Cerca» del navigatore. */
    "ex_get_focus",
    "ex_shutdown_desktop",
    "ex_clipboard_set",
    "ex_clipboard_get",

    /* Aggiunti il 3 settembre 2026: la spunta, il radio, la barra di
     * scorrimento, l'elenco a discesa e le linguette. */
    "ex_is_checked",
    "ex_set_checked",
    "ex_scroll_set_range",
    "ex_scroll_get_pos",
    "ex_scroll_set_pos",
    "ex_items_clear",
    "ex_item_add",
    "ex_items_count",
    "ex_item_get_selected",
    "ex_item_select",
    "ex_item_text",

    /* Il testo colorato: «areacodice», il cursore che si porta, il gancio del
     * coloritore e quello del C gia' fatto. */
    "ex_textarea_set_cursor",
    "ex_textarea_set_line",
    "ex_textarea_set_highlighter",
    "ex_highlight_c",

    /* Il contenitore MDI. */
    "ex_mdi_get_active",
    "ex_mdi_activate",

    /* Le icone: aperte una volta, disegnate a qualunque misura. */
    "ex_list_set_icon",
    "ex_list_margin",
    "ex_icon_open",
    "ex_icon_size",
    "ex_icon_draw",
    "ex_set_icon",
    "ex_icon_close",

    /* La vista di un'area, per la barra di scorrimento accanto. */
    "ex_textarea_get_view",
    "ex_textarea_scroll_to",

    /* Un descrittore sorvegliato dal ciclo dei messaggi. */
    "ex_watch_fd",

    "ex_tab_content",

    /* Added on 26 September 2026: the taskbar (@FIN-ICONA). */
    "ex_track_windows",
    "ex_list_windows",
    "ex_activate_window_id",
    "ex_minimize_window_id",
    "ex_close_others",
    "ex_textarea_select",

    /* Added on 27 September 2026: the desktop image (@PM-SFONDO). */
    "ex_draw_image_mode",

    /* Added on 27 September 2026: side menus, and the version (Calctor). */
    "ex_version",
    "ex_enable",
    "ex_activate",
    "ex_draw_text_buffer",
    "ex_popup_menu",
    "ex_open_file",
    "ex_list_set_multiselect",
    "ex_list_get_selection",
    "ex_list_row_at",
    "ex_items_as_tabs",               /* 29 settembre 2026, @TOOLKIT-SCHEDE */
    "ex_item_remove",
    "ex_item_rename",
    "ex_track_mouse_hover",           /* 30 settembre 2026, @EXWIN-PASSAGGIO */
    "ex_redraw",
    "ex_menu_check",
    "ex_set_clip",
    "ex_pixmap_blend",

    /* L'avvio della libreria: lo chiama chi la apre, non l'applicazione. */
    "__lib_avvio",

    /* I nomi italiani di prima del 3 ottobre 2026, per i programmi gia'
     * compilati: stesse funzioni (vedi exwin.h, EXWIN_SENZA_NOMI_ITALIANI). */
    "ex_crea",
    "ex_distruggi",
    "ex_titolo",
    "ex_sposta",
    "ex_misura",
    "ex_mostra",
    "ex_fuoco",
    "ex_testo_metti",
    "ex_testo_prendi",
    "ex_prendi_msg",
    "ex_msg_ora",
    "ex_smista",
    "ex_esci",
    "ex_procedura_base",
    "ex_riempi",
    "ex_riquadro_disegna",
    "ex_scrivi",
    "ex_aggiorna",
    "ex_immagine",
    "ex_lista_svuota",
    "ex_lista_aggiungi",
    "ex_lista_quante",
    "ex_lista_scelta",
    "ex_lista_scegli",
    "ex_lista_testo",
    "ex_area_svuota",
    "ex_area_aggiungi",
    "ex_area_righe",
    "ex_area_riga",
    "ex_area_modificato",
    "ex_area_pulita",
    "ex_area_cursore",
    "ex_area_seleziona_tutto",
    "ex_area_copia",
    "ex_area_taglia",
    "ex_area_incolla",
    "ex_area_cancella",
    "ex_menu",
    "ex_menu_voce",
    "ex_rilievo",
    "ex_incavo",
    "ex_schermo",
    "ex_font_apri",
    "ex_font_chiudi",
    "ex_font_altezza",
    "ex_font_base",
    "ex_larghezza_testo",
    "ex_scrivi_con",
    "ex_sveglia",
    "ex_font_trova",
    "ex_font_nome",
    "ex_fuoco_via",
    "ex_fuoco_chi",
    "ex_spegni_scrivania",
    "ex_appunti_metti",
    "ex_appunti_prendi",
    "ex_acceso",
    "ex_accendi",
    "ex_scorri_limiti",
    "ex_scorri_dove",
    "ex_scorri_vai",
    "ex_voci_svuota",
    "ex_voce_aggiungi",
    "ex_voci_quante",
    "ex_voce_scelta",
    "ex_voce_scegli",
    "ex_voce_testo",
    "ex_area_vai",
    "ex_area_riga_metti",
    "ex_area_colora",
    "ex_colora_c",
    "ex_mdi_attivo",
    "ex_mdi_attiva",
    "ex_lista_icona",
    "ex_lista_margine",
    "ex_icona_apri",
    "ex_icona_lato",
    "ex_icona_disegna",
    "ex_icona_metti",
    "ex_icona_chiudi",
    "ex_area_vista",
    "ex_area_mostra_da",
    "ex_guarda_fd",
    "ex_tab_contenuto",
    "ex_finestre_segui",
    "ex_finestre_elenco",
    "ex_finestra_attiva",
    "ex_finestra_riduci",
    "ex_chiudi_le_altre",
    "ex_area_seleziona",
    "ex_immagine_disponi",
    "ex_versione",
    "ex_abilita",
    "ex_attiva",
    "ex_scrivi_in",
    "ex_menu_comparsa",
    "ex_apri_file",
    "ex_lista_multipla",
    "ex_lista_scelte",
    "ex_lista_riga_a",
    "ex_voci_schede",
    "ex_voce_togli",
    "ex_voce_rinomina",
    "ex_mouse_passaggio",
    "ex_ridisegna",
    "ex_menu_spunta",
    "ex_ritaglio",
    "ex_pixmap_fuso",
};

static void *const g_indirizzi[] = {
    (void *)ex_create,
    (void *)ex_destroy,
    (void *)ex_set_title,
    (void *)ex_move,
    (void *)ex_resize,
    (void *)ex_show,
    (void *)ex_set_focus,

    (void *)ex_set_text,
    (void *)ex_get_text,

    (void *)ex_get_message,
    (void *)ex_peek_message,
    (void *)ex_dispatch,
    (void *)ex_quit,
    (void *)ex_default_proc,

    (void *)ex_fill_rect,
    (void *)ex_draw_rect,
    (void *)ex_draw_text,
    (void *)ex_update,
    (void *)ex_pixmap,
    (void *)ex_draw_image,

    (void *)ex_list_clear,
    (void *)ex_list_add,
    (void *)ex_list_count,
    (void *)ex_list_get_selected,
    (void *)ex_list_select,
    (void *)ex_list_text,

    (void *)ex_textarea_clear,
    (void *)ex_textarea_add_line,
    (void *)ex_textarea_line_count,
    (void *)ex_textarea_line,
    (void *)ex_textarea_is_modified,
    (void *)ex_textarea_set_unmodified,
    (void *)ex_textarea_get_cursor,

    (void *)ex_textarea_select_all,
    (void *)ex_textarea_copy,
    (void *)ex_textarea_cut,
    (void *)ex_textarea_paste,
    (void *)ex_textarea_delete,
    (void *)ex_menu_bar,
    (void *)ex_menu_add_item,
    (void *)ex_draw_raised,
    (void *)ex_draw_sunken,
    (void *)ex_screen_size,

    (void *)ex_font_open,
    (void *)ex_font_close,
    (void *)ex_font_height,
    (void *)ex_font_baseline,
    (void *)ex_text_width,
    (void *)ex_draw_text_font,
    (void *)ex_set_timer,

    (void *)ex_font_find,
    (void *)ex_font_name,

    (void *)ex_clear_focus,
    (void *)ex_get_focus,
    (void *)ex_shutdown_desktop,
    (void *)ex_clipboard_set,
    (void *)ex_clipboard_get,

    (void *)ex_is_checked,
    (void *)ex_set_checked,
    (void *)ex_scroll_set_range,
    (void *)ex_scroll_get_pos,
    (void *)ex_scroll_set_pos,
    (void *)ex_items_clear,
    (void *)ex_item_add,
    (void *)ex_items_count,
    (void *)ex_item_get_selected,
    (void *)ex_item_select,
    (void *)ex_item_text,

    (void *)ex_textarea_set_cursor,
    (void *)ex_textarea_set_line,
    (void *)ex_textarea_set_highlighter,
    (void *)ex_highlight_c,

    (void *)ex_mdi_get_active,
    (void *)ex_mdi_activate,

    (void *)ex_list_set_icon,
    (void *)ex_list_margin,
    (void *)ex_icon_open,
    (void *)ex_icon_size,
    (void *)ex_icon_draw,
    (void *)ex_set_icon,
    (void *)ex_icon_close,

    (void *)ex_textarea_get_view,
    (void *)ex_textarea_scroll_to,

    (void *)ex_watch_fd,

    (void *)ex_tab_content,

    (void *)ex_track_windows,
    (void *)ex_list_windows,
    (void *)ex_activate_window_id,
    (void *)ex_minimize_window_id,
    (void *)ex_close_others,
    (void *)ex_textarea_select,

    (void *)ex_draw_image_mode,

    (void *)ex_version,
    (void *)ex_enable,
    (void *)ex_activate,
    (void *)ex_draw_text_buffer,
    (void *)ex_popup_menu,
    (void *)ex_open_file,
    (void *)ex_list_set_multiselect,
    (void *)ex_list_get_selection,
    (void *)ex_list_row_at,
    (void *)ex_items_as_tabs,
    (void *)ex_item_remove,
    (void *)ex_item_rename,
    (void *)ex_track_mouse_hover,
    (void *)ex_redraw,
    (void *)ex_menu_check,
    (void *)ex_set_clip,
    (void *)ex_pixmap_blend,

    (void *)__libc_ponti_avvia,

    /* gli stessi nomi italiani: vedi g_nomi */
    (void *)ex_create,
    (void *)ex_destroy,
    (void *)ex_set_title,
    (void *)ex_move,
    (void *)ex_resize,
    (void *)ex_show,
    (void *)ex_set_focus,
    (void *)ex_set_text,
    (void *)ex_get_text,
    (void *)ex_get_message,
    (void *)ex_peek_message,
    (void *)ex_dispatch,
    (void *)ex_quit,
    (void *)ex_default_proc,
    (void *)ex_fill_rect,
    (void *)ex_draw_rect,
    (void *)ex_draw_text,
    (void *)ex_update,
    (void *)ex_draw_image,
    (void *)ex_list_clear,
    (void *)ex_list_add,
    (void *)ex_list_count,
    (void *)ex_list_get_selected,
    (void *)ex_list_select,
    (void *)ex_list_text,
    (void *)ex_textarea_clear,
    (void *)ex_textarea_add_line,
    (void *)ex_textarea_line_count,
    (void *)ex_textarea_line,
    (void *)ex_textarea_is_modified,
    (void *)ex_textarea_set_unmodified,
    (void *)ex_textarea_get_cursor,
    (void *)ex_textarea_select_all,
    (void *)ex_textarea_copy,
    (void *)ex_textarea_cut,
    (void *)ex_textarea_paste,
    (void *)ex_textarea_delete,
    (void *)ex_menu_bar,
    (void *)ex_menu_add_item,
    (void *)ex_draw_raised,
    (void *)ex_draw_sunken,
    (void *)ex_screen_size,
    (void *)ex_font_open,
    (void *)ex_font_close,
    (void *)ex_font_height,
    (void *)ex_font_baseline,
    (void *)ex_text_width,
    (void *)ex_draw_text_font,
    (void *)ex_set_timer,
    (void *)ex_font_find,
    (void *)ex_font_name,
    (void *)ex_clear_focus,
    (void *)ex_get_focus,
    (void *)ex_shutdown_desktop,
    (void *)ex_clipboard_set,
    (void *)ex_clipboard_get,
    (void *)ex_is_checked,
    (void *)ex_set_checked,
    (void *)ex_scroll_set_range,
    (void *)ex_scroll_get_pos,
    (void *)ex_scroll_set_pos,
    (void *)ex_items_clear,
    (void *)ex_item_add,
    (void *)ex_items_count,
    (void *)ex_item_get_selected,
    (void *)ex_item_select,
    (void *)ex_item_text,
    (void *)ex_textarea_set_cursor,
    (void *)ex_textarea_set_line,
    (void *)ex_textarea_set_highlighter,
    (void *)ex_highlight_c,
    (void *)ex_mdi_get_active,
    (void *)ex_mdi_activate,
    (void *)ex_list_set_icon,
    (void *)ex_list_margin,
    (void *)ex_icon_open,
    (void *)ex_icon_size,
    (void *)ex_icon_draw,
    (void *)ex_set_icon,
    (void *)ex_icon_close,
    (void *)ex_textarea_get_view,
    (void *)ex_textarea_scroll_to,
    (void *)ex_watch_fd,
    (void *)ex_tab_content,
    (void *)ex_track_windows,
    (void *)ex_list_windows,
    (void *)ex_activate_window_id,
    (void *)ex_minimize_window_id,
    (void *)ex_close_others,
    (void *)ex_textarea_select,
    (void *)ex_draw_image_mode,
    (void *)ex_version,
    (void *)ex_enable,
    (void *)ex_activate,
    (void *)ex_draw_text_buffer,
    (void *)ex_popup_menu,
    (void *)ex_open_file,
    (void *)ex_list_set_multiselect,
    (void *)ex_list_get_selection,
    (void *)ex_list_row_at,
    (void *)ex_items_as_tabs,
    (void *)ex_item_remove,
    (void *)ex_item_rename,
    (void *)ex_track_mouse_hover,
    (void *)ex_redraw,
    (void *)ex_menu_check,
    (void *)ex_set_clip,
    (void *)ex_pixmap_blend,
};

/* Se qualcuno aggiunge un nome e dimentica l'indirizzo (o viceversa), la
 * compilazione si ferma qui invece di esportare un puntatore preso a caso. */
typedef char exwin_esporta_elenchi_pari[
    (sizeof(g_nomi) / sizeof(g_nomi[0]) ==
     sizeof(g_indirizzi) / sizeof(g_indirizzi[0])) ? 1 : -1];

EXLIB_TESTA(exwin_tabella, g_nomi, g_indirizzi);
