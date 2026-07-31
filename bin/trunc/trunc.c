/* =============================================================================
 * bin/trunc/trunc.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Cambia la dimensione di un file.
 *
 *   trunc <file> <byte>
 *
 * -----------------------------------------------------------------------
 * ALLUNGARE NON OCCUPA SPAZIO
 *
 * Portare un file da 1 KB a 100 MB non consuma 100 MB: lo spazio in mezzo
 * diventa un BUCO. Il filesystem non alloca blocchi per contenuto che
 * nessuno ha scritto, e leggere dentro il buco restituisce zeri. I blocchi
 * si materializzano solo quando ci si scrive davvero.
 *
 * ⚠️ Non tutti i filesystem lo fanno: e' una proprieta' di ext2. Su FAT il
 * kernel alloca sul serio, perche' FAT non sa rappresentare un buco.
 *
 * -----------------------------------------------------------------------
 * ACCORCIARE E' DISTRUTTIVO E NON SI CHIEDE CONFERMA
 *
 * I byte oltre la nuova dimensione se ne vanno, e riallungando si
 * ottengono zeri — non i dati di prima. La conferma non c'e' per la stessa
 * ragione per cui non ce l'ha `delete` su un nome preciso: chi scrive il
 * nome di un file e un numero piu' piccolo della sua dimensione ha gia'
 * detto cosa vuole. La conferma serve quando il comando fa piu' di quanto
 * l'utente abbia nominato, e qui nomina esattamente un file.
 * ============================================================================= */
#include "libc.h"

/* =============================================================================
 * Identità del programma
 *
 * ▲ INCREMENTARE TR_VERSION DI 0.001 A OGNI MODIFICA ▲
 * ============================================================================= */
#define TR_VERSION  "0.001"

static const char *spiega(int e)
{
    switch (-e) {
        case 2:  return "non trovato";
        case 5:  return "errore di I/O";
        case 21: return "e' una directory";
        case 22: return "percorso non valido";
        case 30: return "montato in sola lettura";
        case 38: return "il filesystem non sa troncare";
        default: return "errore";
    }
}

/* Numero decimale, con suffisso K o M facoltativo. Ritorna 0 e mette 1 in
 * *ok se il testo non e' un numero: senza distinguerli, "trunc file pippo"
 * troncherebbe a zero — cioe' cancellerebbe il contenuto — invece di dire
 * che l'argomento non si capisce. */
static unsigned int a_numero(const char *s, int *ok)
{
    unsigned int v = 0;
    int i = 0, cifre = 0;

    *ok = 0;

    while (s[i] == ' ') i++;
    for (; s[i] >= '0' && s[i] <= '9'; i++) {
        v = v * 10u + (unsigned int)(s[i] - '0');
        cifre++;
    }
    if (cifre == 0) return 0;

    if (s[i] == 'K' || s[i] == 'k') { v *= 1024u;        i++; }
    else if (s[i] == 'M' || s[i] == 'm') { v *= 1048576u; i++; }

    if (s[i] != '\0') return 0;

    *ok = 1;
    return v;
}

int main(int argc, char **argv)
{
    unsigned int nuova, vecchia = 0;
    int          ok, r, aveva = 0;

    if (argc != 3) {
        printf("uso: trunc <file> <byte>\n\n");
        printf("  trunc dati.bin 0        svuota il file\n");
        printf("  trunc dati.bin 4096     lo porta a 4096 byte\n");
        printf("  trunc dati.bin 512K     suffissi K e M ammessi\n\n");
        printf("Allungare non occupa spazio su ext2: lo spazio in mezzo\n");
        printf("diventa un buco e si legge come zeri.\n");
        printf("Accorciare PERDE i byte in coda, senza chiedere conferma.\n");
        return 1;
    }

    nuova = a_numero(argv[2], &ok);
    if (!ok) {
        printf("trunc: '%s' non e' un numero di byte.\n", argv[2]);
        printf("Ammessi: 4096, 512K, 2M.\n");
        return 1;
    }

    /* La dimensione di prima serve solo per raccontare cosa e' successo.
     * Se non si riesce a leggerla non e' un errore: il troncamento si fa
     * lo stesso, e si dice meno. */
    {
        DirEntry v[32];
        int n, i, k;
        char dir[128];
        const char *nome = argv[1];

        /* Il nome senza il percorso, per confrontarlo con l'elenco. */
        for (i = 0, k = -1; argv[1][i]; i++) if (argv[1][i] == '/') k = i;
        if (k < 0) { dir[0] = '.'; dir[1] = '\0'; }
        else if (k == 0) { dir[0] = '/'; dir[1] = '\0'; nome = argv[1] + 1; }
        else {
            for (i = 0; i < k && i < 127; i++) dir[i] = argv[1][i];
            dir[i] = '\0';
            nome = argv[1] + k + 1;
        }

        n = listdir(dir, v, 32);
        for (i = 0; i < n; i++) {
            if (strcmp(v[i].name, nome) == 0) {
                vecchia = v[i].size;
                aveva = 1;
                break;
            }
        }
    }

    r = truncate(argv[1], nuova);
    if (r != 0) {
        printf("trunc: %s: %s (errore %d)\n", argv[1], spiega(r), r);
        return 1;
    }

    if (aveva && vecchia != nuova) {
        printf("%s: %u -> %u byte (%s)\n", argv[1], vecchia, nuova,
               (nuova > vecchia) ? "allungato, lo spazio nuovo e' un buco"
                                 : "accorciato, i byte in coda sono persi");
    } else {
        printf("%s: %u byte\n", argv[1], nuova);
    }

    return 0;
}
