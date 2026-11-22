/* =============================================================================
 * lib/libc_dl.c — a real ELF dynamic loader for dlopen (included by lib/libc.c)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Until 14 October 2026 dlopen opened only EX-OS libraries (the ExLibTesta
 * table, see dlopen in lib/libc.c). This file loads ordinary ELF shared
 * objects, the ones `gcc -shared -fPIC` and rustc make - at 64 bit (x86_64,
 * RELA) and at 32 (i386, REL):
 *
 *   - the PT_LOAD segments mapped by the kernel from its cache of shared
 *     objects (SYS_SO_MAPPA, kernel 0.351): the code is one copy for all;
 *   - DT_NEEDED, loaded first, searched in LD_LIBRARY_PATH, DT_RUNPATH /
 *     DT_RPATH (with $ORIGIN), then /lib, /exos/lib, /exwin/lib and the CD's;
 *   - the relocations: RELATIVE, 64/32, PC32, GLOB_DAT, JUMP_SLOT (bound at
 *     load: there is no lazy binding), IRELATIVE, and TLS DTPMOD/DTPOFF;
 *   - DT_INIT and DT_INIT_ARRAY in dependency order, DT_FINI_ARRAY at the
 *     last dlclose, which unmaps;
 *   - thread-local variables of the general-dynamic model through
 *     __tls_get_addr: one block per thread and module, made on first use.
 *     The initial-exec model (R_X86_64_TPOFF64) needs room reserved in every
 *     thread's static block and is refused with a message.
 *
 * ! WHERE A LIBRARY FINDS malloc AND printf. EX-OS programs are static and
 * keep no symbol table at run time (not even with --export-dynamic: a static
 * link makes no .dynsym). So the libc carries its own: lib/libc_esportati.h,
 * written by tools/libc-esportati.py, is every global symbol of the libc as
 * an assembly table of name and address. It is kept only in programs that
 * call dlopen (--gc-sections drops it elsewhere), and it keeps the whole
 * libc in them - a library may want any of it. A program can export symbols
 * of its own with a table named __exos_esportati (see DlEsportato below).
 * Look-up order: that table, the libc's, then the libraries in load order.
 *
 * Only in the static libc.a: lib/libc.c includes it under !EXOS_LIBC_SO.
 * ! AND IN /lib/ld-exos.so (EXOS_LIBC_ELF, 0.350, voce 105): the shared libc
 * is also the interpreter, lib/libc_ld.c. There the libc is a module of the
 * global scope like any library - slot 1, after the program - and has no
 * table of its own: its .dynsym is the table.
 * ============================================================================= */

/* --- what libc.c does not define for itself --------------------------------------- */
#ifndef PROT_EXEC
#define PROT_EXEC 0x4
#endif
typedef long long          int64_t_dl;
typedef __INTPTR_TYPE__    intptr_t_dl;
#define int64_t  int64_t_dl
#define intptr_t intptr_t_dl
long  lseek(int fd, long offset, int whence);   /* defined further on in libc.c */
char *strstr(const char *fieno, const char *ago);
typedef struct { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; } DlInfo;

/* --- ELF, the few pieces used -------------------------------------------------- */
#if defined(__x86_64__)
typedef struct { unsigned int st_name; unsigned char st_info, st_other; unsigned short st_shndx;
                 uint64_t st_value, st_size; } DlSim;
typedef struct { uint32_t p_type, p_flags; uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align; } DlFase;
typedef struct { uint64_t r_offset, r_info; int64_t r_addend; } DlRel;
#define DL_MACCHINA   62                  /* EM_X86_64 */
#define DL_CLASSE     2                   /* ELFCLASS64 */
#define DL_R_SIM(i)   ((unsigned int)((i) >> 32))
#define DL_R_TIPO(i)  ((unsigned int)((i) & 0xffffffffu))
#define DL_DT_REL     7                   /* DT_RELA, DT_RELASZ, DT_RELAENT */
#define DL_DT_RELSZ   8
#else
typedef struct { unsigned int st_name; uint32_t st_value, st_size; unsigned char st_info, st_other;
                 unsigned short st_shndx; } DlSim;
typedef struct { uint32_t p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align; } DlFase;
typedef struct { uint32_t r_offset, r_info; } DlRel;
#define DL_MACCHINA   3                   /* EM_386 */
#define DL_CLASSE     1
#define DL_R_SIM(i)   ((unsigned int)((i) >> 8))
#define DL_R_TIPO(i)  ((unsigned int)((i) & 0xff))
#define DL_DT_REL     17                  /* DT_REL, DT_RELSZ */
#define DL_DT_RELSZ   18
#endif
typedef struct { intptr_t d_tag; uintptr_t d_val; } DlDin;

typedef struct {
    unsigned char  e_ident[16];
    unsigned short e_type, e_machine;
    unsigned int   e_version;
    uintptr_t      e_entry, e_phoff, e_shoff;
    unsigned int   e_flags;
    unsigned short e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} DlTestaElf;

/* a name and an address the program offers to the libraries it opens */
typedef struct { const char *nome; void *indirizzo; } DlEsportato;
extern const DlEsportato __exos_esportati[] __attribute__((weak));
extern const unsigned long __exos_esportati_n __attribute__((weak));

/* ! NO FIXED NUMBER OF LIBRARIES (4 November 2026, voce 85). Until then 32
 * modules, 16 DT_NEEDED each, 32 TLS modules, 256 thread blocks and 24
 * program headers: VLC with its Qt interface loads more than 32 libraries
 * (Qt, FreeType, HarfBuzz, FFmpeg and its codecs, libstdc++...). Now every
 * table grows. The tables read WITHOUT the lock (dlsym, dladdr,
 * __tls_get_addr: a library constructor run inside dlopen may call dlsym,
 * and the lock is not recursive) are never freed when they grow: a reader
 * still walking the old copy reads valid memory, and the old copies add up
 * to less than the last one (each is half the next). */
typedef struct DlTab { int n; void *v[]; } DlTab;
#define DL_MOD_MAGIA   0x464C4544u        /* "DELF": tells our handle from an ExLibTesta */

typedef struct DlModulo {
    unsigned int     magia;
    char            *percorso;            /* malloc'd: no length limit */
    uintptr_t        base;                /* load bias: address = base + p_vaddr */
    void            *mappa;
    size_t           misura;
    const DlSim     *sim;
    const char      *str;
    unsigned int     nsim;
    const DlDin     *din;
    void           (**fini_v)(void);
    size_t           fini_n;
    void           (*fini)(void);
    struct DlModulo **dip;                /* DT_NEEDED, grows (dl_dip_metti) */
    int              ndip, dip_posti;
    int              conta;               /* dlopen calls and dependents */
    int              tls_id;              /* 0 = no PT_TLS */
    uintptr_t        tls_imm;             /* the initial image */
    size_t           tls_file, tls_mem, tls_alli;
    void           (*init)(void);         /* DT_INIT, DT_INIT_ARRAY */
    void           (**init_v)(void);
    size_t           init_n;
    const void      *rel_v, *jmp_v;       /* relocations, done by dl_finisci */
    size_t           rel_n, jmp_n;
} DlModulo;

