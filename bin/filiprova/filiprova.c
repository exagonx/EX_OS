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

/* =============================================================================
 * LA PROVA DEL TLS
 *
 * ! IL VALORE INIZIALE NON E' ZERO, APPOSTA. Un blocco TLS azzerato invece che
 * copiato dall'immagine dell'eseguibile passerebbe qualunque prova che parta da
 * zero: il difetto si vede solo se il valore di partenza e' diverso. Sette lo
 * e'.
 *
 * ! E OGNI FILO SCRIVE IL SUO, POI CEDE LA CPU, POI RILEGGE. Se i blocchi
 * fossero in comune, dopo aver ceduto la CPU si troverebbe il numero di
 * qualcun altro: e' l'unico modo di distinguere «una copia per filo» da «una
 * copia sola che nessuno ha ancora sovrascritto».
 * ============================================================================= */
static __thread int g_mio = 7;

static void filo_tls(void *arg)
{
    int io       = (int)(long)arg;
    int iniziale = g_mio;           /* deve essere 7 per tutti */

    g_mio = 100 + io;
    sched_yield();
    sched_yield();

    if (iniziale != 7) {
        printf("  filo %d: partiva da %d invece che da 7\n", io, iniziale);
        thread_esci(1);
    }
    if (g_mio != 100 + io) {
        printf("  filo %d: ci trovo %d invece di %d — blocco condiviso\n",
               io, g_mio, 100 + io);
        thread_esci(1);
    }
    thread_esci(0);
}

static int prova_tls(void)
{
    int tid[FILI], i, codice, esito = 0;

    g_mio = 42;                     /* il filo principale marca il suo */
    printf("filiprova: ogni filo deve avere il SUO blocco TLS\n");

    for (i = 0; i < FILI; i++) {
        tid[i] = thread_crea(filo_tls, (void *)(long)i);
        if (tid[i] < 0) { printf("  thread_crea: errno %d\n", errno); return 1; }
    }
    for (i = 0; i < FILI; i++) {
        codice = -1;
        thread_attendi(tid[i], &codice);
        if (codice != 0) esito = 1;
    }

    printf("  il filo principale ci ritrova %d (atteso 42)   %s\n",
           g_mio, g_mio == 42 ? "intatto" : "SOVRASCRITTO");
    if (g_mio != 42) esito = 1;

    printf("\nfiliprova tls: %s\n", esito ? "QUALCOSA NON VA" : "tutto a posto");
    return esito;
}

/* =============================================================================
 * LA PROVA DI errno
 *
 * ! DUE FILI CHE SBAGLIANO IN MODI DIVERSI, ed e' l'unico modo di vedere la
 * differenza: se errno fosse in comune, il secondo errore cancellerebbe il
 * primo e uno dei due fili si ritroverebbe il motivo sbagliato — che e'
 * esattamente il danno, perche' un programma che chiede «perche' e' fallito?»
 * riceverebbe la risposta di qualcun altro.
 *
 * ! E SI CEDE LA CPU IN MEZZO, cinquanta volte: senza, i due fili potrebbero
 * non incrociarsi mai e la prova passerebbe anche con un errno solo.
 * ============================================================================= */
static void filo_errno(void *arg)
{
    int io = (int)(long)arg;
    int mio, i;

    /* ! TUTTI I FILI SBAGLIANO ALLO STESSO MODO, e il PRINCIPALE in un altro:
     * cosi' un errno condiviso si vede dal filo principale, che ci ritroverebbe
     * il motivo dei figli invece del suo. E' il verso in cui la prova e'
     * decisiva — fra fili che scrivono lo stesso numero non si distinguerebbe
     * niente. */
    open("/non-c-e-proprio", 0, 0);   /* ENOENT */
    mio = errno;

    for (i = 0; i < 50; i++) sched_yield();

    if (errno != mio) {
        printf("  filo %d: errno era %d e adesso e' %d — condiviso\n",
               io, mio, errno);
        thread_esci(1);
    }
    thread_esci(0);
}

