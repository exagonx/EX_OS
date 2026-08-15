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

typedef struct {
    int       usata;
    uint32_t  testa;        /* prossimo byte da LEGGERE */
    uint32_t  quanti;       /* byte presenti nel buffer */
    uint32_t  lettori;      /* estremita' di lettura aperte */
    uint32_t  scrittori;    /* estremita' di scrittura aperte */
    uint32_t  pid_lettore;  /* chi aspetta dati (0 = nessuno) */
    uint32_t  pid_scrittore;/* chi aspetta spazio (0 = nessuno) */
    uint8_t   buf[PIPE_DIM];
} Pipe;

static Pipe g_pipe[PIPE_MAX];

/* Una pipe senza piu' nessuna delle due estremita' aperte non serve a
 * niente e il suo posto in tabella deve tornare disponibile: e' l'unico
 * punto in cui una pipe smette di esistere. */
static void pipe_libera_se_orfana(Pipe *p)
{
    if (p->lettori == 0 && p->scrittori == 0) {
        p->usata  = 0;
        p->testa  = 0;
        p->quanti = 0;
        p->pid_lettore = p->pid_scrittore = 0;
    }
}

static Pipe *pipe_da_handle(int h)
{
    if (h < 0 || h >= PIPE_MAX) return NULL;
    if (!g_pipe[h].usata)       return NULL;
    return &g_pipe[h];
}

int pipe_crea(void)
{
    int h;

    interrupts_disable();
    for (h = 0; h < PIPE_MAX; h++) {
        if (!g_pipe[h].usata) {
            g_pipe[h].usata     = 1;
            g_pipe[h].testa     = 0;
            g_pipe[h].quanti    = 0;
            /* Nasce con UN lettore e UNO scrittore: sono i due descrittori
             * che sys_pipe sta per consegnare. Contarli dopo lascerebbe
             * una finestra in cui la pipe sembra orfana. */
            g_pipe[h].lettori   = 1;
            g_pipe[h].scrittori = 1;
            g_pipe[h].pid_lettore   = 0;
            g_pipe[h].pid_scrittore = 0;
            interrupts_enable();
            return h;
        }
    }
    interrupts_enable();

    klog(LOG_WARN, "PIPE: tabella piena (%d pipe)", PIPE_MAX);
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
    if (p->lettori == 0 && p->pid_scrittore != 0) {
        uint32_t s = p->pid_scrittore;
        p->pid_scrittore = 0;
        sched_unblock_locked(s);          /* si svegliera' e prendera' EPIPE */
    }
    pipe_libera_se_orfana(p);
}

void pipe_chiudi_scrittore_locked(int h)
{
    Pipe *p = pipe_da_handle(h);

    if (p == NULL) return;

    if (p->scrittori > 0) p->scrittori--;
    if (p->scrittori == 0 && p->pid_lettore != 0) {
        uint32_t s = p->pid_lettore;
        p->pid_lettore = 0;
        sched_unblock_locked(s);          /* si svegliera' e vedra' la fine */
    }
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

int pipe_attesa_registra_locked(int h, int per_lettura, uint32_t pid)
{
    Pipe     *p = pipe_da_handle(h);
    uint32_t *slot;

    if (p == NULL) return 0;

    slot = per_lettura ? &p->pid_lettore : &p->pid_scrittore;

    /* Libero, o gia' nostro da un giro precedente di poll: va bene. */
    if (*slot == 0 || *slot == pid) { *slot = pid; return 1; }

    return 0;                   /* occupato da un altro: chi chiama ripiega */
}

void pipe_attesa_togli_locked(int h, int per_lettura, uint32_t pid)
{
    Pipe     *p = pipe_da_handle(h);
    uint32_t *slot;

    if (p == NULL) return;

    slot = per_lettura ? &p->pid_lettore : &p->pid_scrittore;
    if (*slot == pid) *slot = 0;
}

int32_t pipe_leggi(int h, void *buf, uint32_t n)
{
    Pipe     *p;
    uint8_t  *dst = (uint8_t *)buf;
    uint8_t   appoggio[PIPE_TRANCHE];
    uint32_t  presi = 0, i;
    uint32_t  sveglia = 0;

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

        p->pid_lettore = cur->pid;
        sched_block(PROC_BLOCKED);              /* riabilita IF al risveglio */
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

    sveglia = p->pid_scrittore;                 /* si e' liberato spazio */
    p->pid_scrittore = 0;
    interrupts_enable();

    /* La memoria UTENTE si tocca solo qui, a interrupt abilitati: puo'
     * essere su una pagina non ancora presente. */
    for (i = 0; i < presi; i++) dst[i] = appoggio[i];

    if (sveglia) sched_unblock(sveglia);
    return (int32_t)presi;
}

int32_t pipe_scrivi(int h, const void *buf, uint32_t n)
{
    Pipe          *p;
    const uint8_t *src = (const uint8_t *)buf;
    uint8_t        appoggio[PIPE_TRANCHE];
    uint32_t       messi = 0, i, inizio, spazio;
    uint32_t       sveglia = 0;

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

        p->pid_scrittore = cur->pid;
        sched_block(PROC_BLOCKED);
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

    sveglia = p->pid_lettore;                   /* sono arrivati dati */
    p->pid_lettore = 0;
    interrupts_enable();

    if (sveglia) sched_unblock(sveglia);
    return (int32_t)messi;
}
