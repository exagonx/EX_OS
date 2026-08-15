/* =============================================================================
 * bin/mouse/mouse.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 *     mouse            segue il mouse e stampa quello che arriva
 *     mouse -n <n>     legge n volte e poi esce (per le prove automatiche)
 *
 * Il mouse PS/2 lo serve /dev/kbd.drv: e' la seconda porta dello stesso 8042
 * della tastiera, e due processi che leggono 0x60 si rubano i byte. Il perche'
 * per esteso sta in drivers/kbd/kbd_proto.h.
 * ============================================================================= */

#include "libc.h"
#include "kbd_proto.h"

static int chiedi(int pid, MouseStato *s, unsigned int attendi)
{
    MouseLeggi  r;
    IpcMessage  meta;

    r.attendi = attendi;
    if (ipc_send((unsigned int)pid, MOUSE_MSG_LEGGI, &r, sizeof(r)) < 0) return -1;
    if (ipc_recv(&meta, s, sizeof(*s)) < 0) return -1;
    if (meta.tipo != MOUSE_MSG_STATO) return -1;
    return 0;
}

int main(int argc, char **argv)
{
    MouseStato s;
    int pid, i, quante = 0;
    long tot_x = 0, tot_y = 0;

    if (argc > 2 && argv[1][0] == '-' && argv[1][1] == 'n') quante = atoi(argv[2]);

    /* Prima il driver dedicato, poi il PS/2 dentro kbd.drv: la regola e'
     * spiegata in kbd_proto.h e vale anche per il server a finestre. */
    pid = ipc_lookup(MOUSE_SERVICE_NAME);
    if (pid >= 0) {
        printf("sorgente: servizio '%s' (driver dedicato)\n", MOUSE_SERVICE_NAME);
    } else {
        pid = ipc_lookup(KBD_SERVICE_NAME);
        if (pid < 0) {
            printf("mouse: nessun servizio mouse ne' '%s' (%d)\n",
                   KBD_SERVICE_NAME, pid);
            return 1;
        }
        printf("sorgente: servizio '%s' (mouse PS/2)\n", KBD_SERVICE_NAME);
    }

    /* Prima lettura senza attesa: dice solo se il mouse esiste. */
    if (chiedi(pid, &s, 0) != 0) { printf("mouse: nessuna risposta\n"); return 1; }

    if (!s.presente) {
        printf("mouse: la sorgente c'e' ma non risponde nessun mouse.\n");
        return 1;
    }

    printf("dy positivo = verso il BASSO.\n");

    for (i = 0; quante == 0 || i < quante; i++) {
        if (chiedi(pid, &s, 1) != 0) { printf("mouse: lettura fallita\n"); return 1; }

        tot_x += s.dx;
        tot_y += s.dy;

        printf("  dx %+5d  dy %+5d   bottoni %c%c%c   totale %+ld,%+ld%s\n",
               s.dx, s.dy,
               (s.bottoni & MOUSE_BTN_SIN) ? 'S' : '-',
               (s.bottoni & MOUSE_BTN_CEN) ? 'C' : '-',
               (s.bottoni & MOUSE_BTN_DES) ? 'D' : '-',
               tot_x, tot_y,
               s.persi ? "  (pacchetti persi!)" : "");
    }

    printf("\ntotale letto: dx %+ld  dy %+ld\n", tot_x, tot_y);
    return 0;
}
