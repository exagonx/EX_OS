/* =============================================================================
 * bin/rm/rm.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * rm — cancella file e directory, con i nomi e i codici di POSIX.
 *
 *   rm [-f] [-r|-R] <nome> [nome...]
 *
 *   -f   non e' un errore se il file non c'e', e non si stampa niente
 *   -r   scende dentro le directory e le svuota prima di toglierle
 *
 * -----------------------------------------------------------------------------
 * ! MA C'E' GIA' `delete`. PERCHE' DUE COMANDI CHE CANCELLANO?
 *
 * Perche' fanno due cose diverse, e la differenza sta tutta in CHI li
 * scrive.
 *
 *   `delete` lo scrive una PERSONA al prompt. Espande i caratteri jolly da
 *            se' — `delete *.tmp` — dice quanti file ha selezionato prima
 *            di toccarli, e non scende nelle directory. E' il modello di
 *            MS-DOS, ed e' quello giusto per chi guarda lo schermo.
 *
 *   `rm`     lo scrive un MAKEFILE. Deve chiamarsi `rm` perche' e' cosi'
 *            che c'e' scritto nelle ricette di ogni progetto del mondo —
 *            centoquarantasei volte in quello di FreeBASIC — e deve
 *            comportarsi come quello di POSIX: `-f` che tace su un file
 *            assente, `-r` che scende, codice di uscita 0 o 1.
 *
 * Dare a `delete` il nome `rm` avrebbe voluto dire promettere quei flag e
 * non averli; dare a `rm` i modi di `delete` avrebbe voluto dire che
 * `rm -f cosa-che-non-c-e` — che ogni makefile fa a ogni ricetta — stampa
 * un errore e ferma la costruzione.
 *
 * ! E NON SI ESPANDONO I CARATTERI JOLLY. `rm *.o` qui cancella un file
 * chiamato davvero `*.o`, non tutti gli oggetti. Su Unix quell'espansione
 * la fa LA SHELL prima di lanciare rm, e la shell di EX-OS non la fa
 * ancora: farla qui vorrebbe dire che `rm` e `cp` si comportano
 * diversamente davanti allo stesso asterisco. Chi vuole i jolly usa
 * `delete`, che li fa e lo dichiara. Il giorno che la shell li espandera',
 * questo file non cambiera' di una riga.
 *
 * -----------------------------------------------------------------------------
 * ! SENZA -r UNA DIRECTORY E' UN ERRORE, ANCHE CON -f. Sono due domande
 * diverse: `-f` dice «non lamentarti se non c'e'», non «cancella qualunque
 * cosa ci sia». Un `rm -f build` che porta via un albero intero perche'
 * qualcuno ha dimenticato la `-r` e' il genere di comodita' che si paga una
 * volta sola.
 * ============================================================================= */

#include "libc.h"

/* Quanto puo' essere profondo un albero che `rm -r` scende.
 *
 * ! SERVE UN TETTO PERCHE' LA RICORSIONE E' VERA, e lo stack di un
 * processo EX-OS cresce su richiesta fino a 256 KB (USER_STACK_MAX): un
 * albero patologico — o un difetto del filesystem che facesse tornare una
 * directory dentro se' stessa — lo esaurirebbe, e il processo morirebbe di
 * page fault invece che con un messaggio. Trentadue livelli sono piu' di
 * quanti ne abbia qualunque albero di sorgenti. */
#define PROFONDITA_MAX  32

#define PERCORSO_MAX    320

static int g_forza     = 0;
static int g_ricorsivo = 0;
static int g_errori    = 0;

/* Compone "<dir>/<nome>" in `dst`. Rende 0, oppure -1 se non ci sta —
 * e in quel caso NON si cancella niente: un percorso troncato punta a un
 * altro file, e cancellare un altro file e' il peggiore dei modi di
 * sbagliare. */
static int unisci(char *dst, size_t dim, const char *dir, const char *nome)
{
    size_t ld = strlen(dir);
    size_t ln = strlen(nome);
    int    barra = (ld > 0 && dir[ld - 1] != '/');

    if (ld + (size_t)barra + ln + 1 > dim) return -1;

    memcpy(dst, dir, ld);
    if (barra) dst[ld++] = '/';
    memcpy(dst + ld, nome, ln + 1);
    return 0;
}

static void lamenta(const char *cosa, const char *nome)
{
    /* Con -f si tace SOLO su «non esiste»: gli altri errori restano errori.
     * Un `rm -f` che ingoia anche «sola lettura» direbbe di aver cancellato
     * un file che c'e' ancora, e la costruzione proseguirebbe usandolo. */
    if (g_forza && errno == ENOENT) return;

    fprintf(stderr, "rm: %s %s: %s\n", cosa, nome, strerror(errno));
    g_errori = 1;
}

static int togli(const char *percorso, int profondita);

