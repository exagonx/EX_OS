/* =============================================================================
 * kernel/ipc/pipe.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Le pipe. Il perche' e le tre regole di confine stanno in
 * kernel/include/pipe.h: qui c'e' il come.
 *
 * -----------------------------------------------------------------------------
 * ! LA RACE DEL RISVEGLIO PERDUTO, e come la si evita QUI
 *
 * E' lo stesso guasto che nel luglio 2026 teneva bloccata la shell al
 * prompt (vedi drivers/tty/tty.c): la sequenza
 *
 *     p->pid_lettore = mio_pid;
 *     sched_block(PROC_BLOCKED);
 *
 * con gli interrupt abilitati e' rotta. Se l'altro processo scrive nella
 * finestra fra le due righe, chiama sched_unblock() su un processo ancora
 * RUNNING — che e' un no-op — e azzera il campo. Subito dopo sched_block()
 * mette il processo in BLOCKED: la sveglia e' gia' stata consumata e
 * nessuno la emettera' piu'.
 *
 * ! Qui e' peggio che con la tastiera, perche' chi sveglia non e' un
 * interrupt ma un ALTRO PROCESSO: la finestra non e' di microsecondi, e'
 * lunga quanto un quanto di scheduling.
 *
 * La regola, applicata a ogni attesa di questo file:
 *
 *     interrupts_disable();
 *     ricontrolla la condizione;      <- a interrupt disabilitati
 *     se e' cambiata: riabilita ed esci
 *     registra il pid in attesa;
 *     sched_block();                  <- riabilita IF al risveglio
 *
 * -----------------------------------------------------------------------------
 * ! IL BUFFER DI RIMBALZO, e perche' non se ne puo' fare a meno
 *
 * Il buffer UTENTE puo' stare su una pagina non ancora presente (EX-OS
 * carica su richiesta, vedi kernel/loader/elf.c): toccarla con `cli` in
 * mano vorrebbe dire un page fault a interrupt disabilitati. Quindi la
 * memoria utente si tocca SOLO fuori dalla sezione critica.
 *
 * Ma spostare gli indici del buffer circolare fuori dalla sezione critica
 * e' altrettanto sbagliato, e la prima stesura di questo file lo faceva:
 *
 *   - in scrittura, alzare `quanti` prima di aver copiato i byte espone
 *     al lettore memoria non ancora scritta;
 *   - in lettura, avanzare `testa` prima di aver copiato via i byte
 *     lascia uno scrittore libero di sovrascriverli mentre li leggiamo.
 *
 * Nessuno dei due da' un errore: danno byte sbagliati, ogni tanto, sotto
 * carico. Da qui il rimbalzo, che separa i due vincoli:
 *
 *   SCRITTURA:  utente -> appoggio  (IF on)
 *               appoggio -> anello  (IF off, indici coerenti)
 *   LETTURA:    anello -> appoggio  (IF off, indici coerenti)
 *               appoggio -> utente  (IF on)
 *
 * L'appoggio sta sullo stack del kernel, che e' di 128 KB per processo:
 * PIPE_TRANCHE byte per chiamata non sono un problema, e ogni chiamata ha
 * il suo, quindi non serve nessun lucchetto in piu'.
 * ============================================================================= */

#include "kernel.h"
#include "pipe.h"
#include "sched.h"
#include "syscall.h"
#include "kmalloc.h"

void *memset(void *dst, int c, uint32_t n);

/* =============================================================================
 * ! EVERY WAITER IS REMEMBERED (kernel 0.313, fork, voce 33)
 *
 * Until 0.312 a pipe remembered ONE reader and ONE writer waiting: the last
 * one to block took the place of the one before, who was never woken again.
 * Without fork two processes on the same end of a pipe were rare (the threads
 * of one program); with fork they are the normal case - the 300 children of
 * tools/prova-fork waiting on one pipe stayed blocked for ever, as would the
 * children of a prefork server reading its "pipe of death". Now the first
 * waiter is kept inline (the usual case costs nothing) and the others in an
 * array that doubles when full: no ceiling. A change wakes ALL the waiters of
 * that side; each one looks again at the pipe (they all loop) and the ones
 * that find nothing go back to sleep.
 *
 * ! THE ARRAY GROWS WITH INTERRUPTS ON (attese_cresci): a reader that finds no
 * room enables interrupts, allocates, and goes round the loop again - so the
 * condition is checked once more with interrupts off before it blocks.
 * ============================================================================= */
