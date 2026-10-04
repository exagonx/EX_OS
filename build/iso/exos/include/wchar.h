/* =============================================================================
 * lib/include/wchar.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * <wchar.h> — stringhe di caratteri larghi.
 *
 * ! FINO AD AGOSTO 2026 QUESTO FILE NON PROMETTEVA NIENTE, e la nota che
 * c'era sopra spiegava perche': EX-OS lavora a byte, la console e' una VGA
 * a 80x25 con una code page a 8 bit, i nomi dei file sono byte su FAT e su
 * ext2, e l'unica locale che esiste e' "C" (vedi setlocale in lib/libc.c).
 * L'header c'era solo perche' del codice di terzi lo INCLUDE senza usarlo —
 * gas/read.c di binutils fa cosi'.
 *
 * Le funzioni le ha fatte entrare la RUNTIME DI FREEBASIC: WSTRING e' un
 * tipo di dato del linguaggio, non un vezzo, e senza queste meta' di
 * src/rtlib non compila.
 *
 * ! LA CODIFICA E' LATIN-1: un wchar_t E' un byte esteso a 32 bit. Non e'
 * UTF-32 e non fa finta di esserlo — sopra 255 la conversione verso i byte
 * FALLISCE invece di troncare, perche' un troncamento silenzioso e' un
 * carattere sbagliato che sembra giusto. Il ragionamento per esteso sta in
 * lib/libc.c, sopra wcslen.
 *
 * Il giorno che servisse Unicode vero, la parte difficile non sono queste
 * funzioni: e' decidere in che codifica stanno i nomi di file gia' scritti
 * sui volumi.
 * ============================================================================= */

#ifndef EXOS_WCHAR_H
#define EXOS_WCHAR_H

#include "libc.h"
#include <stdarg.h>

/* I limiti di wchar_t, che lo standard vuole anche qui (oltre che in
 * <stdint.h>). */
#ifndef WCHAR_MIN
#define WCHAR_MIN __WCHAR_MIN__
#define WCHAR_MAX __WCHAR_MAX__
#endif

