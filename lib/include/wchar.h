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
int  vswprintf(wchar_t *buf, size_t dim, const wchar_t *fmt, va_list ap);

#endif /* EXOS_WCHAR_H */
