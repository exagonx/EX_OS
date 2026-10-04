/* =============================================================================
 * lib/include/wctype.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <wctype.h> — classificare un carattere largo.
 *
 * ! NELLA LOCALE "C" SOPRA IL 127 E' SEMPRE NO. Non e' una scorciatoia:
 * e' proprio cio' che dice lo standard per la locale "C", che e' l'unica
 * che EX-OS ha (vedi setlocale in lib/libc.c). Un `iswalpha(L'à')` che
 * rispondesse si' racconterebbe di un supporto Unicode che non c'e'.
 *
 * Le implementazioni delegano alle sorelle strette di <ctype.h>, cosi' la
 * definizione di «lettera» in tutta la libc e' UNA SOLA.
 *
 * ! wctype_t E LE MACRO PER NOME NON CI SONO. `wctype("alpha")` e
 * `iswctype()` servono a chi sceglie la classe a tempo di esecuzione
 * leggendola da una locale — cosa che qui non puo' succedere, perche' di
 * locale ce n'e' una sola e si conosce a priori. Dichiararle senza
 * scriverle sposterebbe l'errore dalla riga che le chiama a un
 * "undefined reference" a valle di tutto il lavoro.
 * ============================================================================= */

#ifndef EXOS_WCTYPE_H
#define EXOS_WCTYPE_H

#include "wchar.h"

/* ! extern "C": senza, dal C++ i nomi escono decorati e non si collegano
 * (la libstdc++ con wchar_t chiamava «wctype(char const*)»). */
#ifdef __cplusplus
extern "C" {
#endif

int    iswalnum(wint_t c);
int    iswalpha(wint_t c);
int    iswblank(wint_t c);
int    iswcntrl(wint_t c);
int    iswdigit(wint_t c);
int    iswgraph(wint_t c);
int    iswlower(wint_t c);
int    iswprint(wint_t c);
int    iswpunct(wint_t c);
int    iswspace(wint_t c);
int    iswupper(wint_t c);
int    iswxdigit(wint_t c);

wint_t towlower(wint_t c);
wint_t towupper(wint_t c);

/* Le classi e le trasformazioni per nome («alpha», «tolower»), come le vuole
 * <cwctype> della libstdc++ quando ha wchar_t. Solo in libc.a. Un valore e'
 * l'indice della classe, 0 se il nome non e' noto. */
typedef unsigned int wctype_t;
typedef unsigned int wctrans_t;
wctype_t  wctype(const char *nome);
int       iswctype(wint_t c, wctype_t classe);
wctrans_t wctrans(const char *nome);
wint_t    towctrans(wint_t c, wctrans_t t);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_WCTYPE_H */
