/* =============================================================================
 * lib/include/stdint.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <stdint.h> — interi di larghezza dichiarata.
 *
 * PERCHE' QUESTO NON E' UNA FACCIATA COME GLI ALTRI. <string.h>, <stdio.h>
 * e compagni rimandano a libc.h perche' le funzioni le dichiara quello.
 * Qui non ci sono funzioni: ci sono TIPI, e i tipi di <stdint.h> non li sa
 * la libreria — li sa il compilatore, che per ogni bersaglio conosce la
 * larghezza di `int`, `long` e `long long`. Perciò ogni riga di questo
 * file chiede a lui, con le macro che predefinisce (__INT32_TYPE__,
 * __UINT32_MAX__, __INT32_C()...), invece di scrivere `unsigned int` e
 * sperare.
 *
 * E' la stessa scelta fatta in libc.h per size_t, per la stessa ragione:
 * un tipo scritto a mano vale finché non cambia compilatore. Sul gcc di
 * sistema con -m32 int32_t e' `int`; sul bersaglio i386-exos e' `long
 * int`. Stessa larghezza, stesso codice generato, ma un typedef scritto a
 * mano ne avrebbe contraddetto uno dei due — e il conflitto si vede solo
 * quando il primo sorgente di terzi include davvero questo header.
 *
 * PERCHE' ESISTE, dato che GCC un <stdint.h> ce l'ha. Il bersaglio
 * i386-exos non se lo installa: gcc/config.gcc non imposta
 * `use_gcc_stdint` per il nostro caso, quindi `#include <stdint.h>` sul
 * cross NON trova niente. Questo file colma il buco senza ricompilare
 * GCC: prepara-cross.sh copia tutti gli header di lib/include nell'albero
 * del bersaglio, quindi basta che il file esista.
 *
 * ⚠️ SUL BERSAGLIO int32_t E' `long int`, NON `int`. E' corretto (32 bit
 * in entrambi i casi) ma inconsueto: i386-linux dichiara INT32_TYPE "int",
 * il nostro gcc/config/i386/exos.h non dichiara niente e GCC ripiega su
 * "long int". Si vede in due punti: gli avvisi del compilatore parlano di
 * `long` dove ci si aspetta `int`, e %d di printf riceve un `long`
 * (innocuo qui — cdecl passa 32 bit in entrambi i casi). Il rimedio vero
 * e' in exos.h, e costa un rebuild di GCC: vedi tools/gcc-exos/leggimi.md.
 * ============================================================================= */

#ifndef EXOS_STDINT_H
#define EXOS_STDINT_H

/* size_t, intptr_t e uintptr_t vengono da li': una fonte sola, come per
 * tutti gli altri header standard di EX-OS. */
#include "libc.h"

/* --- Larghezza esatta -------------------------------------------------------- */
typedef __INT8_TYPE__           int8_t;
typedef __INT16_TYPE__          int16_t;
typedef __INT32_TYPE__          int32_t;
typedef __INT64_TYPE__          int64_t;
typedef __UINT8_TYPE__          uint8_t;
typedef __UINT16_TYPE__         uint16_t;
typedef __UINT32_TYPE__         uint32_t;
typedef __UINT64_TYPE__         uint64_t;

/* --- Almeno quella larghezza (int_leastN_t) ---------------------------------- */
typedef __INT_LEAST8_TYPE__     int_least8_t;
typedef __INT_LEAST16_TYPE__    int_least16_t;
typedef __INT_LEAST32_TYPE__    int_least32_t;
typedef __INT_LEAST64_TYPE__    int_least64_t;
typedef __UINT_LEAST8_TYPE__    uint_least8_t;
typedef __UINT_LEAST16_TYPE__   uint_least16_t;
typedef __UINT_LEAST32_TYPE__   uint_least32_t;
typedef __UINT_LEAST64_TYPE__   uint_least64_t;

/* --- Il piu' veloce con almeno quella larghezza (int_fastN_t) ----------------
 * Su i386 sono quasi tutti `int`: un accesso a 32 bit costa meno di uno a
 * 8 con l'estensione di segno. Anche questo lo decide il compilatore. */
typedef __INT_FAST8_TYPE__      int_fast8_t;
typedef __INT_FAST16_TYPE__     int_fast16_t;
typedef __INT_FAST32_TYPE__     int_fast32_t;
typedef __INT_FAST64_TYPE__     int_fast64_t;
typedef __UINT_FAST8_TYPE__     uint_fast8_t;
typedef __UINT_FAST16_TYPE__    uint_fast16_t;
typedef __UINT_FAST32_TYPE__    uint_fast32_t;
typedef __UINT_FAST64_TYPE__    uint_fast64_t;

/* --- Il piu' largo disponibile ----------------------------------------------- */
typedef __INTMAX_TYPE__         intmax_t;
typedef __UINTMAX_TYPE__        uintmax_t;

