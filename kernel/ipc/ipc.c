/* =============================================================================
 * kernel/ipc/ipc.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Implementazione IPC. Ogni operazione critica sulla mailbox di un
 * processo (lettura/scrittura head/tail/count) avviene con interrupt
 * disabilitati: la mailbox può essere toccata sia dal processo
 * proprietario (in ipc_recv, chiamata dalla sua stessa syscall) sia da
 * un mittente arbitrario (in ipc_send, chiamata dalla syscall di un
 * ALTRO processo) — è una struttura condivisa e serve mutua esclusione,
 * anche su singolo core, contro i context switch dell'IRQ0 timer.
 * ============================================================================= */

#include "kernel.h"
#include "ipc.h"
#include "sched.h"
#include "syscall.h"

/* How long a sender waits in line for room before giving up with EBUSY:
 * the ten seconds of the old 2000 x 5 ms retries. */
#define IPC_SEND_ATTESA_TICK  1000

/* IF saved and cleared, then restored: ipc_processo_finito is called from
 * paths that already run with interrupts off and from paths that do not. */
static inline unsigned long if_salva(void)
{
    unsigned long f;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(f) : : "memory");
    return f;
}

static inline void if_ripristina(unsigned long f)
{
    if (f & 0x200) __asm__ volatile("sti" : : : "memory");
}

/* =============================================================================
 * THE LINE IN FRONT OF A FULL MAILBOX (kernel 0.280)
 *
 * Every helper here runs with interrupts disabled. The IRQ path
 * (ipc_notify_irq) never touches the line: it may take a free slot that was
 * promised to someone, and the promised sender then simply waits for the
 * next one (fila_avanza wakes it again).
 * ============================================================================= */
static void fila_togli(Process *w)
{
    Process *d = w->ipc_fila_su;

    if (d == NULL) return;
    if (w->ipc_fila_prima) w->ipc_fila_prima->ipc_fila_dopo = w->ipc_fila_dopo;
    else                   d->ipc_fila_testa = w->ipc_fila_dopo;
    if (w->ipc_fila_dopo)  w->ipc_fila_dopo->ipc_fila_prima = w->ipc_fila_prima;
    else                   d->ipc_fila_coda = w->ipc_fila_prima;
    if (w->ipc_fila_concesso && d->ipc_riservati > 0) d->ipc_riservati--;
    w->ipc_fila_su = w->ipc_fila_prima = w->ipc_fila_dopo = NULL;
    w->ipc_fila_concesso = 0;
}

static void fila_metti(Process *d, Process *w)
{
    w->ipc_fila_su       = d;
    w->ipc_fila_dopo     = NULL;
    w->ipc_fila_prima    = d->ipc_fila_coda;
    w->ipc_fila_concesso = 0;
    if (d->ipc_fila_coda) d->ipc_fila_coda->ipc_fila_dopo = w;
    else                  d->ipc_fila_testa = w;
    d->ipc_fila_coda = w;
}

/* Room in d's mailbox: promise the free slots to the line, oldest first,
 * and wake whoever holds a promise. The promised ones are always at the
 * front, at most IPC_MAILBOX_DEPTH of them, so the walk is short. */
static void fila_avanza(Process *d)
{
    Process *w;

    for (w = d->ipc_fila_testa; w != NULL; w = w->ipc_fila_dopo) {
        if (!w->ipc_fila_concesso) {
            if (d->ipc_count + d->ipc_riservati >= IPC_MAILBOX_DEPTH) break;
            w->ipc_fila_concesso = 1;
            d->ipc_riservati++;
        }
        if (w->state == PROC_BLOCKED) sched_unblock_locked(w->pid);
    }
}

void ipc_processo_finito(Process *p)
{
    unsigned long f = if_salva();
    Process      *d = p->ipc_fila_su, *w;

    if (d != NULL) {
        fila_togli(p);
        fila_avanza(d);     /* a slot promised to p goes to the next one */
    }
    while ((w = p->ipc_fila_testa) != NULL) {
        fila_togli(w);
        if (w->state == PROC_BLOCKED) sched_unblock_locked(w->pid);
    }
    p->ipc_riservati = 0;
    if_ripristina(f);
}

/* Tipo/mittente riservati per le notifiche hardware IRQ, consegnate con
 * sender_pid = IPC_SENDER_KERNEL. Un driver distingue una notifica
 * hardware da una richiesta client controllando sender_pid: se è
 * IPC_SENDER_KERNEL, il payload è un solo uint32_t col numero IRQ. */
