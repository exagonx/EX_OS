/* =============================================================================
 * bin/mv/mv.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * mv — sposta o rinomina un file.
 *
 *   mv [-f] <origine> <destinazione>
 *   mv [-f] <origine...> <directory>
 *
 *   -f   sostituisce la destinazione senza chiedere (qui non si chiede mai:
 *        c'e' per i makefile che lo scrivono, e per non far fallire una
 *        ricetta su un'opzione che su ogni altro sistema esiste)
 *
 * -----------------------------------------------------------------------------
 * ! MA C'E' GIA' `rename`. E LA DIFFERENZA E' LA STESSA DI rm/delete.
 *
 * `rename` dichiara di fare UNA cosa: riscrivere la voce di directory. Sta
 * scritto in testa al suo sorgente, ed e' onesto — non attraversa
 * directory, non sostituisce una destinazione esistente, i dati non toccano
 * il bus.
 *
 * `mv` deve fare quello che ogni makefile si aspetta da `mv`, e quello che
 * si aspetta e' di piu':
 *
 *     mv $(FBCNEW_EXE) $@                 rinomina, stessa directory
 *     mv src/compiler/prova.asm bootstrap/   sposta ALTROVE
 *
 * Il secondo caso, fra due directory, e' una copia piu' una cancellazione:
 * un'altra operazione, con un altro costo e un altro modo di fallire a
 * meta'. `rename` si e' rifiutato di prometterlo; `mv` lo promette e lo fa,
 * ed e' per questo che sono due programmi e non un nome in piu'.
 *
 * -----------------------------------------------------------------------------
 * ! SI PROVA PRIMA rename(), E NON E' SOLO UN'OTTIMIZZAZIONE
 *
 * Nella stessa directory, rename() e' ATOMICO: o il file ha il nome
 * vecchio o ha quello nuovo, mai nessuno dei due e mai tutti e due. La
 * copia+cancellazione no — fra le due c'e' una finestra in cui il file
 * esiste due volte, e se la macchina si spegne li' resta cosi'.
 *
 * Percio' la copia e' il RIPIEGO, e scatta solo quando rename() dice che
 * non puo' (EXDEV, oppure ENOSYS — che e' cio' che il VFS di EX-OS risponde
 * quando i due percorsi stanno in directory diverse). Un `mv` che copiasse
 * sempre funzionerebbe uguale nei casi normali e perderebbe l'atomicita'
 * proprio nel caso in cui c'era.
 *
 * ! E LA CANCELLAZIONE VIENE DOPO CHE LA COPIA E' CHIUSA E VERIFICATA. Se
 * la copia fallisce a meta', l'originale c'e' ancora e il messaggio dice
 * dove ci si e' fermati. L'ordine inverso — o anche solo un close() non
 * controllato — vuol dire perdere il file per un disco pieno.
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `mv -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("mv", "0.001");

#define PERCORSO_MAX  320
#define BLOCCO        4096

static int g_errori = 0;

/* Compone "<dir>/<basename di orig>" in `dst`. */
static int dentro(char *dst, size_t dim, const char *dir, const char *orig)
{
    const char *base = orig;
    const char *p;
    size_t      ld, lb;
    int         barra;

    for (p = orig; *p; p++) if (*p == '/') base = p + 1;

    ld    = strlen(dir);
    lb    = strlen(base);
    barra = (ld > 0 && dir[ld - 1] != '/');

    if (ld + (size_t)barra + lb + 1 > dim) return -1;

    memcpy(dst, dir, ld);
    if (barra) dst[ld++] = '/';
    memcpy(dst + ld, base, lb + 1);
    return 0;
}

