/* =============================================================================
 * kernel/sched/segnali.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * SIGNALS, WITH REAL DELIVERY (kernel 0.227)
 *
 * Until 0.226 a signal was a name: the libc called the handler itself from
 * raise(), and a fault killed the process. Exilla needs more: SpiderMonkey
 * and Firefox install a SIGSEGV handler and expect it to run, with the
 * faulting address and the registers, on an alternate stack if they asked.
 *
 * HOW A SIGNAL ARRIVES
 *
 *   1. The kernel copies the interrupted registers into a SegTelaio (see
 *      syscall.h) that it writes on the user stack — or on the sigaltstack —
 *      and points EIP at the ONE dispatcher the libc registered with
 *      sigaction. ESP points at the frame.
 *   2. The dispatcher saves the FPU, looks up the handler in the libc's own
 *      table and calls it, restores the FPU, and calls SYS_SEG_RITORNO with
 *      the context.
 *   3. seg_ritorno reloads the registers from the context — the handler may
 *      have changed them — and the interrupted code goes on.
 *
 * WHEN
 *
 *   - A FAULT (#PF, #GP, #UD, #DE...) in ring 3: at once, from the exception
 *     handler. If the signal is not caught, or is blocked (a fault inside
 *     its own handler), the process dies as before.
 *   - A SENT signal (raise, kill, pthread_kill): it becomes PENDING on the
 *     target thread, and is delivered when that thread next leaves a
 *     syscall (syscall_handler calls segnali_consegna). sigreturn and
 *     sigprocmask are syscalls too, so a signal unblocked or queued behind
 *     another one comes out at the right moment.
 *
 * ! WHAT IS NOT THERE, and on purpose for now:
 *
 *   - A thread that never calls the kernel does not receive sent signals:
 *     delivery from the timer interrupt is the same work as terminating
 *     from it (see the comment at the bottom of syscall_handler).
 *   - A blocked wait is not interrupted by a caught signal (no EINTR): the
 *     signal waits for the wait to end. SA_RESTART is therefore what
 *     happens anyway, and is accepted.
 *   - A signal whose default is to terminate, sent to ANOTHER process that
 *     does not catch it, goes through proc_interrompi — the Ctrl+C path —
 *     so the exit code is 130 whatever the signal.
 *   - One bit per signal: two sends before a delivery are one signal, as
 *     POSIX allows for non-realtime signals.
 * ============================================================================= */

#include "kernel.h"
#include "sched.h"
#include "paging.h"
#include "syscall.h"
#include "vfs.h"    /* vfs_sync: dying of a signal flushes like any exit */

#define BIT(s)      (1u << (s))
#define SIGKILL_    9
#define SIGSTOP_    19
#define SIGCHLD_    17
#define SIGCONT_    18
#define SIGURG_     23
#define SIGWINCH_   28
#define NON_BLOCCABILI  (BIT(SIGKILL_) | BIT(SIGSTOP_))

/* EFLAGS bits a handler may change through the context: the arithmetic
 * flags, TF, DF and AC. IF, IOPL, VM and the rest stay the kernel's. */
#define EFL_UTENTE  0x00040DD5u

/* The actions are the program's: they live on the group leader. */
static Process *capo_di(Process *p)
{
    if (p != NULL && p->tgid != 0 && p->tgid != p->pid) {
        Process *c = proc_get_by_pid(p->tgid);
        if (c != NULL) return c;
    }
    return p;
}

static int predefinito_ignora(int sig)
{
    return sig == SIGCHLD_ || sig == SIGCONT_ || sig == SIGURG_ ||
           sig == SIGWINCH_;
}

/* =============================================================================
 * costruisci — write the frame and redirect the thread to the dispatcher
 *
 * 1 if the thread will now run the handler, 0 if it cannot (not caught, no
 * dispatcher, no room for the frame): the caller decides what dying means.
 * ============================================================================= */
