/* =============================================================================
 * lib/include/inttypes.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under la GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <inttypes.h> — come si stampano i tipi di <stdint.h>.
 *
 * PRId32 e compagne esistono perche' su un sistema a 64 bit int64_t e'
 * `long` e su uno a 32 bit e' `long long`: "%ld" e "%lld" non sono
 * intercambiabili, e il codice portabile non puo' sceglierne uno.
 *
 * ! SUL BERSAGLIO i386-exos int32_t E' `long int`, non `int` — vedi il
 * commento in <stdint.h> — eppure qui PRId32 vale "d" e non "ld". Non e'
 * una svista: nella chiamata variadica di printf `int` e `long` a 32 bit
 * sono la stessa cosa, occupano lo stesso spazio e si leggono con lo
 * stesso codice, quindi "%d" stampa correttamente un int32_t su entrambi i
 * compilatori. La differenza si vedrebbe solo con un controllo di formato
 * (-Wformat), che la printf di EX-OS non ha perche' non e' dichiarata con
 * l'attributo `format`. Il giorno che quel controllo arrivera', queste
 * macro andranno derivate dal tipo e non scelte a mano — e sara' un altro
 * buon motivo per correggere INT32_TYPE in gcc/config/i386/exos.h.
 * ============================================================================= */

#ifndef EXOS_INTTYPES_H
#define EXOS_INTTYPES_H

#include "stdint.h"

/* --- Interi a larghezza esatta ----------------------------------------------- */
#define PRId8       "d"
#define PRIi8       "i"
#define PRIu8       "u"
#define PRIo8       "o"
#define PRIx8       "x"
#define PRIX8       "X"

#define PRId16      "d"
#define PRIi16      "i"
#define PRIu16      "u"
#define PRIo16      "o"
#define PRIx16      "x"
#define PRIX16      "X"

#define PRId32      "d"
#define PRIi32      "i"
#define PRIu32      "u"
#define PRIo32      "o"
#define PRIx32      "x"
#define PRIX32      "X"

#define PRId64      "lld"
#define PRIi64      "lli"
#define PRIu64      "llu"
#define PRIo64      "llo"
#define PRIx64      "llx"
#define PRIX64      "llX"

/* --- least / fast: su i386 coincidono con quelle sopra ----------------------- */
#define PRIdLEAST8  PRId8
#define PRIuLEAST8  PRIu8
#define PRIxLEAST8  PRIx8
#define PRIdLEAST16 PRId16
#define PRIuLEAST16 PRIu16
#define PRIxLEAST16 PRIx16
#define PRIdLEAST32 PRId32
#define PRIuLEAST32 PRIu32
#define PRIxLEAST32 PRIx32
#define PRIdLEAST64 PRId64
#define PRIuLEAST64 PRIu64
#define PRIxLEAST64 PRIx64

#define PRIdFAST8   "d"
#define PRIuFAST8   "u"
#define PRIxFAST8   "x"
#define PRIdFAST16  "d"
#define PRIuFAST16  "u"
#define PRIxFAST16  "x"
#define PRIdFAST32  "d"
#define PRIuFAST32  "u"
#define PRIxFAST32  "x"
#define PRIdFAST64  PRId64
#define PRIuFAST64  PRIu64
#define PRIxFAST64  PRIx64

/* --- puntatori e tipi massimi ------------------------------------------------ */
#define PRIdPTR     "d"
#define PRIiPTR     "i"
#define PRIuPTR     "u"
#define PRIoPTR     "o"
#define PRIxPTR     "x"
#define PRIXPTR     "X"

#define PRIdMAX     "lld"
#define PRIiMAX     "lli"
#define PRIuMAX     "llu"
#define PRIoMAX     "llo"
#define PRIxMAX     "llx"
#define PRIXMAX     "llX"

/* --- Lettura (sscanf) -------------------------------------------------------- */
#define SCNd8       "hhd"
#define SCNu8       "hhu"
#define SCNx8       "hhx"
#define SCNd16      "hd"
#define SCNu16      "hu"
#define SCNx16      "hx"
#define SCNd32      "d"
#define SCNu32      "u"
#define SCNx32      "x"
#define SCNd64      "lld"
#define SCNu64      "llu"
#define SCNx64      "llx"
#define SCNdPTR     "d"
#define SCNuPTR     "u"
#define SCNxPTR     "x"
#define SCNdMAX     "lld"
#define SCNuMAX     "llu"
#define SCNxMAX     "llx"

/* =============================================================================
 * Gli interi piu' larghi della macchina
 *
 * ! SU EX-OS `intmax_t` E' `long long`, cioe' 64 bit. Non e' una scelta:
 * e' il tipo intero piu' grande che il bersaglio abbia, e lo standard dice
 * che intmax_t dev'essere quello.
 *
 * Fino ad agosto 2026 qui c'era scritto che imaxabs, imaxdiv e strtoimax
 * non c'erano perche' «nessuno li ha ancora chiesti». Adesso li chiede la
 * libstdc++: il suo configure compila un programma che li usa tutti per
 * decidere se `<inttypes.h>` e' conforme al C99, e una sola assenza fa
 * dichiarare non conforme l'header intero.
 * ============================================================================= */
typedef long long           intmax_t;
typedef unsigned long long  uintmax_t;

typedef struct { intmax_t quot; intmax_t rem; } imaxdiv_t;

/* ! extern "C" come in libc.h e math.h: senza, un programma C++ cerca
 * `_Z7imaxabsx` e in libc.a non c'e'. Vedi il commento esteso in libc.h. */
#ifdef __cplusplus
extern "C" {
#endif

intmax_t  imaxabs(intmax_t v);
imaxdiv_t imaxdiv(intmax_t num, intmax_t den);
intmax_t  strtoimax(const char *s, char **fine, int base);
uintmax_t strtoumax(const char *s, char **fine, int base);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_INTTYPES_H */
