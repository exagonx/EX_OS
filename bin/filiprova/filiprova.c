/* =============================================================================
 * bin/filiprova/filiprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * LA PROVA DEI FILI — che girino davvero, e che il lucchetto serva
 *
 * ! UNA PROVA CHE PASSA ANCHE SENZA FILI NON PROVA NIENTE, ed e' il rischio di
 * questo programma: se `thread_crea` eseguisse la funzione nel chiamante — cioe'
 * se i fili fossero finti — un contatore protetto verrebbe fuori giusto lo
 * stesso. Percio' qui si guardano TRE cose diverse:
 *
 *   1. che il totale col lucchetto sia esatto (la correttezza);
 *   2. che i fili si ALTERNINO davvero, contando quante volte il testimone
 *      cambia di mano (senza concorrenza sarebbe zero);
 *   3. che il totale SENZA lucchetto risulti, almeno qualche volta, sbagliato
 *      — la dimostrazione che c'era qualcosa da proteggere.
 *
 * Il terzo punto e' l'unico che puo' "fallire" senza che sia colpa di nessuno:
 * due fili possono anche non incrociarsi mai. Percio' non e' un errore, e' un
 * numero che si stampa.
 * ============================================================================= */
#include "libc.h"

#define FILI    4
#define GIRI    20000

static Mutex    g_lucchetto = MUTEX_LIBERO;
static volatile int g_conto_protetto;
static volatile int g_conto_nudo;

/* Il testimone: chi ha lavorato per ultimo. Ogni volta che cambia, vuol dire
 * che due fili si sono davvero alternati sulla CPU. */
static volatile int g_ultimo   = -1;
static volatile int g_scambi;

static void lavoro(void *arg)
{
    int io = (int)(long)arg;
    int i;

    for (i = 0; i < GIRI; i++) {
        mutex_prendi(&g_lucchetto);
        g_conto_protetto++;
        if (g_ultimo != io) { g_ultimo = io; g_scambi++; }
        mutex_lascia(&g_lucchetto);

        /* ! LO STESSO CONTO, SENZA PROTEZIONE, e volutamente scritto in tre
         * passi: `x = x + 1` in una riga sola il compilatore lo tiene in un
         * registro e la corsa non si vede mai. Cosi' invece la finestra fra
         * lettura e scrittura c'e' davvero, ed e' il punto della prova. */
        {
            int v = g_conto_nudo;
            sched_yield();
            g_conto_nudo = v + 1;
        }
    }
    thread_esci(io);
}

/* Un filo che non finisce mai: serve alla prova dell'abbandono. */
static void per_sempre(void *arg)
{
    (void)arg;
    for (;;) sched_yield();
}

/* ! USCIRE SENZA ASPETTARE I FILI E' IL CASO PERICOLOSO, ed e' il motivo per
 * cui questa prova esiste: i fili vivono nella memoria del processo, e se
 * qualcuno restasse vivo mentre lo spazio di indirizzamento se ne va,
 * girerebbe sopra pagine liberate. Il kernel deve portarseli via tutti. Se
 * questa prova lascia la macchina in piedi e il comando dopo funziona, la
 * regola e' rispettata. */
static int abbandona(void)
{
    int i, tid;

    printf("filiprova: creo fili che non finiscono, poi esco senza aspettarli\n");
    for (i = 0; i < 3; i++) {
        tid = thread_crea(per_sempre, 0);
        if (tid < 0) { printf("  thread_crea: errno %d\n", errno); return 1; }
        printf("  filo eterno, tid %d\n", tid);
    }
    printf("  esco adesso: se li porta via il kernel, il prompt torna\n");
    return 0;
}

/* Piu' fili di quanti ce ne stiano: si deve fermare dicendolo, non a caso. */
static int troppi(void)
{
    int i, tid, ultimo_errno = 0, quanti = 0;

    printf("filiprova: ne chiedo dodici, le piazzole sono %d meno il capogruppo\n",
           FILI_MAX_PER_PROCESSO);
    for (i = 0; i < 12; i++) {
        tid = thread_crea(per_sempre, 0);
        if (tid < 0) { ultimo_errno = errno; break; }
        quanti++;
    }
    printf("  creati %d, poi si e' fermato con errno %d\n", quanti, ultimo_errno);
    return (quanti == FILI_MAX_PER_PROCESSO - 1) ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "abbandona") == 0) return abbandona();
    if (argc > 1 && strcmp(argv[1], "troppi") == 0)    return troppi();
    {
    int tid[FILI];
    int i, codice, esito = 0;
    int atteso = FILI * GIRI;

    printf("filiprova: %d fili, %d giri l'uno\n", FILI, GIRI);

    for (i = 0; i < FILI; i++) {
        tid[i] = thread_crea(lavoro, (void *)(long)i);
        if (tid[i] < 0) {
            printf("  thread_crea %d: fallito (errno %d)\n", i, errno);
            return 1;
        }
        printf("  filo %d creato, tid %d\n", i, tid[i]);
    }

    for (i = 0; i < FILI; i++) {
        codice = -1;
        if (thread_attendi(tid[i], &codice) != 0) {
            printf("  thread_attendi %d: fallito (errno %d)\n", tid[i], errno);
            esito = 1;
        } else if (codice != i) {
            printf("  filo %d: codice d'uscita %d invece di %d\n", i, codice, i);
            esito = 1;
        }
    }

    printf("\n  col lucchetto  %6d   atteso %6d   %s\n",
           g_conto_protetto, atteso,
           g_conto_protetto == atteso ? "esatto" : "SBAGLIATO");
    printf("  senza          %6d   atteso %6d   %s\n",
           g_conto_nudo, atteso,
           g_conto_nudo == atteso ? "(stavolta nessuna corsa)" : "perso per strada");
    printf("  scambi di mano %6d   %s\n", g_scambi,
           g_scambi > 1 ? "i fili si alternano davvero" : "NESSUNA ALTERNANZA");

    if (g_conto_protetto != atteso) esito = 1;
    if (g_scambi <= 1)              esito = 1;

    printf("\nfiliprova: %s\n", esito ? "QUALCOSA NON VA" : "tutto a posto");
    return esito;
    }
}