typedef struct {
    uint32_t  uno;          /* the first waiter, 0 = none */
    uint32_t *altri;        /* the others (kmalloc), kept across reuse */
    uint32_t  n, posti;
} Attese;

static int attese_c_e(const Attese *a, uint32_t pid)
{
    uint32_t i;

    if (a->uno == pid) return 1;
    for (i = 0; i < a->n; i++) if (a->altri[i] == pid) return 1;
    return 0;
}

/* 1 = registered (or already there), 0 = no room: attese_cresci, then again */
static int attese_metti(Attese *a, uint32_t pid)
{
    if (attese_c_e(a, pid)) return 1;
    if (a->uno == 0) { a->uno = pid; return 1; }
    if (a->n >= a->posti) return 0;
    a->altri[a->n++] = pid;
    return 1;
}

static void attese_togli(Attese *a, uint32_t pid)
{
    uint32_t i;

    if (a->uno == pid) {
        a->uno = a->n ? a->altri[--a->n] : 0;
        return;
    }
    for (i = 0; i < a->n; i++)
        if (a->altri[i] == pid) { a->altri[i] = a->altri[--a->n]; return; }
}

/* Interrupts off. Everyone waiting on that side is woken and forgotten. */
static void attese_sveglia_locked(Attese *a)
{
    uint32_t i;

    if (a->uno) sched_unblock_locked(a->uno);
    for (i = 0; i < a->n; i++) sched_unblock_locked(a->altri[i]);
    a->uno = 0;
    a->n   = 0;
}

/* Interrupts ON. Doubles the array of `a` (the pipe is never freed, so `a`
 * stays valid). Returns 0 out of memory. */
static int attese_cresci(Attese *a)
{
    uint32_t  posti, i, *nuovo, *vecchio = NULL;

    interrupts_disable();
    posti = a->posti ? a->posti * 2u : 8u;
    interrupts_enable();
    nuovo = (uint32_t *)kmalloc(posti * sizeof(uint32_t));
    if (nuovo == NULL) return 0;
    interrupts_disable();
    if (a->posti < posti) {
        for (i = 0; i < a->n; i++) nuovo[i] = a->altri[i];
        vecchio  = a->altri;
        a->altri = nuovo;
        a->posti = posti;
        nuovo    = NULL;
    }
    interrupts_enable();
    if (nuovo)   kfree(nuovo);          /* somebody else grew it meanwhile */
    if (vecchio) kfree(vecchio);
    return 1;
}

typedef struct {
    int       usata;
    uint32_t  testa;        /* prossimo byte da LEGGERE */
    uint32_t  quanti;       /* byte presenti nel buffer */
    uint32_t  lettori;      /* estremita' di lettura aperte */
    uint32_t  scrittori;    /* estremita' di scrittura aperte */
    Attese    att_lettori;  /* chi aspetta dati */
    Attese    att_scrittori;/* chi aspetta spazio */
    uint8_t   buf[PIPE_DIM];
} Pipe;

/* THE PIPES ARE AS MANY AS ARE NEEDED (kernel 0.282). There were 16 in the
 * whole system: a web server running CGI programs (three pipes each) had
 * five at a time (the user's rule, 28 October 2026: no fixed ceilings). A
 * pipe is kmalloc'd the first time every existing one is in use, and is
 * reused afterwards, never freed: the handle stays an index, and a reader
 * that looked it up keeps valid memory. The directory doubles when full and
 * the old one is left in place for the same reason. */
static Pipe   **volatile g_pipe_dir;
static uint32_t          g_pipe_dir_posti;
static volatile uint32_t g_pipe_n;

/* Una pipe senza piu' nessuna delle due estremita' aperte non serve a
 * niente e il suo posto in tabella deve tornare disponibile: e' l'unico
 * punto in cui una pipe smette di esistere. */