static DlTab *volatile g_dlm_tab;          /* every module loaded; 0 = free slot */
static volatile int g_dl_lucchetto;      /* one dlopen at a time (the libc's mutex) */
static volatile int g_dl_tls_lucchetto;  /* the TLS table: a constructor run by
                                          * dlopen may touch a __thread */
static int       g_dl_non_dyn;            /* the last candidate was an ELF but not ET_DYN */
/* while a program linked to shared objects starts (dl_avvio_programma), the
 * constructors of its libraries wait for the program's own relocations, as
 * ld.so does: a COPY relocation must have put a variable in the program
 * before a constructor writes to it. They are run in the order the modules
 * were finished, dependencies first.
 * ! AND THE RELOCATIONS WAIT FOR THE WHOLE TREE (5 November 2026), for the
 * program and for dlopen: a library may use a symbol it does not take from
 * its own DT_NEEDED but from a library loaded after it. libapr calls modf
 * and names no libm - on Linux libm comes with the program, which names it
 * after libapr - and relocated as soon as its own dependencies were in,
 * libapr found no modf and httpd stopped before main ("modf: undefined
 * symbol"). Now every module of one load is mapped first, then all are
 * relocated in the same order (dependencies first), then the constructors
 * run: dl_finisci. */
static DlModulo **g_dl_da_avviare;        /* only while the program starts: one thread */
#ifdef EXOS_LIBC_ELF
static int      (*g_ld_dopo_programma)(void);   /* lib/libc_ld.c */
static DlModulo  *g_ld_libc;                    /* the libc as a module, slot 1 */
static int        g_ld_avviato;                 /* the interpreter did the start */
#endif
static int        g_dl_n_da_avviare, g_dl_posti_da_avviare;

static void dl_dici(const char *cosa, const char *chi)
{
    snprintf(g_dl_testo, sizeof(g_dl_testo), "%s: %s", chi ? chi : "?", cosa);
    g_dl_errore = g_dl_testo;
}

/* a table with room for at least `minimo` entries, the old ones copied in;
 * the old table stays allocated (see DlTab above) */
static DlTab *dl_tab_cresci(DlTab *vecchia, int minimo)
{
    int    n = vecchia ? vecchia->n : 0, nuovo = n ? n * 2 : 32;
    DlTab *t;

    while (nuovo < minimo) nuovo *= 2;
    t = (DlTab *)calloc(1, sizeof(DlTab) + (size_t)nuovo * sizeof(void *));
    if (!t) return 0;
    t->n = nuovo;
    if (vecchia) memcpy(t->v, vecchia->v, (size_t)n * sizeof(void *));
    return t;
}

static DlTab *dl_tab_leggi(DlTab *volatile *dove)
{
    return __atomic_load_n(dove, __ATOMIC_ACQUIRE);
}

/* puts p in the first free slot from `primo` on, growing the table; the
 * index, or -1 without memory. Called with the dlopen lock held. */
static int dl_tab_metti(DlTab *volatile *dove, int primo, void *p)
{
    DlTab *t = dl_tab_leggi(dove), *nuova;
    int    i;

    for (i = primo; t && i < t->n; i++)
        if (!t->v[i]) { __atomic_store_n(&t->v[i], p, __ATOMIC_RELEASE); return i; }
    nuova = dl_tab_cresci(t, primo + 1);
    if (!nuova) return -1;
    i = t && t->n > primo ? t->n : primo;
    nuova->v[i] = p;
    __atomic_store_n(dove, nuova, __ATOMIC_RELEASE);
    return i;
}

static int dl_dip_metti(DlModulo *m, DlModulo *dm)
{
    if (m->ndip == m->dip_posti) {
        int        posti = m->dip_posti ? m->dip_posti * 2 : 8;
        DlModulo **v = (DlModulo **)realloc(m->dip, (size_t)posti * sizeof(DlModulo *));
        if (!v) return -1;
        m->dip = v; m->dip_posti = posti;
    }
    m->dip[m->ndip++] = dm;
    return 0;
}

/* --- the symbols ----------------------------------------------------------------- */
#if defined(__x86_64__)
#define DL_PAROLA ".quad"
#else
#define DL_PAROLA ".long"
#endif
#ifndef EXOS_LIBC_ELF
#include "libc_esportati.h"               /* g_dl_libc, g_dl_libc_n */
#endif

static void *dl_tabella(const DlEsportato *t, unsigned long n, const char *nome)
{
    unsigned long i;

    for (i = 0; i < n; i++)
        if (strcmp(t[i].nome, nome) == 0) return t[i].indirizzo;
    return 0;
}

static unsigned int dl_conta_sim(const DlDin *d, uintptr_t base)
{
    const uint32_t *h = 0, *g = 0;

    for (; d->d_tag; d++) {
        if (d->d_tag == 4) h = (const uint32_t *)(base + d->d_val);            /* DT_HASH */
        if (d->d_tag == 0x6ffffef5) g = (const uint32_t *)(base + d->d_val);   /* DT_GNU_HASH */
    }
    if (h) return h[1];
    if (g) {
        uint32_t        nb = g[0], primo = g[1], nbloom = g[2], max = 0, i;
        const uint32_t *secchi = g + 4 + nbloom * (sizeof(uintptr_t) / 4), *catena;

        for (i = 0; i < nb; i++) if (secchi[i] > max) max = secchi[i];
        if (max < primo) return primo;
        catena = secchi + nb;
        while (!(catena[max - primo] & 1)) max++;
        return max + 1;
    }
    return 0;
}

/* a symbol DEFINED in module m, or 0.
 * ! THE PROGRAM'S PLT SLOT IS THE FUNCTION'S ADDRESS for everybody but the
 * PLT itself (19 November 2026, as glibc does): a program built without -fPIC
 * that takes &strlen gets its own PLT entry, an undefined FUNC symbol with a
 * value. Every other reference (GOT, R_*_32, dlsym) must get that same
 * address, or two pointers to one function differ; only the JUMP_SLOT that
 * fills the slot (plt = 1) goes to the real definition. */
