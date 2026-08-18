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
''     function procedura cdecl (byval f as ExFinestra, byval msg as ulong, _
''                               byval wp as ulong, byval lp as long) as long
''         select case msg
''         case EXM_COMANDO : ex_titolo(f, "premuto")
''         case EXM_CHIUDI  : ex_esci(0)
''         case else        : return ex_procedura_base(f, msg, wp, lp)
''         end select
''         return 0
''     end function
''
''     dim as ExFinestra f = ex_crea("finestra", "Prova", EX_TITOLO or EX_BORDO, _
''                                   100, 100, 320, 200, 0, 0, @procedura)
''     ex_crea("pulsante", "OK", EX_FIGLIO, 20, 140, 80, 24, f, 1, 0)
''
''     dim as ExMsg m
''     while ex_prendi_msg(@m) <> 0
''         ex_smista(@m)
''     wend
'' =============================================================================

#ifndef EXWIN_BI
#define EXWIN_BI

'' La maniglia: un intero opaco, come in C. 0 = nessuna finestra.
type ExFinestra as ulong

type ExMsg
    finestra as ExFinestra
    msg      as ulong
    wp       as ulong
    lp       as long
end type

type ExProcedura as function cdecl (byval as ExFinestra, byval as ulong, _
                                    byval as ulong, byval as long) as long

'' --- I messaggi --------------------------------------------------------------
const EXM_CREA      = &h0001
const EXM_DISEGNA   = &h0002
const EXM_COMANDO   = &h0003
const EXM_CHIUDI    = &h0004
const EXM_MOUSE_GIU = &h0005
const EXM_MOUSE_SU  = &h0006
const EXM_TASTO     = &h0007
const EXM_DISTRUGGI = &h0008
const EXM_TERMFINITO= &h0009
'' La finestra ha cambiato misura: EX_X(lp) e EX_Y(lp) sono quella nuova.
const EXM_MISURA    = &h000A

'' --- Gli stili ---------------------------------------------------------------
const EX_TITOLO   = &h0001
const EX_BORDO    = &h0002
const EX_CHIUDI   = &h0004
const EX_VISIBILE = &h0008
const EX_SFONDO   = &h0010
const EX_SOPRA    = &h0020
const EX_MODALE   = &h0040
'' Con questo la finestra si puo' tirare per l'angolo. Chi lo mette DEVE
'' gestire EXM_MISURA: vedi exwin.h.
const EX_RIDIM    = &h0080
'' x e y a EX_AUTO: la posizione la sceglie il server, a cascata. Serve a
'' poter aprire due volte lo stesso programma senza sovrapporlo a se stesso.
const EX_AUTO     = -1
const EX_FIGLIO   = &h0100

'' --- I colori, in ARGB -------------------------------------------------------
const EX_NERO      = &h00000000
const EX_BIANCO    = &h00FFFFFF
const EX_GRIGIO    = &h00C0C0C0
const EX_GRIGIO_SC = &h00808080
const EX_BLU       = &h00305A8A
const EX_ROSSO     = &h00C04040
'' La luce viene da sopra a sinistra, sempre: vedi ex_rilievo/ex_incavo.
const EX_LUCE      = &h00FFFFFF
const EX_OMBRA     = &h00000000

'' Le coordinate impacchettate in lp
#define EX_X(lp) (cint((lp) and &hFFFF))
#define EX_Y(lp) (cint(((lp) shr 16) and &hFFFF))

'' --- Creare ------------------------------------------------------------------
''
'' Per una finestra di primo livello: padre = 0, id = 0, proc = la procedura.
'' Per un controllo: padre = la finestra, id = il numero che tornera' in
'' EXM_COMANDO, proc = 0.
declare function ex_crea cdecl alias "ex_crea" ( _
    byval classe as const zstring ptr, _
    byval titolo as const zstring ptr, _
    byval stile  as ulong, _
    byval x as long, byval y as long, _
    byval w as long, byval h as long, _
    byval padre as ExFinestra, _
    byval id    as ulong, _
    byval proc  as ExProcedura) as ExFinestra

