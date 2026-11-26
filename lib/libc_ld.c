/* =============================================================================
 * lib/libc_ld.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * /lib/ld-exos.so — THE SHARED LIBC, AND ITS OWN INTERPRETER (voce 105)
 *
 * One file, as musl does it: the libc built -fPIC is also the program
 * interpreter. A program linked to the shared libc carries PT_INTERP
 * "/lib/ld-exos.so" and DT_NEEDED "ld-exos.so"; the kernel (0.350) maps this
 * file after the program and starts here, with the auxv address in the first
 * word of the stack (kernel/loader/elf.c). Here:
 *
 *   1. the libc relocates itself: RELATIVE, and every symbol to its own
 *      definition. Until that is done nothing here may touch a global that is
 *      reached through the GOT: only static functions and PC-relative data;
 *   2. the libc becomes module 1 of the global scope, after the program;
 *   3. __exos_dl_avvio_programma (libc_dl.c), the same code a program with
 *      libc.a inside used: DT_NEEDED, the program's relocations (COPY too);
 *   4. the libc binds itself again, to the global scope: a variable the
 *      program has copied (environ, optarg, stdout...) is used there by both,
 *      and a function the program defines (malloc, getenv...) wins, as on
 *      Linux. Then the libc's constructors, then the libraries';
 *   5. the jump to the program's entry, the stack as the kernel left it: its
 *      _start and _libc_start (lib/libc_avvio.c, linked into the program)
 *      run the program's constructors and main.
 *
 * ! A PROGRAM WITH libc.a INSIDE (built before the shared libc, with some .so
 * of its own) also has PT_INTERP but no DT_NEEDED "ld-exos.so": it loads its
 * own libraries. For it this file does nothing and jumps at once.
 *
 * Included at the end of libc_dl.c, only with -DEXOS_LIBC_ELF.
 * ============================================================================= */

#define LD_NOME "ld-exos.so"

extern const DlDin _DYNAMIC[] __attribute__((visibility("hidden")));
extern const DlTestaElf __ehdr_start __attribute__((visibility("hidden")));
extern char *__exos_argv0;          /* lib/libc_avvio.c */
char ***__environ_dove(void);

static DlModulo g_ld_modulo;

/* the address S a symbol of the libc had after step 1: 0 = undefined */
static uintptr_t ld_proprio(uintptr_t base, const DlSim *s)
{
    return s->st_shndx != 0 ? base + (uintptr_t)s->st_value : 0;
}

/* Step 1. ! NO GLOBAL THROUGH THE GOT IN HERE, nor any call that is not to
 * the libc itself (-Bsymbolic-functions makes those direct). */
static void ld_riloca_tabella(uintptr_t base, const DlSim *sim, const DlRel *r, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        unsigned int tipo = DL_R_TIPO(r[i].r_info), si = DL_R_SIM(r[i].r_info);
        uintptr_t   *p = (uintptr_t *)(base + r[i].r_offset);
        uintptr_t    S = si ? ld_proprio(base, &sim[si]) : 0;
#if defined(__x86_64__)
        uintptr_t    A = (uintptr_t)r[i].r_addend;

        switch (tipo) {
        case 8:  *p = base + A; break;                     /* RELATIVE */
        case 1:  *p = S + A; break;                        /* 64 */
        case 6: case 7: *p = S; break;                     /* GLOB_DAT, JUMP_SLOT */
        }
#else
        switch (tipo) {
        case 8:  *p += base; break;                        /* RELATIVE: addend in place */
        case 1:  *p += S; break;                           /* 32 */
        case 6: case 7: *p = S; break;                     /* GLOB_DAT, JMP_SLOT */
        }
#endif
    }
}

