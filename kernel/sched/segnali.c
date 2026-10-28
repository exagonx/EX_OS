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
 *
 * SIGWAIT AND SIGSUSPEND (0.279, for MariaDB, whose signal thread waits for
 * SIGTERM and SIGHUP with sigwait while every thread blocks them)
 *
 *   - A thread asleep in sigwait (seg_attesi) gets a signal of its set sent
 *     to ANY thread of its program, and wakes; sigwait also takes what is
 *     pending on the group leader, where kill(pid) lands.
 *   - A signal that another program sends and the target thread BLOCKS stays
 *     pending, as POSIX says, instead of going the Ctrl+C way: the program
 *     takes it with sigwait when it wants to.
 *   - sigsuspend sleeps with a mask until a signal it lets through is
 *     pending; the handler's context holds the OLD mask (seg_salvata), so
 *     sigreturn puts it back.
 * ============================================================================= */

#include "kernel.h"
#include "sched.h"
#include "kmalloc.h"
#include "paging.h"
#include "syscall.h"
#include "vfs.h"    /* vfs_sync: dying of a signal flushes like any exit */

#define BIT(s)      (1u << (s))
#define SIGKILL_    9
#define SIGTERM_    15
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

/* A thread of p's program asleep in sigwait for sig, or NULL. Under cli. */
static Process *chi_attende(Process *p, int sig)
{
    uint32_t gruppo = p->tgid ? p->tgid : p->pid;
    uint32_t i;

    for (i = 0; i < g_proc_posti; i++) {
        Process *q = PROC_POSTO(i);

        if (q->state == PROC_UNUSED || q->state == PROC_ZOMBIE) continue;
        if ((q->tgid ? q->tgid : q->pid) != gruppo) continue;
        if (q->seg_attesi & BIT(sig)) return q;
    }
    return NULL;
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
/* =============================================================================
 * I REGISTRI DENTRO IL CONTESTO DI UN SEGNALE: la parte che e' del processore
 * (@EXOS-64, 10 ottobre 2026)
 *
 * Il contesto che il gestore di un segnale riceve e' l'elenco dei registri
 * del programma interrotto, nell'ordine che l'ABI di quella macchina fissa
 * (a 32 bit: quello di Linux i386, REG_GS..REG_SS). E' l'unico punto di
 * questo file che conosce i registri per nome: tutto il resto li legge con
 * le macro FR_* di idt.h.
 *
 * ! EXOS64-DAFARE: a 64 bit l'elenco e' un altro (R8..R15, RDI... come in
 * Linux x86_64) e va scritto insieme alla libc a 64 bit, che ne tiene la
 * copia per i programmi. Fino ad allora un segnale a 64 bit non si consegna:
 * il kernel si ferma e lo dice, invece di dare a un gestore dei registri
 * messi a caso.
 * ============================================================================= */
#if defined(__i386__)
static void reg_salva(const InterruptFrame *f, SegContesto *uc)
{
    uc->gregs[SEG_REG_GS]  = f->ds;
    uc->gregs[SEG_REG_FS]  = f->ds;
    uc->gregs[SEG_REG_ES]  = f->ds;
    uc->gregs[SEG_REG_DS]  = f->ds;
    uc->gregs[SEG_REG_EDI] = f->edi;
    uc->gregs[SEG_REG_ESI] = f->esi;
    uc->gregs[SEG_REG_EBP] = f->ebp;
    uc->gregs[SEG_REG_ESP] = f->user_esp;
    uc->gregs[SEG_REG_EBX] = f->ebx;
    uc->gregs[SEG_REG_EDX] = f->edx;
    uc->gregs[SEG_REG_ECX] = f->ecx;
    uc->gregs[SEG_REG_EAX] = f->eax;
    uc->gregs[SEG_REG_TRAPNO] = f->int_no;
    uc->gregs[SEG_REG_ERR] = f->err_code;
    uc->gregs[SEG_REG_EIP] = f->eip;
    uc->gregs[SEG_REG_CS]  = f->cs;
    uc->gregs[SEG_REG_EFL] = f->eflags;
    uc->gregs[SEG_REG_UESP]= f->user_esp;
    uc->gregs[SEG_REG_SS]  = f->user_ss;
}

static void reg_rimetti(InterruptFrame *frame, const SegContesto *c)
{
    frame->edi      = c->gregs[SEG_REG_EDI];
    frame->esi      = c->gregs[SEG_REG_ESI];
    frame->ebp      = c->gregs[SEG_REG_EBP];
    frame->ebx      = c->gregs[SEG_REG_EBX];
    frame->edx      = c->gregs[SEG_REG_EDX];
    frame->ecx      = c->gregs[SEG_REG_ECX];
    frame->eip      = c->gregs[SEG_REG_EIP];
    frame->user_esp = c->gregs[SEG_REG_UESP];
    frame->eflags   = (frame->eflags & ~EFL_UTENTE) |
                      (c->gregs[SEG_REG_EFL] & EFL_UTENTE);
}
#else
/* @EXOS-64: i sedici registri generali, RIP, i flag e la pila. Dei segmenti
 * conta solo CS (in long mode gli altri non hanno base ne' limite). */
static void reg_salva(const InterruptFrame *f, SegContesto *uc)
{
    uc->gregs[SEG_REG_R8]  = f->r8;   uc->gregs[SEG_REG_R9]  = f->r9;
    uc->gregs[SEG_REG_R10] = f->r10;  uc->gregs[SEG_REG_R11] = f->r11;
    uc->gregs[SEG_REG_R12] = f->r12;  uc->gregs[SEG_REG_R13] = f->r13;
    uc->gregs[SEG_REG_R14] = f->r14;  uc->gregs[SEG_REG_R15] = f->r15;
    uc->gregs[SEG_REG_RDI] = f->rdi;  uc->gregs[SEG_REG_RSI] = f->rsi;
    uc->gregs[SEG_REG_RBP] = f->rbp;  uc->gregs[SEG_REG_RBX] = f->rbx;
    uc->gregs[SEG_REG_RDX] = f->rdx;  uc->gregs[SEG_REG_RAX] = f->rax;
    uc->gregs[SEG_REG_RCX] = f->rcx;  uc->gregs[SEG_REG_RSP] = f->user_rsp;
    uc->gregs[SEG_REG_RIP] = f->rip;  uc->gregs[SEG_REG_EFL] = f->rflags;
    uc->gregs[SEG_REG_CSGSFS] = f->cs & 0xFFFF;
    uc->gregs[SEG_REG_ERR]    = f->err_code;
    uc->gregs[SEG_REG_TRAPNO] = f->int_no;
}

/* ! RAX SI RIMETTE QUI, TUTTO: a 64 bit syscall_handler non scrive il
 * risultato di seg_ritorno nel registro (vedi syscall.c), perche' un
 * risultato e' di 32 bit e il registro interrotto ne ha 64. CS e SS non si
 * toccano: restano quelli con cui il programma e' entrato nel kernel. */
static void reg_rimetti(InterruptFrame *frame, const SegContesto *c)
{
    frame->r8  = c->gregs[SEG_REG_R8];   frame->r9  = c->gregs[SEG_REG_R9];
    frame->r10 = c->gregs[SEG_REG_R10];  frame->r11 = c->gregs[SEG_REG_R11];
    frame->r12 = c->gregs[SEG_REG_R12];  frame->r13 = c->gregs[SEG_REG_R13];
    frame->r14 = c->gregs[SEG_REG_R14];  frame->r15 = c->gregs[SEG_REG_R15];
    frame->rdi = c->gregs[SEG_REG_RDI];  frame->rsi = c->gregs[SEG_REG_RSI];
    frame->rbp = c->gregs[SEG_REG_RBP];  frame->rbx = c->gregs[SEG_REG_RBX];
    frame->rdx = c->gregs[SEG_REG_RDX];  frame->rax = c->gregs[SEG_REG_RAX];
    frame->rcx = c->gregs[SEG_REG_RCX];
    frame->rip      = c->gregs[SEG_REG_RIP];
    frame->user_rsp = c->gregs[SEG_REG_RSP];
    frame->rflags   = (frame->rflags & ~(uint64_t)EFL_UTENTE) |
                      (c->gregs[SEG_REG_EFL] & EFL_UTENTE);
}
#endif

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
                 FR_SP(f) >  p->seg_pila_base &&
                 FR_SP(f) <= p->seg_pila_base + p->seg_pila_dim;

    if ((fl & SA_ONSTACK) && p->seg_pila_base != 0 && !sulla_pila)
        cima = p->seg_pila_base + p->seg_pila_dim;
    else
        cima = FR_SP(f);
#if defined(__x86_64__)
    /* ! LA ZONA ROSSA: l'ABI di x86-64 lascia a una funzione i 128 byte SOTTO
     * la pila, senza che sposti RSP. Un telaio scritto li' sopra cancellerebbe
     * le variabili locali di chi e' stato interrotto. Si salta. */
    if (cima == FR_SP(f)) cima -= 128;
#endif

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

    reg_salva(f, &t.uc);
    t.uc.cr2      = (f->int_no == 14) ? indirizzo : 0;
    t.uc.maschera = p->seg_sospeso ? p->seg_salvata : p->seg_bloccati;
    t.uc.oldmask  = t.uc.maschera;
    p->seg_sospeso = 0;

    /* The page directory is this thread's: the frame lands in its memory. */
    __builtin_memcpy((void *)(uintptr_t)dove, &t, sizeof(t));

    p->seg_bloccati |= capo->seg_maschera_az[sig];
    if (!(fl & SA_NODEFER)) p->seg_bloccati |= BIT(sig);
    p->seg_bloccati &= ~NON_BLOCCABILI;

    if (fl & SA_RESETHAND) {
        capo->seg_flag[sig]        = SEG_PREDEFINITO;
        capo->seg_maschera_az[sig] = 0;
    }

    FR_IP(f)      = capo->seg_ingresso;
    FR_SP(f) = dove;
    FR_FLAGS(f)  &= ~0x00000500u;     /* TF and DF off, as the ABI expects */
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
         p->pid, sig, FR_IP(f), indirizzo);
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
    /* sigsuspend woken by a signal that was ignored after all: its mask goes
     * back here, as a handler's sigreturn would have done */
    if (p->seg_sospeso) {
        p->seg_bloccati = p->seg_salvata;
        p->seg_sospeso  = 0;
    }
}