/* --- Limiti ------------------------------------------------------------------
 * Il minimo si scrive `-MAX - 1` e non con la costante negativa: -2147483648
 * non e' una costante intera, e' il meno unario applicato a 2147483648, che
 * in un int non ci sta. E' il modo in cui lo scrivono tutte le libc. */
#define INT8_MIN            (-__INT8_MAX__ - 1)
#define INT16_MIN           (-__INT16_MAX__ - 1)
#define INT32_MIN           (-__INT32_MAX__ - 1)
#define INT64_MIN           (-__INT64_MAX__ - 1)
#define INT8_MAX            __INT8_MAX__
#define INT16_MAX           __INT16_MAX__
#define INT32_MAX           __INT32_MAX__
#define INT64_MAX           __INT64_MAX__
#define UINT8_MAX           __UINT8_MAX__
#define UINT16_MAX          __UINT16_MAX__
#define UINT32_MAX          __UINT32_MAX__
#define UINT64_MAX          __UINT64_MAX__

#define INT_LEAST8_MIN      (-__INT_LEAST8_MAX__ - 1)
#define INT_LEAST16_MIN     (-__INT_LEAST16_MAX__ - 1)
#define INT_LEAST32_MIN     (-__INT_LEAST32_MAX__ - 1)
#define INT_LEAST64_MIN     (-__INT_LEAST64_MAX__ - 1)
#define INT_LEAST8_MAX      __INT_LEAST8_MAX__
#define INT_LEAST16_MAX     __INT_LEAST16_MAX__
#define INT_LEAST32_MAX     __INT_LEAST32_MAX__
#define INT_LEAST64_MAX     __INT_LEAST64_MAX__
#define UINT_LEAST8_MAX     __UINT_LEAST8_MAX__
#define UINT_LEAST16_MAX    __UINT_LEAST16_MAX__
#define UINT_LEAST32_MAX    __UINT_LEAST32_MAX__
#define UINT_LEAST64_MAX    __UINT_LEAST64_MAX__

#define INT_FAST8_MIN       (-__INT_FAST8_MAX__ - 1)
#define INT_FAST16_MIN      (-__INT_FAST16_MAX__ - 1)
#define INT_FAST32_MIN      (-__INT_FAST32_MAX__ - 1)
#define INT_FAST64_MIN      (-__INT_FAST64_MAX__ - 1)
#define INT_FAST8_MAX       __INT_FAST8_MAX__
#define INT_FAST16_MAX      __INT_FAST16_MAX__
#define INT_FAST32_MAX      __INT_FAST32_MAX__
#define INT_FAST64_MAX      __INT_FAST64_MAX__
#define UINT_FAST8_MAX      __UINT_FAST8_MAX__
#define UINT_FAST16_MAX     __UINT_FAST16_MAX__
#define UINT_FAST32_MAX     __UINT_FAST32_MAX__
#define UINT_FAST64_MAX     __UINT_FAST64_MAX__

#define INTPTR_MIN          (-__INTPTR_MAX__ - 1)
#define INTPTR_MAX          __INTPTR_MAX__
#define UINTPTR_MAX         __UINTPTR_MAX__
#define INTMAX_MIN          (-__INTMAX_MAX__ - 1)
#define INTMAX_MAX          __INTMAX_MAX__
#define UINTMAX_MAX         __UINTMAX_MAX__

/* Limiti dei tipi che stanno altrove ma che <stdint.h> deve dichiarare:
 * lo standard li mette qui, non in <stddef.h>. */
#define PTRDIFF_MIN         (-__PTRDIFF_MAX__ - 1)
#define PTRDIFF_MAX         __PTRDIFF_MAX__
#define SIZE_MAX            __SIZE_MAX__
#define WCHAR_MIN           __WCHAR_MIN__
#define WCHAR_MAX           __WCHAR_MAX__
#define WINT_MIN            __WINT_MIN__
#define WINT_MAX            __WINT_MAX__
#define SIG_ATOMIC_MIN      __SIG_ATOMIC_MIN__
#define SIG_ATOMIC_MAX      __SIG_ATOMIC_MAX__

/* --- Costanti del tipo giusto ------------------------------------------------
 * INT64_C(1) deve produrre 1LL e non 1: senza il suffisso la costante e'
 * un int, e uno spostamento come (1 << 40) darebbe zero invece di quello
 * che il lettore si aspetta. Il suffisso giusto per il bersaglio lo sa il
 * compilatore, ed e' l'ennesima ragione per non scrivere niente a mano. */
#define INT8_C(c)           __INT8_C(c)
#define INT16_C(c)          __INT16_C(c)
#define INT32_C(c)          __INT32_C(c)
#define INT64_C(c)          __INT64_C(c)
#define UINT8_C(c)          __UINT8_C(c)
#define UINT16_C(c)         __UINT16_C(c)
#define UINT32_C(c)         __UINT32_C(c)
#define UINT64_C(c)         __UINT64_C(c)
#define INTMAX_C(c)         __INTMAX_C(c)
#define UINTMAX_C(c)        __UINTMAX_C(c)

#endif /* EXOS_STDINT_H */
