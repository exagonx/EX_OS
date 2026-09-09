/* =============================================================================
 * kernel/block/blkr3.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * UN DISCO SERVITO DA UN PROCESSO — l'attuazione. Il progetto, e soprattutto
 * il PERCHE', stanno in kernel/include/blkr3.h: qui c'e' il meccanismo.
 *
 * -----------------------------------------------------------------------------
 * ! IL GIRO, IN OTTO PASSI
 *
 *   1. un processo qualunque legge un file;  il VFS chiede un settore
 *   2. blk_read vede un dispositivo di tipo RING3 e chiama qui
 *   3. si prende la casella della richiesta (una per dispositivo)
 *   4. si sveglia il servente, se dormiva dentro blkr3_attendi()
 *   5. il chiamante DORME sul dispositivo — non sulla propria mailbox
 *   6. il servente fa il suo lavoro e chiama blkr3_risposta()
 *   7. il chiamante si sveglia, si porta via i settori, libera la casella
 *   8. chi era in coda per la casella se ne accorge al tick dopo
 *
 * -----------------------------------------------------------------------------
 * ! LA CODA PER LA CASELLA NON E' UNA CODA, ED E' VOLUTO
 *
 * Chi trova la casella occupata non si registra da nessuna parte: dorme un
 * tick e riguarda. E' lo stesso ripiego di poll_registra() in
 * syscall_impl.c — «chi non ha trovato posto si ricontrolla al tick dopo» —
 * e la ragione e' la stessa: tenere una lista di attesa vuol dire tenerla
 * anche quando un processo muore mentre e' in lista, e quel caso si sbaglia
 * molto piu' facilmente di un risveglio ogni dieci millisecondi.
 *
 * La contesa vera e' rara: le richieste a un disco arrivano in fila da un
 * filesystem, non in parallelo da mille processi.
 *
 * -----------------------------------------------------------------------------
 * ! GLI INTERRUPT SI SPENGONO A MANO, E QUI NON C'E' IL CONTATORE
 *
 * interrupts_disable()/enable() in questo kernel sono cli/sti grezzi (vedi la
 * nota in kernel/include/pipe.h): non si annidano. Ogni funzione di questo
 * file entra e esce dalla sezione critica in un posto solo, e sched_block()
 * riabilita le interruzioni da se' al risveglio — per questo dopo ogni
 * sched_block() si rispegne prima di riguardare lo stato.
 * ============================================================================= */

#include "kernel.h"
#include "blk.h"
#include "blkr3.h"
#include "sched.h"
#include "syscall.h"   /* gli errno del kernel stanno li' */

#define BLKR3_MAX_DEV   8

/* Lo stato della casella. */
#define CASELLA_LIBERA   0
#define CASELLA_PRONTA   1   /* c'e' una richiesta, il servente non l'ha presa */
#define CASELLA_IN_CORSO 2   /* il servente ce l'ha in mano */
#define CASELLA_SERVITA  3   /* la risposta c'e', il chiamante deve prenderla */

typedef struct {
    uint8_t  usato;
    uint32_t pid;             /* il servente */
    int      blkdev;          /* indice nello strato a blocchi */

    uint8_t  stato;           /* CASELLA_* */
    uint32_t op;
    uint64_t lba;
    uint32_t n;               /* settori */
    uint32_t pid_chiamante;
    int      esito;
    uint8_t  dati[BLKR3_BYTE_MAX];
} R3Dev;

static R3Dev g_r3[BLKR3_MAX_DEV];

/* Il servente che dorme dentro blkr3_attendi(), per poterlo svegliare senza
 * cercarlo. Zero = nessuno sta dormendo. */
static uint32_t g_dorme[BLKR3_MAX_DEV];

static void copia(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    uint32_t k;
    for (k = 0; k < n; k++) dst[k] = src[k];
}

/* Il dispositivo servito da `pid` che ha una richiesta nello stato voluto.
 * A interruzioni spente. */
static R3Dev *cerca_per_pid(uint32_t pid, int stato)
{
    int i;
    for (i = 0; i < BLKR3_MAX_DEV; i++) {
        if (!g_r3[i].usato || g_r3[i].pid != pid) continue;
        if (stato < 0 || g_r3[i].stato == (uint8_t)stato) return &g_r3[i];
    }
    return NULL;
}