#define IPC_SENDER_KERNEL   0
#define IPC_TYPE_IRQ_NOTIFY 0xFFFFFFFFu

/* =============================================================================
 * ipc_notify_irq — consegna una notifica di IRQ hardware a un driver.
 *
 * Chiamata da irq_handler() (isr.c) DENTRO il contesto dell'interrupt:
 * non deve mai bloccare né chiamare lo scheduler in modo da cedere la
 * CPU (sched_unblock è sicura: si limita a spostare il processo in
 * READY, non fa context switch essa stessa). Se la mailbox del driver è
 * piena, la notifica viene scartata — è sicuro perché è solo un segnale
 * "hai lavoro da fare, ricontrolla l'hardware", non un dato univoco: il
 * driver che processa in ritardo troverà comunque lo stato reale del
 * controller alla prossima lettura.
 * ============================================================================= */
/* ! RENDE SE HA CONSEGNATO, dal 1 settembre 2026, e serve a chi condivide una
 * linea: il dispatcher segna «questo driver deve ancora rispondere» solo per
 * chi la notifica l'ha davvero ricevuta. Segnarlo per una notifica caduta
 * vorrebbe dire aspettare per sempre una risposta che nessuno ha motivo di
 * dare — cioe' una linea chiusa a chiave, e con lei l'hardware di chi la
 * condivide. */
int ipc_notify_irq(uint32_t dest_pid, uint8_t irq_num)
{
    Process *dest = proc_get_by_pid(dest_pid);
    if (dest == NULL || dest->state == PROC_UNUSED || dest->state == PROC_ZOMBIE) {
        return 0;
    }

    /* NIENTE cli/sti qui: i gate IRQ sono "interrupt gate" (0x8E), quindi
     * la CPU ha già azzerato IF all'ingresso e lo ripristina solo con
     * l'iret finale in irq_common. Chiamare interrupts_enable() (sti) in
     * questo punto riabiliterebbe gli interrupt PRIMA di quell'iret,
     * rendendo rientrante un dispatcher che non è scritto per esserlo. */
    if (dest->ipc_count < IPC_MAILBOX_DEPTH) {
        IpcMessage *slot = &dest->ipc_mailbox[dest->ipc_tail];
        slot->sender_pid = IPC_SENDER_KERNEL;
        slot->tipo       = IPC_TYPE_IRQ_NOTIFY;
        slot->len        = sizeof(uint32_t);
        *(uint32_t *)slot->data = (uint32_t)irq_num;
        dest->ipc_tail   = (dest->ipc_tail + 1) % IPC_MAILBOX_DEPTH;
        dest->ipc_count++;

        if (dest->state == PROC_BLOCKED) {
            sched_unblock_locked(dest_pid);
        }
        /* Un PROC_NASCENTE non compare qui apposta: il messaggio resta in
         * cassetta e lo legge quando parte. Vedi PROC_NASCENTE in sched.h. */
        return 1;
    }
    /* Mailbox piena: notifica scartata, vedi commento sopra la funzione. */
    return 0;
}

/* =============================================================================
 * ipc_send — consegna un messaggio a dest_pid
 * ============================================================================= */