declare sub ex_distruggi cdecl alias "ex_distruggi" (byval f as ExFinestra)
declare sub ex_titolo     cdecl alias "ex_titolo" (byval f as ExFinestra, byval s as const zstring ptr)
declare sub ex_sposta     cdecl alias "ex_sposta" (byval f as ExFinestra, byval x as long, byval y as long)
declare sub ex_misura     cdecl alias "ex_misura" (byval f as ExFinestra, byval w as long, byval h as long)
declare sub ex_mostra     cdecl alias "ex_mostra" (byval f as ExFinestra, byval visibile as long)

'' --- Il rilievo: sporge cio' che si preme, rientra cio' in cui si scrive ----
declare sub ex_rilievo cdecl alias "ex_rilievo" (byval f as ExFinestra, byval x as long, byval y as long, byval w as long, byval h as long)
declare sub ex_incavo  cdecl alias "ex_incavo"  (byval f as ExFinestra, byval x as long, byval y as long, byval w as long, byval h as long)

'' --- I menu a tendina -------------------------------------------------------
'' La scelta arriva come EXM_COMANDO con l'id della voce, come un pulsante.
'' voce = "-" e' un solco; un tab nel testo allinea a destra la scorciatoia.
declare function ex_menu      cdecl alias "ex_menu" (byval finestra as ExFinestra) as ExFinestra
declare function ex_menu_voce cdecl alias "ex_menu_voce" ( _
    byval menu as ExFinestra, _
    byval titolo as const zstring ptr, _
    byval voce as const zstring ptr, _
    byval id as ulong) as long

declare sub      ex_testo_metti  cdecl alias "ex_testo_metti"  (byval f as ExFinestra, byval s as const zstring ptr)
declare function ex_testo_prendi cdecl alias "ex_testo_prendi" (byval f as ExFinestra) as const zstring ptr

'' --- Il ciclo dei messaggi ---------------------------------------------------
declare function ex_prendi_msg cdecl alias "ex_prendi_msg" (byval m as ExMsg ptr) as long
declare sub      ex_smista     cdecl alias "ex_smista"     (byval m as const ExMsg ptr)
declare sub      ex_esci       cdecl alias "ex_esci"       (byval codice as long)

declare function ex_procedura_base cdecl alias "ex_procedura_base" ( _
    byval f as ExFinestra, byval msg as ulong, _
    byval wp as ulong, byval lp as long) as long

'' --- Disegnare ---------------------------------------------------------------
declare sub ex_riempi           cdecl alias "ex_riempi" (byval f as ExFinestra, byval x as long, byval y as long, byval w as long, byval h as long, byval c as ulong)
declare sub ex_riquadro_disegna cdecl alias "ex_riquadro_disegna" (byval f as ExFinestra, byval x as long, byval y as long, byval w as long, byval h as long, byval c as ulong)
declare sub ex_scrivi           cdecl alias "ex_scrivi" (byval f as ExFinestra, byval x as long, byval y as long, byval s as const zstring ptr, byval c as ulong)
declare sub ex_aggiorna         cdecl alias "ex_aggiorna" (byval f as ExFinestra)

'' --- Le immagini -------------------------------------------------------------
'' Rende 1 se l'ha disegnata, 0 se il formato non e' (ancora) riconosciuto.
'' Il formato si riconosce dai primi byte del file, non dall'estensione.
declare function ex_immagine cdecl alias "ex_immagine" ( _
    byval f as ExFinestra, byval percorso as const zstring ptr, _
    byval x as long, byval y as long) as long

declare sub ex_schermo cdecl alias "ex_schermo" (byval larghezza as ulong ptr, byval altezza as ulong ptr)

#endif '' EXWIN_BI