static int costruisci(Process *p, InterruptFrame *f, int sig,
                      int32_t codice, uint32_t indirizzo)
{
    Process  *capo = capo_di(p);
    uint32_t  fl, cima, dove, sulla_pila;
    SegTelaio t;

    if (capo == NULL || capo->seg_ingresso == 0) return 0;
    fl = capo->seg_flag[sig];
    if ((fl & SEG_TIPO) != SEG_PRESO) return 0;

    /* A frame can only be built for code that was running in ring 3: the
     * registers to resume are in the frame only then. */
    if ((f->cs & 3) != 3) return 0;

    sulla_pila = p->seg_pila_base != 0 &&
                 f->user_esp >  p->seg_pila_base &&
                 f->user_esp <= p->seg_pila_base + p->seg_pila_dim;

    if ((fl & SA_ONSTACK) && p->seg_pila_base != 0 && !sulla_pila)
        cima = p->seg_pila_base + p->seg_pila_dim;
    else
        cima = f->user_esp;

    if (cima < sizeof(SegTelaio) + 16) return 0;
    dove = (cima - sizeof(SegTelaio)) & ~15u;

    /* ! BEFORE writing: see paging_utente_pronta. A stack that has run out
     * is the most common reason to be here. */
    if (!paging_utente_pronta(p, dove, sizeof(SegTelaio))) {
        klog(LOG_WARN, "SEGNALI: PID %u, nessuno spazio per il segnale %d a "
             "0x%08x", p->pid, sig, dove);
        return 0;
    }

    __builtin_memset(&t, 0, sizeof(t));
    t.sig       = (uint32_t)sig;
    t.info      = dove + (uint32_t)__builtin_offsetof(SegTelaio, si);
    t.contesto  = dove + (uint32_t)__builtin_offsetof(SegTelaio, uc);

    t.si.signo     = sig;
    t.si.codice    = codice;
    t.si.indirizzo = indirizzo;

    t.uc.pila.sp   = p->seg_pila_base;
    t.uc.pila.dim  = p->seg_pila_dim;
    t.uc.pila.flag = (p->seg_pila_base == 0) ? SS_DISABLE
                   : (sulla_pila ? SS_ONSTACK : 0);

    t.uc.gregs[SEG_REG_GS]  = f->ds;
    t.uc.gregs[SEG_REG_FS]  = f->ds;
    t.uc.gregs[SEG_REG_ES]  = f->ds;
    t.uc.gregs[SEG_REG_DS]  = f->ds;
    t.uc.gregs[SEG_REG_EDI] = f->edi;
    t.uc.gregs[SEG_REG_ESI] = f->esi;
    t.uc.gregs[SEG_REG_EBP] = f->ebp;
    t.uc.gregs[SEG_REG_ESP] = f->user_esp;
    t.uc.gregs[SEG_REG_EBX] = f->ebx;
    t.uc.gregs[SEG_REG_EDX] = f->edx;
    t.uc.gregs[SEG_REG_ECX] = f->ecx;
    t.uc.gregs[SEG_REG_EAX] = f->eax;
    t.uc.gregs[SEG_REG_TRAPNO] = f->int_no;
    t.uc.gregs[SEG_REG_ERR] = f->err_code;
    t.uc.gregs[SEG_REG_EIP] = f->eip;
    t.uc.gregs[SEG_REG_CS]  = f->cs;
    t.uc.gregs[SEG_REG_EFL] = f->eflags;
    t.uc.gregs[SEG_REG_UESP]= f->user_esp;
    t.uc.gregs[SEG_REG_SS]  = f->user_ss;
    t.uc.cr2      = (f->int_no == 14) ? indirizzo : 0;
    t.uc.maschera = p->seg_bloccati;
    t.uc.oldmask  = p->seg_bloccati;

    /* The page directory is this thread's: the frame lands in its memory. */
    __builtin_memcpy((void *)dove, &t, sizeof(t));

    p->seg_bloccati |= capo->seg_maschera_az[sig];
    if (!(fl & SA_NODEFER)) p->seg_bloccati |= BIT(sig);
    p->seg_bloccati &= ~NON_BLOCCABILI;

    if (fl & SA_RESETHAND) {
        capo->seg_flag[sig]        = SEG_PREDEFINITO;
        capo->seg_maschera_az[sig] = 0;
    }

    f->eip      = capo->seg_ingresso;
    f->user_esp = dove;
    f->eflags  &= ~0x00000500u;     /* TF and DF off, as the ABI expects */
    return 1;
}

/* =============================================================================
 * segnali_fault — a CPU exception in ring 3 becomes a signal, if it can
 *
 * Called by page_fault_handler and isr_handler before they kill. 1: the
 * thread will run its handler, the caller just returns (the iret goes to the
 * dispatcher). 0: nothing caught it — or it is blocked, which for a fault
 * means it happened inside its own handler — and the caller kills as before.
 * ============================================================================= */
int segnali_fault(InterruptFrame *f, int sig, int32_t codice, uint32_t indirizzo)
{
    Process *p = proc_get_current();

    if (p == NULL || sig <= 0 || sig >= 32) return 0;
    if (p->seg_bloccati & BIT(sig)) return 0;

    if (!costruisci(p, f, sig, codice, indirizzo)) return 0;

    klog(LOG_DEBUG, "SEGNALI: PID %u, segnale %d a EIP=0x%08x (0x%08x)",
         p->pid, sig, f->eip, indirizzo);
    return 1;
}

/* =============================================================================
 * segnali_consegna — the pending signals of the current thread, on the way
 * back to ring 3 from a syscall
 *
 * One signal per call: after building a frame the thread must run it, and
 * the NEXT pending one comes out when that handler's sigreturn — itself a
 * syscall — goes back through here.
 * ============================================================================= */
