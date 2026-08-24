/* =============================================================================
 * bin/cmp/cmp.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Confronta due file BYTE PER BYTE, come il cmp di POSIX.
 *
 *     cmp uno due          dice dove differiscono, o tace se sono uguali
 *     cmp -s uno due       non dice niente: parla solo il codice di uscita
 *     cmp -l uno due       elenca TUTTE le differenze, non solo la prima
 *
 * Codice di uscita:  0 uguali   1 diversi   2 errore (file mancante, ...)
 *
 * -----------------------------------------------------------------------------
 * PERCHE' ESISTE
 *
 * E' servito per chiudere il PUNTO FISSO di un compilatore: costruire fbc con
 * se' stesso finche' due generazioni di seguito non danno lo stesso binario.
 * Senza un confronto byte per byte quella frase non si puo' verificare —
 * confrontare le DIMENSIONI non basta, perche' due binari possono pesare
 * uguale e differire, ed e' esattamente il caso che il punto fisso deve
 * escludere.
 *
 * Serve anche ai makefile, che usano `cmp -s nuovo vecchio || cp nuovo
 * vecchio` per non ritoccare la data di un file che non e' cambiato — e
 * quindi per non far ricostruire tutto cio' che ne dipende.
 *
 * -----------------------------------------------------------------------------
 * ! IL SILENZIO E' IL RISULTATO BUONO
 *
 * Due file uguali fanno stampare NIENTE, e non e' scortesia: e' il contratto
 * di POSIX, ed e' cio' che rende usabile `cmp -s a b || ...` in uno script.
 * Un «i file sono identici» sullo standard output romperebbe ogni catena che
 * legge quell'output.
 *
 * ! E IL CODICE DI USCITA DISTINGUE TRE CASI, NON DUE. «diversi» (1) e
 * «non ho potuto guardare» (2) sono cose diverse: uno script che tratta un
 * file mancante come «diverso» ricopia allegramente sopra un errore.
 *
 * -----------------------------------------------------------------------------
 * ! SI LEGGE A BLOCCHI, NON A BYTE
 *
 * Un getc() per byte su due file da 1,9 MB dentro EX-OS sono quattro milioni
 * di chiamate di sistema. Con i blocchi sono duemila. La differenza non e'
 * accademica: il primo binario su cui e' stato usato era proprio da 1,9 MB.
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `cmp -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("cmp", "0.001");

#define BLOCCO 4096

/* Le righe si contano per dire «riga N» come fa POSIX. Una riga finisce con
 * '\n'; il conteggio parte da 1 perche' i file non hanno una riga zero. */
static long g_riga = 1;

static void uso(void)
{
    printf("uso: cmp [-s] [-l] file1 file2\n");
    printf("  -s   non stampa niente: parla solo il codice di uscita\n");
    printf("  -l   elenca tutte le differenze, non solo la prima\n");
    printf("\n");
    printf("  uscita:  0 uguali   1 diversi   2 errore\n");
}

int main(int argc, char **argv)
{
    const char   *n1 = NULL, *n2 = NULL;
    int           silenzio = 0, tutte = 0, i;
    FILE         *f1, *f2;
    unsigned char b1[BLOCCO], b2[BLOCCO];
    long          pos = 1;          /* POSIX conta i byte da 1, non da 0 */
    int           diverso = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0)      silenzio = 1;
        else if (strcmp(argv[i], "-l") == 0) tutte = 1;
        else if (strcmp(argv[i], "-h") == 0) { uso(); return 0; }
        else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            printf("cmp: opzione sconosciuta '%s'\n", argv[i]);
            uso();
            return 2;
        }
        else if (n1 == NULL) n1 = argv[i];
        else if (n2 == NULL) n2 = argv[i];
        else {
            printf("cmp: troppi argomenti\n");
            return 2;
        }
    }

    if (n1 == NULL || n2 == NULL) { uso(); return 2; }

    /* ! Un file che non si apre e' un ERRORE (2), non «diverso» (1). */
    f1 = fopen(n1, "rb");
    if (f1 == NULL) {
        if (!silenzio) printf("cmp: non riesco ad aprire %s\n", n1);
        return 2;
    }
    f2 = fopen(n2, "rb");
    if (f2 == NULL) {
        if (!silenzio) printf("cmp: non riesco ad aprire %s\n", n2);
        fclose(f1);
        return 2;
    }

    for (;;) {
        size_t l1 = fread(b1, 1, BLOCCO, f1);
        size_t l2 = fread(b2, 1, BLOCCO, f2);
        size_t n  = (l1 < l2) ? l1 : l2;
        size_t k;

        for (k = 0; k < n; k++) {
            if (b1[k] != b2[k]) {
                diverso = 1;
                if (silenzio) goto fine;
                printf("%s %s differiscono: byte %ld, riga %ld"
                       " (0x%02x contro 0x%02x)\n",
                       n1, n2, pos + (long)k, g_riga,
                       b1[k], b2[k]);
                if (!tutte) goto fine;
            }
            /* Il conteggio delle righe segue il PRIMO file, come POSIX. */
            if (b1[k] == '\n') g_riga++;
        }

        /* ! UNO DEI DUE E' FINITO PRIMA. Non e' lo stesso caso di un byte
         * diverso: i primi n byte possono essere identici e i file esserlo
         * comunque no. POSIX lo dice con un messaggio suo, e serve — «EOF
         * su uno dei due» spiega perche' non c'e' un numero di byte da
         * indicare. */
        if (l1 != l2) {
            diverso = 1;
            if (!silenzio)
                printf("cmp: EOF su %s dopo %ld byte\n",
                       (l1 < l2) ? n1 : n2, pos + (long)n - 1);
            goto fine;
        }

        if (l1 == 0) break;         /* finiti insieme: identici */
        pos += (long)l1;
    }

fine:
    fclose(f1);
    fclose(f2);
    /* ! SILENZIO SE SONO UGUALI: e' il contratto, non una dimenticanza. */
    return diverso ? 1 : 0;
}