static void *dl_nel_modulo(const DlModulo *m, const char *nome, const DlSim **s_out, int plt)
{
    unsigned int i;

    for (i = 1; i < m->nsim; i++) {
        const DlSim *s = &m->sim[i];
        unsigned int leg = s->st_info >> 4;

        if (leg != 1 && leg != 2 && leg != 10) continue;                       /* GLOBAL WEAK UNIQUE */
        if (s->st_shndx == 0 && (plt || (s->st_info & 0xf) != 2 || s->st_value == 0)) continue;
        if (strcmp(m->str + s->st_name, nome) != 0) continue;
        if (s_out) *s_out = s;
        if ((s->st_info & 0xf) == 6) return (void *)(uintptr_t)s->st_value;     /* STT_TLS: offset */
        if ((s->st_info & 0xf) == 10)                                           /* STT_GNU_IFUNC */
            return ((void *(*)(void))(m->base + s->st_value))();
        return (void *)(m->base + s->st_value);
    }
    return 0;
}

/* the global scope: the program's table, the libc's, then every library */
static void *dl_globale(const char *nome, DlModulo **chi, const DlSim **s_out, int plt)
{
    void  *p;
    DlTab *t = dl_tab_leggi(&g_dlm_tab);
    int    i;

    *chi = 0;
    if (__exos_esportati && &__exos_esportati_n &&
        (p = dl_tabella(__exos_esportati, __exos_esportati_n, nome)) != 0) return p;
#ifndef EXOS_LIBC_ELF
    if ((p = dl_tabella(g_dl_libc, g_dl_libc_n, nome)) != 0) return p;
#endif
    /* ! FOUND IS "THE MODULE DEFINES IT", NOT "THE VALUE IS NOT 0": the first
     * __thread variable of a library is at offset 0 of its block, and was
     * "undefined symbol" (4 November 2026, tools/prova-dl/prova-tante.sh) */
    for (i = 0; t && i < t->n; i++) {
        DlModulo    *m = (DlModulo *)t->v[i];
        const DlSim *s = 0;
        if (!m) continue;
        p = dl_nel_modulo(m, nome, &s, plt);
        if (p || s) { *chi = m; if (s_out) *s_out = s; return p; }
    }
    return 0;
}

/* --- thread-local storage --------------------------------------------------------- */
typedef struct { uintptr_t modulo, offset; } DlTlsIndice;

/* the block of each (thread, module) pair: a list of pieces that never move
 * nor go away, so __tls_get_addr walks it without the lock; a new piece is
 * added when every slot is taken */
#define DL_TLS_PEZZO 64
typedef struct DlTlsPezzo {
    struct DlTlsPezzo *volatile succ;
    struct { volatile uintptr_t tp; int id; void *blocco; } p[DL_TLS_PEZZO];
} DlTlsPezzo;
static DlTlsPezzo     g_dl_tls_primo;
static DlTab *volatile g_dl_tls_tab;            /* id -> module (v[0] unused) */

static uintptr_t dl_tp(void)
{
    uintptr_t tp;
#if defined(__x86_64__)
    __asm__ __volatile__("mov %%fs:0, %0" : "=r"(tp));
#else
    __asm__ __volatile__("movl %%gs:0, %0" : "=r"(tp));
#endif
    return tp;
}

void *__tls_get_addr(DlTlsIndice *ti)
{
    uintptr_t   tp = dl_tp();
    int         i, id = (int)ti->modulo;
    DlModulo   *m;
    DlTab      *mt;
    DlTlsPezzo *z, *ultimo = 0;
    void       *b = 0;

    for (z = &g_dl_tls_primo; z; z = __atomic_load_n(&z->succ, __ATOMIC_ACQUIRE))
        for (i = 0; i < DL_TLS_PEZZO; i++)
            if (z->p[i].tp == tp && z->p[i].id == id) return (char *)z->p[i].blocco + ti->offset;
    mt = dl_tab_leggi(&g_dl_tls_tab);
    m = (mt && id > 0 && id < mt->n) ? (DlModulo *)mt->v[id] : 0;
    if (!m) return 0;
    if (posix_memalign(&b, m->tls_alli < sizeof(void *) ? sizeof(void *) : m->tls_alli, m->tls_mem) != 0)
        return 0;
    memcpy(b, (const void *)m->tls_imm, m->tls_file);
    memset((char *)b + m->tls_file, 0, m->tls_mem - m->tls_file);
    mutex_prendi(&g_dl_tls_lucchetto);
    for (z = &g_dl_tls_primo; z; ultimo = z, z = z->succ)
        for (i = 0; i < DL_TLS_PEZZO; i++)
            if (z->p[i].tp == 0) goto posto;
    z = (DlTlsPezzo *)calloc(1, sizeof(DlTlsPezzo));
    if (!z) { mutex_lascia(&g_dl_tls_lucchetto); free(b); return 0; }
    i = 0;
    __atomic_store_n(&ultimo->succ, z, __ATOMIC_RELEASE);
posto:
    z->p[i].id = id; z->p[i].blocco = b;
    __atomic_store_n(&z->p[i].tp, tp, __ATOMIC_RELEASE);
    mutex_lascia(&g_dl_tls_lucchetto);
    return (char *)b + ti->offset;
}

/* a thread that ends gives its blocks back (thread_esci calls it) */
void dl_tls_lascia(void)
{
    uintptr_t   tp = dl_tp();
    DlTlsPezzo *z;
    int         i;

    if (!tp) return;
    for (z = &g_dl_tls_primo; z; z = __atomic_load_n(&z->succ, __ATOMIC_ACQUIRE))
        for (i = 0; i < DL_TLS_PEZZO; i++)
            if (z->p[i].tp == tp) { free(z->p[i].blocco); z->p[i].blocco = 0; z->p[i].tp = 0; }
}

#if !defined(__x86_64__)
/* i386 GNU TLS: the argument in %eax */
__attribute__((regparm(1))) void *___tls_get_addr(DlTlsIndice *ti);
__attribute__((regparm(1))) void *___tls_get_addr(DlTlsIndice *ti) { return __tls_get_addr(ti); }
#endif

/* --- loading ------------------------------------------------------------------------ */
static DlModulo *dl_carica(const char *nome, const char *runpath, const char *origine);

/* ! A DT_NEEDED OF ld-exos.so IN A PROGRAM WITH libc.a (20 November 2026,
 * voce 105): since gcc -shared links -lc as on Linux, every library names the
 * shared libc. A static program has its libc inside and lends it through its
 * table (lib/libc_esportati.h): loading /lib/ld-exos.so as well would give it
 * a second libc, a second malloc and a second errno. In ld-exos.so itself the
 * name is the libc already in the global scope (dl_carica). */
static int dl_libc_nominata(const char *nome)
{
#ifdef EXOS_LIBC_ELF
    (void)nome;
    return 0;
#else
    const char *b = strrchr(nome, '/');
    return strcmp(b ? b + 1 : nome, "ld-exos.so") == 0;
#endif
}

static int dl_leggi_a(int fd, uintptr_t dove, size_t n, long off)
{
    if (lseek(fd, off, SEEK_SET) != off) return -1;
    while (n) {
        ssize_t r = read(fd, (void *)dove, n);
        if (r <= 0) return -1;
        dove += (size_t)r; n -= (size_t)r;
    }
    return 0;
}

