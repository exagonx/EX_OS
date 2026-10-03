'' =============================================================================
'' lib/exwin/exwin.bi
'' EX-OS — Extensible Operating System
''
'' Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
''
'' SPDX-License-Identifier: GPL-2.0-or-later
'' This file is part of EX-OS, distributed under the GNU GPL v2.
'' See the LICENSE file in the project root for the full license text.
'' =============================================================================
''
'' ExWin per FreeBASIC
''
'' ! E' PER QUESTO FILE CHE L'API E' IN STILE Win32. Una maniglia e' un intero
'' e un messaggio e' una struttura di interi: bastano un TYPE e qualche
'' DECLARE, e non serve nessun adattatore. Un toolkit a segnali avrebbe voluto
'' un thunk per ogni firma di callback, scritto a mano e da rifare a ogni
'' controllo nuovo.
''
'' ! LE FUNZIONI SONO cdecl, come tutta la libc di EX-OS. Dichiararle stdcall
'' compila, gira e sporca lo stack di ritorno: e' il genere di guasto che si
'' vede molte chiamate dopo, in un posto che non c'entra niente.
''
''     #include once "exwin.bi"
''
''     function procedura cdecl (byval f as ExWindow, byval msg as ulong, _
''                               byval wp as ulong, byval lp as long) as long
''         select case msg
''         case EXM_COMMAND : ex_set_title(f, "premuto")
''         case EXM_CLOSE  : ex_quit(0)
''         case else        : return ex_default_proc(f, msg, wp, lp)
''         end select
''         return 0
''     end function
''
''     dim as ExWindow f = ex_create("window", "Prova", EX_CAPTION or EX_BORDER, _
''                                   100, 100, 320, 200, 0, 0, @procedura)
''     ex_create("button", "OK", EX_CHILD, 20, 140, 80, 24, f, 1, 0)
''
''     dim as ExMsg m
''     while ex_get_message(@m) <> 0
''         ex_dispatch(@m)
''     wend
'' =============================================================================

#ifndef EXWIN_BI
#define EXWIN_BI

'' La maniglia: un intero opaco, come in C. 0 = nessuna finestra.
type ExWindow as ulong

type ExMsg
    union
        window as ExWindow      '' dal 3 ottobre 2026
        finestra as ExWindow    '' lo stesso campo, col nome di prima
    end union
    msg      as ulong
    wp       as ulong
    lp       as long
end type

type ExWindowProc as function cdecl (byval as ExWindow, byval as ulong, _
                                    byval as ulong, byval as long) as long

'' --- I messaggi --------------------------------------------------------------
const EXM_CREATE      = &h0001
const EXM_PAINT   = &h0002
const EXM_COMMAND   = &h0003
const EXM_CLOSE    = &h0004
const EXM_MOUSE_DOWN = &h0005
const EXM_MOUSE_UP  = &h0006
const EXM_KEY     = &h0007
const EXM_DESTROY = &h0008
const EXM_TERM_EXITED= &h0009
'' La finestra ha cambiato misura: EX_X(lp) e EX_Y(lp) sono quella nuova.
const EXM_SIZE    = &h000A
'' Il puntatore si e' mosso CON UN BOTTONE PREMUTO, e solo allora.
const EXM_MOUSE_MOVE = &h000B
'' Due clic vicini nel tempo e nello spazio. Su una lista non arriva: diventa
'' EXM_COMMAND con EX_IS_OPEN(lp) a 1, come l'Invio.
const EXM_DOUBLE_CLICK  = &h000C
'' La sveglia periodica e' scattata: vedi ex_set_timer. Risoluzione vera 200 ms.
const EXM_TIMER       = &h000D

'' --- Gli stili ---------------------------------------------------------------
const EX_CAPTION   = &h0001
const EX_BORDER    = &h0002
const EX_CLOSEBOX   = &h0004
const EX_VISIBLE = &h0008
const EX_BACKGROUND   = &h0010
const EX_TOPMOST    = &h0020
const EX_MODAL   = &h0040
'' Con questo la finestra si puo' tirare per l'angolo. Chi lo mette DEVE
'' gestire EXM_SIZE: vedi exwin.h.
const EX_RESIZABLE    = &h0080
'' x e y a EX_AUTO: la posizione la sceglie il server, a cascata. Serve a
'' poter aprire due volte lo stesso programma senza sovrapporlo a se stesso.
const EX_AUTO     = -1
const EX_CHILD   = &h0100