static R3Dev *da_blkdev(int blkdev)
{
    int i;
    for (i = 0; i < BLKR3_MAX_DEV; i++)
        if (g_r3[i].usato && g_r3[i].blkdev == blkdev) return &g_r3[i];
    return NULL;
}

/* -----------------------------------------------------------------------------
 * Offrire un dispositivo
 * --------------------------------------------------------------------------- */

int blkr3_offri(uint32_t pid, const char *nome, uint64_t settori,
                uint32_t byte_settore, int sola_lettura)
{
    int i, blkdev;

    /* ! SOLO SETTORI DA 512. Tutto il resto del kernel — la cache di blk.c, i
     * filesystem, la tabella delle partizioni — e' scritto su questa misura, e
     * accettare qui un numero diverso vorrebbe dire scoprirlo molto piu' in
     * basso, dove il rimedio non c'e'. Una chiavetta da 4096 byte per settore
     * si presenta con la traduzione gia' fatta dal suo driver. */
    if (byte_settore != 512) return -EINVAL;
    if (settori == 0)        return -EINVAL;
    if (nome == NULL)        return -EINVAL;

    for (i = 0; i < BLKR3_MAX_DEV; i++) if (!g_r3[i].usato) break;
    if (i == BLKR3_MAX_DEV) return -ENOSPC;

    blkdev = blk_registra_ring3(nome, settori, sola_lettura);
    if (blkdev < 0) return blkdev;

    g_r3[i].usato   = 1;
    g_r3[i].pid     = pid;
    g_r3[i].blkdev  = blkdev;
    g_r3[i].stato   = CASELLA_LIBERA;
    g_dorme[i]      = 0;

    klog(LOG_INFO, "BLKR3: '%s' offerto dal PID %u, %u settori",
         nome, pid, (uint32_t)settori);
    return blkdev;
}

/* -----------------------------------------------------------------------------
 * Il lato del CHIAMANTE — chi sta leggendo un file
 * --------------------------------------------------------------------------- */

