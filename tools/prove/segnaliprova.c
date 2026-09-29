/* =============================================================================
 * tools/prove/segnaliprova.c — signals and mprotect inside EX-OS (@SEGNALI)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Built with i386-exos-gcc against the static libc.a and run in QEMU by
 * tools/prova_segnali.sh. Every check prints "ok" or "NO"; the last line is
 * "segnaliprova: <n> NO".
 *
 * What it proves, in the order SpiderMonkey and Firefox need it: a handler
 * runs for raise, kill and pthread_kill; the mask holds a signal back and lets
 * it out; a fault (NULL, mprotect, PROT_NONE, ud2, division) reaches its
 * handler with the right si_code and address; a handler may change the
 * registers and the program goes on from them; sigsetjmp/siglongjmp get out
 * of a handler and give the mask back; the alternate stack is used, even
 * when the normal one has run out.
 * ============================================================================= */
#include <signal.h>
#include <setjmp.h>
#include <pthread.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int g_no = 0;
static int g_n  = 0;

static void verifica(int cond, const char *cosa)
{
    g_n++;
    printf("%s %d %s\n", cond ? "ok" : "NO", g_n, cosa);
    if (!cond) g_no++;
}

static volatile int      g_preso;
static volatile int      g_codice;
static volatile void    *g_indirizzo;
static volatile void    *g_dove_locale;
static volatile int      g_pila_flag;
static sigjmp_buf        g_salto;

static void semplice(int sig) { g_preso = sig; }

static void con_info(int sig, siginfo_t *si, void *uc)
{
    (void)uc;
    g_preso     = sig;
    g_codice    = si->si_code;
    g_indirizzo = si->si_addr;
}

static void e_salta(int sig, siginfo_t *si, void *uc)
{
    (void)uc;
    g_preso     = sig;
    g_codice    = si->si_code;
    g_indirizzo = si->si_addr;
    siglongjmp(g_salto, 1);
}

/* ud2 is two bytes: the handler moves EIP past it and returns. */
static void salta_ud2(int sig, siginfo_t *si, void *v)
{
    ucontext_t *uc = (ucontext_t *)v;
    (void)si;
    g_preso = sig;
    uc->uc_mcontext.gregs[REG_EIP] += 2;
    uc->uc_mcontext.gregs[REG_EAX] = 1234;
}

static void sulla_pila(int sig)
{
    stack_t s;
    int     locale;

    g_preso       = sig;
    g_dove_locale = &locale;
    if (sigaltstack(NULL, &s) == 0) g_pila_flag = s.ss_flags;
}

static int profonda(int n)
{
    volatile char buf[1024];
    if (n < 0) return 0;          /* never: it keeps -Winfinite-recursion quiet */
    buf[0] = (char)n;
    return profonda(n + 1) + buf[0];
}

static volatile pthread_t g_ricevente;
static volatile int       g_filo_giusto;
static volatile int       g_filo_via;

static void nel_filo(int sig)
{
    g_preso       = sig;
    g_filo_giusto = pthread_equal(pthread_self(), g_ricevente);
}

static void *filo(void *arg)
{
    int giri = 0;
    (void)arg;
    while (!g_filo_via && giri++ < 2000) sched_yield();
    return NULL;
}

static void metti(int sig, void (*f)(int, siginfo_t *, void *), int flag)
{
    struct sigaction a;
    memset(&a, 0, sizeof(a));
    a.sa_sigaction = f;
    a.sa_flags     = SA_SIGINFO | flag;
    sigemptyset(&a.sa_mask);
    sigaction(sig, &a, NULL);
}

