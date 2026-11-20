/* =============================================================================
 * lib/libc_chk_locale.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * The "local" __stack_chk_fail of i386 (voce 105)
 *
 * GCC calls this HIDDEN function from PIC code built with -fstack-protector:
 * a hidden name cannot come from /lib/ld-exos.so, so every program and every
 * library that needs it carries its own four bytes of it. glibc keeps it in
 * libc_nonshared.a; here it is a member of its own in libexos_avvio.a
 * (tools/costruisci-ld.sh), so that an EX-OS library (exwin.so...) takes
 * this and NOT the program's start next to it. In libc.a it is in libc.c.
 * The guard and __stack_chk_fail itself are in lib/libc_avvio.c.
 * ============================================================================= */
void __stack_chk_fail(void);

__attribute__((visibility("hidden"))) void __stack_chk_fail_local(void)
{
    __stack_chk_fail();
}
