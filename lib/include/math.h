/* =============================================================================
 * lib/include/math.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under la GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <math.h> — le funzioni matematiche.
 *
 * ! LE IMPLEMENTAZIONI NON SONO NOSTRE, ED E' UNA SCELTA. Fino ad agosto
 * 2026 questo header dichiarava tre funzioni e diceva che una libm non
 * c'era, perche' «una sqrt quasi giusta e' peggio di nessuna sqrt: sbaglia
 * in silenzio». Quel ragionamento non e' cambiato — e' cambiata la
 * conseguenza. La risposta coerente non era scriverne una mediocre: era
 * PORTARNE una vera.
 *
 * Quello che c'e' dietro questi nomi e' **openlibm**, cioe' la `msun` di
 * FreeBSD in versione autonoma (licenza MIT/BSD): l'implementazione di
 * riferimento del settore, con trent'anni di correzioni sugli
 * arrotondamenti che nessuno di noi rifarebbe meglio. Si costruisce e si
 * installa con tools/openlibm-exos/, e i sorgenti non stanno nel
 * repository — vale la stessa regola di GCC e binutils.
 *
 * ! CHI USA QUESTE FUNZIONI DEVE LINKARE -lm. Le tre eccezioni sono
 * `sqrt`, `ldexp` e `frexp`, che stanno nella libc: la prima perche' e'
 * un'istruzione dell'x87 (vedi lib/libc.c), le altre due perche' non
 * calcolano niente — scompongono e ricompongono un numero.
 *
 * Le dichiarazioni qui sotto sono state RICAVATE dai simboli davvero
 * definiti in libm.a, non copiate da uno standard: se una funzione e'
 * dichiarata qui, esiste. E' la stessa regola di prima, applicata a un
 * elenco piu' lungo.
 * ============================================================================= */

#ifndef EXOS_MATH_H
#define EXOS_MATH_H

#include "libc.h"

/* ! extern "C" anche qui: openlibm e' compilata da un compilatore C e in
 * libm.a c'e' `sin`, non `_Z3sind`. Vedi il commento esteso in libc.h —
 * questo e' l'altro header del progetto che dichiara funzioni proprie, e
 * quindi l'altro che ne ha bisogno. */
#ifdef __cplusplus
extern "C" {
#endif

/* Costanti dello standard, nella precisione dell'x87 (che internamente
 * lavora a 80 bit anche quando i valori arrivano e partono a 64). */
#define M_E         2.7182818284590452354
#define M_LOG2E     1.4426950408889634074
#define M_LOG10E    0.43429448190325182765
#define M_LN2       0.69314718055994530942
#define M_LN10      2.30258509299404568402
#define M_PI        3.14159265358979323846
#define M_PI_2      1.57079632679489661923
#define M_PI_4      0.78539816339744830962
#define M_1_PI      0.31830988618379067154
#define M_2_PI      0.63661977236758134308
#define M_SQRT2     1.41421356237309504880
#define M_SQRT1_2   0.70710678118654752440

#define HUGE_VAL    (__builtin_huge_val())
#define HUGE_VALF   (__builtin_huge_valf())
#define HUGE_VALL   (__builtin_huge_vall())
#define INFINITY    (__builtin_inff())
#define NAN         (__builtin_nanf(""))

/* Classificazione: le fa il compilatore, che sa com'e' fatto un double sul
 * bersaglio meglio di qualunque funzione. */
#define FP_NAN          0
#define FP_INFINITE     1
#define FP_ZERO         2
#define FP_SUBNORMAL    3
#define FP_NORMAL       4

#define fpclassify(x)   __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, \
                                             FP_SUBNORMAL, FP_ZERO, (x))
#define isfinite(x)     __builtin_isfinite(x)
#define isinf(x)        __builtin_isinf(x)
#define isnan(x)        __builtin_isnan(x)
#define isnormal(x)     __builtin_isnormal(x)
#define signbit(x)      __builtin_signbit(x)

