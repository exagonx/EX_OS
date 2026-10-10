/* =============================================================================
 * tools/prove64/primo.c — il primo programma a 64 bit di EX-OS (@EXOS-64, 3d)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Non ha la libc: a 64 bit non c'e' ancora (tappa 4). E' la prova che il
 * kernel a 64 bit carica un ELF64, lo avvia in ring 3 e gli risponde: tre
 * chiamate di sistema con «int 0x80» - numero in RAX, argomenti in RBX, RCX,
 * RDX come a 32 bit in EAX, EBX, ECX, EDX (le macro FR_* di idt.h).
 *
 * Fa quattro cose che a 32 bit non si possono fare, e le dice:
 *   - un puntatore di otto byte;
 *   - una moltiplicazione che non sta in 32 bit;
 *   - R8..R15, tenuti attraverso una chiamata di sistema (il kernel li
 *     salva e li rimette: se uno torna cambiato, e' l'ingresso a sbagliare);
 *   - si ferma con sleep, cosi' passa per lo scheduler e ritorna.
 * Sul dischetto di prova si chiama /bin/sh, perche' e' quello che il kernel
 * avvia sulle console.
 * ============================================================================= */
typedef unsigned long long u64;

#define SYS_EXIT    1
#define SYS_WRITE   4
#define SYS_GETPID  20
#define SYS_SLEEP   162

static long chiama(long n, long a, long b, long c)
{
    long r;

    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}

static u64 lung(const char *s) { u64 n = 0; while (s[n]) n++; return n; }
static void scrivi(const char *s) { chiama(SYS_WRITE, 1, (long)s, (long)lung(s)); }

static void esa(u64 v)
{
    char b[19];
    int  i;

    b[0] = '0'; b[1] = 'x';
    for (i = 0; i < 16; i++) b[2 + i] = "0123456789abcdef"[(v >> (60 - 4 * i)) & 15];
    b[18] = 0;
    scrivi(b);
}

static void numero(u64 v)
{
    char b[24];
    int  n = 23;

    b[n] = 0;
    do { b[--n] = (char)('0' + v % 10); v /= 10; } while (v);
    scrivi(b + n);
}

void _start(void)
{
    volatile u64 a = 0x100000001ull, b = 16;
    u64 r8 = 0, r15 = 0;
    long pid;
    int  bene = 1;

    scrivi("\nprimo64: un programma a 64 bit in ring 3\n");
    pid = chiama(SYS_GETPID, 0, 0, 0);
    scrivi("  il mio PID e' "); numero((u64)pid);
    scrivi(", un puntatore e' di "); numero(sizeof(void *)); scrivi(" byte\n");
    scrivi("  _start sta a "); esa((u64)(void *)_start); scrivi("\n");
    scrivi("  0x100000001 * 16 = "); esa(a * b);
    if (a * b != 0x1000000010ull) bene = 0;
    scrivi(bene ? "   giusto\n" : "   SBAGLIATO\n");

    /* R8 e R15 attraverso una chiamata di sistema */
    __asm__ volatile(
        "mov $0x1122334455667788, %%r8\n"
        "mov $0x0fedcba987654321, %%r15\n"
        "mov $20, %%eax\n"
        "int $0x80\n"
        "mov %%r8, %0\n"
        "mov %%r15, %1\n"
        : "=&r"(r8), "=&r"(r15) : : "rax", "r8", "r15", "memory");
    if (r8 != 0x1122334455667788ull || r15 != 0x0fedcba987654321ull) bene = 0;
    scrivi("  R8 e R15 dopo una chiamata di sistema: ");
    scrivi((r8 == 0x1122334455667788ull && r15 == 0x0fedcba987654321ull) ? "intatti\n" : "CAMBIATI\n");

    if (pid <= 0) bene = 0;
    chiama(SYS_SLEEP, 200, 0, 0);
    scrivi("  dormito 200 ms e tornato\n");

    scrivi(bene ? "EXOS64-TAPPA3D-OK\n" : "EXOS64-TAPPA3D: qualcosa non torna\n");
    chiama(SYS_EXIT, 0, 0, 0);
    for (;;) { }
}
