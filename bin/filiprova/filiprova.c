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

/* =============================================================================
 * LA PROVA DELLE VARIABILI DI CONDIZIONE — un produttore, un consumatore, e una
 * coda di UN posto
 *
 * ! LA CODA E' DI UN POSTO APPOSTA. Con dieci posti i due si incrociano poco e
 * la prova diventa quasi sequenziale; con uno solo ognuno dei mille elementi
 * costringe l'altro ad aspettare, cioe' apre mille volte la finestra fra
 * «lascia il lucchetto» e «dormi» che e' l'unica cosa che qui si sta provando.
 *
 * ! E LE TRE MISURE SONO SCELTE PER POTER FALLIRE, ognuna su un guasto diverso:
 *
 *   1. LA SOMMA. Il consumatore somma quel che legge; se un elemento va perso o
 *      viene letto due volte il totale non torna. Un conteggio da solo non
 *      basterebbe: perderne uno e leggerne un altro due volte darebbe lo stesso
 *      numero di giri.
 *   2. I GIRI A VUOTO. Qui devono essere ZERO, e non «pochi»: c'e' un solo
 *      consumatore e un solo produttore, quindi chi si sveglia trova sempre la
 *      roba — nessun altro puo' avergliela portata via. Un numero maggiore di
 *      zero vuol dire che ci si e' svegliati per la scadenza, cioe' che un
 *      segnale si e' perso.
 *   3. IL CRONOMETRO, ed e' il testimone del punto 2. L'attesa qui dentro ha
 *      una scadenza di mezzo secondo — una RETE, non un modo di funzionare: se
 *      un segnale si perde, il giro costa 500 ms invece di frazioni di
 *      millisecondo. Senza quella rete un segnale perso sarebbe una macchina
 *      ferma per sempre e la prova non «fallirebbe», resterebbe li'.
 * ============================================================================= */
#define ELEMENTI  1000

static Mutex       q_lucchetto = MUTEX_LIBERO;
static Condizione  q_piena     = CONDIZIONE_ZERO;   /* c'e' roba da prendere */
static Condizione  q_vuota     = CONDIZIONE_ZERO;   /* c'e' posto dove mettere */
static volatile int q_valore;
static volatile int q_c_e;          /* 0 = vuota, 1 = piena */
static volatile int q_somma;
static volatile int q_presi;
static volatile int q_vuoto_cons;   /* svegliato e non c'era niente */
static volatile int q_vuoto_prod;   /* svegliato e non c'era posto */

static void filo_consuma(void *arg)
{
    int i;

    (void)arg;
    for (i = 0; i < ELEMENTI; i++) {
        mutex_prendi(&q_lucchetto);
        while (!q_c_e) {
            condizione_aspetta_ms(&q_piena, &q_lucchetto, 500);
            if (!q_c_e) q_vuoto_cons++;
        }
        q_somma += q_valore;
        q_presi++;
        q_c_e = 0;
        mutex_lascia(&q_lucchetto);

        /* Si segnala FUORI dal lucchetto: chi si sveglia dentro lo troverebbe
         * in mano a chi l'ha svegliato e si riaddormenterebbe subito. */
        condizione_segnala(&q_vuota);
    }
    thread_esci(0);
}

static int prova_condizione(void)
{
    unsigned t0, dt;
    int      tid, i, codice = -1, esito = 0;
    int      atteso = ELEMENTI * (ELEMENTI + 1) / 2;

    printf("filiprova: un produttore, un consumatore, una coda di un posto\n");

    tid = thread_crea(filo_consuma, 0);
    if (tid < 0) { printf("  thread_crea: errno %d\n", errno); return 1; }

    t0 = uptime_ms();
    for (i = 1; i <= ELEMENTI; i++) {
        mutex_prendi(&q_lucchetto);
        while (q_c_e) {
            condizione_aspetta_ms(&q_vuota, &q_lucchetto, 500);
            if (q_c_e) q_vuoto_prod++;
        }
        q_valore = i;
        q_c_e    = 1;
        mutex_lascia(&q_lucchetto);
        condizione_segnala(&q_piena);
    }
    thread_attendi(tid, &codice);
    dt = uptime_ms() - t0;

    printf("  elementi passati %5d   atteso %5d   %s\n",
           q_presi, ELEMENTI, q_presi == ELEMENTI ? "tutti" : "NE MANCA");
    printf("  somma            %5d   attesa %5d   %s\n",
           q_somma, atteso,
           q_somma == atteso ? "esatta" : "PERSI O LETTI DUE VOLTE");
    printf("  giri a vuoto: consumatore %d, produttore %d   %s\n",
           q_vuoto_cons, q_vuoto_prod,
           (q_vuoto_cons == 0 && q_vuoto_prod == 0)
               ? "nessuno" : "UN SEGNALE SI E' PERSO");
    /* ! IL 400 NON E' A CASO: mille passaggi ne costano fra 20 e 60 (misurato
     * tre volte, e balla di tre volte perche' dipende da come cadono i quanti),
     * e una SOLA scadenza ne costerebbe 500. Fra i due numeri non c'e' niente,
     * quindi la riga puo' stare comoda sopra la misura e restare capace di
     * distinguere. */
    printf("  %d passaggi in %u ms   %s\n", ELEMENTI, dt,
           dt < 400 ? "e' andata a segnali" : "TROPPO: sono scadenze");

    if (q_presi != ELEMENTI)  esito = 1;
    if (q_somma != atteso)    esito = 1;
    if (q_vuoto_cons || q_vuoto_prod) esito = 1;
    if (dt >= 400)            esito = 1;

    printf("\nfiliprova condizione: %s\n",
           esito ? "QUALCOSA NON VA" : "tutto a posto");
    return esito;
}

