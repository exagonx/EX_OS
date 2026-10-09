/* =============================================================================
 * bin/ahci/ahci.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ahci — passare i dischi all'AHCI, e misurare quanto va un disco
 * (9 ottobre 2026, vuole il kernel 0.245)
 *
 *     ahci             passa all'AHCI i dischi che il controller offre anche
 *                      di li', PER QUESTA SESSIONE: al riavvio si torna com'era
 *     ahci -s          lo rende stabile: scrive  ahci = 1  in /boot/kernel.cfg,
 *                      ma solo se in questa sessione il passaggio e' riuscito
 *     ahci -d          stampa i registri dei controller, in sola lettura
 *     ahci -m FILE     legge FILE per intero e dice a quanti MB/s
 *
 * ! E' LA PROVA SENZA RISCHIO di `ahci = 1` in /boot/kernel.cfg. Quella voce
 * vale a ogni avvio, e se su una macchina l'AHCI non partisse la si
 * ritroverebbe a ogni accensione; questo comando fa la stessa cosa una volta
 * sola, e un riavvio la disfa. Si misura prima, si passa, si misura dopo: se
 * va, la voce in kernel.cfg la rende stabile.
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `ahci -version` la stampa. */
EX_VERSIONE("ahci", "0.003");

static char g_buf[65536];

static int misura(const char *nome)
{
    unsigned int t0, ms, tot = 0;
    int fd = open(nome, O_RDONLY), n;

    if (fd < 0) { printf("ahci: %s: %s\n", nome, strerror(errno)); return 1; }
    t0 = uptime_ms();
    while ((n = (int)read(fd, g_buf, sizeof(g_buf))) > 0) tot += (unsigned int)n;
    ms = uptime_ms() - t0;
    close(fd);
    if (ms == 0) ms = 1;
    printf("ahci: %s: %u KB in %u ms = %u KB/s\n", nome, tot / 1024u, ms,
           (tot / 1024u) * 1000u / ms);
    return 0;
}

/* Scrive `ahci = 1` nella sezione [kernel] di /boot/kernel.cfg, subito dopo
 * la sua intestazione. Non tocca nient'altro. 0 fatto, 1 c'era gia', -1 errore. */
static int rendi_stabile(void)
{
    static char testo[16384], nuovo[16384 + 64];
    static const char riga[] = "ahci        = 1\n";
    int fd = open("/boot/kernel.cfg", O_RDONLY), n = 0, k, dove;
    char *p, *q;

    if (fd < 0) return -1;
    while (n < (int)sizeof(testo) - 1 &&
           (k = (int)read(fd, testo + n, (unsigned int)(sizeof(testo) - 1 - n))) > 0) n += k;
    close(fd);
    testo[n] = 0;

    /* C'e' gia' una riga `ahci`, in qualunque forma? */
    for (p = testo; p && *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : 0) {
        q = p;
        while (*q == ' ' || *q == '\t') q++;
        if (strncmp(q, "ahci", 4) == 0 && (q[4] == ' ' || q[4] == '\t' || q[4] == '=')) return 1;
    }

    p = strstr(testo, "[kernel]");
    if (p == 0) return -1;
    q = strchr(p, '\n');
    dove = q ? (int)(q - testo) + 1 : n;

    memcpy(nuovo, testo, (unsigned int)dove);
    memcpy(nuovo + dove, riga, sizeof(riga) - 1);
    memcpy(nuovo + dove + sizeof(riga) - 1, testo + dove, (unsigned int)(n - dove));
    n += (int)sizeof(riga) - 1;

    fd = open("/boot/kernel.cfg", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return -1;
    k = (int)write(fd, nuovo, (unsigned int)n);
    close(fd);
    return k == n ? 0 : -1;
}

int main(int argc, char **argv)
{
    int r;

    if (argc == 3 && strcmp(argv[1], "-m") == 0) return misura(argv[2]);

    /* -d: guardare e basta. I registri finiscono nel log del kernel, e da li'
     * si rileggono: e' il kernel che li ha davanti, non questo programma. */
    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        static char log[32768 + 1];
        int k, n, i = 0;

        r = ahci_guarda();
        if (r < 0) {
            printf("ahci: il kernel ha rifiutato (%d): serve la 0.245 aggiornata, e "
                   "bisogna essere amministratore\n", r);
            return 1;
        }
        for (k = 0; k < 2; k++) {
            n = klog_leggi(log, 32768, k);
            if (n <= 0) continue;
            log[n] = 0;
            for (i = 0; i < n; ) {
                int j = i;

                while (j < n && log[j] != '\n') j++;
                log[j] = 0;
                if (strstr(log + i, "AHCI: ") != 0) {
                    const char *q = strstr(log + i, "AHCI: ");

                    printf("%s\n", q);
                }
                i = j + 1;
            }
        }
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "-s") == 0) {
        r = ahci_stato();
        if (r != 2) {
            printf("ahci: in questa sessione nessun disco e' passato all'AHCI: non scrivo\n"
                   "      niente. Prima  ahci  e, se riesce,  ahci -s\n");
            return 1;
        }
        r = rendi_stabile();
        if (r == 1) printf("ahci: /boot/kernel.cfg ha gia' la sua riga  ahci\n");
        else if (r == 0) printf("ahci: scritto  ahci = 1  in /boot/kernel.cfg: vale a ogni avvio\n");
        else { printf("ahci: non riesco a scrivere /boot/kernel.cfg\n"); return 1; }
        return 0;
    }
    if (argc != 1) {
        printf("uso: ahci            passa i dischi all'AHCI, per questa sessione\n");
        printf("     ahci -s         lo rende stabile (ahci = 1 in /boot/kernel.cfg)\n");
        printf("     ahci -d         i registri del controller, SENZA TOCCARE NIENTE:\n");
        printf("                     si guarda prima di provare\n");
        printf("     ahci -m FILE    legge FILE e dice a quanti KB/s\n");
        return 2;
    }

    if (ahci_stato() == 2) {
        printf("ahci: il disco lavora gia' in AHCI.\n");
        return 0;
    }
    r = ahci_passa();
    if (r < 0) {
        printf("ahci: il kernel ha rifiutato (%d): serve la 0.245, e bisogna "
               "essere amministratore\n", r);
        return 1;
    }
    if (r == 0) printf("ahci: nessun disco e' passato all'AHCI. Il perche' lo dice:  dmesg AHCI\n");
    else        printf("ahci: %d disc%s all'AHCI fino al riavvio. Per sempre:  ahci -s\n",
                       r, r == 1 ? "o passato" : "hi passati");
    return 0;
}
