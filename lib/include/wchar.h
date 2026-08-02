/* =============================================================================
 * lib/include/wchar.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * <wchar.h> — i TIPI dei caratteri larghi, e nient'altro.
 *
 * ⚠️ QUI NON C'E' NESSUNA FUNZIONE, ed e' voluto. EX-OS lavora a byte:
 * la console e' una VGA a 80x25 con una code page a 8 bit, i nomi dei file
 * sono byte su FAT e su ext2, e l'unica locale che esiste e' "C" (vedi
 * setlocale in lib/libc.c). wcslen, mbstowcs, fwprintf e le altre non
 * esistono, e dichiararle senza scriverle sposterebbe l'errore dalla riga
 * che le chiama a un "undefined reference" a valle di tutto il lavoro.
 *
 * L'header esiste perche' del codice di terzi lo INCLUDE senza usarlo:
 * gas/read.c di binutils fa `#include "wchar.h"` e non chiama una sola
 * funzione di caratteri larghi. Un file che non c'e' ferma la
 * compilazione; un file che c'e' e non promette niente no.
 *
 * Il giorno che servisse davvero Unicode, il posto e' questo — e la parte
 * difficile non sono le funzioni, e' decidere in che codifica stanno i
 * nomi dei file gia' scritti sui volumi.
 * ============================================================================= */

#ifndef EXOS_WCHAR_H
#define EXOS_WCHAR_H

#include "libc.h"

/* wchar_t, wint_t, WEOF, mbstate_t, mbstowcs, mbrtowc e wcstombs stanno
 * tutti in libc.h, come ogni altra cosa: la fonte e' una sola. Qui c'e'
 * solo il nome dell'header, che e' cio' che il codice di terzi include. */

#endif /* EXOS_WCHAR_H */