/* Un giro solo: al massimo BLKR3_SETTORI_MAX settori. Rende 0 o -errno. */
static int un_giro(R3Dev *d, uint32_t op, uint64_t lba, uint32_t n,
                   const uint8_t *scrivi, uint8_t *leggi)
{
    Process *me = proc_get_current();
    uint32_t scadenza;
    int      esito;

    if (me == NULL) return -EIO;

    /* ! IL SERVENTE NON PUO' ESSERE ANCHE IL CHIAMANTE. Un driver che apre un
     * file sul proprio dispositivo aspetterebbe la risposta che deve dare lui:
     * si fermerebbe per sempre, e il sintomo sarebbe «la macchina si e'
     * piantata montando la chiavetta». E' un errore di chi scrive il driver, e
     * va detto. */
    if (me->pid == d->pid) {
        klog(LOG_ERROR, "BLKR3: il PID %u legge il dispositivo che serve lui "
             "stesso: si aspetterebbe per sempre", me->pid);
        return -EDEADLK;
    }

    /* ! IL SERVENTE MORTO SI RICONOSCE SUBITO, NON DOPO CINQUE SECONDI.
     *
     * blkr3_processo_morto() la chiama proc_reap_zombie(), cioe' quando il
     * PADRE raccoglie il figlio — e un driver lanciato in secondo piano da una
     * shell che sta facendo altro puo' restare zombie per parecchio. In quella
     * finestra ogni richiesta aspettava la scadenza intera: dieci secondi per
     * due settori, su un dispositivo che non c'e' piu'.
     *
     * Visto succedere il 9 settembre 2026 nella prova di -vita: due
     * «non ha risposto in 5000 ms» di fila prima che la morte venisse
     * registrata. Il ritardo era corretto e inutile — il servente era gia'
     * andato, e si poteva saperlo guardando. */
    {
        Process *serv = proc_get_by_pid(d->pid);
        if (serv == NULL || serv->state == PROC_ZOMBIE ||
            serv->state == PROC_UNUSED)
            return -EIO;
    }

    scadenza = g_ticks + (BLKR3_SCADENZA_MS / 10);

    interrupts_disable();

    /* Passo 3: la casella. Chi la trova occupata riguarda al tick dopo. */
    while (d->stato != CASELLA_LIBERA) {
        if (g_ticks >= scadenza || !d->usato) { interrupts_enable(); return -EIO; }
        me->block_until = g_ticks + 1;
        sched_block(PROC_BLOCKED);
        interrupts_disable();
    }

    d->op            = op;
    d->lba           = lba;
    d->n             = n;
    d->pid_chiamante = me->pid;
    d->esito         = -EIO;
    if (op == BLKR3_SCRIVI && scrivi != NULL) copia(d->dati, scrivi, n * 512);
    d->stato = CASELLA_PRONTA;

    /* Passo 4: il servente, se dormiva. */
    {
        int i = (int)(d - g_r3);
        if (g_dorme[i] != 0) { sched_unblock_locked(g_dorme[i]); g_dorme[i] = 0; }
    }

    /* Passo 5: si dorme finche' la risposta non c'e'. La sveglia arriva da
     * blkr3_risposta(); la scadenza serve al caso in cui non arrivi mai. */
    while (d->stato != CASELLA_SERVITA) {
        if (!d->usato) { interrupts_enable(); return -EIO; }
        if (g_ticks >= scadenza) {
            klog(LOG_ERROR, "BLKR3: il PID %u non ha risposto in %d ms",
                 d->pid, BLKR3_SCADENZA_MS);
            d->stato = CASELLA_LIBERA;
            interrupts_enable();
            return -EIO;
        }
        me->block_until = scadenza;
        sched_block(PROC_BLOCKED);
        interrupts_disable();
    }

    /* Passo 7 */
    esito = d->esito;
    if (esito == 0 && op == BLKR3_LEGGI && leggi != NULL)
        copia(leggi, d->dati, n * 512);
    d->stato = CASELLA_LIBERA;

    interrupts_enable();
    return esito;
}

/* Spezza una richiesta lunga in giri da BLKR3_SETTORI_MAX. */
static int spezza(int i, uint32_t op, uint64_t lba, uint32_t n,
                  const uint8_t *scrivi, uint8_t *leggi)
{
    R3Dev *d = da_blkdev(i);
    uint32_t fatti = 0;

    if (d == NULL) return -ENODEV;

    while (fatti < n) {
        uint32_t q = n - fatti;
        int      r;

        if (q > BLKR3_SETTORI_MAX) q = BLKR3_SETTORI_MAX;
        r = un_giro(d, op, lba + fatti, q,
                    scrivi ? scrivi + fatti * 512 : NULL,
                    leggi  ? leggi  + fatti * 512 : NULL);
        if (r != 0) return r;
        fatti += q;
    }
    return 0;
}

int blkr3_read(int i, uint64_t lba, uint32_t n, void *buf)
{
    return spezza(i, BLKR3_LEGGI, lba, n, NULL, (uint8_t *)buf);
}

int blkr3_write(int i, uint64_t lba, uint32_t n, const void *buf)
{
    return spezza(i, BLKR3_SCRIVI, lba, n, (const uint8_t *)buf, NULL);
}

int blkr3_flush(int i)
{
    R3Dev *d = da_blkdev(i);
    if (d == NULL) return -ENODEV;
    return un_giro(d, BLKR3_SVUOTA, 0, 0, NULL, NULL);
}

/* -----------------------------------------------------------------------------
 * Il lato del SERVENTE — il driver in ring 3
 * --------------------------------------------------------------------------- */

