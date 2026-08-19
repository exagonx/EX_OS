/* =============================================================================
 * bin/scarica/scarica.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * scarica — prende una pagina e la mette da qualche parte
 *
 * ! ESISTE PER PROVARE L'HTTP PRIMA CHE CI SIA UN BROWSER, e non e' un
 * ripiego: fra «i conti sulle intestazioni tornano» e «si vede una pagina» ci
 * sono l'impaginazione e il disegno, cioe' due pezzi grossi in cui un difetto
 * dell'HTTP si confonderebbe con un difetto loro. Qui l'HTTP e' l'unica cosa
 * che puo' sbagliare.
 *
 * ! E RESTA UTILE DOPO. Un sistema che sa prendere un file da un URL e metterlo
 * su disco ha un modo in piu' di installare qualcosa, e non dipende dal fatto
 * che la grafica sia accesa.
 *
 *     scarica <url>                    lo stampa
 *     scarica <url> <file>             lo salva
 *     scarica -i <url>                 solo le intestazioni che contano
 * ============================================================================= */

#include "libc.h"
#include "exhttp.h"

/* ! IL TETTO LO METTE CHI SCARICA, NON IL SERVER. Un megabyte tiene qualunque
 * pagina di testo; se non basta si tronca e si dice, invece di far decidere a
 * chi sta dall'altra parte quanta memoria prendere qui. */
#define BUF_MAX     (1024u * 1024u)

static unsigned char g_buf[BUF_MAX];

static void uso(void)
{
    printf("uso: scarica [-i] <url> [file]\n\n");
    printf("  scarica http://esempio.it/           stampa la pagina\n");
    printf("  scarica http://esempio.it/ pag.html  la salva\n");
    printf("  scarica -i http://esempio.it/        solo l'esito\n\n");
    printf("Segue fino a %d redirezioni. https non ancora: manca il TLS.\n",
           EXHTTP_SALTI_MAX);
}

int main(int argc, char **argv)
{
    ExHttpEsito  e;
    const char  *url = 0, *dove = 0;
    int          solo_info = 0, i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0)      solo_info = 1;
        else if (strcmp(argv[i], "-h") == 0) { uso(); return 0; }
        else if (!url)                       url = argv[i];
        else if (!dove)                      dove = argv[i];
    }

    if (!url) { uso(); return 1; }

    if (!exhttp_prendi(url, g_buf, sizeof(g_buf), &e)) {
        printf("scarica: %s\n", e.errore[0] ? e.errore : "non riuscito");
        return 1;
    }

    printf("scarica: %d, %s, %u byte%s\n", e.codice,
           e.tipo[0] ? e.tipo : "(nessun tipo)", e.byte,
           e.troncata ? " (TRONCATA: il buffer era piccolo)" : "");

    /* ! SI DICE DOVE SI E' FINITI, se non e' dove si voleva andare. Con una
     * redirezione seguita in silenzio, chi guarda una pagina inattesa non ha
     * modo di sapere che l'indirizzo non e' piu' quello che aveva scritto. */
    if (e.salti > 0)
        printf("         dopo %d redirezion%s: %s\n", e.salti,
               e.salti == 1 ? "e" : "i", e.finale);

    if (solo_info) return 0;

    if (dove) {
        int fd = open(dove, O_WRONLY | O_CREAT | O_TRUNC);
        unsigned int fatti = 0;

        if (fd < 0) { printf("scarica: non riesco a creare %s\n", dove); return 1; }

        while (fatti < e.byte) {
            int k = (int)write(fd, g_buf + fatti, e.byte - fatti);

            if (k <= 0) break;
            fatti += (unsigned int)k;
        }
        close(fd);

        if (fatti != e.byte) {
            printf("scarica: scritti %u byte su %u\n", fatti, e.byte);
            return 1;
        }
        printf("         salvata in %s\n", dove);
        return 0;
    }

    /* Sullo schermo, e non con printf: il corpo puo' contenere degli zeri, e
     * printf si fermerebbe li'. */
    {
        unsigned int fatti = 0;

        while (fatti < e.byte) {
            int k = (int)write(1, g_buf + fatti, e.byte - fatti);

            if (k <= 0) break;
            fatti += (unsigned int)k;
        }
    }
    return 0;
}