static void ld_se_stesso(uintptr_t base)
{
    const DlDin *d;
    const DlSim *sim = 0;
    const DlRel *rel = 0, *jmp = 0;
    size_t       reln = 0, jmpn = 0;

    for (d = _DYNAMIC; d->d_tag; d++) {
        switch (d->d_tag) {
        case 6:           sim = (const DlSim *)(base + d->d_val); break;
        case DL_DT_REL:   rel = (const DlRel *)(base + d->d_val); break;
        case DL_DT_RELSZ: reln = d->d_val / sizeof(DlRel); break;
        case 23:          jmp = (const DlRel *)(base + d->d_val); break;
        case 2:           jmpn = d->d_val / sizeof(DlRel); break;
        }
    }
    if (rel) ld_riloca_tabella(base, sim, rel, reln);
    if (jmp) ld_riloca_tabella(base, sim, jmp, jmpn);
}

/* Step 4: each symbol of the libc moved to what the global scope says, by
 * the difference: at 32 bit the addend of R_386_32 is gone into the word. */
static int ld_ricollega_tabella(const DlRel *r, size_t n)
{
    DlModulo *m = &g_ld_modulo;
    size_t    i;

    for (i = 0; i < n; i++) {
        unsigned int tipo = DL_R_TIPO(r[i].r_info), si = DL_R_SIM(r[i].r_info);
        uintptr_t   *p, S, prima;
        DlModulo    *chi;
        const DlSim *s = 0, *ds;

        if (!si || (tipo != 1 && tipo != 6 && tipo != 7)) continue;
        ds = &m->sim[si];
        if ((ds->st_info >> 4) == 0) continue;                       /* local */
        S = (uintptr_t)dl_globale(m->str + ds->st_name, &chi, &s, tipo == 7);
        if (!S && !(chi && s)) continue;                             /* nobody: as it was */
        prima = ld_proprio(m->base, ds);
        if (S == prima) continue;
        p = (uintptr_t *)(m->base + r[i].r_offset);
        *p += S - prima;
    }
    return 0;
}

static int ld_dopo_programma(void)
{
    DlModulo *m = &g_ld_modulo;

    if (m->rel_n) ld_ricollega_tabella((const DlRel *)m->rel_v, m->rel_n);
    if (m->jmp_n) ld_ricollega_tabella((const DlRel *)m->jmp_v, m->jmp_n);
    dl_avvia_modulo(m);                  /* the libc's own constructors, before any library's */
    return 0;
}

/* the program's DT_NEEDED: does it name the shared libc? */
static int ld_programma_condiviso(const DlDin *din)
{
    const DlDin *d;
    const char  *str = 0;

    for (d = din; d->d_tag; d++) if (d->d_tag == 5) str = (const char *)d->d_val;
    if (!str) return 0;
    for (d = din; d->d_tag; d++) {
        const char *n, *b;
        if (d->d_tag != 1) continue;
        n = str + d->d_val;
        b = strrchr(n, '/');
        if (strcmp(b ? b + 1 : n, LD_NOME) == 0) return 1;
    }
    return 0;
}

/* Called from _ld_avvio with the stack the kernel made: [auxv, argc, argv,
 * envp]. Returns where to jump: the program's entry. */
