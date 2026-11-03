/* =============================================================================
 * bin/sync/sync.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * sync — everything written so far goes to the disks, journals included.
 * Without it a test had no way to ask: delayed commits wait up to 5 seconds.
 * ============================================================================= */
#include "libc.h"

EX_VERSIONE("sync", "0.001");

int main(void)
{
    sync();
    return 0;
}
