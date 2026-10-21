/* =============================================================================
 * kernel/loader/modulo.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * The loader of kernel modules. What a module is, and its trailer, is in
 * kernel/include/modulo.h.
 *
 * ! THE SAME LOADER AT 32 AND AT 64 BIT: it reads ELF32 or ELF64 by the
 * architecture it was compiled for and refuses the other class. A module is
 * an ET_DYN with only RELATIVE relocations - position independent code that
 * needs nothing but its own base address added to its pointers. Anything
 * else (a symbol to resolve, a PLT, a library it names, constructors) is
 * refused: there is nobody here to resolve or run them, and a pointer left
 * unrelocated in kernel code is a jump to a random address.
 *
 * ! EVERY OFFSET IN THE FILE IS CHECKED against the file's size and every
 * address against the image's, before it is used. The SHA-256 says the file
 * is the one the build made; the checks say that even a file made on purpose
 * cannot make the loader write outside the memory it allocated.
 *
 * Modules are never unloaded: the ext2 write operations point into the one
 * that is loaded, and a mount in the middle of a write cannot be told to
 * stop using them.
 * ============================================================================= */

#include "kernel.h"
#include "modulo.h"
#include "kmalloc.h"
#include "sha256.h"
#include "vfs.h"
#include "version.h"
#include "ext2_interno.h"

const Ext2Interfaccia *ext2_interfaccia(void);
void *memcpy(void *dst, const void *src, size_t n);    /* arch/x86/memfun.c */
void *memset(void *dst, int c, size_t n);

/* --- ELF, only what a module uses ------------------------------------------ */
#define ET_DYN          3u
#define EM_386          3u
#define EM_X86_64       62u
#define PT_LOAD         1u
#define PT_DYNAMIC      2u
#define PT_INTERP       3u

#define DT_NULL         0
#define DT_NEEDED       1
#define DT_PLTRELSZ     2
#define DT_RELA         7
#define DT_RELASZ       8
#define DT_RELAENT      9
#define DT_INIT         12
#define DT_FINI         13
#define DT_REL          17
#define DT_RELSZ        18
#define DT_RELENT       19
#define DT_JMPREL       23
#define DT_INIT_ARRAY   25
#define DT_INIT_ARRAYSZ 27
#define DT_PREINIT_ARRAY 32

#define R_386_RELATIVE      8u
#define R_X86_64_RELATIVE   8u

#if defined(__x86_64__)
typedef uint64_t  ElfAddr;
typedef struct PACKED {
    uint8_t  e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version;
    uint64_t e_entry, e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} MElfEhdr;
typedef struct PACKED {
    uint32_t p_type, p_flags;
    uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} MElfPhdr;
typedef struct PACKED { int64_t d_tag; uint64_t d_val; } MElfDyn;
typedef struct PACKED { uint64_t r_offset, r_info; int64_t r_addend; } MElfRel;
#define ELF_CLASSE      2u
#define ELF_MACCHINA    EM_X86_64
#define REL_TIPO(i)     ((uint32_t)((i) & 0xFFFFFFFFu))
#define REL_RELATIVO    R_X86_64_RELATIVE
#define DT_I_REL        DT_RELA
#define DT_I_RELSZ      DT_RELASZ
#define DT_I_RELENT     DT_RELAENT
#else
typedef uint32_t  ElfAddr;
typedef struct PACKED {
    uint8_t  e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version;
    uint32_t e_entry, e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} MElfEhdr;
typedef struct PACKED {
    uint32_t p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align;
} MElfPhdr;
typedef struct PACKED { int32_t d_tag; uint32_t d_val; } MElfDyn;
typedef struct PACKED { uint32_t r_offset, r_info; } MElfRel;
#define ELF_CLASSE      1u
#define ELF_MACCHINA    EM_386
#define REL_TIPO(i)     ((uint32_t)((i) & 0xFFu))
#define REL_RELATIVO    R_386_RELATIVE
#define DT_I_REL        DT_REL
#define DT_I_RELSZ      DT_RELSZ
#define DT_I_RELENT     DT_RELENT
#endif