static int e_directory(const char *percorso)
{
    struct stat st;

    if (stat(percorso, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

/* Copia `da` in `a`. Rende 0, oppure -1 lasciando errno impostato.
 *
 * ! SE FALLISCE, LA DESTINAZIONE A META' SI TOGLIE. Un file di lunghezza
 * sbagliata ma con il nome giusto e' peggio di un file assente: `make` lo
 * troverebbe piu' nuovo dei propri prerequisiti e non lo rifarebbe mai. */
static int copia(const char *da, const char *a)
{
    static char buf[BLOCCO];
    int         in, out;
    int         salva;

    in = open(da, O_RDONLY);
    if (in < 0) return -1;

    out = open(a, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) { salva = errno; close(in); errno = salva; return -1; }

    for (;;) {
        ssize_t n = read(in, buf, sizeof(buf));
        ssize_t scritti = 0;

        if (n == 0) break;
        if (n < 0) goto male;

        /* La scrittura puo' essere parziale: si richiama finche' il blocco
         * non e' finito. Ignorarlo perde silenziosamente dei byte in mezzo
         * al file, che e' il difetto piu' difficile da vedere di tutti. */
        while (scritti < n) {
            ssize_t w = write(out, buf + scritti, (size_t)(n - scritti));

            if (w <= 0) goto male;
            scritti += w;
        }
    }

    if (close(out) != 0) { salva = errno; close(in); goto male_chiuso; }
    close(in);
    return 0;

male:
    salva = errno;
    close(out);
male_chiuso:
    close(in);
    unlink(a);
    errno = salva;
    return -1;
}

static int sposta(const char *da, const char *a)
{
    struct stat st;

    if (stat(da, &st) != 0) {
        fprintf(stderr, "mv: %s: %s\n", da, strerror(errno));
        g_errori = 1;
        return -1;
    }

    /* ! UNA DIRECTORY NON SI SPOSTA. Fra due directory diverse vorrebbe
     * dire copiare un albero e cancellarlo, cioe' `xcp` piu' `rm -r`: due
     * programmi che ci sono gia'. Farlo qui a meta' — riuscendo nello
     * stesso posto e fallendo altrove — darebbe un comando che funziona o
     * no a seconda di dove si trova. */
    if (S_ISDIR(st.st_mode)) {
        fprintf(stderr, "mv: %s e' una directory: usa `xcp` e poi `rm -r`\n", da);
        g_errori = 1;
        return -1;
    }

    if (rename(da, a) == 0) return 0;

    /* =====================================================================
     * LA DESTINAZIONE ESISTE GIA': mv la SOSTITUISCE.
     *
     * ! QUESTO PEZZO MANCAVA, e senza di lui `mv` non serviva a niente. La
     * `rename()` di EX-OS si rifiuta di sovrascrivere — e per LEI e' la
     * scelta giusta, scritta in testa a bin/rename/rename.c: cancellare un
     * file che l'utente non ha nominato come vittima dev'essere una scelta.
     * Ma `mv` e' l'altro comando, quello che i makefile chiamano, e li' la
     * sostituzione E' la richiesta:
     *
     *     $(FBC) ... -x bin/fbc-new $^
     *     mv bin/fbc-new bin/fbc          <- bin/fbc c'e' gia', per forza
     *
     * Con il rifiuto si otteneva `mv: bin/fbc-new -> bin/fbc: esiste gia'`
     * DOPO che il collegamento era riuscito: il lavoro fatto e buttato
     * all'ultima riga della ricetta.
     *
     * ! NON E' ATOMICO, e va detto. Su un sistema POSIX `rename()`
     * sostituisce in un colpo solo e non esiste un istante in cui il nome
     * non punta a niente. Qui bisogna togliere prima, quindi quell'istante
     * c'e'. Non si perdono dati — se la seconda rename fallisce, l'origine
     * e' ancora al suo posto e lo diciamo — ma chi si aspetta l'atomicita'
     * di POSIX qui non ce l'ha.
     *
     * ! E NON SI TOGLIE UNA DIRECTORY. `mv file cartella-piena` deve
     * fallire, non svuotare la cartella.
     * ===================================================================== */
    if (errno == EEXIST) {
        struct stat dst;

        if (stat(a, &dst) == 0 && S_ISDIR(dst.st_mode)) {
            fprintf(stderr, "mv: %s e' una directory: non la sostituisco\n", a);
            g_errori = 1;
            return -1;
        }

        if (unlink(a) != 0) {
            fprintf(stderr, "mv: non riesco a togliere %s: %s\n", a, strerror(errno));
            g_errori = 1;
            return -1;
        }

        if (rename(da, a) == 0) return 0;

        /* Tolta la destinazione e la rename fallisce lo stesso: puo'
         * succedere se i due stanno in directory diverse (il VFS rende
         * ENOSYS). Non si esce: si ricade sulla copia qui sotto, che ora
         * trova il posto libero. L'origine e' intatta. */
    }

    /* rename() ha detto di no. Se e' perche' i due stanno in posti diversi,
     * si copia; per ogni altro motivo si riferisce e basta — un `mv` che
     * ripiega sulla copia davanti a QUALUNQUE errore trasformerebbe «disco
     * pieno» in una copia fallita a meta'. */
    if (errno != EXDEV && errno != ENOSYS) {
        fprintf(stderr, "mv: %s -> %s: %s\n", da, a, strerror(errno));
        g_errori = 1;
        return -1;
    }

    if (copia(da, a) != 0) {
        fprintf(stderr, "mv: copia di %s in %s: %s\n", da, a, strerror(errno));
        g_errori = 1;
        return -1;
    }

    if (unlink(da) != 0) {
        /* La copia c'e' ed e' buona: il file NON e' perso, e' duplicato.
         * Va detto, perche' e' uno stato in cui `mv` non dovrebbe lasciare
         * le cose e chi legge deve poterlo sistemare a mano. */
        fprintf(stderr, "mv: copiato in %s, ma non riesco a togliere %s: %s\n",
                a, da, strerror(errno));
        g_errori = 1;
        return -1;
    }

    return 0;
}

static void uso(void)
{
    fprintf(stderr, "uso: mv [-f] <origine> <destinazione>\n");
    fprintf(stderr, "     mv [-f] <origine...> <directory>\n");
}

int main(int argc, char **argv)
{
    int i = 1;
    int n;

    for (; i < argc; i++) {
        const char *a = argv[i];

        if (strcmp(a, "--") == 0) { i++; break; }
        if (a[0] != '-' || a[1] == '\0') break;

        {
            int k;

            for (k = 1; a[k]; k++) {
                switch (a[k]) {
                /* -f qui non cambia niente: non si chiede mai conferma,
                 * perche' non c'e' un terminale da cui aspettarla in una
                 * ricetta di make. Si accetta perche' i makefile la
                 * scrivono, e rifiutarla fermerebbe la costruzione su
                 * un'opzione che non avrebbe cambiato il risultato. */
                case 'f': break;
                default:
                    fprintf(stderr, "mv: opzione sconosciuta: -%c\n", a[k]);
                    uso();
                    return 2;
                }
            }
        }
    }

    n = argc - i;
    if (n < 2) { uso(); return 2; }

    /* Due argomenti e la destinazione non e' una directory: e' il caso
     * semplice, «chiamalo cosi'». */
    if (n == 2 && !e_directory(argv[argc - 1]))
        return sposta(argv[i], argv[argc - 1]) == 0 ? 0 : 1;

    /* Altrimenti l'ultimo argomento DEVE essere una directory: con tre o
     * piu' origini non c'e' un altro modo di leggerlo, e con due si e' gia'
     * verificato sopra. */
    if (!e_directory(argv[argc - 1])) {
        fprintf(stderr, "mv: %s non e' una directory\n", argv[argc - 1]);
        return 2;
    }

    for (; i < argc - 1; i++) {
        char dest[PERCORSO_MAX];

        if (dentro(dest, sizeof(dest), argv[argc - 1], argv[i]) != 0) {
            fprintf(stderr, "mv: %s/%s: percorso troppo lungo\n",
                    argv[argc - 1], argv[i]);
            g_errori = 1;
            continue;
        }
        sposta(argv[i], dest);
    }

    return g_errori ? 1 : 0;
}
