'' =============================================================================
'' crt/exos/wchar.bi — i tipi larghi di EX-OS, per FreeBASIC
''
'' Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
'' SPDX-License-Identifier: GPL-2.0-or-later
'' =============================================================================
'' Serve a crt/wchar.bi, che subito dopo l'inclusione di piattaforma usa
'' wint_t e mbstate_t: senza questo file quei due nomi non esisterebbero e
'' l'errore parlerebbe di btowc, non della loro assenza.
'' =============================================================================

#ifndef __crt_exos_wchar_bi__
#define __crt_exos_wchar_bi__

#include once "crt/stddef.bi"

'' typedef int wint_t;
type wint_t as long

#define WEOF (-1)
#define MB_CUR_MAX 1

'' Lo stato di una conversione multibyte. E' una struttura perche' lo
'' standard vuole un tipo completo da dichiarare e azzerare, ma non c'e'
'' niente da ricordare: la codifica di EX-OS e' Latin-1, un byte per
'' carattere, e non c'e' nessuna conversione a stati.
type mbstate_t
	__nulla as long
end type

#endif '' __crt_exos_wchar_bi__