/* =============================================================================
 * LA PROVA DEI SEMAFORI — la stessa coda, senza lucchetto
 *
 * ! DUE SEMAFORI E NESSUN MUTEX, ed e' il punto: `posti` comincia a uno e
 * `roba` a zero, e il posto stesso fa da lucchetto — dentro la coda non ci puo'
 * essere piu' di uno per volta perche' i posti sono uno. Se il semaforo
 * contasse male, due fili entrerebbero insieme e il valore si sovrascriverebbe:
 * la somma lo direbbe.
 *
 * ! E QUI IL SONNO SI MISURA DAVVERO, col cronometro sulla presa a vuoto: un
 * semaforo a zero preso con scadenza 200 ms deve tornare DOPO ~200 con
 * ETIMEDOUT. Se tornasse subito non avrebbe dormito; se tornasse riuscendo
 * avrebbe contato un posto che non c'era.
 * ============================================================================= */
static Semaforo    s_posti = 1;                 /* un posto libero */
static Semaforo    s_roba  = SEMAFORO_ZERO;     /* niente dentro */
static volatile int s_valore;
static volatile int s_somma;
static volatile int s_presi;

static void filo_consuma_sem(void *arg)
{
    int i;

    (void)arg;
    for (i = 0; i < ELEMENTI; i++) {
        semaforo_prendi(&s_roba);
        s_somma += s_valore;
        s_presi++;
        semaforo_lascia(&s_posti);
    }
    thread_esci(0);
}

static int prova_semaforo(void)
{
    unsigned t0, dt;
    int      tid, i, codice = -1, esito = 0, r;
    int      atteso = ELEMENTI * (ELEMENTI + 1) / 2;
    Semaforo vuoto  = SEMAFORO_ZERO;
    Semaforo tre    = 3;

    printf("filiprova: la stessa coda, con due semafori e nessun lucchetto\n");

    /* Il contatore, senza nessuno che aspetti: tre prese riescono, la quarta no. */
    r = semaforo_prova(&tre) + semaforo_prova(&tre) + semaforo_prova(&tre);
    printf("  un semaforo da 3: prese riuscite %d, la quarta %s\n", r,
           semaforo_prova(&tre) ? "RIESCE (sbagliato)" : "no, ed e' giusto");
    if (r != 3) esito = 1;

    tid = thread_crea(filo_consuma_sem, 0);
    if (tid < 0) { printf("  thread_crea: errno %d\n", errno); return 1; }

    t0 = uptime_ms();
    for (i = 1; i <= ELEMENTI; i++) {
        semaforo_prendi(&s_posti);
        s_valore = i;
        semaforo_lascia(&s_roba);
    }
    thread_attendi(tid, &codice);
    dt = uptime_ms() - t0;

    printf("  elementi passati %5d   atteso %5d   %s\n",
           s_presi, ELEMENTI, s_presi == ELEMENTI ? "tutti" : "NE MANCA");
    printf("  somma            %5d   attesa %5d   %s\n",
           s_somma, atteso, s_somma == atteso ? "esatta" : "SBAGLIATA");
    printf("  %d passaggi in %u ms\n", ELEMENTI, dt);
    if (s_presi != ELEMENTI) esito = 1;
    if (s_somma != atteso)   esito = 1;

    /* La presa a vuoto: deve dormire fino alla scadenza e dirlo. */
    errno = 0;
    t0 = uptime_ms();
    r  = semaforo_prendi_ms(&vuoto, 200);
    dt = uptime_ms() - t0;
    printf("  presa di un semaforo a zero, scadenza 200 ms: rende %d errno %d "
           "dopo %u ms  %s\n", r, errno, dt,
           (r == -1 && errno == ETIMEDOUT && dt >= 150 && dt < 900)
               ? "ha dormito e l'ha detto" : "SBAGLIATO");
    if (r != -1 || errno != ETIMEDOUT || dt < 150 || dt >= 900) esito = 1;

    printf("\nfiliprova semaforo: %s\n",
           esito ? "QUALCOSA NON VA" : "tutto a posto");
    return esito;
}