int32_t ipc_send(uint32_t dest_pid, uint32_t tipo,
                  const void *data, uint32_t len)
{
    if (len > IPC_MSG_MAX_DATA) len = IPC_MSG_MAX_DATA;

    Process *self     = proc_get_current();
    uint32_t senza    = (tipo & IPC_SENZA_ATTESA) != 0;
    uint32_t scadenza = g_ticks + IPC_SEND_ATTESA_TICK;

    if (scadenza == 0) scadenza = 1;    /* 0 means "no deadline" */
    tipo &= ~IPC_SENZA_ATTESA;

    for (;;) {
        Process *dest = proc_get_by_pid(dest_pid);
        int      posto;

        interrupts_disable();

        if (dest == NULL || dest->state == PROC_UNUSED ||
            dest->state == PROC_ZOMBIE) {
            Process *d = self->ipc_fila_su;

            fila_togli(self);
            if (d != NULL) fila_avanza(d);
            interrupts_enable();
            return ERR(ESRCH);
        }

        /* Who may take a slot:
         *   - without waiting (IPC_SENZA_ATTESA): any free slot, even one
         *     promised to the line. These are mostly drivers answering a
         *     request of the receiver itself (a frame for ip.drv): making
         *     them queue behind a hundred clients would stall the receiver;
         *   - a sender in this line: only with a slot promised to it;
         *   - anybody else: only if nobody is waiting and the free slots are
         *     not all promised. Otherwise it joins the end of the line. */
        if (senza)
            posto = dest->ipc_count < IPC_MAILBOX_DEPTH;
        else if (self->ipc_fila_su == dest)
            posto = self->ipc_fila_concesso && dest->ipc_count < IPC_MAILBOX_DEPTH;
        else
            posto = dest->ipc_fila_testa == NULL &&
                    dest->ipc_count + dest->ipc_riservati < IPC_MAILBOX_DEPTH;

        if (posto) {
            IpcMessage *slot = &dest->ipc_mailbox[dest->ipc_tail];

            if (self->ipc_fila_su == dest) fila_togli(self);
            slot->sender_pid = self->pid;
            slot->tipo       = tipo;
            slot->len        = len;
            if (len > 0 && data != NULL) {
                const uint8_t *src = (const uint8_t *)data;
                for (uint32_t i = 0; i < len; i++) slot->data[i] = src[i];
            }
            dest->ipc_tail   = (dest->ipc_tail + 1) % IPC_MAILBOX_DEPTH;
            dest->ipc_count++;

            int need_wake = (dest->state == PROC_BLOCKED);
            interrupts_enable();

            /* sched_unblock() gestisce da sé cli/sti: chiamarla FUORI
             * dalla nostra sezione critica evita di annidare due
             * coppie cli/sti senza contatore (bug già documentato per
             * sched_unblock_locked altrove nel kernel). Se dest era
             * bloccato per un motivo diverso da IPC (es. waitpid), il
             * risveglio è innocuo: quel percorso ricontrolla la propria
             * condizione e si riblocca se non ancora soddisfatta. */
            if (need_wake) sched_unblock(dest_pid);

            return 0;
        }

        if (senza) {
            interrupts_enable();
            return ERR(EBUSY);
        }

        if (g_ticks >= scadenza) {
            Process *d = self->ipc_fila_su;

            fila_togli(self);
            if (d != NULL) fila_avanza(d);  /* a promise to me goes on */
            interrupts_enable();
            return ERR(EBUSY);
        }

        if (self->ipc_fila_su != dest) {
            /* A line for another destination cannot be left over (ipc_send
             * always leaves its line before returning), but be safe. */
            Process *d = self->ipc_fila_su;

            fila_togli(self);
            if (d != NULL) fila_avanza(d);
            fila_metti(dest, self);
        }

        /* Asleep until the receiver promises me a slot (fila_avanza), the
         * receiver ends (ipc_processo_finito) or the deadline: each case is
         * sorted out by the next turn of the loop. Interrupts stay disabled
         * into sched_block, as in ipc_recv_timeout (the lost wakeup). */
        self->block_until = scadenza;
        sched_block(PROC_BLOCKED);
    }
}

/* =============================================================================
 * ipc_recv — attende ed estrae il prossimo messaggio dalla propria mailbox
 * ============================================================================= */
int32_t ipc_recv(IpcMessage *out, void *buf, uint32_t buf_len)
{
    return ipc_recv_timeout(out, buf, buf_len, 0);
}

/* =============================================================================
 * ipc_recv_timeout — come ipc_recv, ma rinuncia dopo timeout_ms
 *
 * timeout_ms == 0 significa "aspetta per sempre", cioè esattamente la
 * ipc_recv di prima. Con una scadenza ritorna -ETIMEDOUT se allo
 * scadere non è arrivato niente.
 *
 * A COSA SERVE. Senza, un programma interattivo di EX-OS non può fare
 * NULLA mentre aspetta un tasto: resta fermo in ipc_recv finché
 * qualcuno non preme qualcosa. È il motivo per cui l'orologio di
 * /bin/gfedit avanzava solo alla pressione di un tasto — l'editor non
 * aveva modo di riprendere il controllo allo scadere di un secondo.
 * Con la scadenza il ciclo diventa quello di qualunque interfaccia:
 * aspetta un evento O il prossimo tick di lavoro, quello che arriva
 * prima.
 *
 * ATTENZIONE alla finestra fra l'armare la scadenza e il bloccarsi: le
 * interrupt restano DISABILITATE fino dentro sched_block(). Riabilitarle
 * prima aprirebbe la stessa "lost wakeup" già documentata in
 * drivers/tty/tty.c — il tick potrebbe trovare il processo ancora
 * RUNNING, non svegliarlo, e subito dopo sched_block() lo metterebbe a
 * dormire su una sveglia già consumata.
 * ============================================================================= */