'' --- I colori, in ARGB -------------------------------------------------------
const EX_BLACK      = &h00000000
const EX_WHITE    = &h00FFFFFF
const EX_GRAY    = &h00C0C0C0
const EX_DARK_GRAY = &h00808080
const EX_BLUE       = &h00305A8A
const EX_RED     = &h00C04040
'' La luce viene da sopra a sinistra, sempre: vedi ex_draw_raised/ex_draw_sunken.
const EX_HIGHLIGHT      = &h00FFFFFF
const EX_SHADOW     = &h00000000

'' Le coordinate impacchettate in lp
#define EX_X(lp) (cint((lp) and &hFFFF))
#define EX_Y(lp) (cint(((lp) shr 16) and &hFFFF))

'' Per un EXM_COMMAND che viene da una LISTA: EX_IS_OPEN dice se si e' chiesto
'' di aprire (Invio o doppio clic) invece di solo scegliere, EX_COLUMN dice in
'' quale colonna della riga e' caduto il clic, o -1 se veniva dalla tastiera.
'' Vedi il commento lungo in exwin.h.
#define EX_IS_OPEN(lp) (cint((lp) and 1))
#define EX_COLUMN(lp) (iif((((lp) shr 8) and &hFFFF) <> 0, cint((((lp) shr 8) and &hFFFF) - 1), -1))

'' --- I font ------------------------------------------------------------------
''
'' Zero e' il font di sistema, l'8x16 compilato dentro il toolkit: c'e' sempre e
'' non si puo' chiudere. ex_font_open rende 0 se non trova il file, e zero e'
'' proprio il font di sistema — si scrive lo stesso, con un altro carattere.
const EX_FONT_SYSTEM = 0

'' corpo = altezza voluta in pixel; 0 = quella che il font ha per natura.
declare function ex_font_open cdecl alias "ex_font_open" ( _
    byval percorso as const zstring ptr, byval corpo as long) as ulong
declare sub ex_font_close cdecl alias "ex_font_close" (byval f as ulong)
declare function ex_font_height cdecl alias "ex_font_height" (byval f as ulong) as long
declare function ex_font_baseline cdecl alias "ex_font_baseline" (byval f as ulong) as long
declare function ex_text_width cdecl alias "ex_text_width" ( _
    byval f as ulong, byval s as const zstring ptr) as long
declare sub ex_draw_text_font cdecl alias "ex_draw_text_font" ( _
    byval w as ulong, byval f as ulong, byval x as long, byval y as long, _
    byval s as const zstring ptr, byval c as ulong)
declare sub ex_set_timer cdecl alias "ex_set_timer" ( _
    byval f as ulong, byval ms as ulong)

'' --- Creare ------------------------------------------------------------------
''
'' Per una finestra di primo livello: padre = 0, id = 0, proc = la procedura.
'' Per un controllo: padre = la finestra, id = il numero che tornera' in
'' EXM_COMMAND, proc = 0.
declare function ex_create cdecl alias "ex_create" ( _
    byval classe as const zstring ptr, _
    byval titolo as const zstring ptr, _
    byval stile  as ulong, _
    byval x as long, byval y as long, _
    byval w as long, byval h as long, _
    byval padre as ExWindow, _
    byval id    as ulong, _
    byval proc  as ExWindowProc) as ExWindow

declare sub ex_destroy cdecl alias "ex_destroy" (byval f as ExWindow)
declare sub ex_set_title     cdecl alias "ex_set_title" (byval f as ExWindow, byval s as const zstring ptr)
declare sub ex_move     cdecl alias "ex_move" (byval f as ExWindow, byval x as long, byval y as long)
declare sub ex_resize     cdecl alias "ex_resize" (byval f as ExWindow, byval w as long, byval h as long)
declare sub ex_show     cdecl alias "ex_show" (byval f as ExWindow, byval visibile as long)

'' --- Il rilievo: sporge cio' che si preme, rientra cio' in cui si scrive ----
declare sub ex_draw_raised cdecl alias "ex_draw_raised" (byval f as ExWindow, byval x as long, byval y as long, byval w as long, byval h as long)
declare sub ex_draw_sunken  cdecl alias "ex_draw_sunken"  (byval f as ExWindow, byval x as long, byval y as long, byval w as long, byval h as long)