static void pipe_libera_se_orfana(Pipe *p)
{
    if (p->lettori == 0 && p->scrittori == 0) {
        p->usata  = 0;
        p->testa  = 0;
        p->quanti = 0;
        p->att_lettori.uno = p->att_scrittori.uno = 0;
        p->att_lettori.n   = p->att_scrittori.n   = 0;
    }
}

static Pipe *pipe_da_handle(int h)
{
    if (h < 0 || (uint32_t)h >= g_pipe_n) return NULL;
    if (!g_pipe_dir[h]->usata)            return NULL;
    return g_pipe_dir[h];
}

int pipe_crea(void)
{
    int h;

    for (;;) {
        Pipe  *nuova;
        Pipe **dir = NULL, **scarto = NULL;

        interrupts_disable();
        for (h = 0; h < (int)g_pipe_n; h++) {
            Pipe *p = g_pipe_dir[h];

            if (!p->usata) {
                p->usata     = 1;
                p->testa     = 0;
                p->quanti    = 0;
                /* Nasce con UN lettore e UNO scrittore: sono i due descrittori
                 * che sys_pipe sta per consegnare. Contarli dopo lascerebbe
                 * una finestra in cui la pipe sembra orfana. */
                p->lettori   = 1;
                p->scrittori = 1;
                p->att_lettori.uno = p->att_scrittori.uno = 0;
                p->att_lettori.n   = p->att_scrittori.n   = 0;
                interrupts_enable();
                return h;
            }
        }
        interrupts_enable();

        /* all in use: one more, allocated with interrupts on */
        nuova = (Pipe *)kmalloc(sizeof(Pipe));
        if (nuova == NULL) break;
        memset(nuova, 0, sizeof(Pipe) - PIPE_DIM);   /* no waiters, no arrays */
        nuova->usata = 0;
        if (g_pipe_n >= g_pipe_dir_posti) {
            uint32_t posti = g_pipe_dir_posti ? g_pipe_dir_posti * 2u : 16u;

            dir = (Pipe **)kmalloc(posti * sizeof(Pipe *));
            if (dir == NULL) { kfree(nuova); break; }
            interrupts_disable();
            if (g_pipe_n >= g_pipe_dir_posti) {
                uint32_t i;

                for (i = 0; i < g_pipe_n; i++) dir[i] = g_pipe_dir[i];
                g_pipe_dir       = dir;     /* the old one stays: see above */
                g_pipe_dir_posti = posti;
            } else {
                scarto = dir;               /* somebody else grew it */
            }
        } else {
            interrupts_disable();
        }
        if (g_pipe_n < g_pipe_dir_posti) {
            g_pipe_dir[g_pipe_n] = nuova;
            g_pipe_n++;
            nuova = NULL;
        }
        interrupts_enable();
        if (scarto) kfree(scarto);
        if (nuova)  kfree(nuova);
    }

    klog(LOG_WARN, "PIPE: niente memoria per un'altra pipe (%u in tutto)",
         (unsigned)g_pipe_n);
    return -ENFILE;
}

void pipe_apri_lettore(int h)
{
    Pipe *p;
    interrupts_disable();
    p = pipe_da_handle(h);
    if (p) p->lettori++;
    interrupts_enable();
}

void pipe_apri_scrittore(int h)
{
    Pipe *p;
    interrupts_disable();
    p = pipe_da_handle(h);
    if (p) p->scrittori++;
    interrupts_enable();
}

/* ! CHIUDERE UN'ESTREMITA' E' ANCHE UN RISVEGLIO. Chi dorme dall'altra
 * parte sta aspettando qualcosa che ora non arrivera' mai: va svegliato
 * per andare a leggere il conteggio e scoprire che e' finita. Senza
 * questo, `cmd1 | cmd2` con cmd1 che esce lascia cmd2 bloccato per
 * sempre. */
