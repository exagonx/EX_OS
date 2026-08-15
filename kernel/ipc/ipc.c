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

#define IPC_SEND_RETRY_MS   5    /* intervallo di poll quando la mailbox dest è piena */
#define IPC_SEND_MAX_RETRY  2000 /* ~10s totali prima di arrendersi (ETIMEDOUT-like) */

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
void ipc_notify_irq(uint32_t dest_pid, uint8_t irq_num)
{
    Process *dest = proc_get_by_pid(dest_pid);
    if (dest == NULL || dest->state == PROC_UNUSED || dest->state == PROC_ZOMBIE) {
        return;
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
    }
    /* Mailbox piena: notifica scartata, vedi commento sopra la funzione. */
}

/* =============================================================================
 * ipc_send — consegna un messaggio a dest_pid
 * ============================================================================= */
int32_t ipc_send(uint32_t dest_pid, uint32_t tipo,
                  const void *data, uint32_t len)
{
    if (len > IPC_MSG_MAX_DATA) len = IPC_MSG_MAX_DATA;

    uint32_t sender_pid = proc_get_current()->pid;
    uint32_t attempts;

    for (attempts = 0; attempts < IPC_SEND_MAX_RETRY; attempts++) {
        Process *dest = proc_get_by_pid(dest_pid);
        if (dest == NULL || dest->state == PROC_UNUSED ||
            dest->state == PROC_ZOMBIE) {
            return ERR(ESRCH);
        }

        interrupts_disable();

        if (dest->ipc_count < IPC_MAILBOX_DEPTH) {
            IpcMessage *slot = &dest->ipc_mailbox[dest->ipc_tail];
            slot->sender_pid = sender_pid;
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

        interrupts_enable();

        /* Mailbox piena: il caso comune è un driver temporaneamente
         * indietro con l'elaborazione — riprova dopo una breve attesa
         * invece di bloccare indefinitamente il mittente. */
        sched_sleep(IPC_SEND_RETRY_MS);
    }

    return ERR(EBUSY);
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
    }
}

/* =============================================================================
 * ipc_register / ipc_lookup — registro nomi servizi
 * ============================================================================= */
int32_t ipc_register(const char *name)
{
    if (name == NULL || name[0] == '\0') return ERR(EINVAL);

    uint32_t len = 0;
    while (name[len] && len < IPC_NAME_LEN - 1) len++;
    if (name[len] != '\0') return ERR(EINVAL);  /* nome troppo lungo */

    interrupts_disable();

    /* Rifiuta se il nome è già registrato da un processo vivo */
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        Process *p = &g_process_pool[i];
        if (p->state == PROC_UNUSED || p->state == PROC_ZOMBIE) continue;
        if (p->ipc_service_name[0] == '\0') continue;

        uint32_t j = 0;
        while (j < IPC_NAME_LEN && p->ipc_service_name[j] == name[j]) {
            if (name[j] == '\0') break;
            j++;
        }
        if (p->ipc_service_name[j] == name[j]) {  /* uguali fino al terminatore */
            interrupts_enable();
            return ERR(EEXIST);
        }
    }

    Process *self = proc_get_current();
    for (uint32_t i = 0; i < IPC_NAME_LEN; i++) {
        self->ipc_service_name[i] = name[i];
        if (name[i] == '\0') break;
    }

    interrupts_enable();
    return 0;
}

int32_t ipc_lookup(const char *name)
{
    if (name == NULL || name[0] == '\0') return ERR(EINVAL);

    interrupts_disable();

    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        Process *p = &g_process_pool[i];
        if (p->state == PROC_UNUSED || p->state == PROC_ZOMBIE) continue;
        if (p->ipc_service_name[0] == '\0') continue;

        uint32_t j = 0;
        while (j < IPC_NAME_LEN && p->ipc_service_name[j] == name[j]) {
            if (name[j] == '\0') break;
            j++;
        }
        if (p->ipc_service_name[j] == name[j]) {
            uint32_t pid = p->pid;
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
    interrupts_disable();

    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        Process *p = &g_process_pool[i];
        if (p->pid == pid) {
            p->ipc_service_name[0] = '\0';
            p->ipc_head = p->ipc_tail = p->ipc_count = 0;
            break;
        }
    }

    interrupts_enable();
}
