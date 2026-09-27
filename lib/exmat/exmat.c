/* =============================================================================
 * lib/exmat/exmat.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * The functions of math.h that lib/libc does not have, for any program that
 * wants them: compile this file in and include "math.h" (lib/include). Born
 * as bin/runbas/runbas_mat.c for the BASIC interpreter (SIN, COS, TAN, ATN,
 * EXP, LOG, ^, INT); moved here and completed on 27 September 2026 for
 * Calctor's scientific mode — asin, acos, atan2, log10, log2, ceil, fmod,
 * trunc, round and the hyperbolic ones.
 *
 * ! THEY ARE THE x87's OWN INSTRUCTIONS, not openlibm. The full libm lives
 * in the cross toolchain (cross_build/exos-cross), which only one development
 * machine can run; a small program must build anywhere the rest of the
 * system builds. The FPU has had fsin, fcos, fptan, fpatan, fyl2x, f2xm1 and
 * fprem since the 387, and the target is a Pentium MMX. They are not as exact
 * as openlibm at the edges (fsin loses precision beyond about 2^63), which a
 * calculator or a QBASIC program never reaches.
 *
 * sqrt, fabs, ldexp and frexp are in lib/libc already: not repeated here.
 * ============================================================================= */
#include "libc.h"
#include "math.h"

double sin(double x)  { double r; __asm__ ("fsin" : "=t"(r) : "0"(x)); return r; }
double cos(double x)  { double r; __asm__ ("fcos" : "=t"(r) : "0"(x)); return r; }

double tan(double x)
{
    double r;
    /* fptan leaves 1.0 on top of the result: pop it. */
    __asm__ ("fptan\n\tfstp %%st(0)" : "=t"(r) : "0"(x));
    return r;
}

double atan(double x)
{
    double r;
    /* fpatan computes atan(st1 / st0): st1 = x, st0 = 1. */
    __asm__ ("fld1\n\tfpatan" : "=t"(r) : "0"(x));
    return r;
}

/* ln(x) = ln(2) * log2(x): fyl2x computes st1 * log2(st0). */
double log(double x)
{
    double r;
    if (x <= 0.0) return x == 0.0 ? -__builtin_inf() : __builtin_nan("");
    __asm__ ("fldln2\n\tfxch\n\tfyl2x" : "=t"(r) : "0"(x));
    return r;
}

/* e^x = 2^(x * log2 e): the integer part with fscale, the fraction with
 * f2xm1, which only takes |arg| <= 1.
 *
 * ! THE SPLIT IS DONE IN C, NOT WITH fsub: in AT&T syntax gas swaps the
 * operands of fsub when the destination is st(1) — a well-known trap, and a
 * wrong sign here would make EXP quietly return 1/e^x. */
double floor(double x);

double exp(double x)
{
    double t = x * 1.4426950408889634074, i, f, r;

    if (t >  1023.0) return __builtin_inf();
    if (t < -1075.0) return 0.0;
    i = floor(t + 0.5);
    f = t - i;                                  /* in [-0.5, 0.5] */
    __asm__ ("f2xm1\n\tfld1\n\tfaddp" : "=t"(r) : "0"(f));   /* 2^f */
    __asm__ ("fscale" : "=t"(r) : "0"(r), "u"(i));             /* * 2^i */
    return r;
}

double floor(double x)
{
    double t;
    if (x >= 9.2e18 || x <= -9.2e18 || x != x) return x;   /* already whole, or NaN */
    t = (double)(long long)x;
    return (t > x) ? t - 1.0 : t;
}

double pow(double x, double y)
{
    double i = floor(y);

    if (y == 0.0) return 1.0;
    if (x == 0.0) return y > 0.0 ? 0.0 : __builtin_inf();
    if (x > 0.0)  return exp(y * log(x));

    /* A negative base only with a whole exponent, as in QBASIC. */
    if (i != y) return __builtin_nan("");
    {
        double r = exp(y * log(-x));
        long long k = (long long)i;
        return (k & 1) ? -r : r;
    }
}

/* atan2(y, x): fpatan takes y in st1 and x in st0, and knows the quadrant. */
double atan2(double y, double x)
{
    double r;
    __asm__ ("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)");
    return r;
}

double asin(double x)
{
    if (x > 1.0 || x < -1.0) return __builtin_nan("");
    return atan2(x, sqrt(1.0 - x * x));
}

double acos(double x)
{
    if (x > 1.0 || x < -1.0) return __builtin_nan("");
    return atan2(sqrt(1.0 - x * x), x);
}

/* log10(x) = log10(2) * log2(x): fldlg2, then fyl2x. */
double log10(double x)
{
    double r;
    if (x <= 0.0) return x == 0.0 ? -__builtin_inf() : __builtin_nan("");
    __asm__ ("fldlg2\n\tfxch\n\tfyl2x" : "=t"(r) : "0"(x));
    return r;
}

double log2(double x)
{
    double r;
    if (x <= 0.0) return x == 0.0 ? -__builtin_inf() : __builtin_nan("");
    __asm__ ("fld1\n\tfxch\n\tfyl2x" : "=t"(r) : "0"(x));
    return r;
}

double ceil(double x)  { double f = floor(x); return f == x ? x : f + 1.0; }
double trunc(double x) { return x < 0.0 ? -floor(-x) : floor(x); }
double round(double x) { return x < 0.0 ? -floor(-x + 0.5) : floor(x + 0.5); }

/* fprem gives a PARTIAL remainder when the exponents are far apart, and says
 * so in C2 (the parity flag after sahf): it is repeated until done. */
double fmod(double x, double y)
{
    double r;
    if (y == 0.0) return __builtin_nan("");
    __asm__ ("1:\n\tfprem\n\tfnstsw %%ax\n\tsahf\n\tjp 1b"
             : "=t"(r) : "0"(x), "u"(y) : "ax", "cc");
    return r;
}

double sinh(double x) { double e = exp(x); return (e - 1.0 / e) / 2.0; }
double cosh(double x) { double e = exp(x); return (e + 1.0 / e) / 2.0; }
double tanh(double x)
{
    double e;
    if (x > 20.0)  return 1.0;
    if (x < -20.0) return -1.0;
    e = exp(2.0 * x);
    return (e - 1.0) / (e + 1.0);
}
