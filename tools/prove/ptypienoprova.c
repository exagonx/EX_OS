/* =============================================================================
 * tools/prove/ptypienoprova.c - a full pty input says so (analisi voce 53)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A pty in raw mode. 20000 bytes go into the master in pieces of 3000 and
 * come out of the slave: the master write takes only what fits (until
 * 0.330 it said 3000 and ate the rest), gives EAGAIN when full, poll says
 * POLLOUT only with room. Every byte must arrive, in order.
 * Prints "PTYPIENO FINE N".
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <libc.h>

#define TOT 20000

static unsigned char in[3000], out[8192];

int main(void)
{
    int  fd[2], no = 0, eagain = 0, poll_pieno = -1, poll_dopo = -1;
    long dato = 0, ricevuto = 0, sbagliati = 0;

    if (pty_apri(fd) != 0) { printf("PTYPIENO pty_apri fallita\n"); return 1; }
    pty_ctl(fd[0], PTY_CTL_MODO, 0);            /* raw, no echo */

    while (ricevuto < TOT) {
        int k, w, r;

        /* letters only: ICRNL is still on in raw mode, a 13 would become 10.
         * Written until the master says EAGAIN, then read. */
        while (dato < TOT) {
            int q = (TOT - dato < (long)sizeof(in)) ? (int)(TOT - dato) : (int)sizeof(in);

            for (k = 0; k < (int)sizeof(in); k++) in[k] = (unsigned char)('A' + (dato + k) % 26);
            errno = 0;
            w = (int)write(fd[0], in, (size_t)q);
            if (w > 0) { dato += w; continue; }
            if (errno == EAGAIN) {
                struct pollfd v;

                eagain++;
                v.fd = fd[0]; v.events = POLLOUT; v.revents = 0;
                poll(&v, 1, 0);
                if (poll_pieno < 0) poll_pieno = (v.revents & POLLOUT) ? 1 : 0;
            } else { printf("PTYPIENO write: %d errno %d\n", w, errno); no++; }
            break;
        }
        if (no) break;
        r = (int)read(fd[1], out, sizeof(out));
        if (r <= 0) { printf("PTYPIENO read: %d\n", r); no++; break; }
        for (k = 0; k < r; k++) if (out[k] != (unsigned char)('A' + (ricevuto + k) % 26)) sbagliati++;
        ricevuto += r;
        if (poll_pieno >= 0 && poll_dopo < 0) {
            struct pollfd v;

            v.fd = fd[0]; v.events = POLLOUT; v.revents = 0;
            poll(&v, 1, 0);
            poll_dopo = (v.revents & POLLOUT) ? 1 : 0;
        }
    }
    printf("PTYPIENO dati %ld, ricevuti %ld, sbagliati %ld, EAGAIN %d\n", dato, ricevuto, sbagliati, eagain);
    if (ricevuto != TOT || sbagliati) no++;
    if (eagain == 0) { printf("PTYPIENO il master non e' mai stato pieno\n"); no++; }
    if (poll_pieno != 0) { printf("PTYPIENO pieno ma POLLOUT %d\n", poll_pieno); no++; }
    if (poll_dopo != 1) { printf("PTYPIENO svuotato ma POLLOUT %d\n", poll_dopo); no++; }
    printf("PTYPIENO FINE %d\n", no);
    return no ? 1 : 0;
}