static int prova_errno(void)
{
    int tid[FILI], i, codice, esito = 0, mio_main;

    /* Il principale si sporca il suo con un errore DIVERSO da quello dei fili. */
    close(999);                       /* EBADF */
    mio_main = errno;

    printf("filiprova: errno deve essere di ciascuno\n");
    printf("  il principale ha errno %d (EBADF), i fili avranno ENOENT\n",
           mio_main);
    if (mio_main == 0) {
        printf("  ATTENZIONE: close(999) non ha impostato errno, prova nulla\n");
        return 1;
    }

    for (i = 0; i < FILI; i++) {
        tid[i] = thread_crea(filo_errno, (void *)(long)i);
        if (tid[i] < 0) { printf("  thread_crea: errno %d\n", errno); return 1; }
    }
    for (i = 0; i < FILI; i++) {
        codice = -1;
        thread_attendi(tid[i], &codice);
        if (codice != 0) esito = 1;
    }

    printf("  il principale ci ritrova %d (atteso %d)   %s\n",
           errno, mio_main, errno == mio_main ? "intatto" : "SOVRASCRITTO");
    if (errno != mio_main) esito = 1;

    printf("\nfiliprova errno: %s\n", esito ? "QUALCOSA NON VA" : "tutto a posto");
    return esito;
}

/* =============================================================================
 * LA PROVA CHE L'ATTESA DORMA DAVVERO
 *
 * ! IL CRONOMETRO E' L'UNICO TESTIMONE ONESTO. Un'attesa che gira a vuoto e
 * una che dorme fanno la stessa cosa vista da fuori — il programma prosegue —
 * e si distinguono solo guardando QUANTO tempo passa e se il risveglio arriva
 * dall'esterno o dalla scadenza. Percio' due misure con esiti opposti:
 *
 *   1. nessuno sveglia, scadenza 300 ms -> deve tornare DOPO ~300, non subito:
 *      se tornasse subito vorrebbe dire che non ha dormito affatto;
 *   2. un filo sveglia dopo ~100 ms, scadenza 2000 -> deve tornare MOLTO prima
 *      della scadenza: se tornasse a 2000 vorrebbe dire che la sveglia non e'
 *      arrivata e a svegliarlo e' stato l'orologio.
 * ============================================================================= */
static volatile int g_posto = 0;

static void filo_sveglia(void *arg)
{
    (void)arg;
    usleep(100000);                  /* 100 ms */
    attesa_sveglia(&g_posto, 1);
    thread_esci(0);
}

static int prova_attesa(void)
{
    unsigned t0, dt;
    int tid, esito = 0;

    printf("filiprova: l'attesa deve DORMIRE, non girare\n");

    /* 1. nessuno sveglia: deve arrivare la scadenza */
    t0 = uptime_ms();
    attesa_dormi(&g_posto, 0, 300);
    dt = uptime_ms() - t0;
    printf("  senza nessuno che sveglia, scadenza 300 ms: tornata dopo %u ms  %s\n",
           dt, (dt >= 250 && dt < 900) ? "ha dormito" : "NON HA DORMITO");
    if (dt < 250 || dt >= 900) esito = 1;

    /* 2. qualcuno sveglia dopo 100 ms, con la scadenza lontana */
    tid = thread_crea(filo_sveglia, 0);
    if (tid < 0) { printf("  thread_crea: errno %d\n", errno); return 1; }

    t0 = uptime_ms();
    attesa_dormi(&g_posto, 0, 2000);
    dt = uptime_ms() - t0;
    printf("  svegliata da un filo, scadenza 2000 ms: tornata dopo %u ms  %s\n",
           dt, (dt >= 50 && dt < 900) ? "svegliata, non scaduta" : "SBAGLIATO");
    if (dt < 50 || dt >= 900) esito = 1;
    thread_attendi(tid, 0);

    printf("\nfiliprova attesa: %s\n", esito ? "QUALCOSA NON VA" : "tutto a posto");
    return esito;
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
    if (argc > 1 && strcmp(argv[1], "tls") == 0)       return prova_tls();
    if (argc > 1 && strcmp(argv[1], "errno") == 0)     return prova_errno();
    if (argc > 1 && strcmp(argv[1], "attesa") == 0)    return prova_attesa();
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