/* =============================================================================
 * The syscalls
 * ============================================================================= */
int32_t sys_seg_azione(InterruptFrame *frame)
{
    int              sig   = (int)FR_A1(frame);
    const SegAzione *nuova = (const SegAzione *)FR_A2(frame);
    SegAzione       *prima = (SegAzione *)FR_A3(frame);
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
    uint32_t        come  = FR_A1(frame);
    const uint32_t *nuova = (const uint32_t *)FR_A2(frame);
    uint32_t       *prima = (uint32_t *)FR_A3(frame);
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
    const SegContesto *uc = (const SegContesto *)FR_A1(frame);
    Process           *p  = proc_get_current();
    SegContesto        c;

    if (p == NULL) return ERR(ESRCH);
    if (!syscall_verify_ptr(uc, sizeof(SegContesto))) {
        klog(LOG_ERROR, "SEGNALI: PID %u, sigreturn con un contesto non "
             "valido (0x%08x)", p->pid, (uint32_t)IN_NUMERO(uc));
        proc_esci_fatale(128 + 11);
    }
    c = *uc;

    reg_rimetti(frame, &c);
    /* CS, SS and DS are not taken back: ring 3 has only one of each, and a
     * context that asked for another would be asking for ring 0. */

    p->seg_bloccati = c.maschera & ~NON_BLOCCABILI;
    return (int32_t)c.gregs[SEG_REG_EAX];
}

