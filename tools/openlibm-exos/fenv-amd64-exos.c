/* =============================================================================
 * tools/openlibm-exos/fenv-amd64-exos.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * The six <fenv.h> functions openlibm's amd64 build does not export.
 *
 * ! FOUND LINKING EXILLA AT 64 BIT (16 October 2026): libxul wanted
 * fegetround and libm.a had no such symbol. openlibm_fenv_amd64.h defines
 * feclearexcept, fegetexceptflag, fesetenv, fetestexcept, fegetround and
 * fesetround as `static inline` (__fenv_static), and amd64/fenv.c's
 * `extern inline` declarations of three of them emit nothing from a static
 * function. At 32 bit the i387 build has them all; our <fenv.h> declares
 * them as ordinary functions, so a caller that is not inlined needs them.
 *
 * The header's own code is used, renamed: the same instructions, now with
 * an external name each.
 * ============================================================================= */
#define feclearexcept    olm_feclearexcept
#define fegetexceptflag  olm_fegetexceptflag
#define fesetenv         olm_fesetenv
#define fetestexcept     olm_fetestexcept
#define fegetround       olm_fegetround
#define fesetround       olm_fesetround
#include "openlibm_fenv.h"
#undef feclearexcept
#undef fegetexceptflag
#undef fesetenv
#undef fetestexcept
#undef fegetround
#undef fesetround

int feclearexcept(int e)                         { return olm_feclearexcept(e); }
int fegetexceptflag(fexcept_t *f, int e)         { return olm_fegetexceptflag(f, e); }
int fesetenv(const fenv_t *env)                  { return olm_fesetenv(env); }
int fetestexcept(int e)                          { return olm_fetestexcept(e); }
int fegetround(void)                             { return olm_fegetround(); }
int fesetround(int r)                            { return olm_fesetround(r); }
