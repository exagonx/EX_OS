/* =============================================================================
 * lib/include/fenv.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * <fenv.h> — l'ambiente in virgola mobile: arrotondamento ed eccezioni.
 *
 * Le funzioni stanno in libm.a (openlibm, il suo fenv per i387): qui ci sono
 * solo i tipi, le costanti e le dichiarazioni.
 * ! fenv_t E' QUELLO DI openlibm (include/openlibm_fenv_i387.h), campo per
 * campo: e' la libm a leggerlo e scriverlo, e una forma diversa qui
 * vorrebbe dire fegetenv che scrive fuori dalla struttura del chiamante.
 * I campi __mxcsr_* li usa solo se la CPU ha l'SSE: sul Pentium MMX restano
 * a zero.
 * ============================================================================= */

#ifndef EXOS_FENV_H
#define EXOS_FENV_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t __control;
    uint16_t __mxcsr_hi;
    uint16_t __status;
    uint16_t __mxcsr_lo;
    uint32_t __tag;
    char     __other[16];
} fenv_t;

typedef uint16_t fexcept_t;

#define FE_INVALID    0x01
#define FE_DENORMAL   0x02
#define FE_DIVBYZERO  0x04
#define FE_OVERFLOW   0x08
#define FE_UNDERFLOW  0x10
#define FE_INEXACT    0x20
#define FE_ALL_EXCEPT (FE_DIVBYZERO | FE_DENORMAL | FE_INEXACT | \
                       FE_INVALID | FE_OVERFLOW | FE_UNDERFLOW)

#define FE_TONEAREST  0x0000
#define FE_DOWNWARD   0x0400
#define FE_UPWARD     0x0800
#define FE_TOWARDZERO 0x0c00

extern const fenv_t __fe_dfl_env;
#define FE_DFL_ENV (&__fe_dfl_env)

int feclearexcept(int excepts);
int fegetexceptflag(fexcept_t *flagp, int excepts);
int feraiseexcept(int excepts);
int fesetexceptflag(const fexcept_t *flagp, int excepts);
int fetestexcept(int excepts);
int fegetround(void);
int fesetround(int round);
int fegetenv(fenv_t *envp);
int feholdexcept(fenv_t *envp);
int fesetenv(const fenv_t *envp);
int feupdateenv(const fenv_t *envp);
int feenableexcept(int excepts);
int fedisableexcept(int excepts);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_FENV_H */