__attribute__((used)) static uintptr_t ld_principale(uintptr_t *sp)
{
    const uintptr_t *aux = (const uintptr_t *)sp[0];
    char           **argv = (char **)sp[2];
    char           **envp = (char **)sp[3];
    const DlFase    *ph = 0, *f;
    uintptr_t        base = 0, entry = 0, alto = 0;
    int              phnum = 0, i;
    DlModulo        *m = &g_ld_modulo;
    const DlDin     *d;

    for (; aux && aux[0] != 0; aux += 2) {
        switch (aux[0]) {
        case 3: ph = (const DlFase *)aux[1]; break;          /* AT_PHDR */
        case 5: phnum = (int)aux[1]; break;                  /* AT_PHNUM */
        case 7: base = aux[1]; break;                        /* AT_BASE */
        case 9: entry = aux[1]; break;                       /* AT_ENTRY */
        }
    }
    if (!base) base = (uintptr_t)&__ehdr_start;              /* started by hand: we know where */
    ld_se_stesso(base);
    /* ---- from here on the libc works ---- */
    if (!ph || !entry) {
        static const char e[] = LD_NOME ": the shared libc; a program runs it, not you\n";
        write(2, e, sizeof(e) - 1);
        _exit(127);
    }
    for (i = 0; i < phnum; i++)
        if (ph[i].p_type == 2) g_ld_prog_din = (const DlDin *)(uintptr_t)ph[i].p_vaddr;   /* PT_DYNAMIC */
    if (!g_ld_prog_din || !ld_programma_condiviso(g_ld_prog_din)) return entry;   /* libc.a inside */

    /* the libc as a module: what dlsym, dladdr and the relocations see */
    m->magia = DL_MOD_MAGIA;
    m->percorso = "/lib/" LD_NOME;
    m->base = base;
    m->mappa = (void *)base;
    m->din = _DYNAMIC;
    m->conta = 1 << 30;                                      /* never closed */
    f = (const DlFase *)((const char *)&__ehdr_start + __ehdr_start.e_phoff);
    for (i = 0; i < __ehdr_start.e_phnum; i++)
        if (f[i].p_type == 1 && f[i].p_vaddr + f[i].p_memsz > alto) alto = f[i].p_vaddr + f[i].p_memsz;
    m->misura = (alto + 4095) & ~(uintptr_t)4095;
    for (d = _DYNAMIC; d->d_tag; d++) {
        switch (d->d_tag) {
        case 5:  m->str = (const char *)(base + d->d_val); break;
        case 6:  m->sim = (const DlSim *)(base + d->d_val); break;
        case DL_DT_REL:   m->rel_v = (const void *)(base + d->d_val); break;
        case DL_DT_RELSZ: m->rel_n = d->d_val / sizeof(DlRel); break;
        case 23: m->jmp_v = (const void *)(base + d->d_val); break;
        case 2:  m->jmp_n = d->d_val / sizeof(DlRel); break;
        case 12: m->init = (void (*)(void))(base + d->d_val); break;
        case 25: m->init_v = (void (**)(void))(base + d->d_val); break;
        case 27: m->init_n = d->d_val / sizeof(void *); break;
        }
    }
    if (!m->rel_v) m->rel_n = 0;
    if (!m->jmp_v) m->jmp_n = 0;
    dl_tabelle(m, _DYNAMIC, base);
    g_ld_libc = m;
    g_ld_phdr = ph;
    g_ld_phnum = phnum;
    g_ld_dopo_programma = ld_dopo_programma;

    /* what dl_carica reads (LD_LIBRARY_PATH) and dladdr (argv[0]); the
     * program's _libc_start sets them again, in its own copies */
    *__environ_dove() = envp;
    __exos_argv0 = (int)sp[1] > 0 ? argv[0] : 0;
    __exos_dl_avvio_programma(__exos_argv0);
    return entry;
}

/* The entry: the stack untouched for the program's _start. */
#if defined(__x86_64__)
__asm__(".text\n"
        ".global _ld_avvio\n"
        ".type _ld_avvio, @function\n"
        "_ld_avvio:\n"
        "    movq  %rsp, %rbx\n"
        "    movq  %rsp, %rdi\n"
        "    andq  $-16, %rsp\n"
        "    call  ld_principale\n"
        "    movq  %rbx, %rsp\n"
        "    xorl  %ebx, %ebx\n"
        "    jmp   *%rax\n"
        ".size _ld_avvio, .-_ld_avvio\n");
#else
__asm__(".text\n"
        ".global _ld_avvio\n"
        ".type _ld_avvio, @function\n"
        "_ld_avvio:\n"
        "    movl  %esp, %esi\n"
        "    andl  $-16, %esp\n"
        "    subl  $12, %esp\n"
        "    pushl %esi\n"
        "    call  ld_principale\n"
        "    movl  %esi, %esp\n"
        "    xorl  %esi, %esi\n"
        "    jmp   *%eax\n"
        ".size _ld_avvio, .-_ld_avvio\n");
#endif
