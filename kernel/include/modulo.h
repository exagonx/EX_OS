/* =============================================================================
 * kernel/include/modulo.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Kernel modules: code that runs INSIDE the kernel but is a file on disk,
 * loaded after the root is mounted (kernel/loader/modulo.c). The first one
 * is /dev/ext4.mod, the writing half of ext2/ext3/ext4.
 *
 * A module is an ELF shared object (ELF32 at 32 bit, ELF64 at 64 bit) with
 * only RELATIVE relocations: it names no kernel symbol. Everything it uses
 * comes through the ModuloKernel table handed to its entry point, and an
 * interface of the kernel is asked by name and ABI number - so a module
 * built against another layout says so instead of calling into the void.
 *
 * At the end of the file, 64 bytes the build adds (tools/modulo-coda.py):
 *
 *     "EXOSMOD1"          8 bytes
 *     kernel version     16 bytes, NUL padded ("0.263")
 *     architecture        8 bytes, NUL padded ("i386" / "x86_64")
 *     SHA-256            32 bytes, of everything before them
 *
 * ! THE SHA-256 FINDS A DAMAGED FILE, NOT A WRONG ONE. Whoever can write
 * /dev can also recompute it; what it protects against is a half-copied
 * update or a bad sector, which in kernel code means a crash or a write to
 * a random block. The version ties the module to the kernel whose structures
 * it was built for.
 * ============================================================================= */
#ifndef MODULO_H
#define MODULO_H

#include "kernel.h"

#define MODULO_ABI          1u
#define MODULO_CODA         64u
#define MODULO_MAGIA        "EXOSMOD1"
#define MODULO_MAX          8
#define MODULO_NOME_MAX     32
#define MODULO_DIM_MAX      (1024u * 1024u)     /* a larger file is not a module */

#if defined(__x86_64__)
#define MODULO_ARCH         "x86_64"
#else
#define MODULO_ARCH         "i386"
#endif

/* The table a module's entry point receives. Fields are only ever added at
 * the end (dim tells the module how many there are). */
typedef struct {
    uint32_t     abi;                   /* MODULO_ABI */
    uint32_t     dim;                   /* sizeof(ModuloKernel) */
    void       (*klog)(int livello, const char *fmt, ...);
    const void *(*interfaccia)(const char *nome, uint32_t abi);
    void      *(*kmalloc)(size_t n);
    void       (*kfree)(void *p);
} ModuloKernel;

/* The entry point (e_entry): 0 = running, anything else = it refused, and
 * then it must not have registered anything. */
typedef int (*ModuloIngresso)(const ModuloKernel *k);

/* Errors, as the syscall returns them (negated errno numbers). */
#define MODULO_E_NON_CORR   74      /* EBADMSG: version, arch or SHA-256 */
#define MODULO_E_GIA        17      /* EEXIST:  a module with that name is loaded */
#define MODULO_E_NON_MOD    8       /* ENOEXEC: not a module, or it refused */

/* Loads a module. forza = 1 skips version, arch and SHA-256 (never the ELF
 * checks). 0, or a negative MODULO_E_* / errno. */
int  modulo_carica(const char *percorso, int forza);

/* The names of the loaded modules, one per line, in buf (NUL terminated).
 * Returns how many. */
int  modulo_elenco(char *buf, uint32_t max);

/* At boot, right after vfs_init: /dev/ext4.mod, with a message on screen and
 * the remedy if it is missing or refused. */
void modulo_avvio(void);

#endif /* MODULO_H */