'' --- I menu a tendina -------------------------------------------------------
'' La scelta arriva come EXM_COMMAND con l'id della voce, come un pulsante.
'' voce = "-" e' un solco; un tab nel testo allinea a destra la scorciatoia.
declare function ex_menu_bar      cdecl alias "ex_menu_bar" (byval finestra as ExWindow) as ExWindow
declare function ex_menu_add_item cdecl alias "ex_menu_add_item" ( _
    byval menu as ExWindow, _
    byval titolo as const zstring ptr, _
    byval voce as const zstring ptr, _
    byval id as ulong) as long

declare sub      ex_set_text  cdecl alias "ex_set_text"  (byval f as ExWindow, byval s as const zstring ptr)
declare function ex_get_text cdecl alias "ex_get_text" (byval f as ExWindow) as const zstring ptr

'' --- Il ciclo dei messaggi ---------------------------------------------------
declare function ex_get_message cdecl alias "ex_get_message" (byval m as ExMsg ptr) as long
declare sub      ex_dispatch     cdecl alias "ex_dispatch"     (byval m as const ExMsg ptr)
declare sub      ex_quit       cdecl alias "ex_quit"       (byval codice as long)

declare function ex_default_proc cdecl alias "ex_default_proc" ( _
    byval f as ExWindow, byval msg as ulong, _
    byval wp as ulong, byval lp as long) as long

'' --- Disegnare ---------------------------------------------------------------
declare sub ex_fill_rect           cdecl alias "ex_fill_rect" (byval f as ExWindow, byval x as long, byval y as long, byval w as long, byval h as long, byval c as ulong)
declare sub ex_draw_rect cdecl alias "ex_draw_rect" (byval f as ExWindow, byval x as long, byval y as long, byval w as long, byval h as long, byval c as ulong)
declare sub ex_draw_text           cdecl alias "ex_draw_text" (byval f as ExWindow, byval x as long, byval y as long, byval s as const zstring ptr, byval c as ulong)
declare sub ex_update         cdecl alias "ex_update" (byval f as ExWindow)

'' --- Le immagini -------------------------------------------------------------
'' Rende 1 se l'ha disegnata, 0 se il formato non e' (ancora) riconosciuto.
'' Il formato si riconosce dai primi byte del file, non dall'estensione.
declare function ex_draw_image cdecl alias "ex_draw_image" ( _
    byval f as ExWindow, byval percorso as const zstring ptr, _
    byval x as long, byval y as long) as long

declare sub ex_screen_size cdecl alias "ex_screen_size" (byval larghezza as ulong ptr, byval altezza as ulong ptr)