static void dl_avvia_modulo(DlModulo *m)
{
    size_t i;

    if (m->init) m->init();
    for (i = 0; i < m->init_n; i++) {
        void (*fn)(void) = m->init_v[i];
        if (fn && fn != (void (*)(void))-1) fn();
    }
}

/* ! THE PROGRAM'S OWN TLS IN THE STATIC BLOCK (5 November 2026, Valkey): a
 * program linked with -rdynamic keeps a TPOFF relocation for its own
 * __thread variables (thread_shared_qb in valkey-server). Their block is the
 * one the kernel built under the thread pointer (kernel/loader/elf.c):
 * tp = block + ALIGN_UP(p_memsz, max(p_align, 4)), so a variable at offset
 * st_value is at tp - that size + st_value. The size is read from the
 * program's PT_TLS, found from __ehdr_start (the ELF header, loaded with the
 * first segment). 0 = no PT_TLS. A TLS variable of a library opened later
 * has no place in the static block: that stays an error. */
static DlModulo g_dl_prog_collegato;
#ifdef EXOS_LIBC_ELF
/* the program's headers, from the kernel's auxv (lib/libc_ld.c): here
 * __ehdr_start would be the libc's own */
static const DlFase *g_ld_phdr;
static int           g_ld_phnum;
#else
extern const DlTestaElf __ehdr_start __attribute__((weak));
#endif

static uintptr_t dl_tls_programma(void)
{
    static uintptr_t dim = (uintptr_t)-1;
    const DlFase    *f;
    int              i, n;

    if (dim != (uintptr_t)-1) return dim;
    dim = 0;
#ifdef EXOS_LIBC_ELF
    f = g_ld_phdr; n = g_ld_phnum;
    if (!f) return 0;
#else
    if (!&__ehdr_start) return 0;
    f = (const DlFase *)((const char *)&__ehdr_start + __ehdr_start.e_phoff);
    n = __ehdr_start.e_phnum;
#endif
    for (i = 0; i < n; i++)
        if (f[i].p_type == 7 && f[i].p_memsz) {                                     /* PT_TLS */
            uintptr_t a = f[i].p_align < 4 ? 4 : (uintptr_t)f[i].p_align;
            dim = ((uintptr_t)f[i].p_memsz + a - 1) & ~(a - 1);
        }
    return dim;
}

static int dl_riloca(DlModulo *m, const DlRel *r, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        unsigned int tipo = DL_R_TIPO(r[i].r_info), si = DL_R_SIM(r[i].r_info);
        uintptr_t   *p = (uintptr_t *)(m->base + r[i].r_offset), S = 0;
        DlModulo    *chi = m;
        const DlSim *s = 0;
#if defined(__x86_64__)
        intptr_t     A = (intptr_t)r[i].r_addend;
#else
        intptr_t     A = (intptr_t)*p;           /* REL: the addend is in place */
#endif
        if (tipo == 5 && si) {                   /* COPY (5 at 32 and 64 bit): only in a program */
            const DlSim *ds = &m->sim[si], *dove = 0;
            const char  *nome = m->str + ds->st_name;
            void        *da = 0;
            DlTab       *mt = dl_tab_leggi(&g_dlm_tab);
            int          k;

            for (k = 0; mt && k < mt->n && !da; k++) {  /* the definition, not the copy itself */
                DlModulo *x = (DlModulo *)mt->v[k];
                if (x && x != m) da = dl_nel_modulo(x, nome, &dove, 1);
            }
            if (!da) { dl_dici("undefined symbol (COPY)", nome); return -1; }
            memcpy(p, da, ds->st_size < dove->st_size ? ds->st_size : dove->st_size);
            continue;
        }
        if (si) {
            const DlSim *ds = &m->sim[si];
            const char  *nome = m->str + ds->st_name;

            if (ds->st_shndx != 0 && (ds->st_info >> 4) == 0) {             /* local */
                S = (ds->st_info & 0xf) == 6 ? ds->st_value : m->base + ds->st_value; s = ds;
            } else {
                S = (uintptr_t)dl_globale(nome, &chi, &s, tipo == 7);   /* JUMP_SLOT at 32 and 64 */
                if (!S && !(chi && s)) {
                    if ((ds->st_info >> 4) == 2) S = 0;                       /* weak: may be 0 */
                    else { dl_dici("undefined symbol", nome); return -1; }
                }
                if (!chi) chi = m;
            }
        }
#if defined(__x86_64__)
        switch (tipo) {
        case 0:  break;                                                  /* NONE */
        case 1:  *p = S + (uintptr_t)A; break;                           /* 64 */
        case 2:  *(uint32_t *)p = (uint32_t)(S + (uintptr_t)A - (uintptr_t)p); break;   /* PC32 */
        case 6: case 7: *p = S; break;                                   /* GLOB_DAT, JUMP_SLOT */
        case 8:  *p = m->base + (uintptr_t)A; break;                     /* RELATIVE */
        case 16: *p = si ? (uintptr_t)chi->tls_id : (uintptr_t)m->tls_id; break;      /* DTPMOD64 */
        case 17: *p = S + (uintptr_t)A; break;                           /* DTPOFF64 */
        case 37: *p = ((uintptr_t (*)(void))(m->base + (uintptr_t)A))(); break;        /* IRELATIVE */
        case 18: {                                                       /* TPOFF64 */
            uintptr_t dim = dl_tls_programma();

            if (chi != &g_dl_prog_collegato || dim == 0) {
                dl_dici("initial-exec TLS (R_X86_64_TPOFF64) of a library is not supported: "
                        "build it -fPIC with the general-dynamic model", m->percorso);
                return -1;
            }
            *p = S + (uintptr_t)A - dim;
            break;
        }
#else
        switch (tipo) {
        case 0:  break;
        case 1:  *p = S + (uintptr_t)A; break;                           /* 32 */
        case 2:  *p = S + (uintptr_t)A - (uintptr_t)p; break;            /* PC32 */
        case 6: case 7: *p = S; break;                                   /* GLOB_DAT, JMP_SLOT */
        case 8:  *p = m->base + (uintptr_t)A; break;                     /* RELATIVE */
        case 35: *p = si ? (uintptr_t)chi->tls_id : (uintptr_t)m->tls_id; break;      /* DTPMOD32 */
        case 36: *p = S + (uintptr_t)A; break;                           /* DTPOFF32 */
        case 42: *p = ((uintptr_t (*)(void))(m->base + (uintptr_t)A))(); break;        /* IRELATIVE */
        case 14: case 37: {                                              /* TLS_TPOFF, TLS_TPOFF32 */
            uintptr_t dim = dl_tls_programma();

            if (chi != &g_dl_prog_collegato || dim == 0) {
                dl_dici("initial-exec TLS (R_386_TLS_TPOFF) of a library is not supported: "
                        "build it -fPIC with the general-dynamic model", m->percorso);
                return -1;
            }
            *p = tipo == 14 ? S + (uintptr_t)A - dim : (uintptr_t)A + dim - S;
            break;
        }
#endif
        default: {
            char t[48];
            snprintf(t, sizeof(t), "relocation type %u not supported", tipo);
            dl_dici(t, m->percorso);
            return -1;
        }
        }
        (void)s;
    }
    return 0;
}

