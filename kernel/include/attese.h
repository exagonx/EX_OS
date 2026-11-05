/* =============================================================================
 * kernel/include/attese.h - who sleeps waiting on an object, all of them
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Born in pipe.c (kernel 0.313, fork): the first waiter inline, the others in
 * an array that doubles; a change wakes all of them and each looks again.
 * Since 0.317 the pty uses it too (voce 33 h): up to 0.316 a pty remembered
 * one waiter per direction, and the second replaced the first, who slept
 * until something else came - a parent and its forked child reading one
 * terminal, or two threads.
 *
 * Rules: attese_metti, attese_togli, attese_sveglia_locked with interrupts
 * OFF; attese_cresci with interrupts ON (it allocates), then check again.
 * The object holding an Attese is never freed (pipes and ptys are reused):
 * when it is reset, attese_azzera_tieni keeps the array.
 * ============================================================================= */
#ifndef ATTESE_H
#define ATTESE_H

#include "kernel.h"
#include "sched.h"
#include "kmalloc.h"

typedef struct {
    uint32_t  uno;          /* the first waiter, 0 = none */
    uint32_t *altri;        /* the others (kmalloc), kept across reuse */
    uint32_t  n, posti;
} Attese;

static inline int attese_c_e(const Attese *a, uint32_t pid)
{
    uint32_t i;

    if (a->uno == pid) return 1;
    for (i = 0; i < a->n; i++) if (a->altri[i] == pid) return 1;
    return 0;
}

/* 1 = registered (or already there), 0 = no room: attese_cresci, then again */
static inline int attese_metti(Attese *a, uint32_t pid)
{
    if (attese_c_e(a, pid)) return 1;
    if (a->uno == 0) { a->uno = pid; return 1; }
    if (a->n >= a->posti) return 0;
    a->altri[a->n++] = pid;
    return 1;
}

static inline void attese_togli(Attese *a, uint32_t pid)
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
static inline void attese_sveglia_locked(Attese *a)
{
    uint32_t i;

    if (a->uno) sched_unblock_locked(a->uno);
    for (i = 0; i < a->n; i++) sched_unblock_locked(a->altri[i]);
    a->uno = 0;
    a->n   = 0;
}

/* Interrupts ON. Doubles the array of `a` (the pipe is never freed, so `a`
 * stays valid). Returns 0 out of memory. */
static inline int attese_cresci(Attese *a)
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

/* The object is reset (memset): the waiters go, the array stays for reuse. */
#define ATTESE_TIENI(a, salva)   do { (salva) = *(a); } while (0)
static inline void attese_rimetti(Attese *a, const Attese *salva)
{
    a->uno   = 0;
    a->n     = 0;
    a->altri = salva->altri;
    a->posti = salva->posti;
}

#endif
