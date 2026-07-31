/* =============================================================================
 * bin/hello/hello.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Programma utente di esempio per EX-OS (/bin/hello)
 *
 * Mostra lo schema minimo per scrivere un binario eseguibile da /bin:
 *   - nessuna libc, nessuna dipendenza esterna
 *   - syscall dirette via int 0x80 (stesso ABI usato da bin/sh/shell.c)
 *   - entry point _start (richiesto dall'ELF loader del kernel)
 *   - termina sempre con SYS_EXIT, non deve "ritornare" da _start
 *
 * Uso questo file come scheletro per qualsiasi nuovo programma da
 * inserire in /bin. Copia la cartella, rinomina i file, cambia la
 * logica dentro _start.
 * ============================================================================= */

/* ============================================================================
 * NOTA COMPILAZIONE:
 * Compilato come programma utente ELF32 statico (vedi Makefile, target hello).
 * Stessi CFLAGS_USER usati per bin/sh/shell.c.
 * ============================================================================ */

typedef unsigned int   uint32_t;
typedef int            int32_t;
typedef uint32_t       size_t;
#define NULL ((void*)0)

/* =============================================================================
 * Syscall numbers (identici a kernel/include/syscall.h e a bin/sh/shell.c)
 * ============================================================================= */
#define SYS_EXIT    1
#define SYS_READ    3
#define SYS_WRITE   4
#define SYS_GETPID  20

#define STDIN   0
#define STDOUT  1
#define STDERR  2

/* =============================================================================
 * Wrapper syscall inline ASM (identici a shell.c)
 * ============================================================================= */
static inline int32_t syscall1(uint32_t num, uint32_t a)
{
    int32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a)
        : "memory"
    );
    return ret;
}

static inline int32_t syscall3(uint32_t num, uint32_t a, uint32_t b, uint32_t c)
{
    int32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a), "c"(b), "d"(c)
        : "memory"
    );
    return ret;
}

static uint32_t hl_strlen(const char *s)
{
    uint32_t n = 0;
    while (s && *s++) n++;
    return n;
}

static int hl_write(int fd, const char *buf, uint32_t n)
{
    return syscall3(SYS_WRITE, (uint32_t)fd, (uint32_t)buf, n);
}

static void print(const char *s)
{
    if (s) hl_write(STDOUT, s, hl_strlen(s));
}

static void println(const char *s)
{
    print(s);
    hl_write(STDOUT, "\n", 1);
}

/* Stampa un intero senza segno in decimale (nessuna libc disponibile) */
static void print_uint(uint32_t v)
{
    char buf[12];
    uint32_t i = 0, j;

    if (v == 0) { print("0"); return; }
    while (v > 0) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    for (j = 0; j < i / 2; j++) {
        char tmp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = tmp;
    }
    buf[i] = '\0';
    print(buf);
}

/* =============================================================================
 * _start — Entry point del programma
 *
 * Chiamato direttamente dall'ELF loader del kernel quando lo shell
 * esegue "exec /bin/hello" (o "hello" se /bin è nel PATH).
 * ============================================================================= */
void _start(void)
{
    int pid = syscall1(SYS_GETPID, 0);

    println("Ciao da /bin/hello!");
    print("PID corrente: ");
    print_uint((uint32_t)pid);
    print("\n");

    /* Un programma utente NON deve mai ritornare da _start:
     * non c'e' nessun "caller" a cui tornare. Termina sempre con SYS_EXIT. */
    syscall1(SYS_EXIT, 0);

    /* Non dovrebbe mai arrivare qui */
    for (;;) {}
}