static void dl_libera(DlModulo *m)
{
    DlTab *t = dl_tab_leggi(&g_dlm_tab), *mt = dl_tab_leggi(&g_dl_tls_tab);
    int    i;

    for (i = 0; t && i < t->n; i++) if (t->v[i] == m) t->v[i] = 0;
    if (m->tls_id && mt && m->tls_id < mt->n) mt->v[m->tls_id] = 0;
    if (m->mappa) munmap(m->mappa, m->misura);
    m->magia = 0;
    free(m->dip);
    free(m->percorso);
    free(m);
}

static DlModulo *dl_da_file(const char *percorso)
{
    DlTestaElf    t;
    DlFase       *f;
    DlModulo     *m;
    int           fd, i, n;
    uintptr_t     lo = ~(uintptr_t)0, hi = 0;
    uintptr_t     din = 0;
    const DlDin  *d;
    const char   *runpath = 0;
    uintptr_t     rel = 0, jmp = 0, ia = 0, initf = 0;
    size_t        reln = 0, jmpn = 0, ian = 0;
    char         *origine;

    fd = open(percorso, O_RDONLY);
    if (fd < 0) return 0;
    if (read(fd, &t, sizeof(t)) != (ssize_t)sizeof(t) || memcmp(t.e_ident, "\177ELF", 4) != 0) {
        close(fd); dl_dici("not an ELF file", percorso); return (DlModulo *)-1;
    }
    if (t.e_ident[4] == DL_CLASSE && t.e_machine == DL_MACCHINA && t.e_type == 2) {
        close(fd); g_dl_non_dyn = 1; return 0;     /* ET_EXEC: an EX-OS library, see dlopen */
    }
    if (t.e_ident[4] != DL_CLASSE || t.e_machine != DL_MACCHINA || t.e_type != 3 ||
        t.e_phnum == 0 || t.e_phentsize != sizeof(DlFase)) {
        close(fd); dl_dici("not a shared object for this machine (ET_DYN, same class)", percorso);
        return (DlModulo *)-1;
    }
    n = t.e_phnum;
    f = (DlFase *)malloc((size_t)n * sizeof(DlFase));
    if (!f) { close(fd); dl_dici("out of memory", percorso); return (DlModulo *)-1; }
    if (dl_leggi_a(fd, (uintptr_t)f, n * sizeof(DlFase), (long)t.e_phoff) != 0) {
        free(f); close(fd); dl_dici("cannot read the program headers", percorso); return (DlModulo *)-1;
    }
    for (i = 0; i < n; i++) if (f[i].p_type == 1) {                                 /* PT_LOAD */
        if (f[i].p_vaddr < lo) lo = f[i].p_vaddr;
        if (f[i].p_vaddr + f[i].p_memsz > hi) hi = f[i].p_vaddr + f[i].p_memsz;
    }
    if (hi <= lo) { free(f); close(fd); dl_dici("no loadable segment", percorso); return (DlModulo *)-1; }
    lo &= ~(uintptr_t)4095; hi = (hi + 4095) & ~(uintptr_t)4095;

    m = (DlModulo *)calloc(1, sizeof(DlModulo));
    if (m) m->percorso = strdup(percorso);
    if (!m || !m->percorso) { free(m); free(f); close(fd); dl_dici("out of memory", percorso); return (DlModulo *)-1; }
    m->magia = DL_MOD_MAGIA;
    /* ! THE KERNEL MAPS IT, FROM ITS CACHE (0.351): the read-only pages are
     * the same frames in every process, the writable ones our own copy.
     * Up to 0.350 the file was read here into anonymous pages, one copy of
     * the same code per process (kernel/loader/condivise.c). */
    {
        struct { uint32_t base, inizio, misura, entry; } e;
        int32_t r = _syscall3(SYS_SO_MAPPA, (uint32_t)fd, (uintptr_t)&e, (uintptr_t)percorso);

        if (r < 0) {
            free(f); close(fd); dl_libera(m);
            dl_dici(r == -ENOMEM ? "out of memory (mapping)" : "the kernel cannot map it", percorso);
            return (DlModulo *)-1;
        }
        m->mappa  = (void *)(uintptr_t)e.inizio;
        m->misura = e.misura;
        m->base   = (uintptr_t)e.base;
    }
    for (i = 0; i < n; i++) {
        if (f[i].p_type == 2) din = m->base + f[i].p_vaddr;                         /* PT_DYNAMIC */
        if (f[i].p_type == 7) {                                                     /* PT_TLS */
            m->tls_imm = m->base + f[i].p_vaddr; m->tls_file = f[i].p_filesz;
            m->tls_mem = f[i].p_memsz; m->tls_alli = f[i].p_align ? f[i].p_align : 1;
        }
    }
    free(f);
    close(fd);
    if (!din) { dl_libera(m); dl_dici("no PT_DYNAMIC", percorso); return (DlModulo *)-1; }
    m->din = (const DlDin *)din;

    for (d = m->din; d->d_tag; d++) {
        switch (d->d_tag) {
        case 5:  m->str = (const char *)(m->base + d->d_val); break;               /* DT_STRTAB */
        case 6:  m->sim = (const DlSim *)(m->base + d->d_val); break;              /* DT_SYMTAB */
        case DL_DT_REL:   rel = m->base + d->d_val; break;
        case DL_DT_RELSZ: reln = d->d_val / sizeof(DlRel); break;
        case 23: jmp = m->base + d->d_val; break;                                  /* DT_JMPREL */
        case 2:  jmpn = d->d_val / sizeof(DlRel); break;                           /* DT_PLTRELSZ */
        case 12: initf = m->base + d->d_val; break;                                /* DT_INIT */
        case 13: m->fini = (void (*)(void))(m->base + d->d_val); break;            /* DT_FINI */
        case 25: ia = m->base + d->d_val; break;                                   /* DT_INIT_ARRAY */
        case 27: ian = d->d_val / sizeof(void *); break;
        case 26: m->fini_v = (void (**)(void))(m->base + d->d_val); break;         /* DT_FINI_ARRAY */
        case 28: m->fini_n = d->d_val / sizeof(void *); break;
        }
    }
    for (d = m->din; d->d_tag; d++)
        if ((d->d_tag == 29 || d->d_tag == 15) && m->str) runpath = m->str + d->d_val;   /* RUNPATH, RPATH */
    m->nsim = dl_conta_sim(m->din, m->base);
    if (!m->sim || !m->str) { dl_libera(m); dl_dici("no dynamic symbol table", percorso); return (DlModulo *)-1; }

    /* the module is in the global scope before its own relocations: it may
     * refer to its own exported symbols through the GOT */
    if (dl_tab_metti(&g_dlm_tab, 0, m) < 0) { dl_libera(m); dl_dici("out of memory", percorso); return (DlModulo *)-1; }
    if (m->tls_mem) {
        int k = dl_tab_metti(&g_dl_tls_tab, 1, m);                                 /* ids from 1 */
        if (k < 0) { dl_libera(m); dl_dici("out of memory", percorso); return (DlModulo *)-1; }
        m->tls_id = k;
    }

    /* the dependencies first */
    origine = strdup(percorso);
    if (!origine) { dl_libera(m); dl_dici("out of memory", percorso); return (DlModulo *)-1; }
    { char *b = strrchr(origine, '/'); if (b) *b = 0; else strcpy(origine, "."); }
    for (d = m->din; d->d_tag; d++) {
        DlModulo *dm;
        if (d->d_tag != 1) continue;                                               /* DT_NEEDED */
        if (dl_libc_nominata(m->str + d->d_val)) continue;
        dm = dl_carica(m->str + d->d_val, runpath, origine);
        if (!dm) { free(origine); dl_libera(m); return (DlModulo *)-1; }             /* dlerror says it */
        if (dl_dip_metti(m, dm) != 0) { free(origine); dl_libera(m); dl_dici("out of memory", percorso); return (DlModulo *)-1; }
    }
    free(origine);

    m->rel_v = (const void *)rel; m->rel_n = rel ? reln : 0;
    m->jmp_v = (const void *)jmp; m->jmp_n = jmp ? jmpn : 0;
    m->conta = 1;
    m->init = (void (*)(void))initf;
    m->init_v = (void (**)(void))ia;
    m->init_n = ian;
    /* relocated and started later, with the whole tree in (dl_finisci) */
    if (g_dl_n_da_avviare == g_dl_posti_da_avviare) {
        int        posti = g_dl_posti_da_avviare ? g_dl_posti_da_avviare * 2 : 32;
        DlModulo **v = (DlModulo **)realloc(g_dl_da_avviare, (size_t)posti * sizeof(DlModulo *));
        if (!v) { dl_libera(m); dl_dici("out of memory", percorso); return (DlModulo *)-1; }
        g_dl_da_avviare = v; g_dl_posti_da_avviare = posti;
    }
    g_dl_da_avviare[g_dl_n_da_avviare++] = m;
    return m;
}

