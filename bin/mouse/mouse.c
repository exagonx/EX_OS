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
 *     mouse            segue il mouse e stampa quello che arriva; ESC o `q`
 *                      per uscire
 *     mouse -n <n>     legge n volte e poi esce (per le prove automatiche)
 *
 * ! SI ESCE CON UN TASTO, E NON SOLO CON Ctrl-C. Un programma che si ferma
 * unicamente con l'interruzione chiede all'utente di ammazzarlo per uscirne,
 * e su una macchina che si sta ancora provando — dove magari e' proprio la
 * tastiera il sospettato — quel gesto e' l'ultimo che si vuole imparare.
 *
 * ! E PER ASPETTARE DUE COSE INSIEME NON SI FA POLLING. Il mouse e la
 * tastiera rispondono con due messaggi diversi nella STESSA cassetta postale:
 * si tiene una richiesta aperta per ciascuno e si aspetta la prima che
 * arriva, riemettendo solo quella servita. Un ciclo che interrogasse i due a
 * turno con una pausa in mezzo consumerebbe CPU per non fare niente e
 * mostrerebbe i movimenti a scatti.
 *
 * Il mouse PS/2 lo serve /dev/kbd.drv: e' la seconda porta dello stesso 8042
 * della tastiera, e due processi che leggono 0x60 si rubano i byte. Il perche'
 * per esteso sta in drivers/kbd/kbd_proto.h.
 * ============================================================================= */

#include "libc.h"
#include "kbd_proto.h"

/* +0.001 a ogni modifica: `mouse -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("mouse", "0.001");

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
    int pid, i = 0, quante = 0, kbd = -1, raw = 0;
    unsigned int mia = 0;
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
    if (quante == 0) printf("ESC o `q` per uscire.\n");

    /* --- la tastiera in raw, per poter vedere un tasto singolo --------- */
    kbd = ipc_lookup(KBD_SERVICE_NAME);
    if (quante == 0 && kbd > 0) {
        ConsoleInfo ci;
        KbdSetMode  m;

        if (console_info(&ci) == 0) mia = ci.mia;

        m.modo    = KBD_MODE_RAW;
        m.console = mia;
        if (ipc_send((unsigned int)kbd, KBD_MSG_SETMODE, &m, sizeof(m)) == 0) {
            raw = 1;
            ipc_send((unsigned int)kbd, KBD_MSG_READKEY, &mia, sizeof(mia));
        }
    }

    /* Una richiesta aperta per ciascuna sorgente; si riemette solo quella
     * che ha risposto. Vedi il commento in testa al file. */
    {
        MouseLeggi r;
        r.attendi = 1;
        ipc_send((unsigned int)pid, MOUSE_MSG_LEGGI, &r, sizeof(r));
    }

    while (quante == 0 || i < quante) {
        IpcMessage    meta;
        unsigned char buf[64];

        if (ipc_recv(&meta, buf, sizeof(buf)) < 0) continue;

        if (meta.tipo == KBD_MSG_KEY && meta.len >= sizeof(unsigned int)) {
            unsigned int k = 0;
            memcpy(&k, buf, sizeof(k));
            k &= KBD_KEY_MASK;

            if (k == 0x1B || k == 'q' || k == 'Q') break;

            ipc_send((unsigned int)kbd, KBD_MSG_READKEY, &mia, sizeof(mia));
            continue;
        }

        if (meta.tipo != MOUSE_MSG_STATO || meta.len < sizeof(s)) continue;

        memcpy(&s, buf, sizeof(s));
        tot_x += s.dx;
        tot_y += s.dy;
        i++;

        printf("  dx %+5d  dy %+5d   bottoni %c%c%c   totale %+ld,%+ld%s\n",
               s.dx, s.dy,
               (s.bottoni & MOUSE_BTN_SIN) ? 'S' : '-',
               (s.bottoni & MOUSE_BTN_CEN) ? 'C' : '-',
               (s.bottoni & MOUSE_BTN_DES) ? 'D' : '-',
               tot_x, tot_y,
               s.persi ? "  (pacchetti persi!)" : "");

        if (quante == 0 || i < quante) {
            MouseLeggi r;
            r.attendi = 1;
            ipc_send((unsigned int)pid, MOUSE_MSG_LEGGI, &r, sizeof(r));
        }
    }

    /* ! LA TASTIERA SI RIMETTE IN COOKED PRIMA DI USCIRE, sempre. Lasciarla
     * in raw vuol dire una shell che non vede piu' una riga intera: il
     * programma sarebbe uscito e la macchina resterebbe rotta per colpa sua. */
    if (raw) {
        KbdSetMode m;
        m.modo    = KBD_MODE_COOKED;
        m.console = mia;
        ipc_send((unsigned int)kbd, KBD_MSG_SETMODE, &m, sizeof(m));
    }

    printf("\ntotale letto: dx %+ld  dy %+ld\n", tot_x, tot_y);
    return 0;
}
