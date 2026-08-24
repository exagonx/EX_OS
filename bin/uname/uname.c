/* =============================================================================
 * bin/uname/uname.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * uname — dice che sistema e' questo, nel formato che POSIX prescrive.
 *
 *   uname          il nome del sistema           EX-OS
 *   uname -s       lo stesso, detto esplicito
 *   uname -n       il nome della macchina
 *   uname -r       la versione
 *   uname -v       la variante (qui vuota: sta tutta in -r)
 *   uname -m       l'architettura                i686
 *   uname -a       tutto, in quest'ordine
 *
 * -----------------------------------------------------------------------------
 * ! ERA SOLO UN BUILT-IN DELLA SHELL, E NON BASTAVA
 *
 * `uname` c'era gia', dentro /bin/sh, e stampava una frase per una persona:
 *
 *     EX-OS version 0.176 (x86 32-bit) - Copyright (C) 2025 ... GPL v2
 *
 * Il primo makefile che gira dentro EX-OS lo chiama cosi':
 *
 *     uname := $(shell uname)
 *     ifeq ($(uname),Linux) ...
 *
 * e confronta la risposta con una parola. Una frase non combacia con niente.
 *
 * ! MA IL MOTIVO VERO E' UN ALTRO, e l'ha detto la prova: GNU make NON
 * PASSA DALLA SHELL per un comando senza metacaratteri. Lo esegue diretto,
 * per risparmiare un processo. Quindi `$(shell uname)` non trovava nessun
 * built-in — cercava un PROGRAMMA di nome `uname`, non lo trovava, e
 * rispondeva
 *
 *     make: makefile:601: fork: file o directory inesistente
 *
 * cioe' un errore che parla di fork mentre a mancare e' un eseguibile. Il
 * ragionamento «tanto passa dalla shell, il built-in basta» era sbagliato, e
 * si e' visto solo eseguendo.
 *
 * ! E IL BUILT-IN E' STATO TOLTO DALLA SHELL. Tenerli tutti e due avrebbe
 * voluto dire che `uname` risponde due cose diverse a seconda di CHI lo
 * chiama: una frase se lo scrive una persona al prompt, una parola se lo
 * scrive un makefile. Due comportamenti sotto lo stesso nome sono peggio di
 * un comportamento scomodo. La frase per le persone c'era gia' altrove e c'e'
 * ancora: si chiama `ver`.
 *
 * -----------------------------------------------------------------------------
 * ! `-m` DICE i686 E NON x86. E' il nome che usa `uname -m` su ogni Unix
 * per questa architettura, ed e' quello che i configure e i makefile
 * riconoscono. Dire `x86` sarebbe piu' vero e non lo capirebbe nessuno.
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `uname -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("uname", "0.001");

/* =============================================================================
 * ! IL NOME DEL SISTEMA NON VIENE DA kernel.cfg, ED E' UNA SCELTA.
 *
 * La tentazione era leggerlo da OSNAME, come fa la shell per il banner: sta
 * gia' li', e' configurabile, e sembra la cosa coerente. La prova ha detto
 * altro — OSNAME vale «EX OS», con uno spazio, perche' e' un nome da
 * mostrare a una persona. E `uname -s` NON e' un nome da mostrare: e' un
 * token che gli script confrontano e tagliano.
 *
 *     uname -a | cut -d' ' -f1        prenderebbe «EX»
 *     ifeq ($(shell uname),EX OS)     dipende da come make spezza gli spazi
 *
 * E c'e' di peggio del formato: sarebbe CONFIGURABILE. Chi mettesse
 * OSNAME=«Il mio sistema» in kernel.cfg cambierebbe il ramo che ogni
 * makefile del disco imbocca, senza sapere di averlo fatto e senza che
 * niente lo segnali. L'identita' del sistema operativo non e' una
 * preferenza.
 *
 * Percio' `-s` e' fisso, ed e' il nome che il progetto usa in ogni
 * intestazione di sorgente. Restano dall'ambiente le cose che e' giusto
 * cambiare: la versione e il nome della macchina.
 * ============================================================================= */
#define UNAME_SYSNAME  "EX-OS"

/* Queste si', dall'ambiente: la versione e il nome della macchina sono cio'
 * che kernel.cfg puo' legittimamente decidere. I valori di scorta servono a
 * rispondere qualcosa su un'immagine senza configurazione. */
static const char *da_ambiente(const char *chiave, const char *scorta)
{
    const char *v = getenv(chiave);

    return (v && *v) ? v : scorta;
}

static void uso(void)
{
    fprintf(stderr, "uso: uname [-a] [-s] [-n] [-r] [-v] [-m]\n");
    fprintf(stderr, "  -s  nome del sistema (predefinito)   -r  versione\n");
    fprintf(stderr, "  -n  nome della macchina              -v  variante\n");
    fprintf(stderr, "  -m  architettura                     -a  tutto\n");
}

int main(int argc, char **argv)
{
    int s = 0, n = 0, r = 0, v = 0, m = 0;
    int i;
    int primo = 1;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        int         k;

        if (a[0] != '-' || a[1] == '\0') { uso(); return 2; }

        for (k = 1; a[k]; k++) {
            switch (a[k]) {
            case 'a': s = n = r = v = m = 1; break;
            case 's': s = 1; break;
            case 'n': n = 1; break;
            case 'r': r = 1; break;
            case 'v': v = 1; break;
            case 'm': m = 1; break;
            default:
                fprintf(stderr, "uname: opzione sconosciuta: -%c\n", a[k]);
                uso();
                return 2;
            }
        }
    }

    /* Senza opzioni si stampa il solo nome del sistema: e' cio' che dice
     * POSIX, ed e' cio' su cui i makefile contano. */
    if (!s && !n && !r && !v && !m) s = 1;

    /* ! L'ORDINE E' QUELLO DI `uname -a` E NON QUELLO DELLE OPZIONI SULLA
     * RIGA. `uname -m -s` stampa «EX-OS i686», non «i686 EX-OS»: e' cosi'
     * dappertutto, e chi legge l'output con uno script conta su questo. */
    if (s) { printf("%s",   UNAME_SYSNAME);                     primo = 0; }
    if (n) { printf("%s%s", primo ? "" : " ",
                            da_ambiente("HOSTNAME", "exos"));   primo = 0; }
    if (r) { printf("%s%s", primo ? "" : " ",
                            da_ambiente("OSVER", "0"));         primo = 0; }
    /* La variante e' vuota di proposito: su Unix ci sta la data di
     * costruzione del kernel, che qui non c'e'. Stampare una stringa
     * inventata sarebbe peggio di uno spazio. */
    if (v) { printf("%s",   primo ? "" : " ");                  primo = 0; }
    if (m) { printf("%s%s", primo ? "" : " ", "i686");          primo = 0; }

    printf("\n");
    return 0;
}