/* =============================================================================
 * LA PROVA DELLA CANCELLAZIONE ORDINATA — tre casi, e il terzo e' quello vero
 *
 * ! OGNI CASO HA UNA VIA D'USCITA CHE NON E' QUELLA GIUSTA, ed e' l'unico modo
 * di far FALLIRE una prova che altrimenti si limiterebbe a non finire. Un filo
 * che non si ferma quando glielo si chiede lascia thread_attendi bloccato per
 * sempre: la macchina resta li' e non c'e' niente da leggere. Percio' ogni filo
 * di questa prova ha anche una scadenza sua, e ESCE CON UN CODICE DIVERSO —
 *
 *     0 = mi hanno chiesto di fermarmi e mi sono fermato
 *     1 = non me l'ha chiesto nessuno, me ne sono andato per stanchezza
 *
 * — cosi' la differenza fra «funziona» e «non funziona» e' un numero stampato,
 * non un prompt che non torna.
 *
 *   1. UN FILO CHE LAVORA. Tre fili che contano in cerchio guardando il
 *      messaggio a ogni giro: devono uscire tutti con 0, e in fretta.
 *   2. UN FILO CHE DORME dentro una condizione che nessuno segnalera' mai. Qui
 *      il messaggio da solo non basterebbe: chi dorme non guarda niente, e
 *      senza la scrollata del kernel resterebbe fermo fino alla scadenza.
 *   3. LA CORSA FRA «GUARDO» E «MI ADDORMENTO», venti volte di fila con
 *      l'attimo spostato ogni volta. E' la finestra che la parola `scuoti`
 *      esiste per chiudere: se la richiesta arriva fra l'occhiata e il sonno,
 *      il filo dorme DOPO aver guardato e la scrollata deve raccoglierla la
 *      sua prossima attesa. Un solo caso perso costa un secondo di scadenza,
 *      e venti giri che ne costano meno di uno dicono che non se ne perde.
 * ============================================================================= */
#define FERMA_SCADENZA  3000        /* la stanchezza: il filo esce da solo */
#define FERMA_GIRI      20          /* quante volte si prova la corsa */

static Mutex        f_lucchetto = MUTEX_LIBERO;
static Condizione   f_mai       = CONDIZIONE_ZERO;  /* nessuno la segnala mai */
static volatile int f_lavoro;

static void filo_lavora(void *arg)
{
    unsigned t0 = uptime_ms();

    (void)arg;
    while (!thread_devo_fermarmi()) {
        mutex_prendi(&f_lucchetto);
        f_lavoro++;
        mutex_lascia(&f_lucchetto);
        sched_yield();
        if (uptime_ms() - t0 >= FERMA_SCADENZA) thread_esci(1);
    }
    thread_esci(0);
}

static void filo_dorme(void *arg)
{
    (void)arg;
    mutex_prendi(&f_lucchetto);
    while (!thread_devo_fermarmi())
        condizione_aspetta_ms(&f_mai, &f_lucchetto, FERMA_SCADENZA);
    mutex_lascia(&f_lucchetto);      /* si esce PULITI: il lucchetto si lascia */
    thread_esci(0);
}

/* Come filo_dorme, ma la richiesta arriva DENTRO la finestra: se la scrollata
 * si perde, questo filo costa un secondo intero, e venti di questi si vedono
 * nel cronometro senza doverli cercare. */
static volatile int f_guardato;   /* il filo ha guardato ed e' li' li' per dormire */