/* =============================================================================
 * I CONFRONTI CHE NON SOLLEVANO ECCEZIONI
 *
 * ! NON SONO SINONIMI DI <, <=, >, >=. Con un operando NaN, `a < b` e
 * `isless(a, b)` danno la stessa RISPOSTA (falso) ma non lo stesso
 * EFFETTO: l'operatore solleva l'eccezione "invalid" dell'x87, queste
 * macro no. E' l'unica ragione per cui esistono, e per il codice che
 * lavora con i NaN e' la differenza fra un confronto e una trappola.
 *
 * Le fa il compilatore, che sa emettere `comisd`/`fucomi` con il
 * predicato giusto — non c'e' niente da chiamare a runtime, quindi
 * ! NON SERVE -lm per queste.
 *
 * CHI LE CHIEDE: la libstdc++. Il suo configure prova a compilare un
 * programma che le usa tutte e sei per decidere se `<math.h>` e' conforme
 * al C99 (`_GLIBCXX_USE_C99_MATH`); se una sola manca, il verdetto e' "no"
 * e la libreria rinuncia a mettere in `std::` decine di funzioni che
 * invece ci sono. L'errore che si vede poi e' lontanissimo dalla causa:
 * `'trunc' has not been declared in 'std'`.
 * ============================================================================= */
#define isgreater(x, y)      __builtin_isgreater((x), (y))
#define isgreaterequal(x, y) __builtin_isgreaterequal((x), (y))
#define isless(x, y)         __builtin_isless((x), (y))
#define islessequal(x, y)    __builtin_islessequal((x), (y))
#define islessgreater(x, y)  __builtin_islessgreater((x), (y))
#define isunordered(x, y)    __builtin_isunordered((x), (y))

/* Il tipo con cui l'x87 fa i conti internamente: `float_t` e `double_t`
 * sono entrambi `long double` perche' i registri dell'x87 sono a 80 bit e
 * un calcolo intermedio ci passa comunque. Dirlo e' piu' onesto che
 * dichiararli `float` e `double`, che darebbe l'idea di
 * un'approssimazione che non avviene. Servono anche loro al configure
 * della libstdc++ (FLT_EVAL_METHOD). */
#define FLT_EVAL_METHOD 2
typedef long double float_t;
typedef long double double_t;

/* =============================================================================
 * Le tre che stanno nella LIBC, non in libm: si usano senza -lm.
 * ============================================================================= */
double ldexp(double x, int e);
double frexp(double x, int *e);
/* ! sqrt e' `fsqrt` dell'x87, quindi correttamente arrotondata — una
 * delle cinque operazioni che l'IEEE 754 obbliga a esserlo. Su argomento
 * negativo da' NaN e imposta EDOM. */
double sqrt(double x);
double fabs(double v);

/* =============================================================================
 * Tutto il resto: openlibm, e ci vuole -lm
 *
 * L'elenco e' generato dai simboli di libm.a. Le varianti `f` lavorano in
 * singola precisione e le `l` in estesa (80 bit sull'x87): non sono
 * scorciatoie, sono implementazioni distinte.
 * ============================================================================= */