void pipe_chiudi_lettore_locked(int h)
{
    Pipe *p = pipe_da_handle(h);

    if (p == NULL) return;

    if (p->lettori > 0) p->lettori--;
    if (p->lettori == 0)
        attese_sveglia_locked(&p->att_scrittori);   /* si sveglieranno e prenderanno EPIPE */
    pipe_libera_se_orfana(p);
}

void pipe_chiudi_scrittore_locked(int h)
{
    Pipe *p = pipe_da_handle(h);

    if (p == NULL) return;

    if (p->scrittori > 0) p->scrittori--;
    if (p->scrittori == 0)
        attese_sveglia_locked(&p->att_lettori);     /* si sveglieranno e vedranno la fine */
    pipe_libera_se_orfana(p);
}

void pipe_chiudi_lettore(int h)
{
    interrupts_disable();
    pipe_chiudi_lettore_locked(h);
    interrupts_enable();
}

void pipe_chiudi_scrittore(int h)
{
    interrupts_disable();
    pipe_chiudi_scrittore_locked(h);
    interrupts_enable();
}

int32_t pipe_disponibili(int h)
{
    Pipe    *p;
    int32_t  n;

    interrupts_disable();
    p = pipe_da_handle(h);
    n = p ? (int32_t)p->quanti : -EBADF;
    interrupts_enable();
    return n;
}

/* =============================================================================
 * Per poll() — vedi il commento in kernel/include/pipe.h
 * ========================================================================== */
int32_t pipe_stato_locked(int h, uint32_t *quanti, uint32_t *spazio,
                          uint32_t *lettori, uint32_t *scrittori)
{
    Pipe *p = pipe_da_handle(h);

    if (p == NULL) return -EBADF;

    if (quanti)    *quanti    = p->quanti;
    if (spazio)    *spazio    = PIPE_DIM - p->quanti;
    if (lettori)   *lettori   = p->lettori;
    if (scrittori) *scrittori = p->scrittori;
    return 0;
}

/* poll(): interrupts are off here, so the array cannot grow. Without room
 * (more than 1 + posti pollers on one end at once) it says 0 and poll falls
 * back to looking again every tick, as when the place was taken until 0.312;
 * the next blocking read/write on that end makes the room. */
int pipe_attesa_registra_locked(int h, int per_lettura, uint32_t pid)
{
    Pipe *p = pipe_da_handle(h);

    if (p == NULL) return 0;
    return attese_metti(per_lettura ? &p->att_lettori : &p->att_scrittori, pid);
}

void pipe_attesa_togli_locked(int h, int per_lettura, uint32_t pid)
{
    Pipe *p = pipe_da_handle(h);

    if (p == NULL) return;
    attese_togli(per_lettura ? &p->att_lettori : &p->att_scrittori, pid);
}

int32_t pipe_leggi(int h, void *buf, uint32_t n)
{
    Pipe     *p;
    uint8_t  *dst = (uint8_t *)buf;
    uint8_t   appoggio[PIPE_TRANCHE];
    uint32_t  presi = 0, i;

    if (n == 0) return 0;
    if (n > PIPE_TRANCHE) n = PIPE_TRANCHE;

    for (;;) {
        Process *cur;

        interrupts_disable();
        p = pipe_da_handle(h);
        if (p == NULL) { interrupts_enable(); return -EBADF; }

        if (p->quanti > 0) break;               /* c'e' roba: si esce col cli */

        /* ! VUOTA E SENZA SCRITTORI = FINE DEI DATI. Vuota CON scrittori =
         * si aspetta. E' la distinzione su cui si regge tutto. */
        if (p->scrittori == 0) { interrupts_enable(); return 0; }

        cur = proc_get_current();
        if (cur == NULL) { interrupts_enable(); return -EAGAIN; }

        if (!attese_metti(&p->att_lettori, cur->pid)) {
            Attese *a = &p->att_lettori;

            interrupts_enable();
            if (!attese_cresci(a)) return -ENOMEM;
            continue;                           /* and check again, IF off */
        }
        sched_block(PROC_BLOCKED);              /* riabilita IF al risveglio */

        /* ! CHI E' STATO INTERROTTO NON TORNA AD ASPETTARE. Senza questo, il
         * risveglio di SYS_INTERROMPI farebbe un giro del ciclo, troverebbe la
         * pipe ancora vuota e si riaddormenterebbe: il processo non tornerebbe
         * mai in ring 3, cioe' non passerebbe mai dal punto in cui si muore. */
        if (proc_interrotto()) return ERR(EINTR);
    }

    /* Qui: interrupt disabilitati, p valido, p->quanti > 0.
     * ! La copia dall'anello avviene ADESSO, non dopo: uno scrittore
     * potrebbe sovrascrivere questi byte appena rilasciamo il lucchetto. */
    presi = (n < p->quanti) ? n : p->quanti;
    for (i = 0; i < presi; i++) {
        appoggio[i] = p->buf[(p->testa + i) % PIPE_DIM];
    }
    p->testa   = (p->testa + presi) % PIPE_DIM;
    p->quanti -= presi;

    attese_sveglia_locked(&p->att_scrittori);   /* si e' liberato spazio */
    interrupts_enable();

    /* La memoria UTENTE si tocca solo qui, a interrupt abilitati: puo'
     * essere su una pagina non ancora presente. */
    for (i = 0; i < presi; i++) dst[i] = appoggio[i];

    return (int32_t)presi;
}