'' =============================================================================
'' I NOMI ITALIANI, come alias deprecati (3 ottobre 2026): l'API e' in inglese
'' (tools/exwin-inglese/mappa.txt). Con EXWIN_SENZA_NOMI_ITALIANI definito
'' prima dell'#include non ci sono.
'' =============================================================================
#ifndef EXWIN_SENZA_NOMI_ITALIANI
#define ExFinestra               ExWindow
#define ExProcedura              ExWindowProc
#define ExVoceFin                ExWindowEntry
#define ExIcona                  ExIcon
#define ExGuarda                 ExWatchProc
#define ExColora                 ExHighlighter
#define ex_crea                  ex_create
#define ex_distruggi             ex_destroy
#define ex_titolo                ex_set_title
#define ex_sposta                ex_move
#define ex_mostra                ex_show
#define ex_misura                ex_resize
#define ex_fuoco                 ex_set_focus
#define ex_fuoco_via             ex_clear_focus
#define ex_fuoco_chi             ex_get_focus
#define ex_spegni_scrivania      ex_shutdown_desktop
#define ex_finestre_segui        ex_track_windows
#define ex_finestre_elenco       ex_list_windows
#define ex_finestra_attiva       ex_activate_window_id
#define ex_attiva                ex_activate
#define ex_menu_comparsa         ex_popup_menu
#define ex_apri_file             ex_open_file
#define ex_lista_multipla        ex_list_set_multiselect
#define ex_lista_scelte          ex_list_get_selection
#define ex_lista_riga_a          ex_list_row_at
#define ex_finestra_riduci       ex_minimize_window_id
#define ex_mouse_passaggio       ex_track_mouse_hover
#define ex_ridisegna             ex_redraw
#define ex_menu_spunta           ex_menu_check
#define ex_ritaglio              ex_set_clip
#define ex_pixmap_fuso           ex_pixmap_blend
#define ex_chiudi_le_altre       ex_close_others
#define ex_appunti_metti         ex_clipboard_set
#define ex_appunti_prendi        ex_clipboard_get
#define ex_testo_metti           ex_set_text
#define ex_testo_prendi          ex_get_text
#define ex_prendi_msg            ex_get_message
#define ex_msg_ora               ex_peek_message
#define ex_smista                ex_dispatch
#define ex_esci                  ex_quit
#define ex_procedura_base        ex_default_proc
#define ex_riempi                ex_fill_rect
#define ex_riquadro_disegna      ex_draw_rect
#define ex_rilievo               ex_draw_raised
#define ex_incavo                ex_draw_sunken
#define ex_scrivi                ex_draw_text
#define ex_font_apri             ex_font_open
#define ex_font_trova            ex_font_find
#define ex_scrivi_in             ex_draw_text_buffer
#define ex_font_nome             ex_font_name
#define ex_font_chiudi           ex_font_close
#define ex_font_altezza          ex_font_height
#define ex_font_base             ex_font_baseline
#define ex_larghezza_testo       ex_text_width
#define ex_scrivi_con            ex_draw_text_font
#define ex_sveglia               ex_set_timer
#define ex_guarda_fd             ex_watch_fd
#define ex_tab_contenuto         ex_tab_content
#define ex_aggiorna              ex_update
#define ex_immagine              ex_draw_image
#define ex_immagine_disponi      ex_draw_image_mode
#define ex_icona_apri            ex_icon_open
#define ex_icona_lato            ex_icon_size
#define ex_icona_disegna         ex_icon_draw
#define ex_icona_metti           ex_set_icon
#define ex_icona_chiudi          ex_icon_close
#define ex_mdi_attivo            ex_mdi_get_active
#define ex_mdi_attiva            ex_mdi_activate
#define ex_acceso                ex_is_checked
#define ex_accendi               ex_set_checked
#define ex_abilita               ex_enable
#define ex_scorri_limiti         ex_scroll_set_range
#define ex_scorri_dove           ex_scroll_get_pos
#define ex_scorri_vai            ex_scroll_set_pos
#define ex_voci_svuota           ex_items_clear
#define ex_voce_aggiungi         ex_item_add
#define ex_voci_quante           ex_items_count
#define ex_voce_scelta           ex_item_get_selected
#define ex_voce_scegli           ex_item_select
#define ex_voce_testo            ex_item_text
#define ex_voci_schede           ex_items_as_tabs
#define ex_voce_togli            ex_item_remove
#define ex_voce_rinomina         ex_item_rename
#define ex_area_colora           ex_textarea_set_highlighter
#define ex_colora_c              ex_highlight_c
#define ex_lista_svuota          ex_list_clear
#define ex_lista_aggiungi        ex_list_add
#define ex_lista_icona           ex_list_set_icon
#define ex_lista_margine         ex_list_margin
#define ex_lista_quante          ex_list_count
#define ex_lista_scelta          ex_list_get_selected
#define ex_lista_scegli          ex_list_select
#define ex_lista_testo           ex_list_text
#define ex_area_svuota           ex_textarea_clear
#define ex_area_aggiungi         ex_textarea_add_line
#define ex_area_righe            ex_textarea_line_count
#define ex_area_riga             ex_textarea_line
#define ex_area_modificato       ex_textarea_is_modified
#define ex_area_pulita           ex_textarea_set_unmodified
#define ex_area_cursore          ex_textarea_get_cursor
#define ex_area_vai              ex_textarea_set_cursor
#define ex_area_vista            ex_textarea_get_view
#define ex_area_mostra_da        ex_textarea_scroll_to
#define ex_area_riga_metti       ex_textarea_set_line
#define ex_area_seleziona        ex_textarea_select
#define ex_area_seleziona_tutto  ex_textarea_select_all
#define ex_area_copia            ex_textarea_copy
#define ex_area_taglia           ex_textarea_cut
#define ex_area_incolla          ex_textarea_paste
#define ex_area_cancella         ex_textarea_delete
#define ex_menu                  ex_menu_bar
#define ex_menu_voce             ex_menu_add_item
#define ex_versione              ex_version
#define ex_schermo               ex_screen_size
#define EX_NON_RIDISEGNARE       EX_NO_REDRAW
#define EXM_CREA                 EXM_CREATE
#define EXM_DISEGNA              EXM_PAINT
#define EXM_COMANDO              EXM_COMMAND
#define EXM_CHIUDI               EXM_CLOSE
#define EXM_MOUSE_GIU            EXM_MOUSE_DOWN
#define EXM_MOUSE_SU             EXM_MOUSE_UP
#define EXM_TASTO                EXM_KEY
#define EXM_DISTRUGGI            EXM_DESTROY
#define EXM_TERMFINITO           EXM_TERM_EXITED
#define EXM_MISURA               EXM_SIZE
#define EXM_MOUSE_MOSSO          EXM_MOUSE_MOVE
#define EXM_DOPPIOCLIC           EXM_DOUBLE_CLICK
#define EXM_TEMPO                EXM_TIMER
#define EXM_FINESTRE             EXM_WINDOW_LIST
#define EXM_SISTEMA              EXM_SYSTEM
#define EXM_ROTELLA              EXM_WHEEL
#define EXM_MOUSE_DESTRO         EXM_RIGHT_CLICK
#define EXM_LASCIATO             EXM_DROP
#define EXM_SCHEDA_CHIUDI        EXM_TAB_CLOSE
#define EXM_MOUSE_SOPRA          EXM_MOUSE_ENTER
#define EXM_MOUSE_FUORI          EXM_MOUSE_LEAVE
#define EX_DA                    EX_FROM
#define EX_A                     EX_TO
#define EX_ROTELLA_RIGHE         EX_WHEEL_LINES
#define EX_APRIRE                EX_IS_OPEN
#define EX_COL                   EX_COLUMN
#define EX_TITOLO                EX_CAPTION
#define EX_BORDO                 EX_BORDER
#define EX_CHIUDI                EX_CLOSEBOX
#define EX_VISIBILE              EX_VISIBLE
#define EX_SFONDO                EX_BACKGROUND
#define EX_SOPRA                 EX_TOPMOST
#define EX_MODALE                EX_MODAL
#define EX_RIDIM                 EX_RESIZABLE
#define EX_FIGLIO                EX_CHILD
#define EX_SPENTO                EX_DISABLED
#define EX_COMPARSA              EX_POPUP
#define EX_NERO                  EX_BLACK
#define EX_BIANCO                EX_WHITE
#define EX_GRIGIO                EX_GRAY
#define EX_GRIGIO_SC             EX_DARK_GRAY
#define EX_BLU                   EX_BLUE
#define EX_ROSSO                 EX_RED
#define EX_SCRIVANIA             EX_DESKTOP_COLOR
#define EX_LUCE                  EX_HIGHLIGHT
#define EX_OMBRA                 EX_SHADOW
#define EX_VF_RIDOTTA            EX_WE_MINIMIZED
#define EX_VF_FUOCO              EX_WE_FOCUSED
#define EX_FONT_SISTEMA          EX_FONT_SYSTEM
#define EX_FAM_SERIF             EX_FAMILY_SERIF
#define EX_FAM_SANS              EX_FAMILY_SANS
#define EX_FAM_MONO              EX_FAMILY_MONO
#define EX_IMM_ANGOLO            EX_IMAGE_TOPLEFT
#define EX_IMM_CENTRO            EX_IMAGE_CENTER
#define EX_IMM_ALLARGA           EX_IMAGE_STRETCH
#define EX_IMM_RIPETI            EX_IMAGE_TILE
#define EX_COD_NORMALE           EX_CODE_NORMAL
#define EX_COD_CHIAVE            EX_CODE_KEYWORD
#define EX_COD_TIPO              EX_CODE_TYPE
#define EX_COD_STRINGA           EX_CODE_STRING
#define EX_COD_NUMERO            EX_CODE_NUMBER
#define EX_COD_COMMENTO          EX_CODE_COMMENT
#define EX_COD_PREPROC           EX_CODE_PREPROC
#define EX_COD_FUNZIONE          EX_CODE_FUNCTION
#define EX_COD_SIMBOLO           EX_CODE_SYMBOL
#endif

#endif '' EXWIN_BI