static void filo_corsa(void *arg)
{
    (void)arg;
    mutex_prendi(&f_lucchetto);
    while (!thread_devo_fermarmi()) {
        /* ! LA FINESTRA SI ALLARGA A COMANDO, ed e' l'unico modo di provarla.
         * Fra l'occhiata qui sopra e il sonno qui sotto ci puo' stare
         * qualunque cosa, ma colpirla per caso vuol dire una prova che
         * dipende dalla fortuna — e infatti, provando a creare i fili e a
         * fermarli subito, non ci si cascava MAI: o la richiesta arrivava
         * prima che il filo guardasse, o a filo gia' addormentato, cioe'
         * sempre in uno dei due casi facili. Cedendo la CPU proprio li' in
         * mezzo, la richiesta cade sempre dentro la finestra. */
        f_guardato = 1;
        sched_yield();
        condizione_aspetta_ms(&f_mai, &f_lucchetto, 1000);
    }
    mutex_lascia(&f_lucchetto);
    thread_esci(0);
}

static int prova_ferma(void)
{
    int      tid[3], i, codice, esito = 0;
    unsigned t0, dt;

    printf("filiprova: fermare un filo e' CHIEDERGLIELO\n");

    /* 1. Tre fili che lavorano. */
    for (i = 0; i < 3; i++) {
        tid[i] = thread_crea(filo_lavora, 0);
        if (tid[i] < 0) { printf("  thread_crea: errno %d\n", errno); return 1; }
    }
    usleep(50000);                              /* lasciali lavorare un po' */

    t0 = uptime_ms();
    for (i = 0; i < 3; i++) {
        if (thread_ferma(tid[i]) != 0) {
            printf("  thread_ferma %d: errno %d\n", tid[i], errno);
            esito = 1;
        }
    }
    for (i = 0; i < 3; i++) {
        codice = -1;
        thread_attendi(tid[i], &codice);
        if (codice != 0) {
            printf("  il filo %d e' uscito con %d: non si e' fermato perche' "
                   "gliel'ho chiesto\n", tid[i], codice);
            esito = 1;
        }
    }
    dt = uptime_ms() - t0;
    printf("  tre fili che lavorano: fermati in %u ms, %d giri fatti   %s\n",
           dt, f_lavoro, (dt < 500 && f_lavoro > 0) ? "subito" : "SBAGLIATO");
    if (dt >= 500 || f_lavoro <= 0) esito = 1;

    /* 2. Un filo che dorme su una condizione che nessuno segnalera' mai. */
    tid[0] = thread_crea(filo_dorme, 0);
    if (tid[0] < 0) { printf("  thread_crea: errno %d\n", errno); return 1; }
    usleep(100000);                             /* il tempo di addormentarsi */

    t0 = uptime_ms();
    thread_ferma(tid[0]);
    codice = -1;
    thread_attendi(tid[0], &codice);
    dt = uptime_ms() - t0;
    printf("  un filo che DORME: sveglio e uscito in %u ms   %s\n", dt,
           dt < 500 ? "il kernel l'ha scrollato" : "HA DORMITO FINO ALLA SCADENZA");
    if (dt >= 500) esito = 1;

    /* 3. La corsa: si chiede di fermarsi mentre il filo sta per dormire. */
    t0 = uptime_ms();
    for (i = 0; i < FERMA_GIRI; i++) {
        int t;

        f_guardato = 0;
        t = thread_crea(filo_corsa, 0);
        if (t < 0) { printf("  thread_crea: errno %d\n", errno); return 1; }

        /* Si aspetta che il filo ABBIA GUARDATO e non dorma ancora: da qui in
         * poi ogni richiesta cade dentro la finestra, e non per fortuna. */
        while (!f_guardato) sched_yield();

        thread_ferma(t);
        codice = -1;
        thread_attendi(t, &codice);
        if (codice != 0) esito = 1;
    }
    dt = uptime_ms() - t0;
    printf("  %d corse fra «guardo» e «mi addormento»: %u ms   %s\n",
           FERMA_GIRI, dt,
           dt < 1000 ? "nessuna scrollata persa"
                     : "UNA SCROLLATA PERSA (e' costata una scadenza)");
    if (dt >= 1000) esito = 1;

    /* Un tid che non e' del gruppo non si ferma. */
    if (thread_ferma(999999) != -1 || errno != ESRCH) {
        printf("  fermare un tid inesistente doveva dare ESRCH, ha dato "
               "errno %d\n", errno);
        esito = 1;
    }

    printf("\nfiliprova ferma: %s\n", esito ? "QUALCOSA NON VA" : "tutto a posto");
    return esito;
}