int32_t pipe_scrivi(int h, const void *buf, uint32_t n)
{
    Pipe          *p;
    const uint8_t *src = (const uint8_t *)buf;
    uint8_t        appoggio[PIPE_TRANCHE];
    uint32_t       messi = 0, i, inizio, spazio;

    if (n == 0) return 0;
    if (n > PIPE_TRANCHE) n = PIPE_TRANCHE;

    /* Si legge dalla memoria utente PRIMA di prendere il lucchetto, per lo
     * stesso motivo per cui in lettura si scrive dopo averlo rilasciato. */
    for (i = 0; i < n; i++) appoggio[i] = src[i];

    for (;;) {
        Process *cur;

        interrupts_disable();
        p = pipe_da_handle(h);
        if (p == NULL) { interrupts_enable(); return -EBADF; }

        /* ! NESSUN LETTORE = EPIPE, non attesa: quei byte non li leggera'
         * nessuno mai. Su Unix arriverebbe anche SIGPIPE; EX-OS non ha i
         * segnali, quindi chi non controlla il ritorno di write() non se
         * ne accorge. Limite noto. */
        if (p->lettori == 0) { interrupts_enable(); return -EPIPE; }

        if (p->quanti < PIPE_DIM) break;        /* c'e' spazio: si esce col cli */

        cur = proc_get_current();
        if (cur == NULL) { interrupts_enable(); return -EAGAIN; }

        if (!attese_metti(&p->att_scrittori, cur->pid)) {
            Attese *a = &p->att_scrittori;

            interrupts_enable();
            if (!attese_cresci(a)) return -ENOMEM;
            continue;
        }
        sched_block(PROC_BLOCKED);
        if (proc_interrotto()) return ERR(EINTR);   /* vedi pipe_leggi */
    }

    /* Qui: interrupt disabilitati, p valido, c'e' spazio.
     * ! LA SCRITTURA PUO' ESSERE PARZIALE, ed e' corretto: write() ritorna
     * quanti byte ha preso e il chiamante richiama. Aspettare di poterli
     * scrivere tutti bloccherebbe per sempre su una scrittura piu' grande
     * del buffer, con il lettore che aspetta i primi byte.
     *
     * ! I byte entrano nell'anello PRIMA che `quanti` salga: alzare il
     * contatore per primo mostrerebbe al lettore memoria non ancora
     * scritta. */
    spazio = PIPE_DIM - p->quanti;
    messi  = (n < spazio) ? n : spazio;
    inizio = (p->testa + p->quanti) % PIPE_DIM;
    for (i = 0; i < messi; i++) {
        p->buf[(inizio + i) % PIPE_DIM] = appoggio[i];
    }
    p->quanti += messi;

    attese_sveglia_locked(&p->att_lettori);     /* sono arrivati dati */
    interrupts_enable();

    return (int32_t)messi;
}