int32_t sys_seg_pila(InterruptFrame *frame)
{
    const SegPila *nuova = (const SegPila *)FR_A1(frame);
    SegPila       *prima = (SegPila *)FR_A2(frame);
    Process       *p     = proc_get_current();
    int            sopra;

    if (p == NULL) return ERR(ESRCH);
    if (nuova != NULL && !syscall_verify_ptr(nuova, sizeof(SegPila)))
        return ERR(EFAULT);
    if (prima != NULL && !syscall_verify_ptr(prima, sizeof(SegPila)))
        return ERR(EFAULT);

    sopra = p->seg_pila_base != 0 &&
            FR_SP(frame) >  p->seg_pila_base &&
            FR_SP(frame) <= p->seg_pila_base + p->seg_pila_dim;

    if (nuova != NULL) {
        SegPila n = *nuova;

        /* The stack we are running on cannot be changed under our feet. */
        if (sopra) return ERR(EPERM);
        if (n.flag & ~(int32_t)SS_DISABLE) return ERR(EINVAL);
        if (!(n.flag & SS_DISABLE)) {
            if (n.dim < MINSIGSTKSZ) return ERR(ENOMEM);
            if (!syscall_verify_ptr((void *)(uintptr_t)n.sp, n.dim)) return ERR(EFAULT);
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

static int32_t manda(Process *self, Process *p, int sig);

int32_t sys_seg_manda(InterruptFrame *frame)
{
    uint32_t pid  = FR_A1(frame);
    int      sig  = (int)FR_A2(frame);
    Process *self = proc_get_current();
    Process *p;

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
    return manda(self, p, sig);
}

/* The delivery of sys_seg_manda, once the sender may send: also what the
 * shutdown uses (segnali_arresto). */
static int32_t manda(Process *self, Process *p, int sig)
{
    Process *capo;
    uint32_t tipo;

    /* a thread of that program asleep in sigwait for it: the signal is its */
    if (sig != SIGKILL_) {
        Process *q;

        interrupts_disable();
        q = chi_attende(p, sig);
        if (q != NULL) {
            q->seg_pendenti |= BIT(sig);
            q->seg_attesi = 0;
            if (q->state == PROC_BLOCKED) sched_unblock_locked(q->pid);
            interrupts_enable();
            return 0;
        }
        interrupts_enable();
    }

    capo = capo_di(p);
    tipo = (capo && sig != SIGKILL_) ? (capo->seg_flag[sig] & SEG_TIPO)
                                     : SEG_PREDEFINITO;

    if (tipo == SEG_IGNORA) return 0;
    if (tipo == SEG_PREDEFINITO && predefinito_ignora(sig)) return 0;

    /* Another program that does not catch it: the only way to stop it
     * safely today is the Ctrl+C one. See the head of this file. */
    if (tipo == SEG_PREDEFINITO && p->tgid != self->tgid) {
        /* ...unless that thread blocks it: then it stays pending, for a
         * sigwait to take (POSIX). SIGKILL cannot be blocked. */
        if (p->seg_bloccati & BIT(sig)) {
            p->seg_pendenti |= BIT(sig);
            return 0;
        }
        proc_interrompi(p->pid);
        return 0;
    }

    interrupts_disable();
    p->seg_pendenti |= BIT(sig);
    /* asleep in sigsuspend with this signal let through: it wakes */
    if (p->seg_sospeso && !(p->seg_bloccati & BIT(sig)) && p->state == PROC_BLOCKED)
        sched_unblock_locked(p->pid);
    interrupts_enable();
    return 0;
}

/* =============================================================================
 * segnali_arresto — the programs are told before the machine goes off
 *
 * shutdown, reboot and halt call it (sys_reboot) before the filesystems are
 * synced: SIGTERM to every program, then up to `ms` milliseconds for them to
 * end. A program that does not catch SIGTERM ends at once, as it would have
 * anyway; one that does (MariaDB: InnoDB writes its buffers and closes its
 * log) gets the time to finish, and the disks are synced after it.
 *
 * ! NOT TO THE DRIVERS, which the sync itself needs (the disk is a driver in
 * ring 3), not to init (pid 1), not to kernel tasks, and not to the program
 * that asked to shut down, which is waiting in here. Threads are not counted
 * on their own: the signal goes to the program, the leader stands for it.
 *
 * Returns how many are still alive when the time is up; their names are
 * printed, and the shutdown goes on without them.
 * ============================================================================= */
static int arresto_bersaglio(const Process *q, const Process *self)
{
    if (q->state == PROC_UNUSED || q->state == PROC_ZOMBIE) return 0;
    if (q->tgid != q->pid || q->pid <= 1)                    return 0;
    if (q->nel_kernel || q->is_driver)                       return 0;
    return q->tgid != self->tgid;
}

int segnali_arresto(uint32_t ms)
{
    uint32_t       *pid_di;                     /* 0 = not signalled */
    uint32_t        posti = g_proc_posti;       /* slots added later: born after the signal */
    Process        *self = proc_get_current();
    uint32_t        scade = g_ticks + ms / 10 + 1;
    int             i, vivi, mandati = 0;

    if (self == NULL) return 0;
    pid_di = (uint32_t *)kmalloc(posti * sizeof(uint32_t));
    if (pid_di == NULL) return 0;
    for (i = 0; i < (int)posti; i++) {
        Process *q = PROC_POSTO(i);

        pid_di[i] = 0;
        if (!arresto_bersaglio(q, self)) continue;
        pid_di[i] = q->pid;
        mandati++;
        manda(self, q, SIGTERM_);
    }
    if (mandati == 0) { kfree(pid_di); return 0; }
    kprintf("\n  Chiudo i programmi (%d): ", mandati);
    for (;;) {
        vivi = 0;
        for (i = 0; i < (int)posti; i++) {
            Process *q = PROC_POSTO(i);

            if (pid_di[i] == 0) continue;
            /* gone, or the slot already holds another process */
            if (q->pid != pid_di[i] || q->state == PROC_UNUSED || q->state == PROC_ZOMBIE)
                pid_di[i] = 0;
            else
                vivi++;
        }
        if (vivi == 0 || (int32_t)(g_ticks - scade) >= 0) break;
        sched_sleep(100);
    }
    if (vivi == 0) { kprintf("fatto.\n"); kfree(pid_di); return 0; }
    kprintf("%d non si sono chiusi in %u s:", vivi, ms / 1000);
    for (i = 0; i < (int)posti; i++)
        if (pid_di[i]) kprintf(" %s", (*PROC_POSTO(i)).name);
    kprintf("\n");
    kfree(pid_di);
    return vivi;
}

/* =============================================================================
 * sigwait — SYS_SEG_ATTENDI
 *
 * The signal is taken from this thread's pending set or from the leader's
 * (kill(pid) lands there), never delivered: that is the point of sigwait.
 * The check and the going to sleep are under one cli, so a signal sent in
 * between finds seg_attesi already set (sys_seg_manda) or is already pending.
 * ============================================================================= */
int32_t sys_seg_attendi(InterruptFrame *frame)
{
    const uint32_t *insieme = (const uint32_t *)FR_A1(frame);
    uint32_t        ms      = FR_A2(frame);
    int32_t        *preso   = (int32_t *)FR_A3(frame);
    Process        *self    = proc_get_current();
    Process        *capo;
    uint32_t        voluti, pronti, scade;
    int             sig;

    if (self == NULL) return ERR(ESRCH);
    if (!syscall_verify_ptr(insieme, sizeof(uint32_t))) return ERR(EFAULT);
    if (preso != NULL && !syscall_verify_ptr(preso, sizeof(int32_t))) return ERR(EFAULT);
    voluti = *insieme & ~NON_BLOCCABILI & ~1u;
    if (voluti == 0) return ERR(EINVAL);
    scade = (ms == 0xFFFFFFFFu) ? 0 : g_ticks + ms / 10 + 1;

    for (;;) {
        interrupts_disable();
        capo   = capo_di(self);
        pronti = self->seg_pendenti & voluti;
        if (pronti == 0 && capo != NULL && capo != self)
            pronti = capo->seg_pendenti & voluti;
        if (pronti != 0) {
            for (sig = 1; sig < 32 && !(pronti & BIT(sig)); sig++) ;
            if (self->seg_pendenti & BIT(sig)) self->seg_pendenti &= ~BIT(sig);
            else                               capo->seg_pendenti &= ~BIT(sig);
            self->seg_attesi = 0;
            interrupts_enable();
            if (preso != NULL) *preso = sig;
            return 0;
        }
        if (self->interrotto) {
            self->seg_attesi = 0;
            interrupts_enable();
            return ERR(EINTR);
        }
        if (ms == 0 || (scade != 0 && (int32_t)(g_ticks - scade) >= 0)) {
            self->seg_attesi = 0;
            interrupts_enable();
            return ERR(EAGAIN);
        }
        self->seg_attesi  = voluti;
        self->block_until = scade;
        sched_block(PROC_BLOCKED);          /* turns interrupts back on */
        self->seg_attesi  = 0;
        self->block_until = 0;
    }
}

/* =============================================================================
 * sigsuspend — SYS_SEG_SOSPENDI
 *
 * Sleeps with the given mask until a signal it lets through is pending, then
 * returns -EINTR; on the way out segnali_consegna runs the handler with the
 * old mask in its context (seg_salvata), or puts the old mask back itself.
 * ============================================================================= */
int32_t sys_seg_sospendi(InterruptFrame *frame)
{
    const uint32_t *m    = (const uint32_t *)FR_A1(frame);
    Process        *self = proc_get_current();
    uint32_t        nuova;

    if (self == NULL) return ERR(ESRCH);
    if (!syscall_verify_ptr(m, sizeof(uint32_t))) return ERR(EFAULT);
    nuova = *m & ~NON_BLOCCABILI;

    interrupts_disable();
    self->seg_salvata = self->seg_bloccati;
    self->seg_bloccati = nuova;
    self->seg_sospeso = 1;
    while ((self->seg_pendenti & ~self->seg_bloccati) == 0 && !self->interrotto) {
        self->block_until = 0;
        sched_block(PROC_BLOCKED);
        interrupts_disable();
    }
    interrupts_enable();
    return ERR(EINTR);
}
