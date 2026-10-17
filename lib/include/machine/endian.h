/* =============================================================================
 * <machine/endian.h> — the BSD name of <endian.h>
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * ! LLVM ASKS FOR THIS ONE (16 October 2026, rustc for EX-OS): llvm/ADT/bit.h
 * includes <endian.h> only on the systems it knows (Linux, Hurd, Haiku...)
 * and <machine/endian.h> everywhere else, as the BSDs have it. Same content.
 * ============================================================================= */
#ifndef EXOS_MACHINE_ENDIAN_H
#define EXOS_MACHINE_ENDIAN_H
#include <endian.h>
#endif
