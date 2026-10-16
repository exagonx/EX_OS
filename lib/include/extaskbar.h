/* =============================================================================
 * lib/include/extaskbar.h — a service puts its item on the status bar
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     #include "extaskbar.h"
 *     extaskbar_voce("WIFI", 15, "WIFI Connected", 0);
 *
 * Header only, nothing to link: the libc's IPC calls are all it needs. It
 * sends only when the text changed, and without /bin/extaskbar running it
 * costs one ipc_lookup every five seconds and sends nothing. The width is
 * the columns the item wants on the bar, its separator included; the user
 * picks which items are shown (`extaskbar -menu`).
 *
 * ! A SERVICE NEVER WAITS FOR AN ANSWER: there is none. extaskbar empties
 * its mailbox all the time and never sends anything to a service, so the
 * send cannot block for long nor lock the two of them together.
 * ============================================================================= */
#ifndef EXTASKBAR_H
#define EXTASKBAR_H

#include "extaskbar_abi.h"

#define EXTB_RICORDA 8

static inline void extaskbar_voce(const char *nome, unsigned int larghezza,
                                  const char *testo, int attenzione)
{
    static int          pid;
    static unsigned int cercato;
    static char         nomi[EXTB_RICORDA][EXTB_VOCE_NOME];
    static char         testi[EXTB_RICORDA][EXTB_VOCE_TESTO];
    static int          att[EXTB_RICORDA];
    ExtbVoce v;
    int      i, libero = -1;

    for (i = 0; i < EXTB_RICORDA; i++) {
        if (!nomi[i][0]) { if (libero < 0) libero = i; continue; }
        if (strcmp(nomi[i], nome) == 0) break;
    }
    if (i < EXTB_RICORDA && strcmp(testi[i], testo) == 0 && att[i] == attenzione && pid > 0) return;
    if (pid <= 0) {
        if (cercato && uptime_ms() - cercato < 5000) return;
        cercato = uptime_ms();
        if (cercato == 0) cercato = 1;
        pid = ipc_lookup(EXTB_SERVIZIO);
        if (pid <= 0) { pid = 0; return; }
    }
    memset(&v, 0, sizeof(v));
    strncpy(v.nome, nome, sizeof(v.nome) - 1);
    strncpy(v.testo, testo, sizeof(v.testo) - 1);
    v.larghezza = larghezza;
    v.attenzione = (unsigned int)attenzione;
    if (ipc_send((unsigned int)pid, EXTB_MSG_VOCE, &v, sizeof(v)) < 0) { pid = 0; return; }
    if (i == EXTB_RICORDA) i = libero;
    if (i >= 0) {
        strncpy(nomi[i], nome, EXTB_VOCE_NOME - 1);
        strncpy(testi[i], testo, EXTB_VOCE_TESTO - 1);
        att[i] = attenzione;
    }
}

#endif