/* The modules queued from `da` on: relocated, in the order they were
 * finished (dependencies first), then `dopo` (the program, whose COPY
 * relocations read the libraries' data), then their constructors. -1 when a
 * relocation fails (dlerror says which symbol): the new modules are freed,
 * none of them has run any code. The queue is taken out first, so a
 * constructor that calls dlopen starts a queue of its own. */
/* a load that failed half way: what it had queued goes, none of it has run */
static void dl_lascia_coda(int da)
{
    int i;

    for (i = g_dl_n_da_avviare - 1; i >= da; i--) dl_libera(g_dl_da_avviare[i]);
    g_dl_n_da_avviare = da;
}

static int dl_finisci(int da, DlModulo *dopo, const DlRel *drel, size_t dreln,
                      const DlRel *djmp, size_t djmpn)
{
    int        n = g_dl_n_da_avviare - da, i;
    DlModulo **v = 0;

    if (n > 0) {
        v = (DlModulo **)malloc((size_t)n * sizeof(DlModulo *));
        if (!v) { dl_dici("out of memory", "dl_finisci"); return -1; }
        memcpy(v, g_dl_da_avviare + da, (size_t)n * sizeof(DlModulo *));
    }
    g_dl_n_da_avviare = da;
    for (i = 0; i < n; i++) {
        DlModulo *m = v[i];
        if ((m->rel_n && dl_riloca(m, (const DlRel *)m->rel_v, m->rel_n) != 0) ||
            (m->jmp_n && dl_riloca(m, (const DlRel *)m->jmp_v, m->jmp_n) != 0)) {
            for (i = n - 1; i >= 0; i--) dl_libera(v[i]);
            free(v);
            return -1;
        }
    }
    if (dopo && ((drel && dl_riloca(dopo, drel, dreln) != 0) ||
                 (djmp && dl_riloca(dopo, djmp, djmpn) != 0))) {
        free(v);
        return -1;
    }
#ifdef EXOS_LIBC_ELF
    /* the program has its COPY of the libc's variables now: the libc's own
     * references go there, and to what the program redefines (libc_ld.c) */
    if (dopo == &g_dl_prog_collegato && g_ld_dopo_programma && g_ld_dopo_programma() != 0) {
        free(v);
        return -1;
    }
#endif
    for (i = 0; i < n; i++) dl_avvia_modulo(v[i]);
    free(v);
    return 0;
}

/* ! LD_DEBUG=libs (22 November 2026), as glibc's: every name looked for,
 * every path tried and how it went, on stderr. Firefox said only "H264/AAC
 * codecs unsupported" while the same dlopen worked outside it, and the
 * reason was lost: dl_carica clears g_dl_errore at each level of DT_NEEDED. */
static int dl_debug(void)
{
    const char *v = getenv("LD_DEBUG");
    return v && (strstr(v, "libs") || strstr(v, "all"));
}

static void dl_debug_dici(const char *a, const char *b, const char *c)
{
    char r[512];
    int  n = snprintf(r, sizeof r, "ld-exos[%d]: %s%s%s\n", (int)getpid(), a ? a : "",
                      b ? b : "", c ? c : "");
    if (n > (int)sizeof r - 1) n = (int)sizeof r - 1;
    if (n > 0) write(2, r, (size_t)n);
}

/* one path from a search list, with $ORIGIN */
static DlModulo *dl_prova_dir(const char *dir, size_t dn, const char *nome, const char *origine)
{
    const char *o = origine ? origine : ".";
    char       *p;
    size_t      k = 0;
    DlModulo   *m;

    p = (char *)malloc(strlen(o) + dn + strlen(nome) + 2);      /* no length limit */
    if (!p) return 0;
    if (dn >= 7 && strncmp(dir, "$ORIGIN", 7) == 0) {
        k = strlen(o); memcpy(p, o, k);
        dir += 7; dn -= 7;
    }
    memcpy(p + k, dir, dn); k += dn;
    if (k && p[k - 1] != '/') p[k++] = '/';
    strcpy(p + k, nome);
    m = dl_da_file(p);
    if (dl_debug())
        dl_debug_dici("  provo ", p, m == (DlModulo *)-1 ? (g_dl_errore ? " -> errore" : " -> errore ?")
                                    : m ? " -> preso" : " -> non c'e'");
    if (m == (DlModulo *)-1 && g_dl_errore && dl_debug()) dl_debug_dici("    ", g_dl_errore, 0);
    free(p);
    return m;
}

