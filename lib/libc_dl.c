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
 *   - the PT_LOAD segments in one anonymous mapping, read from the file;
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

#define DL_MODULI      32
#define DL_DIP         16
#define DL_MOD_MAGIA   0x464C4544u        /* "DELF": tells our handle from an ExLibTesta */

typedef struct DlModulo {
    unsigned int     magia;
    char             percorso[200];
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
    struct DlModulo *dip[DL_DIP];
    int              ndip;
    int              conta;               /* dlopen calls and dependents */
    int              tls_id;              /* 0 = no PT_TLS */
    uintptr_t        tls_imm;             /* the initial image */
    size_t           tls_file, tls_mem, tls_alli;
} DlModulo;

static DlModulo *g_dlm[DL_MODULI];
static volatile int g_dl_lucchetto;      /* one dlopen at a time (the libc's mutex) */
static volatile int g_dl_tls_lucchetto;  /* the TLS table: a constructor run by
                                          * dlopen may touch a __thread */
static int       g_dl_non_dyn;            /* the last candidate was an ELF but not ET_DYN */

static void dl_dici(const char *cosa, const char *chi)
{
    snprintf(g_dl_testo, sizeof(g_dl_testo), "%s: %s", chi ? chi : "?", cosa);
    g_dl_errore = g_dl_testo;
}

/* --- the symbols ----------------------------------------------------------------- */
#if defined(__x86_64__)
#define DL_PAROLA ".quad"
#else
#define DL_PAROLA ".long"
#endif
#include "libc_esportati.h"               /* g_dl_libc, g_dl_libc_n */

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