typedef struct {
    char     nome[MODULO_NOME_MAX];
    uint8_t *base;
    uint32_t dim;
    uint8_t  forzato;
} Modulo;

static Modulo g_mod[MODULO_MAX];
static int    g_n_mod = 0;

/* --- the table handed to every module --------------------------------------- */
static const void *interfaccia(const char *nome, uint32_t abi)
{
    if (nome == NULL) return NULL;
    if (nome[0] == 'e' && nome[1] == 'x' && nome[2] == 't' && nome[3] == '2' &&
        nome[4] == '\0') {
        const Ext2Interfaccia *i = ext2_interfaccia();
        return (i->abi == abi) ? i : NULL;
    }
    return NULL;
}

static void *mod_kmalloc(size_t n) { return kmalloc(n); }
static void  mod_kfree(void *p)    { kfree(p); }

static const ModuloKernel g_tabella = {
    MODULO_ABI,
    sizeof(ModuloKernel),
    klog,
    interfaccia,
    mod_kmalloc,
    mod_kfree,
};

/* --- small helpers ---------------------------------------------------------- */
static uint32_t lun(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }

static int uguali(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* The file name without the directories: "/dev/ext4.mod" -> "ext4.mod". */
static void nome_da_percorso(const char *p, char *out)
{
    const char *n = p;
    uint32_t    i;

    for (; *p; p++) if (*p == '/') n = p + 1;
    for (i = 0; n[i] && i < MODULO_NOME_MAX - 1u; i++) out[i] = n[i];
    out[i] = '\0';
}

/* A NUL-padded field of the trailer, as a printable string (at most n). */
static void campo(const uint8_t *src, uint32_t n, char *out)
{
    uint32_t i;

    for (i = 0; i < n && src[i]; i++)
        out[i] = (src[i] >= 32 && src[i] < 127) ? (char)src[i] : '?';
    out[i] = '\0';
}

/* =============================================================================
 * The trailer. 0 = it matches this kernel; MODULO_E_NON_CORR otherwise, with
 * what was expected and what was found in the log. Not a module at all (no
 * magic): MODULO_E_NON_MOD.
 * ============================================================================= */
static int controlla_coda(const char *nome, const uint8_t *f, uint32_t dim)
{
    const uint8_t *c = f + dim - MODULO_CODA;
    uint8_t        h[32];
    char           ver[17], arch[9];
    int            ok = 1, i;

    for (i = 0; i < 8; i++)
        if (c[i] != (uint8_t)MODULO_MAGIA[i]) {
            klog(LOG_ERROR, "MODULO: %s has no EX-OS module trailer", nome);
            return MODULO_E_NON_MOD;
        }

    campo(c + 8, 16, ver);
    campo(c + 24, 8, arch);

    if (!uguali(ver, EXOS_VERSION)) {
        klog(LOG_ERROR, "MODULO: %s is built for kernel %s, this is %s",
             nome, ver, EXOS_VERSION);
        ok = 0;
    }
    if (!uguali(arch, MODULO_ARCH)) {
        klog(LOG_ERROR, "MODULO: %s is built for %s, this kernel is %s",
             nome, arch, MODULO_ARCH);
        ok = 0;
    }

    sha256(f, dim - 32u, h);
    for (i = 0; i < 32; i++) if (h[i] != c[32 + i]) break;
    if (i < 32) {
        char atteso[65], trovato[65];
        static const char esa[] = "0123456789abcdef";

        for (i = 0; i < 32; i++) {
            atteso[2 * i]      = esa[c[32 + i] >> 4];
            atteso[2 * i + 1]  = esa[c[32 + i] & 15];
            trovato[2 * i]     = esa[h[i] >> 4];
            trovato[2 * i + 1] = esa[h[i] & 15];
        }
        atteso[64] = trovato[64] = '\0';
        klog(LOG_ERROR, "MODULO: %s is damaged: SHA-256 %s, the trailer says %s",
             nome, trovato, atteso);
        ok = 0;
    }

    return ok ? 0 : MODULO_E_NON_CORR;
}

/* =============================================================================
 * The ELF: checked, copied into memory of its own, relocated. Returns the
 * image (and its size and entry point) or NULL with the reason in the log.
 * `f`/`dim` exclude the trailer.
 * ============================================================================= */
static uint8_t *carica_elf(const char *nome, const uint8_t *f, uint32_t dim,
                           uint32_t *dim_img, ModuloIngresso *ingresso)
{
    const MElfEhdr *e = (const MElfEhdr *)f;
    const MElfPhdr *ph;
    uint64_t        fine = 0;
    uint8_t        *img;
    uint32_t        i, n_dyn = 0;
    ElfAddr         rel = 0, relsz = 0, relent = 0, off_dyn = 0;

#define RIFIUTA(...) do { klog(LOG_ERROR, __VA_ARGS__); return NULL; } while (0)

    if (dim < sizeof(MElfEhdr)) RIFIUTA("MODULO: %s: too short for ELF", nome);
    if (e->e_ident[0] != 0x7F || e->e_ident[1] != 'E' || e->e_ident[2] != 'L' ||
        e->e_ident[3] != 'F')
        RIFIUTA("MODULO: %s is not ELF", nome);
    if (e->e_ident[4] != ELF_CLASSE || e->e_ident[5] != 1u ||
        e->e_machine != ELF_MACCHINA)
        RIFIUTA("MODULO: %s is ELF for another processor", nome);
    if (e->e_type != ET_DYN) RIFIUTA("MODULO: %s is not a shared object (ET_DYN)", nome);
    if (e->e_phentsize != sizeof(MElfPhdr) || e->e_phnum == 0 || e->e_phnum > 16u ||
        (uint64_t)e->e_phoff + (uint64_t)e->e_phnum * sizeof(MElfPhdr) > dim)
        RIFIUTA("MODULO: %s: program headers out of the file", nome);

    ph = (const MElfPhdr *)(f + e->e_phoff);

    /* How much memory: up to the end of the last PT_LOAD. The first one starts
     * at 0 (a shared object linked at 0), so the image is [0, fine). */
    for (i = 0; i < e->e_phnum; i++) {
        if (ph[i].p_type == PT_INTERP) RIFIUTA("MODULO: %s asks for an interpreter", nome);
        if (ph[i].p_type == PT_DYNAMIC) { off_dyn = ph[i].p_vaddr; n_dyn = (uint32_t)(ph[i].p_memsz / sizeof(MElfDyn)); }
        if (ph[i].p_type != PT_LOAD) continue;
        if (ph[i].p_filesz > ph[i].p_memsz ||
            (uint64_t)ph[i].p_offset + ph[i].p_filesz > dim ||
            (uint64_t)ph[i].p_vaddr + ph[i].p_memsz > MODULO_DIM_MAX * 2ull)
            RIFIUTA("MODULO: %s: a segment out of bounds", nome);
        if ((uint64_t)ph[i].p_vaddr + ph[i].p_memsz > fine)
            fine = (uint64_t)ph[i].p_vaddr + ph[i].p_memsz;
    }
    if (fine == 0) RIFIUTA("MODULO: %s has nothing to load", nome);
    if (e->e_entry >= fine) RIFIUTA("MODULO: %s: entry point outside the image", nome);

    fine = (fine + 4095u) & ~(uint64_t)4095u;
    img  = (uint8_t *)kmalloc_aligned((size_t)fine, 4096);
    if (img == NULL) RIFIUTA("MODULO: %s: no memory for %u bytes", nome, (uint32_t)fine);
    memset(img, 0, (size_t)fine);

    for (i = 0; i < e->e_phnum; i++)
        if (ph[i].p_type == PT_LOAD)
            memcpy(img + ph[i].p_vaddr, f + ph[i].p_offset, (size_t)ph[i].p_filesz);

#undef RIFIUTA
#define RIFIUTA(...) do { klog(LOG_ERROR, __VA_ARGS__); kfree_aligned(img); return NULL; } while (0)

    /* The dynamic section, read from the image (it is inside a PT_LOAD). */
    if (n_dyn != 0) {
        const MElfDyn *d;

        if ((uint64_t)off_dyn + (uint64_t)n_dyn * sizeof(MElfDyn) > fine)
            RIFIUTA("MODULO: %s: dynamic section out of the image", nome);
        d = (const MElfDyn *)(img + off_dyn);

        for (i = 0; i < n_dyn && d[i].d_tag != DT_NULL; i++) {
            switch (d[i].d_tag) {
            case DT_I_REL:    rel    = d[i].d_val; break;
            case DT_I_RELSZ:  relsz  = d[i].d_val; break;
            case DT_I_RELENT: relent = d[i].d_val; break;
            case DT_NEEDED:
                RIFIUTA("MODULO: %s names a library: a module links with nothing", nome);
            case DT_JMPREL:
            case DT_PLTRELSZ:
                if (d[i].d_val != 0)
                    RIFIUTA("MODULO: %s has PLT relocations (a symbol to resolve)", nome);
                break;
            case DT_INIT: case DT_FINI: case DT_INIT_ARRAY: case DT_PREINIT_ARRAY:
                RIFIUTA("MODULO: %s has constructors, which nobody would run", nome);
            case DT_INIT_ARRAYSZ:
                if (d[i].d_val != 0)
                    RIFIUTA("MODULO: %s has constructors, which nobody would run", nome);
                break;
            default: break;
            }
        }
    }

    if (relsz != 0) {
        const MElfRel *r;
        uint32_t       n;

        if (relent != sizeof(MElfRel) || (uint64_t)rel + relsz > fine ||
            relsz % sizeof(MElfRel))
            RIFIUTA("MODULO: %s: relocation table out of the image", nome);
        r = (const MElfRel *)(img + rel);
        n = (uint32_t)(relsz / sizeof(MElfRel));

        for (i = 0; i < n; i++) {
            if (REL_TIPO(r[i].r_info) != REL_RELATIVO)
                RIFIUTA("MODULO: %s: relocation of type %u (only RELATIVE is allowed)",
                        nome, REL_TIPO(r[i].r_info));
            if ((uint64_t)r[i].r_offset + sizeof(ElfAddr) > fine)
                RIFIUTA("MODULO: %s: a relocation outside the image", nome);
#if defined(__x86_64__)
            *(ElfAddr *)(img + r[i].r_offset) = (ElfAddr)(uintptr_t)img + (ElfAddr)r[i].r_addend;
#else
            *(ElfAddr *)(img + r[i].r_offset) += (ElfAddr)(uintptr_t)img;
#endif
        }
    }
#undef RIFIUTA

    *dim_img  = (uint32_t)fine;
    *ingresso = (ModuloIngresso)(uintptr_t)(img + e->e_entry);
    return img;
}

/* =============================================================================
 * modulo_carica
 * ============================================================================= */
int modulo_carica(const char *percorso, int forza)
{
    VfsStat        st;
    char           nome[MODULO_NOME_MAX];
    uint8_t       *f, *img;
    uint32_t       dim, dim_img = 0;
    ModuloIngresso ingresso = NULL;
    int            h, r, i, coda;

    if (percorso == NULL) return -22;
    nome_da_percorso(percorso, nome);

    for (i = 0; i < g_n_mod; i++)
        if (uguali(g_mod[i].nome, nome)) return -MODULO_E_GIA;
    if (g_n_mod >= MODULO_MAX) return -12;

    if (vfs_stat(percorso, &st) != 0) return -2;
    dim = st.dimensione;
    if (dim <= MODULO_CODA + sizeof(MElfEhdr) || dim > MODULO_DIM_MAX) {
        klog(LOG_ERROR, "MODULO: %s: %u bytes is not the size of a module", nome, dim);
        return -MODULO_E_NON_MOD;
    }

    f = (uint8_t *)kmalloc(dim);
    if (f == NULL) return -12;

    h = vfs_open(percorso, 0);
    if (h < 0) { kfree(f); return -2; }
    r = vfs_read(h, f, dim, 0);
    vfs_close(h);
    if (r != (int)dim) {
        klog(LOG_ERROR, "MODULO: %s: read %d of %u bytes", nome, r, dim);
        kfree(f);
        return -5;
    }

    coda = controlla_coda(nome, f, dim);
    if (coda == MODULO_E_NON_MOD) { kfree(f); return -MODULO_E_NON_MOD; }
    if (coda != 0) {
        if (!forza) { kfree(f); return -MODULO_E_NON_CORR; }
        klog(LOG_WARN, "MODULO: %s loaded WITHOUT the checks, as asked (-forza)", nome);
    }

    img = carica_elf(nome, f, dim - MODULO_CODA, &dim_img, &ingresso);
    kfree(f);
    if (img == NULL) return -MODULO_E_NON_MOD;

    r = ingresso(&g_tabella);
    if (r != 0) {
        klog(LOG_ERROR, "MODULO: %s refused to start (%d)", nome, r);
        kfree_aligned(img);
        return -MODULO_E_NON_MOD;
    }

    for (i = 0; nome[i]; i++) g_mod[g_n_mod].nome[i] = nome[i];
    g_mod[g_n_mod].nome[i] = '\0';
    g_mod[g_n_mod].base    = img;
    g_mod[g_n_mod].dim     = dim_img;
    g_mod[g_n_mod].forzato = (coda != 0) ? 1 : 0;
    g_n_mod++;

    klog(LOG_INFO, "MODULO: %s loaded (%u bytes)", nome, dim_img);
    return 0;
}

int modulo_elenco(char *buf, uint32_t max)
{
    uint32_t p = 0;
    int      i;

    if (buf == NULL || max == 0) return -22;
    for (i = 0; i < g_n_mod; i++) {
        static const char forzato[] = " (caricato senza i controlli)";
        uint32_t l = lun(g_mod[i].nome), f = g_mod[i].forzato ? lun(forzato) : 0u, k;

        if (p + l + f + 2u > max) break;
        for (k = 0; k < l; k++) buf[p++] = g_mod[i].nome[k];
        for (k = 0; k < f; k++) buf[p++] = forzato[k];
        buf[p++] = '\n';
    }
    buf[p] = '\0';
    return g_n_mod;
}

/* =============================================================================
 * At boot. The root is mounted and nothing has been written yet: an ext2
 * root is read-only until the module registers (ext2.c, EXT2_RO_MODULO),
 * and becomes writable then. Missing or refused, the system still starts -
 * read-only - and the screen says what to do.
 * ============================================================================= */
void modulo_avvio(void)
{
    int r = modulo_carica("/dev/ext4.mod", 0);

    if (r == 0) return;
    if (r == -2)
        klog(LOG_ERROR, "MODULO: /dev/ext4.mod is missing: ext2/ext3/ext4 disks "
                        "are READ-ONLY. Copy it from the install CD or run "
                        "netupdate -check -yesall");
    else
        klog(LOG_ERROR, "MODULO: /dev/ext4.mod not loaded (%d): ext2/ext3/ext4 "
                        "disks are READ-ONLY. Reinstall it from the CD or with "
                        "netupdate (the reason is above)", -r);
}