/* =============================================================================
 * LA PROVA CHE LA DIRECTORY E L'AMBIENTE SIANO DEL GRUPPO
 *
 * ! DUE FILI SONO UN PROGRAMMA SOLO, e questa e' la prova che il kernel la
 * pensi allo stesso modo. Un `cd` fatto da un filo che gli altri non vedono
 * non e' una comodita' in meno: e' una funzione che, chiamata da un filo
 * diverso, apre un file in un posto diverso — senza errore.
 *
 * ! SI GUARDA NEI DUE VERSI, perche' uno solo non basterebbe: il filo cambia e
 * il principale se ne accorge, POI il principale cambia e il filo se ne
 * accorge. Con la directory copiata alla creazione il secondo verso passerebbe
 * lo stesso finche' il filo nasce dopo il cambio — e' il primo verso quello
 * che distingue «condivisa» da «copiata al momento giusto».
 *
 * ! E L'AMBIENTE NON PASSA DAL KERNEL: `environ` sta nei dati di libc.so, che
 * i fili condividono perche' condividono la memoria. Qui non c'e' niente da
 * aggiustare — c'e' da CONTROLLARE che sia davvero cosi', che e' un'altra
 * cosa dal darlo per buono.
 * ============================================================================= */
static volatile int c_passo;       /* il testimone fra i due */
static char         c_visto[256];  /* quel che il filo ha visto */
static const char  *c_env_visto;

static void filo_cwd(void *arg)
{
    (void)arg;

    /* 1. il filo si sposta, e il principale deve accorgersene */
    if (chdir("/bin") != 0) { c_passo = -1; thread_esci(1); }
    c_passo = 1;

    /* 2. aspetta che il principale si sposti, e deve accorgersene lui */
    while (c_passo != 2) sched_yield();
    getcwd(c_visto, sizeof(c_visto));
    c_env_visto = getenv("FILOPROVA");
    setenv("FILODICE", "riposto", 1);
    c_passo = 3;
    thread_esci(0);
}

static int prova_cwd(void)
{
    char  dove[256];
    int   tid, codice = -1, esito = 0;
    const char *v;

    printf("filiprova: la directory e l'ambiente sono del GRUPPO\n");

    chdir("/");
    setenv("FILOPROVA", "dal-principale", 1);

    tid = thread_crea(filo_cwd, 0);
    if (tid < 0) { printf("  thread_crea: errno %d\n", errno); return 1; }

    while (c_passo == 0) sched_yield();
    if (c_passo < 0) { printf("  il filo non e' riuscito a fare chdir\n"); return 1; }

    /* Verso 1: il filo ha fatto cd /bin, e lo deve vedere il principale. */
    dove[0] = 0;
    getcwd(dove, sizeof(dove));
    printf("  il filo fa cd /bin, il principale ci trova «%s»   %s\n", dove,
           strcmp(dove, "/bin") == 0 ? "condivisa" : "COPIATA (non la vede)");
    if (strcmp(dove, "/bin") != 0) esito = 1;

    /* Verso 2: adesso si sposta il principale, e lo deve vedere il filo. */
    chdir("/dev");
    c_passo = 2;
    while (c_passo != 3) sched_yield();
    thread_attendi(tid, &codice);

    printf("  il principale fa cd /dev, il filo ci trova «%s»   %s\n", c_visto,
           strcmp(c_visto, "/dev") == 0 ? "condivisa" : "COPIATA (non la vede)");
    if (strcmp(c_visto, "/dev") != 0) esito = 1;

    printf("  l'ambiente del principale, letto dal filo: «%s»   %s\n",
           c_env_visto ? c_env_visto : "(niente)",
           (c_env_visto && strcmp(c_env_visto, "dal-principale") == 0)
               ? "in comune" : "NON LO VEDE");
    if (!c_env_visto || strcmp(c_env_visto, "dal-principale") != 0) esito = 1;

    v = getenv("FILODICE");
    printf("  e quel che il filo ci ha messo, letto dal principale: «%s»   %s\n",
           v ? v : "(niente)",
           (v && strcmp(v, "riposto") == 0) ? "in comune" : "NON LO VEDE");
    if (!v || strcmp(v, "riposto") != 0) esito = 1;

    chdir("/");
    printf("\nfiliprova cwd: %s\n", esito ? "QUALCOSA NON VA" : "tutto a posto");
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
    if (argc > 1 && strcmp(argv[1], "condizione") == 0) return prova_condizione();
    if (argc > 1 && strcmp(argv[1], "semaforo") == 0)  return prova_semaforo();
    if (argc > 1 && strcmp(argv[1], "ferma") == 0)     return prova_ferma();
    if (argc > 1 && strcmp(argv[1], "cwd") == 0)       return prova_cwd();
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