/* a symbol DEFINED in module m, or 0 */
static void *dl_nel_modulo(const DlModulo *m, const char *nome, const DlSim **s_out)
{
    unsigned int i;

    for (i = 1; i < m->nsim; i++) {
        const DlSim *s = &m->sim[i];
        unsigned int leg = s->st_info >> 4;

        if (s->st_shndx == 0 || (leg != 1 && leg != 2 && leg != 10)) continue;  /* GLOBAL WEAK UNIQUE */
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
static void *dl_globale(const char *nome, DlModulo **chi, const DlSim **s_out)
{
    void *p;
    int   i;

    *chi = 0;
    if (__exos_esportati && &__exos_esportati_n &&
        (p = dl_tabella(__exos_esportati, __exos_esportati_n, nome)) != 0) return p;
    if ((p = dl_tabella(g_dl_libc, g_dl_libc_n, nome)) != 0) return p;
    for (i = 0; i < DL_MODULI; i++)
        if (g_dlm[i] && (p = dl_nel_modulo(g_dlm[i], nome, s_out)) != 0) { *chi = g_dlm[i]; return p; }
    return 0;
}

/* --- thread-local storage --------------------------------------------------------- */
typedef struct { uintptr_t modulo, offset; } DlTlsIndice;

#define DL_TLS_POSTI 256
static struct { volatile uintptr_t tp; int id; void *blocco; } g_dl_tls[DL_TLS_POSTI];
static DlModulo *g_dl_tls_mod[DL_MODULI + 1];   /* id -> module */

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
    uintptr_t tp = dl_tp();
    int       i, id = (int)ti->modulo;
    DlModulo *m;
    void     *b = 0;

    for (i = 0; i < DL_TLS_POSTI; i++)
        if (g_dl_tls[i].tp == tp && g_dl_tls[i].id == id) return (char *)g_dl_tls[i].blocco + ti->offset;
    m = (id > 0 && id <= DL_MODULI) ? g_dl_tls_mod[id] : 0;
    if (!m) return 0;
    if (posix_memalign(&b, m->tls_alli < sizeof(void *) ? sizeof(void *) : m->tls_alli, m->tls_mem) != 0)
        return 0;
    memcpy(b, (const void *)m->tls_imm, m->tls_file);
    memset((char *)b + m->tls_file, 0, m->tls_mem - m->tls_file);
    mutex_prendi(&g_dl_tls_lucchetto);
    for (i = 0; i < DL_TLS_POSTI; i++)
        if (g_dl_tls[i].tp == 0) { g_dl_tls[i].id = id; g_dl_tls[i].blocco = b; g_dl_tls[i].tp = tp; break; }
    mutex_lascia(&g_dl_tls_lucchetto);
    if (i == DL_TLS_POSTI) { free(b); return 0; }
    return (char *)b + ti->offset;
}

/* a thread that ends gives its blocks back (thread_esci calls it) */
void dl_tls_lascia(void)
{
    uintptr_t tp = dl_tp();
    int       i;

    for (i = 0; i < DL_TLS_POSTI; i++)
        if (g_dl_tls[i].tp == tp && tp) { free(g_dl_tls[i].blocco); g_dl_tls[i].blocco = 0; g_dl_tls[i].tp = 0; }
}

#if !defined(__x86_64__)
/* i386 GNU TLS: the argument in %eax */
__attribute__((regparm(1))) void *___tls_get_addr(DlTlsIndice *ti);
__attribute__((regparm(1))) void *___tls_get_addr(DlTlsIndice *ti) { return __tls_get_addr(ti); }
#endif

/* --- loading ------------------------------------------------------------------------ */
static DlModulo *dl_carica(const char *nome, const char *runpath, const char *origine);

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
        if (si) {
            const DlSim *ds = &m->sim[si];
            const char  *nome = m->str + ds->st_name;

            if (ds->st_shndx != 0 && (ds->st_info >> 4) == 0) {             /* local */
                S = (ds->st_info & 0xf) == 6 ? ds->st_value : m->base + ds->st_value; s = ds;
            } else {
                S = (uintptr_t)dl_globale(nome, &chi, &s);
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
        case 18:
            dl_dici("initial-exec TLS (R_X86_64_TPOFF64) is not supported: build it -fPIC "
                    "with the general-dynamic model", m->percorso);
            return -1;
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
    int i;

    for (i = 0; i < DL_MODULI; i++) if (g_dlm[i] == m) g_dlm[i] = 0;
    if (m->tls_id) g_dl_tls_mod[m->tls_id] = 0;
    if (m->mappa) munmap(m->mappa, m->misura);
    m->magia = 0;
    free(m);
}

static DlModulo *dl_da_file(const char *percorso)
{
    DlTestaElf    t;
    DlFase        f[24];
    DlModulo     *m;
    int           fd, i, n;
    uintptr_t     lo = ~(uintptr_t)0, hi = 0;
    uintptr_t     din = 0;
    const DlDin  *d;
    const char   *runpath = 0;
    uintptr_t     rel = 0, jmp = 0, ia = 0, initf = 0;
    size_t        reln = 0, jmpn = 0, ian = 0;
    char          origine[200];

    fd = open(percorso, O_RDONLY);
    if (fd < 0) return 0;
    if (read(fd, &t, sizeof(t)) != (ssize_t)sizeof(t) || memcmp(t.e_ident, "\177ELF", 4) != 0) {
        close(fd); dl_dici("not an ELF file", percorso); return (DlModulo *)-1;
    }
    if (t.e_ident[4] == DL_CLASSE && t.e_machine == DL_MACCHINA && t.e_type == 2) {
        close(fd); g_dl_non_dyn = 1; return 0;     /* ET_EXEC: an EX-OS library, see dlopen */
    }
    if (t.e_ident[4] != DL_CLASSE || t.e_machine != DL_MACCHINA || t.e_type != 3 ||
        t.e_phnum == 0 || t.e_phnum > 24 || t.e_phentsize != sizeof(DlFase)) {
        close(fd); dl_dici("not a shared object for this machine (ET_DYN, same class)", percorso);
        return (DlModulo *)-1;
    }
    n = t.e_phnum;
    if (dl_leggi_a(fd, (uintptr_t)f, n * sizeof(DlFase), (long)t.e_phoff) != 0) {
        close(fd); dl_dici("cannot read the program headers", percorso); return (DlModulo *)-1;
    }
    for (i = 0; i < n; i++) if (f[i].p_type == 1) {                                 /* PT_LOAD */
        if (f[i].p_vaddr < lo) lo = f[i].p_vaddr;
        if (f[i].p_vaddr + f[i].p_memsz > hi) hi = f[i].p_vaddr + f[i].p_memsz;
    }
    if (hi <= lo) { close(fd); dl_dici("no loadable segment", percorso); return (DlModulo *)-1; }
    lo &= ~(uintptr_t)4095; hi = (hi + 4095) & ~(uintptr_t)4095;

    m = (DlModulo *)calloc(1, sizeof(DlModulo));
    if (!m) { close(fd); dl_dici("out of memory", percorso); return (DlModulo *)-1; }
    m->magia = DL_MOD_MAGIA;
    strncpy(m->percorso, percorso, sizeof(m->percorso) - 1);
    m->misura = hi - lo;
    m->mappa = mmap(0, m->misura, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m->mappa == MAP_FAILED) { m->mappa = 0; close(fd); free(m); dl_dici("out of memory (mmap)", percorso); return (DlModulo *)-1; }
    m->base = (uintptr_t)m->mappa - lo;
    for (i = 0; i < n; i++) {
        if (f[i].p_type == 1 && f[i].p_filesz &&
            dl_leggi_a(fd, m->base + f[i].p_vaddr, f[i].p_filesz, (long)f[i].p_offset) != 0) {
            close(fd); dl_libera(m); dl_dici("short read", percorso); return (DlModulo *)-1;
        }
        if (f[i].p_type == 2) din = m->base + f[i].p_vaddr;                         /* PT_DYNAMIC */
        if (f[i].p_type == 7) {                                                     /* PT_TLS */
            m->tls_imm = m->base + f[i].p_vaddr; m->tls_file = f[i].p_filesz;
            m->tls_mem = f[i].p_memsz; m->tls_alli = f[i].p_align ? f[i].p_align : 1;
        }
    }
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
    for (i = 0; i < DL_MODULI && g_dlm[i]; i++) { }
    if (i == DL_MODULI) { dl_libera(m); dl_dici("too many libraries open", percorso); return (DlModulo *)-1; }
    g_dlm[i] = m;
    if (m->tls_mem) {
        int k;
        for (k = 1; k <= DL_MODULI && g_dl_tls_mod[k]; k++) { }
        if (k > DL_MODULI) { dl_libera(m); dl_dici("too many TLS modules", percorso); return (DlModulo *)-1; }
        g_dl_tls_mod[k] = m; m->tls_id = k;
    }

    /* the dependencies first */
    strncpy(origine, percorso, sizeof(origine) - 1); origine[sizeof(origine) - 1] = 0;
    { char *b = strrchr(origine, '/'); if (b) *b = 0; else strcpy(origine, "."); }
    for (d = m->din; d->d_tag; d++) {
        DlModulo *dm;
        if (d->d_tag != 1) continue;                                               /* DT_NEEDED */
        if (m->ndip == DL_DIP) { dl_libera(m); dl_dici("too many DT_NEEDED", percorso); return (DlModulo *)-1; }
        dm = dl_carica(m->str + d->d_val, runpath, origine);
        if (!dm) { dl_libera(m); return (DlModulo *)-1; }                            /* dlerror says it */
        m->dip[m->ndip++] = dm;
    }

    if ((rel && dl_riloca(m, (const DlRel *)rel, reln) != 0) ||
        (jmp && dl_riloca(m, (const DlRel *)jmp, jmpn) != 0)) {
        dl_libera(m);
        return (DlModulo *)-1;
    }
    m->conta = 1;
    if (initf) ((void (*)(void))initf)();
    for (i = 0; i < (int)ian; i++) {
        void (*fn)(void) = ((void (**)(void))ia)[i];
        if (fn && fn != (void (*)(void))-1) fn();
    }
    return m;
}

/* one path from a search list, with $ORIGIN */
static DlModulo *dl_prova_dir(const char *dir, size_t dn, const char *nome, const char *origine)
{
    char p[256];
    size_t k = 0;

    if (dn >= 7 && strncmp(dir, "$ORIGIN", 7) == 0) {
        k = (size_t)snprintf(p, sizeof(p), "%s", origine ? origine : ".");
        dir += 7; dn -= 7;
    }
    if (k + dn + strlen(nome) + 2 >= sizeof(p)) return 0;
    memcpy(p + k, dir, dn); k += dn;
    if (k && p[k - 1] != '/') p[k++] = '/';
    strcpy(p + k, nome);
    return dl_da_file(p);
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
static DlModulo *dl_carica(const char *nome, const char *runpath, const char *origine)
{
    static const char *const dove = "/lib:/exos/lib:/exwin/lib:/cdrom/lib:/cdrom/exos/lib:/cdrom/exwin/lib";
    const char *base = strrchr(nome, '/');
    DlModulo   *m;
    int         i;

    for (i = 0; i < DL_MODULI; i++) {
        const char *b;
        if (!g_dlm[i]) continue;
        b = strrchr(g_dlm[i]->percorso, '/');
        if (strcmp(g_dlm[i]->percorso, nome) == 0 ||
            (!base && b && strcmp(b + 1, nome) == 0)) { g_dlm[i]->conta++; return g_dlm[i]; }
    }
    g_dl_errore = 0;
    if (base) m = dl_da_file(nome);
    else {
        m = dl_lista(getenv("LD_LIBRARY_PATH"), nome, origine);
        if (!m) m = dl_lista(runpath, nome, origine);
        if (!m) m = dl_lista(dove, nome, origine);
    }
    if (m == (DlModulo *)-1) return 0;
    if (!m && !g_dl_errore) dl_dici("cannot open shared object file: not found", nome);
    return m;
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
    DlModulo *coda[DL_MODULI];
    int       a = 0, z = 0, i, j;

    coda[z++] = m;
    while (a < z) {
        DlModulo *x = coda[a++];
        void     *p = dl_nel_modulo(x, nome, 0);
        if (p) return p;
        for (i = 0; i < x->ndip && z < DL_MODULI; i++) {
            for (j = 0; j < z && coda[j] != x->dip[i]; j++) { }
            if (j == z) coda[z++] = x->dip[i];
        }
    }
    return 0;
}

static int dl_addr_elf(const void *a, DlInfo *info)
{
    uintptr_t x = (uintptr_t)a;
    int       i;

    for (i = 0; i < DL_MODULI; i++) {
        DlModulo    *m = g_dlm[i];
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

#undef int64_t
#undef intptr_t