static DlModulo *dl_lista(const char *lista, const char *nome, const char *origine)
{
    while (lista && *lista) {
        const char *fine = strchr(lista, ':');
        size_t      n = fine ? (size_t)(fine - lista) : strlen(lista);
        DlModulo   *m = n ? dl_prova_dir(lista, n, nome, origine) : 0;

        if (m) return m;
        lista = fine ? fine + 1 : 0;
    }
    return 0;
}

/* by name: already loaded? else search. 0 = not found or failed (dlerror set) */
/* already loaded: by its path, or by its file name when `nome` has no '/' */
static DlModulo *dl_gia(const char *nome)
{
    const char *base = strrchr(nome, '/');
    DlTab      *t = dl_tab_leggi(&g_dlm_tab);
    int         i;

    for (i = 0; t && i < t->n; i++) {
        DlModulo   *x = (DlModulo *)t->v[i];
        const char *b;
        if (!x) continue;
        b = strrchr(x->percorso, '/');
        if (strcmp(x->percorso, nome) == 0 ||
            (!base && b && strcmp(b + 1, nome) == 0)) return x;
    }
    return 0;
}

static DlModulo *dl_carica(const char *nome, const char *runpath, const char *origine)
{
    static const char *const dove = "/lib:/exos/lib:/exwin/lib:/cdrom/lib:/cdrom/exos/lib:/cdrom/exwin/lib";
    const char *base = strrchr(nome, '/');
    DlModulo   *m = dl_gia(nome);

    if (m) { m->conta++; return m; }
#ifdef EXOS_LIBC_ELF
    /* ! THE LIBC IS ALREADY HERE: it enters the global scope where a
     * DT_NEEDED names it, as ld.so does. A library before it in the program's
     * list wins over it (VLC's libvlc defines setlocale, putenv... for its
     * plugins: lib/override.c) */
    if (g_ld_libc && strcmp(base ? base + 1 : nome, "ld-exos.so") == 0) {
        if (dl_tab_metti(&g_dlm_tab, 0, g_ld_libc) < 0) { dl_dici("out of memory", nome); return 0; }
        return g_ld_libc;
    }
#endif
    g_dl_errore = 0;
    if (dl_debug()) dl_debug_dici("cerco ", nome, base ? "" : " (LD_LIBRARY_PATH, DT_RUNPATH, /lib...)");
    if (base) m = dl_da_file(nome);
    else {
        m = dl_lista(getenv("LD_LIBRARY_PATH"), nome, origine);
        if (!m) m = dl_lista(runpath, nome, origine);
        if (!m) m = dl_lista(dove, nome, origine);
    }
    if (m == (DlModulo *)-1) {
        if (dl_debug()) dl_debug_dici("non caricato ", nome, g_dl_errore ? ": errore sopra" : "");
        return 0;
    }
    if (!m && !g_dl_errore) dl_dici("cannot open shared object file: not found", nome);
    if (!m && dl_debug()) dl_debug_dici(g_dl_errore, 0, 0);
    return m;
}

/* --- a program linked to shared objects (3 November 2026) --------------------------
 *
 * `gcc prog.c -lavcodec` with libavcodec.so in the sysroot makes an ordinary
 * dynamic ET_EXEC: PT_INTERP, PT_DYNAMIC, DT_NEEDED, a GOT and a PLT. The
 * kernel loads only the PT_LOAD segments and ignores PT_INTERP, so the
 * program starts at _start as any other, with the libc inside it (the
 * sysroot has only libc.a). Before the constructors and main, _libc_start
 * calls this when _DYNAMIC is there, and it does the work of ld.so:
 *
 *   1. the program becomes module 0 of the global scope (load bias 0): a
 *      library that uses a variable the program has copied (COPY) finds the
 *      copy, as on Linux;
 *   2. DT_NEEDED, each with its own DT_NEEDED, through dl_carica (the same
 *      search as dlopen: LD_LIBRARY_PATH, DT_RUNPATH with $ORIGIN, /lib,
 *      /exos/lib, /exwin/lib and the same on the CD);
 *   3. the program's relocations: GLOB_DAT, JUMP_SLOT, 64/32, COPY;
 *   4. the constructors of the libraries, dependencies first.
 *
 * The libraries find malloc and printf in the libc's table, as with dlopen.
 * ! Only in the libc of the sysroot (EXOS_LIBC_PORTATI): the system's own
 * programs are linked with --gc-sections and must not keep the loader.
 * ! A library that cannot be found stops the program before main, with the
 * reason on stderr and exit code 127, as ld.so does.
 * ! The address of a library function taken in the program is the library's
 * address, not the program's PLT slot: no canonical PLT. Comparing such a
 * pointer from both sides is the only thing that differs from Linux. */
#ifdef EXOS_LIBC_ELF
static const DlDin *g_ld_prog_din;        /* the program's, from its PT_DYNAMIC */
#define DL_PROG_DIN g_ld_prog_din
#else
extern const DlDin _DYNAMIC[] __attribute__((weak));
#define DL_PROG_DIN _DYNAMIC
#endif

static void dl_muori(const char *argv0)
{
    const char *e = g_dl_errore ? g_dl_errore : "shared object not loaded";

    write(2, argv0, strlen(argv0));
    write(2, ": ", 2);
    write(2, e, strlen(e));
    write(2, "\n", 1);
    _exit(127);
}