int32_t ipc_recv_timeout(IpcMessage *out, void *buf, uint32_t buf_len,
                         uint32_t timeout_ms)
{
    Process *self = proc_get_current();
    uint32_t scadenza = 0;

    if (timeout_ms > 0) {
        /* ms -> tick, arrotondando per eccesso: il PIT è a 100Hz e una
         * scadenza sotto i 10 ms non è rappresentabile. */
        uint32_t ticks = (timeout_ms + 9) / 10;
        if (ticks == 0) ticks = 1;
        scadenza = g_ticks + ticks;
        if (scadenza == 0) scadenza = 1;   /* 0 significa "nessuna scadenza" */
    }

    for (;;) {
        interrupts_disable();

        if (self->ipc_count > 0) {
            IpcMessage *slot = &self->ipc_mailbox[self->ipc_head];

            if (out) {
                out->sender_pid = slot->sender_pid;
                out->tipo       = slot->tipo;
                out->len        = slot->len;
            }

            uint32_t copy_len = slot->len;
            if (copy_len > buf_len) copy_len = buf_len;
            if (buf && copy_len > 0) {
                uint8_t *dst = (uint8_t *)buf;
                for (uint32_t i = 0; i < copy_len; i++) dst[i] = slot->data[i];
            }

            self->ipc_head = (self->ipc_head + 1) % IPC_MAILBOX_DEPTH;
            self->ipc_count--;
            self->block_until = 0;
            if (self->ipc_fila_testa != NULL) fila_avanza(self);

            interrupts_enable();
            return 0;
        }

        /* Scadenza superata e mailbox ancora vuota: si rinuncia. Il
         * controllo sta QUI e non nello scheduler perché è il
         * chiamante — non il tick — a sapere che cosa stava aspettando
         * e quindi se l'attesa sia davvero fallita. */
        if (scadenza != 0 && g_ticks >= scadenza) {
            self->block_until = 0;
            interrupts_enable();
            return ERR(ETIMEDOUT);
        }

        /* Mailbox vuota: blocca finché ipc_send non consegna qualcosa,
         * o finché il tick non fa scattare la scadenza. sched_block()
         * gestisce internamente cli/sti e il context switch — al ritorno
         * da qui la condizione va ricontrollata (potremmo essere stati
         * svegliati da un risveglio spurio, o proprio dalla scadenza). */
        self->block_until = scadenza;
        sched_block(PROC_BLOCKED);   /* con le interrupt ancora disabilitate */

        /* ! CHI E' STATO INTERROTTO SMETTE DI ASPETTARE UN MESSAGGIO. Un
         * driver o un server passa la vita qui dentro: senza questa riga,
         * interromperlo non avrebbe nessun effetto visibile finche' qualcuno
         * non gli scrive — cioe' quasi mai. */
        if (proc_interrotto()) return ERR(EINTR);
    }
}

/* =============================================================================
 * ipc_register / ipc_lookup — registro nomi servizi
 *
 * ! UNA TABELLA SOLA, NON UN NOME PER PROCESSO, dal 23 ottobre 2026. Fino
 * alla 0.270 ogni processo aveva un campo ipc_service_name e un secondo
 * ipc_register lo sovrascriveva: un driver USB che apre lo sportello
 * «usb.ohci» e poi serve un mouse diventava «mouse» e lo sportello spariva
 * (usb -status non lo vedeva più). Un driver può servire più periferiche,
 * e ognuna può volere il suo nome: il limite giusto è sul totale, non sul
 * processo. 128 voci da 20 byte sono 2.5 KB; un campo per processo con lo
 * stesso margine occuperebbe quasi 200 volte tanto, quasi tutto vuoto.
 * ============================================================================= */
typedef struct {
    uint32_t pid;                   /* 0 = voce libera */
    char     nome[IPC_NAME_LEN];
} IpcNome;

static IpcNome g_nomi[IPC_NOMI_MAX];

static int nome_uguale(const char *a, const char *b)
{
    for (uint32_t j = 0; j < IPC_NAME_LEN; j++) {
        if (a[j] != b[j]) return 0;
        if (a[j] == '\0') return 1;
    }
    return 1;
}

/* La voce è di un processo vivo? Quella di un processo morto e non ancora
 * raccolto (zombie) non vale più: il nome si può riprendere. */
static int voce_viva(const IpcNome *v)
{
    Process *p;

    if (v->pid == 0) return 0;
    p = proc_get_by_pid(v->pid);
    return p != NULL && p->state != PROC_UNUSED && p->state != PROC_ZOMBIE;
}