int main(void)
{
    struct sigaction a, vecchia;
    sigset_t m, prima;
    char    *pag;
    static char pila_alt[16384];
    stack_t  s;

    /* 1-2. raise runs the handler before it returns. */
    signal(SIGUSR1, semplice);
    g_preso = 0;
    raise(SIGUSR1);
    verifica(g_preso == SIGUSR1, "raise: il gestore gira prima che raise torni");

    metti(SIGUSR2, con_info, 0);
    g_preso = 0; g_codice = 99;
    raise(SIGUSR2);
    verifica(g_preso == SIGUSR2 && g_codice == SI_TKILL, "SA_SIGINFO: signo e si_code (SI_TKILL)");

    /* 3-4. The mask holds it back and lets it out. */
    sigemptyset(&m);
    sigaddset(&m, SIGUSR1);
    sigprocmask(SIG_BLOCK, &m, &prima);
    g_preso = 0;
    raise(SIGUSR1);
    verifica(g_preso == 0, "bloccato: raise non consegna");
    sigprocmask(SIG_SETMASK, &prima, NULL);
    verifica(g_preso == SIGUSR1, "sbloccato: arriva all'uscita di sigprocmask");

    /* 5. kill to ourselves. */
    g_preso = 0;
    kill(getpid(), SIGUSR1);
    verifica(g_preso == SIGUSR1, "kill(getpid()): consegnato");

    /* 6. SIG_IGN. */
    signal(SIGUSR2, SIG_IGN);
    raise(SIGUSR2);
    verifica(1, "SIG_IGN: raise non fa niente e si prosegue");

    /* 7. SA_RESETHAND: once, then back to SIG_DFL. */
    memset(&a, 0, sizeof(a));
    a.sa_handler = semplice;
    a.sa_flags   = SA_RESETHAND;
    sigaction(SIGUSR1, &a, NULL);
    g_preso = 0;
    raise(SIGUSR1);
    sigaction(SIGUSR1, NULL, &vecchia);
    verifica(g_preso == SIGUSR1 && vecchia.sa_handler == SIG_DFL, "SA_RESETHAND: una volta, poi SIG_DFL");

    /* 8. NULL: SIGSEGV, SEGV_MAPERR, address 0, out with siglongjmp. */
    metti(SIGSEGV, e_salta, 0);
    g_preso = 0;
    if (sigsetjmp(g_salto, 1) == 0) {
        *(volatile int *)0 = 1;
        verifica(0, "NULL: il fault non e' arrivato");
    } else {
        verifica(g_preso == SIGSEGV && g_codice == SEGV_MAPERR && g_indirizzo == NULL,
                 "NULL: SIGSEGV, SEGV_MAPERR, si_addr 0");
    }
    sigprocmask(SIG_BLOCK, NULL, &m);
    verifica(!sigismember(&m, SIGSEGV), "siglongjmp rende la maschera (SIGSEGV non resta bloccato)");

    /* 9-11. mprotect: read-only, then writable again, contents intact. */
    pag = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    verifica(pag != MAP_FAILED, "mmap di due pagine");
    pag[4096] = 42;
    verifica(mprotect(pag + 4096, 4096, PROT_READ) == 0, "mprotect PROT_READ");
    g_preso = 0;
    if (sigsetjmp(g_salto, 1) == 0) {
        volatile char v = pag[4096];
        pag[4096] = (char)(v + 1);
        verifica(0, "sola lettura: la scrittura e' passata");
    } else {
        verifica(g_preso == SIGSEGV && g_codice == SEGV_ACCERR && g_indirizzo == pag + 4096,
                 "sola lettura: SIGSEGV, SEGV_ACCERR, l'indirizzo della pagina");
    }
    pag[0] = 7;
    verifica(pag[0] == 7, "la pagina accanto resta scrivibile");
    mprotect(pag + 4096, 4096, PROT_READ | PROT_WRITE);
    pag[4096] += 1;
    verifica(pag[4096] == 43, "di nuovo scrivibile, il contenuto c'e' ancora");

    /* 12-13. PROT_NONE: not even a read, and back. */
    mprotect(pag, 8192, PROT_NONE);
    g_preso = 0;
    if (sigsetjmp(g_salto, 1) == 0) {
        volatile char v = pag[0];
        (void)v;
        verifica(0, "PROT_NONE: la lettura e' passata");
    } else {
        verifica(g_preso == SIGSEGV && g_codice == SEGV_ACCERR && g_indirizzo == pag,
                 "PROT_NONE: anche la lettura cade, SEGV_ACCERR");
    }
    mprotect(pag, 8192, PROT_READ | PROT_WRITE);
    verifica(pag[0] == 7 && pag[4096] == 43, "PROT_NONE e ritorno: i dati ci sono");

    /* 14. mmap(PROT_NONE) is a reservation that mprotect opens. */
    {
        char *r = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        g_preso = 0;
        if (sigsetjmp(g_salto, 1) == 0) {
            r[0] = 1;
            verifica(0, "mmap PROT_NONE: la scrittura e' passata");
        } else {
            mprotect(r, 4096, PROT_READ | PROT_WRITE);
            r[0] = 5;
            verifica(g_preso == SIGSEGV && r[0] == 5, "mmap PROT_NONE, poi mprotect la apre");
        }
    }

    /* 15. The handler changes EIP and EAX: the program goes on from them. */
    metti(SIGILL, salta_ud2, 0);
    {
        int eax = 0;
        g_preso = 0;
        __asm__ volatile ("movl $0, %%eax\n\tud2\n\tmovl %%eax, %0" : "=r"(eax) : : "eax");
        verifica(g_preso == SIGILL && eax == 1234, "SIGILL: il gestore sposta EIP e cambia EAX");
    }

    /* 16. Division by zero. */
    metti(SIGFPE, e_salta, 0);
    g_preso = 0;
    if (sigsetjmp(g_salto, 1) == 0) {
        volatile int zero = 0, x = 10;
        x = x / zero;
        verifica(0, "divisione per zero: niente segnale");
    } else {
        verifica(g_preso == SIGFPE && g_codice == FPE_INTDIV, "divisione per zero: SIGFPE, FPE_INTDIV");
    }

    /* 17-18. The alternate stack. */
    memset(&s, 0, sizeof(s));
    s.ss_sp   = pila_alt;
    s.ss_size = sizeof(pila_alt);
    verifica(sigaltstack(&s, NULL) == 0, "sigaltstack");
    memset(&a, 0, sizeof(a));
    a.sa_handler = sulla_pila;
    a.sa_flags   = SA_ONSTACK;
    sigaction(SIGUSR1, &a, NULL);
    g_preso = 0; g_dove_locale = NULL; g_pila_flag = -1;
    raise(SIGUSR1);
    verifica(g_preso == SIGUSR1 &&
             (char *)g_dove_locale >= pila_alt &&
             (char *)g_dove_locale <  pila_alt + sizeof(pila_alt) &&
             g_pila_flag == SS_ONSTACK,
             "SA_ONSTACK: il gestore gira sulla pila alternativa (SS_ONSTACK)");

    /* 19. The stack runs out: SIGSEGV still arrives, on the alternate one. */
    metti(SIGSEGV, e_salta, SA_ONSTACK);
    g_preso = 0;
    if (sigsetjmp(g_salto, 1) == 0) {
        profonda(0);
        verifica(0, "stack esaurito: nessun segnale");
    } else {
        verifica(g_preso == SIGSEGV, "stack esaurito: SIGSEGV sulla pila alternativa");
    }

    /* 20. pthread_kill reaches THAT thread. */
    {
        pthread_t t;
        signal(SIGUSR1, nel_filo);
        g_preso = 0; g_filo_giusto = 0; g_filo_via = 0;
        pthread_create(&t, NULL, filo, NULL);
        g_ricevente = t;
        pthread_kill(t, SIGUSR1);
        {
            int giri = 0;
            while (!g_preso && giri++ < 1000) sched_yield();
        }
        g_filo_via = 1;
        pthread_join(t, NULL);
        verifica(g_preso == SIGUSR1 && g_filo_giusto, "pthread_kill: il gestore gira nel filo bersaglio");
    }

    printf("segnaliprova: %d NO\n", g_no);
    return g_no ? 1 : 0;
}