#ifdef __cplusplus
extern "C" {
#endif
/* ! extern "C" (30 settembre 2026): senza, un programma C++ cercava queste
 * funzioni coi nomi decorati del C++ e il collegamento non le trovava. */

/* Le conversioni «ricominciabili» fra byte e caratteri larghi (@EXILLA-JS, 30
 * settembre 2026). Con la codifica Latin-1 di qui (vedi sopra) uno stato non
 * serve: si legge un byte e si fa un carattere, e *src avanza come vuole C99. */
size_t    mbsrtowcs(wchar_t *dst, const char **src, size_t n, mbstate_t *stato);
size_t    wcsrtombs(char *dst, const wchar_t **src, size_t n, mbstate_t *stato);
size_t    wcrtomb(char *dst, wchar_t c, mbstate_t *stato);

/* wchar_t, wint_t, WEOF, mbstate_t, mbstowcs, mbrtowc e wcstombs stanno
 * in libc.h, come ogni altra cosa: la fonte e' una sola. */

size_t    wcslen(const wchar_t *s);
wchar_t  *wcschr(const wchar_t *s, wchar_t c);
wchar_t  *wcsrchr(const wchar_t *s, wchar_t c);
int       wcscmp(const wchar_t *a, const wchar_t *b);
int       wcsncmp(const wchar_t *a, const wchar_t *b, size_t n);
wchar_t  *wcscpy(wchar_t *dst, const wchar_t *src);
wchar_t  *wcsncpy(wchar_t *dst, const wchar_t *src, size_t n);
wchar_t  *wcscat(wchar_t *dst, const wchar_t *src);
wchar_t  *wcsncat(wchar_t *dst, const wchar_t *src, size_t n);
wchar_t  *wcsstr(const wchar_t *ago, const wchar_t *pagliaio);
size_t    wcsspn(const wchar_t *s, const wchar_t *ammessi);
size_t    wcscspn(const wchar_t *s, const wchar_t *rifiutati);
wchar_t  *wcspbrk(const wchar_t *s, const wchar_t *cercati);

wchar_t  *wmemchr(const wchar_t *s, wchar_t c, size_t n);
int       wmemcmp(const wchar_t *a, const wchar_t *b, size_t n);
wchar_t  *wmemcpy(wchar_t *dst, const wchar_t *src, size_t n);
wchar_t  *wmemmove(wchar_t *dst, const wchar_t *src, size_t n);
wchar_t  *wmemset(wchar_t *dst, wchar_t c, size_t n);

long                wcstol(const wchar_t *s, wchar_t **fine, int base);
unsigned long       wcstoul(const wchar_t *s, wchar_t **fine, int base);
long long           wcstoll(const wchar_t *s, wchar_t **fine, int base);
unsigned long long  wcstoull(const wchar_t *s, wchar_t **fine, int base);
double              wcstod(const wchar_t *s, wchar_t **fine);

/* ! %ls e %lc NON sono supportate: tornano -1 con EILSEQ invece di
 * stampare qualcosa di sbagliato. Il perche' sta in lib/libc.c, sopra
 * vswprintf. E ! swprintf NON e' snprintf: quando non ci sta torna -1,
 * non la lunghezza che sarebbe servita — chi confonde i due contratti
 * scrive un ciclo di riallocazione che non termina mai. */
int  swprintf(wchar_t *buf, size_t dim, const wchar_t *fmt, ...);
/* Solo in libc.a. Latin-1: un carattere che non ci sta esce come '?'.
 * ! TUTTI QUESTI NOMI SERVONO INSIEME: la libstdc++ accende wchar_t (e con lui
 * std::wstring, std::wostream) solo se li trova tutti. Vedi lib/libc.c. */
wint_t fputwc(wchar_t c, FILE *f);
int    fputws(const wchar_t *s, FILE *f);
wint_t btowc(int c);
int    wctob(wint_t c);
int    mbsinit(const mbstate_t *stato);
size_t mbrlen(const char *s, size_t n, mbstate_t *stato);
wint_t fgetwc(FILE *f);
wint_t getwc(FILE *f);
wint_t getwchar(void);
wint_t ungetwc(wint_t c, FILE *f);
wint_t putwc(wchar_t c, FILE *f);
wint_t putwchar(wchar_t c);
wchar_t *fgetws(wchar_t *s, int n, FILE *f);
int    fwide(FILE *f, int modo);
int    fwprintf(FILE *f, const wchar_t *fmt, ...);
int    wprintf(const wchar_t *fmt, ...);
int    vfwprintf(FILE *f, const wchar_t *fmt, va_list ap);
int    vwprintf(const wchar_t *fmt, va_list ap);
int    swscanf(const wchar_t *s, const wchar_t *fmt, ...);
int    fwscanf(FILE *f, const wchar_t *fmt, ...);
int    wscanf(const wchar_t *fmt, ...);
int    vswscanf(const wchar_t *s, const wchar_t *fmt, va_list ap);
int    vfwscanf(FILE *f, const wchar_t *fmt, va_list ap);
int    vwscanf(const wchar_t *fmt, va_list ap);
int    wcscoll(const wchar_t *a, const wchar_t *b);
size_t wcsxfrm(wchar_t *dst, const wchar_t *src, size_t n);
wchar_t *wcstok(wchar_t *s, const wchar_t *sep, wchar_t **resto);
struct tm;
size_t wcsftime(wchar_t *dst, size_t max, const wchar_t *fmt, const struct tm *tm);
float       wcstof(const wchar_t *s, wchar_t **fine);
long double wcstold(const wchar_t *s, wchar_t **fine);
int  vswprintf(wchar_t *buf, size_t dim, const wchar_t *fmt, va_list ap);

#ifdef __cplusplus
}
#endif

/* ! PER IL C++: la libstdc++ di EX-OS e' costruita senza _GLIBCXX_USE_WCHAR_T,
 * e allora <cwchar> include questo file ma non porta le funzioni in std::. Il
 * codice di terzi scrive std::wcslen (SpiderMonkey, vm/CharacterEncoding.cpp):
 * le si porta qui, una per una (@EXILLA-JS, 30 settembre 2026). */
#ifdef __cplusplus
namespace std {
using ::mbstate_t;
using ::wcslen;
using ::wcscmp;
using ::wcsncmp;
using ::wcscpy;
using ::wcsncpy;
using ::wcschr;
using ::wmemcpy;
using ::wmemset;
using ::mbrtowc;
using ::wcrtomb;
using ::mbsrtowcs;
using ::wcsrtombs;
using ::fputwc;
using ::fputws;
}
#endif

#endif /* EXOS_WCHAR_H */