double acos(double);
float acosf(float);
double acosh(double);
float acoshf(float);
long double acoshl(long double);
long double acosl(long double);
double asin(double);
float asinf(float);
double asinh(double);
float asinhf(float);
long double asinhl(long double);
long double asinl(long double);
double atan(double);
double atan2(double, double);
float atan2f(float, float);
long double atan2l(long double, long double);
float atanf(float);
double atanh(double);
float atanhf(float);
long double atanhl(long double);
long double atanl(long double);
double cbrt(double);
float cbrtf(float);
long double cbrtl(long double);
double ceil(double);
float ceilf(float);
long double ceill(long double);
double copysign(double, double);
float copysignf(float, float);
long double copysignl(long double, long double);
double cos(double);
float cosf(float);
double cosh(double);
float coshf(float);
long double coshl(long double);
long double cosl(long double);
double erf(double);
double erfc(double);
float erfcf(float);
long double erfcl(long double);
float erff(float);
long double erfl(long double);
double exp(double);
double exp2(double);
float exp2f(float);
long double exp2l(long double);
float expf(float);
long double expl(long double);
double expm1(double);
float expm1f(float);
long double expm1l(long double);
float fabsf(float);
long double fabsl(long double);
double fdim(double, double);
float fdimf(float, float);
long double fdiml(long double, long double);
double floor(double);
float floorf(float);
long double floorl(long double);
double fma(double, double, double);
float fmaf(float, float, float);
long double fmal(long double, long double, long double);
double fmax(double, double);
float fmaxf(float, float);
long double fmaxl(long double, long double);
double fmin(double, double);
float fminf(float, float);
long double fminl(long double, long double);
double fmod(double, double);
float fmodf(float, float);
long double fmodl(long double, long double);
float frexpf(float, int *);
long double frexpl(long double value, int *);
double hypot(double, double);
float hypotf(float, float);
long double hypotl(long double, long double);
int ilogb(double);
int ilogbf(float);
int ilogbl(long double);
int isinff(float);
int isnanf(float);
int isopenlibm(void);
double j0(double);
float j0f(float);
double j1(double);
float j1f(float);
double jn(int, double);
float jnf(int, float);
float ldexpf(float, int);
long double ldexpl(long double, int);
double lgamma(double);
double lgamma_r(double, int *);
float lgammaf(float);
float lgammaf_r(float, int *);
long double lgammal(long double);
long double lgammal_r(long double, int *);
long long llrint(double);
long long llrintf(float);
long long llrintl(long double);
long long llround(double);
long long llroundf(float);
long long llroundl(long double);
double log(double);
double log10(double);
float log10f(float);
long double log10l(long double);
double log1p(double);
float log1pf(float);
long double log1pl(long double);
double log2(double);
float log2f(float);
long double log2l(long double);
double logb(double);
float logbf(float);
long double logbl(long double);
float logf(float);
long double logl(long double);
long lrint(double);
long lrintf(float);
long lrintl(long double);
long lround(double);
long lroundf(float);
long lroundl(long double);
double modf(double, double *);
float modff(float, float *);
long double modfl(long double, long double *);
double nan(const char *);
float nanf(const char *);
long double nanl(const char *);
double nearbyint(double);
float nearbyintf(float);
/* ! Questa NON viene da openlibm — a openlibm 0.8.7 manca. La aggiunge
 * tools/openlibm-exos/nearbyintl-exos.c, che prepara-libm.sh compila e
 * infila nell'archivio. Sta comunque in libm.a e vuole comunque -lm. */
long double nearbyintl(long double);
double nextafter(double, double);
float nextafterf(float, float);
long double nextafterl(long double, long double);
double nexttoward(double, long double);
float nexttowardf(float, long double);
long double nexttowardl(long double, long double);
double pow(double, double);
float powf(float, float);
long double powl(long double, long double);
double remainder(double, double);
float remainderf(float, float);
long double remainderl(long double, long double);
double remquo(double, double, int *);
float remquof(float, float, int *);
long double remquol(long double, long double, int *);
double rint(double);
float rintf(float);
long double rintl(long double);
double round(double);
float roundf(float);
long double roundl(long double);
double scalbln(double, long);
float scalblnf(float, long);
long double scalblnl(long double, long);
double scalbn(double, int);
float scalbnf(float, int);
long double scalbnl(long double, int);
double sin(double);
float sinf(float);
double sinh(double);
float sinhf(float);
long double sinhl(long double);
long double sinl(long double);
float sqrtf(float);
long double sqrtl(long double);
double tan(double);
float tanf(float);
double tanh(double);
float tanhf(float);
long double tanhl(long double);
long double tanl(long double);
double tgamma(double);
float tgammaf(float);
long double tgammal(long double);
double trunc(double);
float truncf(float);
long double truncl(long double);
double y0(double);
float y0f(float);
double y1(double);
float y1f(float);
double yn(int, double);
float ynf(int, float);

#ifdef __cplusplus
}   /* extern "C" */
#endif

#endif /* EXOS_MATH_H */
