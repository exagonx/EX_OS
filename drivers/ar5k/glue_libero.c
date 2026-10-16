/* =============================================================================
 * drivers/ar5k/glue_libero.c — two functions that must NOT see ath5k.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * ath5k.h declares them before the types their parameters name, so those
 * types have prototype scope and a definition next to ath5k.h "conflicts".
 * Here they are defined with the same ABI (a pointer and an int) and nothing
 * else in sight.
 * ============================================================================= */
#include "libc.h"
#include <stdarg.h>

struct ath5k_hw;

/* the adaptive noise immunity is not driven yet */
void ath5k_ani_phy_error_report(struct ath5k_hw *ah, int phyerr) { (void)ah; (void)phyerr; }

void _ath5k_printk(const struct ath5k_hw *ah, const char *level, const char *fmt, ...)
{
    char    t[256];
    va_list a;

    (void)ah; (void)level;
    va_start(a, fmt);
    vsnprintf(t, sizeof(t), fmt, a);
    va_end(a);
    printf("ar5k: %s", t);
}