void segnali_consegna(InterruptFrame *f)
{
    Process *p = proc_get_current();
    Process *capo;
    uint32_t pronti;
    int      sig;

    if (p == NULL) return;

    while ((pronti = p->seg_pendenti & ~p->seg_bloccati) != 0) {
        uint32_t tipo;

        for (sig = 1; sig < 32 && !(pronti & BIT(sig)); sig++) ;
        p->seg_pendenti &= ~BIT(sig);

        capo = capo_di(p);
        tipo = capo ? (capo->seg_flag[sig] & SEG_TIPO) : SEG_PREDEFINITO;

        if (sig == SIGKILL_) tipo = SEG_PREDEFINITO;
        if (tipo == SEG_IGNORA) continue;
        if (tipo == SEG_PREDEFINITO && predefinito_ignora(sig)) continue;

        /* SI_TKILL (-6): sent by a thread with tgkill/raise/kill. */
        if (tipo == SEG_PRESO && costruisci(p, f, sig, -6, 0)) return;

        /* Default action, or a handler that could not be reached. */
        klog(LOG_INFO, "SEGNALI: PID %u '%s' termina per il segnale %d",
             p->pid, p->name, sig);
        vfs_sync();
        proc_esci_fatale(128 + sig);
    }
}

/* =============================================================================
 * The syscalls
 * ============================================================================= */
int32_t sys_seg_azione(InterruptFrame *frame)
{
    int              sig   = (int)frame->ebx;
    const SegAzione *nuova = (const SegAzione *)frame->ecx;
    SegAzione       *prima = (SegAzione *)frame->edx;
    Process         *capo  = capo_di(proc_get_current());

    if (capo == NULL) return ERR(ESRCH);
    if (sig <= 0 || sig >= 32) return ERR(EINVAL);
    if (nuova != NULL && !syscall_verify_ptr(nuova, sizeof(SegAzione)))
        return ERR(EFAULT);
    if (prima != NULL && !syscall_verify_ptr(prima, sizeof(SegAzione)))
        return ERR(EFAULT);

    if (prima != NULL) {
        prima->tipo     = capo->seg_flag[sig] & SEG_TIPO;
        prima->flag     = capo->seg_flag[sig] & ~(uint32_t)SEG_TIPO;
        prima->maschera = capo->seg_maschera_az[sig];
        prima->ingresso = capo->seg_ingresso;
    }

    if (nuova != NULL) {
        SegAzione a = *nuova;

        if (a.tipo > SEG_PRESO) return ERR(EINVAL);
        if ((sig == SIGKILL_ || sig == SIGSTOP_) && a.tipo != SEG_PREDEFINITO)
            return ERR(EINVAL);
        if (a.tipo == SEG_PRESO &&
            (a.ingresso < USER_SPACE_BASE || a.ingresso >= USER_SPACE_END))
            return ERR(EFAULT);

        if (a.tipo == SEG_PRESO) capo->seg_ingresso = a.ingresso;
        capo->seg_flag[sig]        = a.tipo | (a.flag & ~(uint32_t)SEG_TIPO);
        capo->seg_maschera_az[sig] = a.maschera & ~NON_BLOCCABILI;
    }
    return 0;
}

int32_t sys_seg_maschera(InterruptFrame *frame)
{
    uint32_t        come  = frame->ebx;
    const uint32_t *nuova = (const uint32_t *)frame->ecx;
    uint32_t       *prima = (uint32_t *)frame->edx;
    Process        *p     = proc_get_current();
    uint32_t        m;

    if (p == NULL) return ERR(ESRCH);
    if (nuova != NULL && !syscall_verify_ptr(nuova, sizeof(uint32_t)))
        return ERR(EFAULT);
    if (prima != NULL && !syscall_verify_ptr(prima, sizeof(uint32_t)))
        return ERR(EFAULT);

    m = p->seg_bloccati;
    if (nuova != NULL) {
        switch (come) {
            case SIG_BLOCK:   m |=  *nuova; break;
            case SIG_UNBLOCK: m &= ~*nuova; break;
            case SIG_SETMASK: m  =  *nuova; break;
            default:          return ERR(EINVAL);
        }
    }
    if (prima != NULL) *prima = p->seg_bloccati;
    p->seg_bloccati = m & ~NON_BLOCCABILI;
    return 0;
}

/* ! THE RETURN VALUE BECOMES EAX: syscall_handler writes it into the frame
 * after this returns, so returning the restored EAX is how it survives. */