void __exos_dl_avvio_programma(const char *argv0)
{
    DlModulo     *m = &g_dl_prog_collegato;
    const DlDin  *d;
    const char   *runpath = 0;
    uintptr_t     rel = 0, jmp = 0;
    size_t        reln = 0, jmpn = 0;
    char         *origine;

#ifdef EXOS_LIBC_ELF
    /* called by the program's _libc_start: the interpreter already did it */
    if (g_ld_avviato > 1) return;
    g_ld_avviato = 2;
#endif
    if (!DL_PROG_DIN) return;                    /* a static program: nothing to do */
    if (!argv0) argv0 = "?";
    m->percorso = strdup(argv0);
    origine = strdup(argv0);
    if (!m->percorso || !origine) { dl_dici("out of memory", argv0); dl_muori(argv0); }
    m->base = 0;                                 /* ET_EXEC: addresses are absolute */
    m->din = DL_PROG_DIN;
    m->conta = 1 << 30;                          /* never closed */
    for (d = m->din; d->d_tag; d++) {
        switch (d->d_tag) {
        case 5:  m->str = (const char *)d->d_val; break;
        case 6:  m->sim = (const DlSim *)d->d_val; break;
        case DL_DT_REL:   rel = d->d_val; break;
        case DL_DT_RELSZ: reln = d->d_val / sizeof(DlRel); break;
        case 23: jmp = d->d_val; break;
        case 2:  jmpn = d->d_val / sizeof(DlRel); break;
        }
    }
    for (d = m->din; d->d_tag; d++)
        if ((d->d_tag == 29 || d->d_tag == 15) && m->str) runpath = m->str + d->d_val;
    m->nsim = dl_conta_sim(m->din, 0);
    if (!m->sim || !m->str) { dl_dici("no dynamic symbol table", argv0); dl_muori(argv0); }
    if (dl_tab_metti(&g_dlm_tab, 0, m) != 0) { dl_dici("out of memory", argv0); dl_muori(argv0); }
#ifdef EXOS_LIBC_ELF
    /* where the program is: RTLD_NEXT from it starts after it */
    {
        int i;
        for (i = 0; i < g_ld_phnum; i++)
            if (g_ld_phdr[i].p_type == 1 && g_ld_phdr[i].p_memsz) {
                uintptr_t a = (uintptr_t)g_ld_phdr[i].p_vaddr & ~(uintptr_t)4095;
                uintptr_t z = (uintptr_t)(g_ld_phdr[i].p_vaddr + g_ld_phdr[i].p_memsz);
                if (!m->mappa || a < (uintptr_t)m->mappa) {
                    m->misura += m->mappa ? (uintptr_t)m->mappa - a : 0;
                    m->mappa = (void *)a;
                }
                if (z > (uintptr_t)m->mappa + m->misura) m->misura = z - (uintptr_t)m->mappa;
            }
    }
#endif

    { char *b = strrchr(origine, '/'); if (b) *b = 0; else strcpy(origine, "."); }
    for (d = m->din; d->d_tag; d++) {
        DlModulo *dm;
        if (d->d_tag != 1) continue;             /* DT_NEEDED */
        if (dl_libc_nominata(m->str + d->d_val)) continue;
        dm = dl_carica(m->str + d->d_val, runpath, origine);
        if (!dm) dl_muori(argv0);
        if (dl_dip_metti(m, dm) != 0) { dl_dici("out of memory", argv0); dl_muori(argv0); }
    }
    free(origine);
    if (dl_finisci(0, m, (const DlRel *)rel, rel ? reln : 0,
                   (const DlRel *)jmp, jmp ? jmpn : 0) != 0)
        dl_muori(argv0);
}

static void dl_chiudi(DlModulo *m)
{
    int i;

    if (!m || m->magia != DL_MOD_MAGIA || --m->conta > 0) return;
    for (i = (int)m->fini_n - 1; i >= 0; i--)
        if (m->fini_v[i] && m->fini_v[i] != (void (*)(void))-1) m->fini_v[i]();
    if (m->fini) m->fini();
    for (i = 0; i < m->ndip; i++) dl_chiudi(m->dip[i]);
    dl_libera(m);
}

/* a symbol in module m or in what it depends on, breadth first */
static void *dl_nel_grafo(DlModulo *m, const char *nome)
{
    DlModulo **coda, **v;
    void      *p = 0;
    int        a = 0, z = 0, posti = 32, i, j;

    coda = (DlModulo **)malloc((size_t)posti * sizeof(DlModulo *));
    if (!coda) return dl_nel_modulo(m, nome, 0, 0);
    coda[z++] = m;
    while (a < z && !p) {
        DlModulo *x = coda[a++];
        if ((p = dl_nel_modulo(x, nome, 0, 0)) != 0) break;
        for (i = 0; i < x->ndip; i++) {
            for (j = 0; j < z && coda[j] != x->dip[i]; j++) { }
            if (j < z) continue;
            if (z == posti) {
                v = (DlModulo **)realloc(coda, (size_t)posti * 2 * sizeof(DlModulo *));
                if (!v) break;
                coda = v; posti *= 2;
            }
            coda[z++] = x->dip[i];
        }
    }
    free(coda);
    return p;
}

/* RTLD_NEXT: the first definition in the modules after the caller's, in the
 * order of the global scope. ! Until 19 November 2026 RTLD_NEXT was the
 * global scope: a library that wraps a function of the libc and calls the
 * real one through RTLD_NEXT found itself again, and ran in circles. */
static void *dl_dopo(const void *chi, const char *nome)
{
    uintptr_t x = (uintptr_t)chi;
    DlTab    *t = dl_tab_leggi(&g_dlm_tab);
    int       i, k = -1;

    for (i = 0; t && i < t->n; i++) {
        DlModulo *m = (DlModulo *)t->v[i];
        if (m && m->mappa && x >= (uintptr_t)m->mappa && x < (uintptr_t)m->mappa + m->misura) { k = i; break; }
    }
    /* not in a module: the program (module 0 if it is linked to any) */
    if (k < 0 && t && t->n > 0 && t->v[0] == &g_dl_prog_collegato) k = 0;
    for (i = k + 1; t && i < t->n; i++) {
        DlModulo    *m = (DlModulo *)t->v[i];
        const DlSim *s = 0;
        void        *p;
        if (!m) continue;
        p = dl_nel_modulo(m, nome, &s, 0);
        if (p || s) return p;
    }
#ifndef EXOS_LIBC_ELF
    /* the libc is inside the program: after every library, as on Linux */
    return dl_tabella(g_dl_libc, g_dl_libc_n, nome);
#else
    return 0;
#endif
}

static int dl_addr_elf(const void *a, DlInfo *info)
{
    uintptr_t x = (uintptr_t)a;
    DlTab    *t = dl_tab_leggi(&g_dlm_tab);
    int       i;

    for (i = 0; t && i < t->n; i++) {
        DlModulo    *m = (DlModulo *)t->v[i];
        const DlSim *meglio = 0;
        unsigned int k;

        if (!m || x < (uintptr_t)m->mappa || x >= (uintptr_t)m->mappa + m->misura) continue;
        for (k = 1; k < m->nsim; k++) {
            const DlSim *s = &m->sim[k];
            uintptr_t    v = m->base + s->st_value;
            if (s->st_shndx == 0 || (s->st_info & 0xf) == 6 || v > x) continue;
            if (!meglio || v > m->base + meglio->st_value) meglio = s;
        }
        info->dli_fname = m->percorso;
        info->dli_fbase = m->mappa;
        info->dli_sname = meglio ? m->str + meglio->st_name : 0;
        info->dli_saddr = meglio ? (void *)(m->base + meglio->st_value) : 0;
        return 1;
    }
    return 0;
}

#ifdef EXOS_LIBC_ELF
#include "libc_ld.c"                      /* the interpreter: /lib/ld-exos.so */
#endif

#undef int64_t
#undef intptr_t