/* =============================================================================
 * Svuota una directory e poi la toglie.
 *
 * ! SI RILEGGE LA DIRECTORY DA CAPO DOPO OGNI CANCELLAZIONE, e non e' uno
 * spreco: e' l'unico modo corretto.
 *
 * La prima versione faceva la cosa ovvia — `while ((e = readdir(d)))` e
 * dentro la cancellazione — e su venti file ne toglieva SEDICI:
 *
 *     rm -r /disk/pr2
 *     rm: non riesco a togliere /disk/pr2: directory non vuota
 *     ls /disk/pr2  ->  g17 g18 g19 g20
 *
 * La readdir() della libc non tiene un cursore dentro il filesystem: chiede
 * al kernel «dammi le voci a partire dalla numero START» e ne riceve un
 * blocco di LISTDIR_MAX_BATCH (sedici). Cancellare sposta le voci che stanno
 * dopo, quindi al blocco successivo lo START punta oltre quelle rimaste, e
 * quelle scivolate all'indietro non le vede piu' nessuno.
 *
 * ! E SOTTO I DICIASSETTE FILE NON SI VEDE, perche' l'elenco intero entra
 * nel primo blocco — che viene letto PRIMA che si cancelli qualcosa. La
 * prova con sei file passava. Quella con venti no. E quella vera erano i 145
 * oggetti del compilatore FreeBASIC.
 *
 * Rileggendo dall'inizio e togliendo una voce per volta il problema non
 * esiste: nessuna iterazione attraversa una modifica. Costa una lettura in
 * piu' per file — tre syscall invece di una — e su una directory di
 * centoquarantacinque file sono quattrocento chiamate, cioe' niente.
 *
 * ! E NON PUO' GIRARE ALL'INFINITO: se la voce trovata non si riesce a
 * togliere si esce dal ciclo, e la rmdir qui sotto fallisce nominando la
 * directory rimasta. Senza quell'uscita, un file protetto farebbe ripescare
 * la stessa voce per sempre.
 * ============================================================================= */
static int togli_albero(const char *percorso, int profondita)
{
    if (profondita >= PROFONDITA_MAX) {
        fprintf(stderr, "rm: %s: albero piu' profondo di %d livelli, mi fermo\n",
                percorso, PROFONDITA_MAX);
        g_errori = 1;
        return -1;
    }

    for (;;) {
        DIR           *d;
        struct dirent *e;
        char           figlio[PERCORSO_MAX];
        int            trovato = 0;

        d = opendir(percorso);
        if (d == NULL) { lamenta("non riesco ad aprire", percorso); return -1; }

        while ((e = readdir(d)) != NULL) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
                continue;

            if (unisci(figlio, sizeof(figlio), percorso, e->d_name) != 0) {
                fprintf(stderr, "rm: %s/%s: percorso troppo lungo\n",
                        percorso, e->d_name);
                g_errori = 1;
                break;          /* non si puo' comporre: inutile riprovarci */
            }
            trovato = 1;
            break;              /* una per giro: vedi il ! qui sopra */
        }
        closedir(d);

        if (!trovato) break;                              /* vuota */
        if (togli(figlio, profondita + 1) != 0) break;    /* l'ha gia' detto */
    }

    /* ! LA DIRECTORY SI TOGLIE ANCHE SE UN FIGLIO E' FALLITO, e rmdir
     * fallira' a sua volta perche' non e' vuota. E' voluto: cosi' il
     * messaggio finale nomina la directory che e' rimasta, e non solo il
     * file che non si e' potuto togliere. */
    if (rmdir(percorso) != 0) { lamenta("non riesco a togliere", percorso); return -1; }
    return 0;
}

static int togli(const char *percorso, int profondita)
{
    struct stat st;

    if (stat(percorso, &st) != 0) {
        /* Non esiste: con -f e' normale, senza e' un errore. In nessuno dei
         * due casi c'e' altro da fare. */
        lamenta("non trovo", percorso);
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        if (!g_ricorsivo) {
            /* ! NON si passa da lamenta(): questo messaggio va detto anche
             * con -f. Vedi il commento in testa — «non lamentarti se non
             * c'e'» e «cancella qualunque cosa» sono due richieste diverse,
             * e chi ha scritto solo la prima non ha chiesto la seconda. */
            fprintf(stderr, "rm: %s e' una directory: serve -r\n", percorso);
            g_errori = 1;
            return -1;
        }
        return togli_albero(percorso, profondita);
    }

    if (unlink(percorso) != 0) { lamenta("non riesco a cancellare", percorso); return -1; }
    return 0;
}

static void uso(void)
{
    fprintf(stderr, "uso: rm [-f] [-r] <nome> [nome...]\n");
    fprintf(stderr, "  -f  non e' un errore se il file non c'e'\n");
    fprintf(stderr, "  -r  scende dentro le directory\n");
    fprintf(stderr, "I caratteri jolly NON si espandono: per quelli c'e' `delete`.\n");
}

int main(int argc, char **argv)
{
    int i;
    int quanti = 0;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        /* `--` chiude le opzioni: e' l'unico modo di cancellare un file il
         * cui nome comincia per trattino, e i makefile generati lo usano. */
        if (strcmp(a, "--") == 0) { i++; break; }

        if (a[0] != '-' || a[1] == '\0') break;

        /* Le opzioni si possono ammucchiare (`rm -rf`), che e' esattamente
         * come stanno scritte in ogni makefile. */
        {
            int k;

            for (k = 1; a[k]; k++) {
                switch (a[k]) {
                case 'f': g_forza = 1; break;
                case 'r': case 'R': g_ricorsivo = 1; break;
                default:
                    fprintf(stderr, "rm: opzione sconosciuta: -%c\n", a[k]);
                    uso();
                    return 2;
                }
            }
        }
    }

    for (; i < argc; i++) { togli(argv[i], 0); quanti++; }

    if (quanti == 0) {
        /* ! `rm -f` SENZA NOMI NON E' UN ERRORE: lo dice POSIX, e non e' un
         * cavillo. Nasce da `rm -f $(OGGETTI)` con la lista vuota, che e'
         * cio' che succede a ogni `make pulisci` su un albero gia' pulito.
         * Trattarlo come un errore fermerebbe la costruzione al primo
         * bersaglio senza prerequisiti. */
        if (g_forza) return 0;
        uso();
        return 2;
    }

    return g_errori ? 1 : 0;
}