int blkr3_attendi(uint32_t pid, uint32_t *op, uint64_t *lba, uint32_t *n,
                  uint32_t *quale, uint32_t scadenza_ms)
{
    Process *me = proc_get_current();
    R3Dev   *d;
    uint32_t scadenza;
    int      i;

    if (me == NULL || me->pid != pid) return -EPERM;
    if (cerca_per_pid(pid, -1) == NULL) return -ENODEV;

    scadenza = (scadenza_ms == 0) ? 0 : g_ticks + (scadenza_ms / 10);

    interrupts_disable();

    for (;;) {
        d = cerca_per_pid(pid, CASELLA_PRONTA);
        if (d != NULL) break;

        if (cerca_per_pid(pid, -1) == NULL) { interrupts_enable(); return -ENODEV; }
        if (scadenza != 0 && g_ticks >= scadenza) {
            interrupts_enable();
            return -ETIMEDOUT;
        }

        /* ! CI SI DICHIARA ADDORMENTATI PRIMA DI DORMIRE, non dopo: fra le due
         * cose ci puo' entrare una richiesta, e un servente che si segna dopo
         * si perderebbe proprio la sveglia di quella. */
        for (i = 0; i < BLKR3_MAX_DEV; i++)
            if (g_r3[i].usato && g_r3[i].pid == pid) g_dorme[i] = pid;

        me->block_until = scadenza;
        sched_block(PROC_BLOCKED);
        interrupts_disable();

        for (i = 0; i < BLKR3_MAX_DEV; i++)
            if (g_r3[i].usato && g_r3[i].pid == pid) g_dorme[i] = 0;

        if (proc_interrotto()) { interrupts_enable(); return -EINTR; }
    }

    d->stato = CASELLA_IN_CORSO;
    *op    = d->op;
    *lba   = d->lba;
    *n     = d->n;
    *quale = (uint32_t)d->blkdev;

    interrupts_enable();
    return 0;
}

void *blkr3_dati(uint32_t pid, uint32_t *max)
{
    R3Dev *d;

    interrupts_disable();
    d = cerca_per_pid(pid, CASELLA_IN_CORSO);
    interrupts_enable();

    if (d == NULL) return NULL;
    if (max != NULL) *max = BLKR3_BYTE_MAX;
    return d->dati;
}

uint32_t blkr3_op_in_corso(uint32_t pid)
{
    R3Dev *d;
    uint32_t op;

    interrupts_disable();
    d  = cerca_per_pid(pid, CASELLA_IN_CORSO);
    op = (d != NULL) ? d->op : 0;
    interrupts_enable();

    return op;
}

int blkr3_risposta(uint32_t pid, int esito)
{
    R3Dev *d;

    interrupts_disable();

    d = cerca_per_pid(pid, CASELLA_IN_CORSO);
    if (d == NULL) { interrupts_enable(); return -ENOENT; }

    d->esito = esito;
    d->stato = CASELLA_SERVITA;
    if (d->pid_chiamante != 0) sched_unblock_locked(d->pid_chiamante);

    interrupts_enable();
    return 0;
}

/* -----------------------------------------------------------------------------
 * Il servente muore
 *
 * ! E' IL CASO CHE GIUSTIFICA META' DI QUESTO FILE. Un driver USB si schianta,
 * o qualcuno lo termina, e chi stava leggendo un file su quella chiavetta
 * dorme aspettando una risposta che non arrivera' mai. Qui si sveglia con
 * -EIO, e il dispositivo sparisce dallo strato a blocchi.
 * --------------------------------------------------------------------------- */

void blkr3_processo_morto(uint32_t pid)
{
    int i;

    /* La sezione critica se la prende questa funzione, come fa
     * ipc_cleanup_process() nello stesso punto della morte di un processo:
     * proc_reap_zombie() gira a interruzioni accese. */
    interrupts_disable();

    for (i = 0; i < BLKR3_MAX_DEV; i++) {
        if (!g_r3[i].usato || g_r3[i].pid != pid) continue;

        klog(LOG_WARN, "BLKR3: il PID %u e' morto: il suo disco sparisce", pid);

        if (g_r3[i].stato != CASELLA_LIBERA && g_r3[i].pid_chiamante != 0) {
            g_r3[i].esito = -EIO;
            g_r3[i].stato = CASELLA_SERVITA;
            sched_unblock_locked(g_r3[i].pid_chiamante);
        }

        blk_ritira(g_r3[i].blkdev);
        g_r3[i].usato = 0;
        g_dorme[i]    = 0;
    }

    interrupts_enable();
}