int32_t ipc_register(const char *name)
{
    if (name == NULL || name[0] == '\0') return ERR(EINVAL);

    uint32_t len = 0;
    while (name[len] && len < IPC_NAME_LEN - 1) len++;
    if (name[len] != '\0') return ERR(EINVAL);  /* nome troppo lungo */

    interrupts_disable();

    /* Rifiuta se il nome è già registrato da un processo vivo */
    for (uint32_t i = 0; i < IPC_NOMI_MAX; i++) {
        if (voce_viva(&g_nomi[i]) && nome_uguale(g_nomi[i].nome, name)) {
            interrupts_enable();
            return ERR(EEXIST);
        }
    }

    Process *self = proc_get_current();

    /* =========================================================================
     * ! I NOMI SENZA PREFISSO SONO DI root, dal 17 agosto 2026.
     *
     * Il controllo qui sopra impedisce di rubare un nome a un servizio VIVO.
     * Non impedisce di prenderlo PRIMA che nasca, o dopo che e' morto — e un
     * processo che registra `kbd` riceve i tasti che tutti gli altri gli
     * mandano, uno che registra `wserver` riceve le finestre. Su una macchina a
     * un solo utente non cambia niente; in multiutenza e' il modo piu' diretto
     * di leggere quello che sta scrivendo un altro.
     *
     * ! LA REGOLA E' MECCANICA E NON HA UN ELENCO: un utente normale puo'
     * registrare solo nomi che cominciano col proprio uid e due punti —
     * `1000:mio`. Un elenco di nomi riservati sarebbe una seconda verita'
     * accanto ai servizi che esistono davvero, e le due divergono al primo
     * servizio aggiunto.
     * ========================================================================= */
    if (self != NULL && self->uid != 0) {
        char atteso[16];
        uint32_t v = self->uid, l = 0, k;
        char rov[12];

        if (v == 0) rov[l++] = '0';
        while (v > 0) { rov[l++] = (char)('0' + (v % 10u)); v /= 10u; }
        for (k = 0; k < l; k++) atteso[k] = rov[l - 1 - k];
        atteso[l++] = ':';

        for (k = 0; k < l; k++) {
            if (name[k] != atteso[k]) {
                interrupts_enable();
                klog(LOG_WARN, "ipc_register('%s'): il PID %u (uid %u) puo' "
                     "registrare solo nomi che cominciano con '%u:'",
                     name, self->pid, self->uid, self->uid);
                return ERR(EPERM);
            }
        }
    }

    /* Una voce libera, o di un processo che non c'è più. */
    uint32_t i;
    for (i = 0; i < IPC_NOMI_MAX; i++)
        if (!voce_viva(&g_nomi[i])) break;
    if (i == IPC_NOMI_MAX) {
        interrupts_enable();
        klog(LOG_WARN, "ipc_register('%s'): tabella dei nomi piena (%u)",
             name, (unsigned)IPC_NOMI_MAX);
        return ERR(ENOSPC);
    }

    g_nomi[i].pid = self ? self->pid : 0;
    for (uint32_t k = 0; k < IPC_NAME_LEN; k++) {
        g_nomi[i].nome[k] = name[k];
        if (name[k] == '\0') break;
    }

    interrupts_enable();
    return 0;
}

int32_t ipc_lookup(const char *name)
{
    if (name == NULL || name[0] == '\0') return ERR(EINVAL);

    interrupts_disable();

    for (uint32_t i = 0; i < IPC_NOMI_MAX; i++) {
        if (voce_viva(&g_nomi[i]) && nome_uguale(g_nomi[i].nome, name)) {
            uint32_t pid = g_nomi[i].pid;
            interrupts_enable();
            return (int32_t)pid;
        }
    }

    interrupts_enable();
    return ERR(ENOENT);
}

/* =============================================================================
 * ipc_cleanup_process — chiamata da proc_reap_zombie() alla terminazione
 * ============================================================================= */
void ipc_cleanup_process(uint32_t pid)
{
    for (uint32_t i = 0; i < g_proc_posti; i++) {
        Process *p = PROC_POSTO(i);
        if (p->pid == pid) {
            ipc_processo_finito(p);     /* normally done already at exit */
            break;
        }
    }

    interrupts_disable();

    for (uint32_t i = 0; i < g_proc_posti; i++) {
        Process *p = PROC_POSTO(i);
        if (p->pid == pid) {
            p->ipc_head = p->ipc_tail = p->ipc_count = 0;
            break;
        }
    }
    for (uint32_t i = 0; i < IPC_NOMI_MAX; i++)
        if (g_nomi[i].pid == pid) g_nomi[i].pid = 0;

    interrupts_enable();
}