int32_t sys_seg_ritorno(InterruptFrame *frame)
{
    const SegContesto *uc = (const SegContesto *)frame->ebx;
    Process           *p  = proc_get_current();
    SegContesto        c;

    if (p == NULL) return ERR(ESRCH);
    if (!syscall_verify_ptr(uc, sizeof(SegContesto))) {
        klog(LOG_ERROR, "SEGNALI: PID %u, sigreturn con un contesto non "
             "valido (0x%08x)", p->pid, (uint32_t)uc);
        proc_esci_fatale(128 + 11);
    }
    c = *uc;

    frame->edi      = c.gregs[SEG_REG_EDI];
    frame->esi      = c.gregs[SEG_REG_ESI];
    frame->ebp      = c.gregs[SEG_REG_EBP];
    frame->ebx      = c.gregs[SEG_REG_EBX];
    frame->edx      = c.gregs[SEG_REG_EDX];
    frame->ecx      = c.gregs[SEG_REG_ECX];
    frame->eip      = c.gregs[SEG_REG_EIP];
    frame->user_esp = c.gregs[SEG_REG_UESP];
    frame->eflags   = (frame->eflags & ~EFL_UTENTE) |
                      (c.gregs[SEG_REG_EFL] & EFL_UTENTE);
    /* CS, SS and DS are not taken back: ring 3 has only one of each, and a
     * context that asked for another would be asking for ring 0. */

    p->seg_bloccati = c.maschera & ~NON_BLOCCABILI;
    return (int32_t)c.gregs[SEG_REG_EAX];
}

int32_t sys_seg_pila(InterruptFrame *frame)
{
    const SegPila *nuova = (const SegPila *)frame->ebx;
    SegPila       *prima = (SegPila *)frame->ecx;
    Process       *p     = proc_get_current();
    int            sopra;

    if (p == NULL) return ERR(ESRCH);
    if (nuova != NULL && !syscall_verify_ptr(nuova, sizeof(SegPila)))
        return ERR(EFAULT);
    if (prima != NULL && !syscall_verify_ptr(prima, sizeof(SegPila)))
        return ERR(EFAULT);

    sopra = p->seg_pila_base != 0 &&
            frame->user_esp >  p->seg_pila_base &&
            frame->user_esp <= p->seg_pila_base + p->seg_pila_dim;

    if (nuova != NULL) {
        SegPila n = *nuova;

        /* The stack we are running on cannot be changed under our feet. */
        if (sopra) return ERR(EPERM);
        if (n.flag & ~(int32_t)SS_DISABLE) return ERR(EINVAL);
        if (!(n.flag & SS_DISABLE)) {
            if (n.dim < MINSIGSTKSZ) return ERR(ENOMEM);
            if (!syscall_verify_ptr((void *)n.sp, n.dim)) return ERR(EFAULT);
        }
        if (prima != NULL) {
            prima->sp   = p->seg_pila_base;
            prima->dim  = p->seg_pila_dim;
            prima->flag = (p->seg_pila_base == 0) ? SS_DISABLE : 0;
        }
        if (n.flag & SS_DISABLE) {
            p->seg_pila_base = 0;
            p->seg_pila_dim  = 0;
        } else {
            p->seg_pila_base = n.sp;
            p->seg_pila_dim  = n.dim;
        }
        return 0;
    }

    if (prima != NULL) {
        prima->sp   = p->seg_pila_base;
        prima->dim  = p->seg_pila_dim;
        prima->flag = (p->seg_pila_base == 0) ? SS_DISABLE
                    : (sopra ? SS_ONSTACK : 0);
    }
    return 0;
}

int32_t sys_seg_manda(InterruptFrame *frame)
{
    uint32_t pid  = frame->ebx;
    int      sig  = (int)frame->ecx;
    Process *self = proc_get_current();
    Process *p, *capo;
    uint32_t tipo;

    if (self == NULL) return ERR(ESRCH);
    if (sig < 0 || sig >= 32) return ERR(EINVAL);

    p = (pid == 0) ? self : proc_get_by_pid(pid);
    if (p == NULL || p->state == PROC_ZOMBIE || p->state == PROC_UNUSED)
        return ERR(ESRCH);
    if (p != self && p->tgid != self->tgid) {
        if (p->pid <= 1) return ERR(EPERM);             /* init: never */
        if (self->uid != 0 && self->uid != p->uid) return ERR(EPERM);
    }
    if (sig == 0) return 0;

    capo = capo_di(p);
    tipo = (capo && sig != SIGKILL_) ? (capo->seg_flag[sig] & SEG_TIPO)
                                     : SEG_PREDEFINITO;

    if (tipo == SEG_IGNORA) return 0;
    if (tipo == SEG_PREDEFINITO && predefinito_ignora(sig)) return 0;

    /* Another program that does not catch it: the only way to stop it
     * safely today is the Ctrl+C one. See the head of this file. */
    if (tipo == SEG_PREDEFINITO && p->tgid != self->tgid) {
        proc_interrompi(p->pid);
        return 0;
    }

    p->seg_pendenti |= BIT(sig);
    return 0;
}
