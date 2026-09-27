/* =============================================================================
 * bin/runbas/runbas_tasto.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ONE KEY, WITHOUT ENTER — for the GET statement of gfbasic (27 Sept 2026)
 *
 * ! THE TTY OF EX-OS HANDS OVER WHOLE LINES, and tty_raw() only concerns the
 * output (the serial mirror): a single key is asked of the keyboard DRIVER,
 * over IPC, exactly as gfedit does (bin/gfedit/gf_term.c): raw mode for this
 * console, one READKEY, the KEY back, cooked mode again. The first try used
 * tty_raw and getchar, and GET waited for Enter showing the key typed.
 *
 * Without the driver (the in-kernel keyboard) there is no way: a line is read
 * and its first character is the key.
 * ============================================================================= */
#include "libc.h"
#include "kbd_proto.h"

int gfb_tasto_crudo(void)
{
    int         pid = ipc_lookup(KBD_SERVICE_NAME);
    ConsoleInfo ci;
    KbdSetMode  m;
    IpcMessage  meta;
    unsigned    console = 0, key = 0;

    if (pid <= 0) {
        char riga[64];
        int  n = (int)read(0, riga, sizeof(riga));
        return n > 0 ? (unsigned char)riga[0] : 0;
    }
    if (console_info(&ci) == 0) console = ci.mia;

    m.modo = KBD_MODE_RAW;
    m.console = console;
    ipc_send((unsigned)pid, KBD_MSG_SETMODE, &m, sizeof(m));
    if (ipc_send((unsigned)pid, KBD_MSG_READKEY, &console, sizeof(console)) >= 0) {
        for (;;) {
            if (ipc_recv_timeout(&meta, &key, sizeof(key), 0) < 0) { key = 0; break; }
            if (meta.tipo == KBD_MSG_KEY && meta.len >= sizeof(key)) break;
        }
    }
    m.modo = KBD_MODE_COOKED;
    ipc_send((unsigned)pid, KBD_MSG_SETMODE, &m, sizeof(m));
    return (int)(key & KBD_KEY_MASK);
}
