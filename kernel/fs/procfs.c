/* =============================================================================
 * kernel/fs/procfs.c — /proc
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * ! NOT A FOLDER ON THE DISK. Nothing is stored: a file is written the moment
 * it is read, from the kernel's own tables, in the format of Linux, because
 * the programs that read these files are ported and parse that text.
 *
 * What exists is what was found to be asked for (voce 45, the strings of the
 * 64-bit binaries): Firefox self/exe, status, fd/N; MariaDB self/limits,
 * self/cwd; gcc self/exe; plus what a person or a monitor wants: a CPU counter
 * for each thread in task/<tid>/stat, meminfo, cpuinfo, stat, uptime, mounts.
 *
 *     /proc/{meminfo,cpuinfo,stat,uptime,mounts,version}
 *     /proc/self -> the caller's own group, /proc/<pid>/
 *     /proc/<pid>/{stat,status,cmdline,exe,cwd,limits,fd/N,task/<tid>/{stat,status}}
 *
 * ! NO BUFFER AND NO LIMIT ON A FILE. The text is generated from its beginning
 * at each read and only the bytes of the window asked for are copied: a file
 * as long as the process table needs no memory, and a read in small pieces
 * costs a few passes. As on Linux, a file that changes between two reads may
 * show the seam.
 *
 * ! exe, cwd AND fd/N ARE NOT LINKS: EX-OS has no symlinks. They are small
 * files holding the target, and readlink() in the libc reads them.
 *
 * ! THE DATA OF A PROCESS IS ITS USER'S: 0400 and 0500 under /proc/<pid>, 0444
 * for what is about the machine. The environment is deliberately not here.
 *
 * The tables are read without a lock, as sys_procinfo does: PROC_POSTO is valid
 * for every slot (sched.h), and one number may be a moment old.
 * ============================================================================= */
#include "kernel.h"
#include "vfs.h"
#include "procfs.h"
#include "sched.h"
#include "syscall.h"
#include "pmm.h"
#include "swap.h"
#include "smp.h"
#include "tsc.h"
#include "version.h"
#include "ext2.h"

/* In vfs.c: a mount slot, with the lock the caller has */
const VfsMount *vfs_mount_nl_di(int i);

/* --- the window ---------------------------------------------------------- */
typedef struct {
    uint8_t  *buf;
    uint32_t  size;         /* room in buf */
    uint32_t  salta;        /* bytes still to skip */
    uint32_t  scritti;
} Usc;

static void metti(Usc *u, char c)
{
    if (u->salta) { u->salta--; return; }
    if (u->scritti < u->size) u->buf[u->scritti++] = (uint8_t)c;
}

static void testo(Usc *u, const char *s) { while (*s) metti(u, *s++); }

/* ! BY HAND: a 32-bit kernel has no __udivdi3 */
static uint64_t dividi(uint64_t v, uint32_t d, uint32_t *resto)
{
    uint64_t q = 0, r = 0;
    int      k;

    for (k = 63; k >= 0; k--) {
        r = (r << 1) | ((v >> k) & 1u);
        if (r >= d) { r -= d; q |= (uint64_t)1 << k; }
    }
    if (resto) *resto = (uint32_t)r;
    return q;
}

static void num(Usc *u, uint64_t v)
{
    char     b[24];
    int      n = 0;
    uint32_t r;

    do { v = dividi(v, 10u, &r); b[n++] = (char)('0' + (int)r); } while (v);
    while (n > 0) metti(u, b[--n]);
}

/* A label, its number, what follows: most lines of /proc are this */
static void voce(Usc *u, const char *a, uint64_t v, const char *b)
{
    testo(u, a); num(u, v); testo(u, b);
}

/* --- the path ------------------------------------------------------------ */
enum {
    Q_RADICE, Q_MEMINFO, Q_CPUINFO, Q_STAT, Q_UPTIME, Q_MOUNTS, Q_VERSION,
    Q_PID, Q_P_STAT, Q_P_STATUS, Q_P_CMDLINE, Q_P_EXE, Q_P_CWD, Q_P_LIMITS,
    Q_P_FD, Q_P_FD_N, Q_P_TASK, Q_T, Q_T_STAT, Q_T_STATUS
};

typedef struct {
    int       cosa;
    Process  *p;        /* the group's leader */
    Process  *t;        /* the thread, for Q_T* */
    uint32_t  fd;
} Dove;

static const struct { const char *nome; int cosa; } g_cima[] = {
    { "meminfo", Q_MEMINFO }, { "cpuinfo", Q_CPUINFO }, { "stat", Q_STAT },
    { "uptime", Q_UPTIME }, { "mounts", Q_MOUNTS }, { "version", Q_VERSION },
};
#define N_CIMA  (sizeof(g_cima) / sizeof(g_cima[0]))

static const struct { const char *nome; int cosa; } g_pid[] = {
    { "stat", Q_P_STAT }, { "status", Q_P_STATUS }, { "cmdline", Q_P_CMDLINE },
    { "exe", Q_P_EXE }, { "cwd", Q_P_CWD }, { "limits", Q_P_LIMITS },
    { "fd", Q_P_FD }, { "task", Q_P_TASK },
};
#define N_PID   (sizeof(g_pid) / sizeof(g_pid[0]))

/* One component of the path: its length, 0 at the end */
static uint32_t pezzo(const char **p)
{
    uint32_t l = 0;

    while (**p == '/') (*p)++;
    while ((*p)[l] && (*p)[l] != '/') l++;
    return l;
}

static int uguale(const char *a, uint32_t l, const char *b)
{
    uint32_t k;

    for (k = 0; k < l; k++) if (b[k] == '\0' || a[k] != b[k]) return 0;
    return b[l] == '\0';
}

static int numero(const char *a, uint32_t l, uint32_t *v)
{
    uint32_t k, n = 0;

    if (l == 0 || l > 9) return 0;
    for (k = 0; k < l; k++) {
        if (a[k] < '0' || a[k] > '9') return 0;
        n = n * 10u + (uint32_t)(a[k] - '0');
    }
    *v = n;
    return 1;
}

static Process *vivo(uint32_t pid)
{
    Process *p = pid ? proc_get_by_pid(pid) : 0;

    return (p && p->state != PROC_UNUSED) ? p : 0;
}

static int fd_aperto(const Process *p, uint32_t fd)
{
    if (!p->fdt || (int)fd >= FD_N(p)) return 0;
    return FDV(p, fd).type != FD_UNUSED;
}

static int capisci(const char *s, Dove *d)
{
    uint32_t l, v, k;

    d->p = 0; d->t = 0; d->fd = 0;
    l = pezzo(&s);
    if (l == 0) { d->cosa = Q_RADICE; return 0; }

    for (k = 0; k < N_CIMA; k++)
        if (uguale(s, l, g_cima[k].nome)) {
            s += l;
            if (pezzo(&s)) return ERR(ENOTDIR);
            d->cosa = g_cima[k].cosa;
            return 0;
        }

    if (uguale(s, l, "self")) {
        Process *c = proc_get_current();

        d->p = c ? vivo(c->tgid) : 0;
    } else if (numero(s, l, &v)) {
        d->p = vivo(v);
        if (d->p && d->p->tgid != d->p->pid) d->p = 0;  /* a thread is under task/ */
    }
    if (!d->p) return ERR(ENOENT);
    s += l;

    l = pezzo(&s);
    if (l == 0) { d->cosa = Q_PID; return 0; }
    for (k = 0; k < N_PID; k++) if (uguale(s, l, g_pid[k].nome)) break;
    if (k == N_PID) return ERR(ENOENT);
    d->cosa = g_pid[k].cosa;
    s += l;
    l = pezzo(&s);
    if (d->cosa == Q_P_FD && l) {
        if (!numero(s, l, &d->fd) || !fd_aperto(d->p, d->fd)) return ERR(ENOENT);
        d->cosa = Q_P_FD_N;
        s += l;
        return pezzo(&s) ? ERR(ENOTDIR) : 0;
    }
    if (d->cosa == Q_P_TASK && l) {
        if (!numero(s, l, &v)) return ERR(ENOENT);
        d->t = vivo(v);
        if (!d->t || d->t->tgid != d->p->pid) return ERR(ENOENT);
        s += l;
        l = pezzo(&s);
        d->cosa = Q_T;
        if (l == 0) return 0;
        if (uguale(s, l, "stat"))        d->cosa = Q_T_STAT;
        else if (uguale(s, l, "status")) d->cosa = Q_T_STATUS;
        else return ERR(ENOENT);
        s += l;
        return pezzo(&s) ? ERR(ENOTDIR) : 0;
    }
    return l ? ERR(ENOTDIR) : 0;
}

/* --- the machine --------------------------------------------------------- */
static void memoria(uint64_t *tot_kb, uint64_t *lib_kb)
{
    uint64_t tot = pmm_get_total_pages(), lib = pmm_get_free_pages();

#if defined(__x86_64__)
    tot += pmm_alta_totali();
    lib += pmm_alta_libere();
#endif
    *tot_kb = tot * (PAGE_SIZE / 1024u);
    *lib_kb = lib * (PAGE_SIZE / 1024u);
}

static void f_meminfo(Usc *u)
{
    uint64_t tot, lib;
    uint32_t st = 0, su = 0;

    memoria(&tot, &lib);
    if (swap_attivo()) swap_conta(&st, &su);
    voce(u, "MemTotal:       ", tot, " kB\n");
    voce(u, "MemFree:        ", lib, " kB\n");
    voce(u, "MemAvailable:   ", lib, " kB\n");
    voce(u, "Buffers:        ", 0, " kB\n");
    voce(u, "Cached:         ", 0, " kB\n");
    voce(u, "SwapTotal:      ", (uint64_t)st * (PAGE_SIZE / 1024u), " kB\n");
    voce(u, "SwapFree:       ", (uint64_t)(st - su) * (PAGE_SIZE / 1024u), " kB\n");
}

static uint32_t n_cpu(void)
{
#if defined(__x86_64__)
    return 1;       /* smp64.c: one processor for now */
#else
    return 1u + smp_info()->fermi;      /* the ones that answered */
#endif
}

static void f_cpuinfo(Usc *u)
{
    uint32_t a, b, c, d, i, k, regs[12], edx1 = 0, ecx1 = 0, fam = 0, mod = 0;
    char     ven[13], marca[49];
    static const struct { const char *n; int bit; } fd[] = {
        { "fpu", 0 }, { "tsc", 4 }, { "msr", 5 }, { "pae", 6 }, { "cmov", 15 },
        { "mmx", 23 }, { "fxsr", 24 }, { "sse", 25 }, { "sse2", 26 }, { "ht", 28 },
    };
    static const struct { const char *n; int bit; } fc[] = {
        { "pni", 0 }, { "ssse3", 9 }, { "sse4_1", 19 }, { "sse4_2", 20 },
        { "popcnt", 23 }, { "aes", 25 }, { "xsave", 26 }, { "avx", 28 },
    };

    ven[0] = 0; marca[0] = 0;
    if (cpuid_disponibile()) {
        cpuid(0, &a, &b, &c, &d);
        ((uint32_t *)ven)[0] = b; ((uint32_t *)ven)[1] = d; ((uint32_t *)ven)[2] = c;
        ven[12] = 0;
        edx1 = cpuid_edx1();
        ecx1 = cpuid_ecx1();
        cpuid(1, &a, &b, &c, &d);
        fam = (a >> 8) & 15u;
        mod = (a >> 4) & 15u;
        cpuid(0x80000000u, &a, &b, &c, &d);
        if (a >= 0x80000004u) {
            for (k = 0; k < 3; k++)
                cpuid(0x80000002u + k, &regs[k * 4], &regs[k * 4 + 1],
                      &regs[k * 4 + 2], &regs[k * 4 + 3]);
            for (k = 0; k < 48; k++) marca[k] = ((char *)regs)[k];
            marca[48] = 0;
        }
    }
    for (i = 0; i < n_cpu(); i++) {
        voce(u, "processor\t: ", i, "\n");
        testo(u, "vendor_id\t: "); testo(u, ven); metti(u, '\n');
        voce(u, "cpu family\t: ", fam, "\n");
        voce(u, "model\t\t: ", mod, "\n");
        testo(u, "model name\t: "); testo(u, marca); metti(u, '\n');
        voce(u, "cpu MHz\t\t: ", tsc_c_e() ? tsc_hz() / 1000000u : 0, ".000\n");
        testo(u, "flags\t\t:");
        for (k = 0; k < sizeof(fd) / sizeof(fd[0]); k++)
            if (edx1 >> fd[k].bit & 1u) { metti(u, ' '); testo(u, fd[k].n); }
        for (k = 0; k < sizeof(fc) / sizeof(fc[0]); k++)
            if (ecx1 >> fc[k].bit & 1u) { metti(u, ' '); testo(u, fc[k].n); }
        testo(u, "\n\n");
    }
}

/* The idle tasks' ticks are the CPU nobody wanted */
static uint64_t ozio(void)
{
    uint64_t t = 0;
    uint32_t i;

    for (i = 0; i < g_proc_posti; i++) {
        const Process *p = PROC_POSTO(i);

        if (p->state != PROC_UNUSED && p->priority == PRIO_IDLE) t += p->ticks_total;
    }
    return t;
}

static void f_stat(Usc *u)
{
    uint64_t tot = (uint64_t)g_ticks * n_cpu(), id = ozio(), n = 0, usati;
    uint32_t i;

    usati = tot > id ? tot - id : 0;
    voce(u, "cpu  ", usati, " 0 0 ");
    voce(u, "", id, " 0 0 0 0 0 0\n");
    for (i = 0; i < n_cpu(); i++) {
        voce(u, "cpu", i, " ");
        voce(u, "", dividi(usati, n_cpu(), 0), " 0 0 ");
        voce(u, "", dividi(id, n_cpu(), 0), " 0 0 0 0 0 0\n");
    }
    for (i = 0; i < g_proc_posti; i++) if (PROC_POSTO(i)->state != PROC_UNUSED) n++;
    voce(u, "procs_running ", n, "\n");
}

static void f_uptime(Usc *u)
{
    uint32_t r;
    uint64_t q, id = dividi(ozio(), n_cpu(), 0);

    q = dividi(g_ticks, 100u, &r);
    voce(u, "", q, "."); voce(u, r < 10u ? "0" : "", r, " ");
    q = dividi(id, 100u, &r);
    voce(u, "", q, "."); voce(u, r < 10u ? "0" : "", r, "\n");
}

static void f_mounts(Usc *u)
{
    int i;

    for (i = 0; i < VFS_MAX_MOUNT; i++) {
        const VfsMount *m = vfs_mount_nl_di(i);

        if (!m || !m->usato) continue;
        testo(u, m->dev[0] ? m->dev : "none"); metti(u, ' ');
        testo(u, m->punto); metti(u, ' ');
        if (m->tipo == VFS_FS_EXT2) {
            testo(u, "ext");
            metti(u, (char)('0' + ext2_generazione(m->mnt)));
        } else {
            testo(u, m->tipo == VFS_FS_ISO ? "iso9660" :
                     m->tipo == VFS_FS_PROC ? "proc" : "vfat");
        }
        testo(u, m->sola_lettura ? " ro 0 0\n" : " rw 0 0\n");
    }
}

/* --- a process ----------------------------------------------------------- */
static char stato_lettera(const Process *p)
{
    switch (p->state) {
    case PROC_RUNNING:
    case PROC_READY:    return 'R';
    case PROC_ZOMBIE:   return 'Z';
    case PROC_SLEEPING:
    case PROC_BLOCKED:  return 'S';
    default:            return 'X';
    }
}

/* The name without the folders, as Linux's comm */
static const char *breve(const char *n)
{
    const char *b = n;

    for (; *n; n++) if (*n == '/') b = n + 1;
    return b;
}

static uint32_t n_fili(uint32_t tgid)
{
    uint32_t i, n = 0;

    for (i = 0; i < g_proc_posti; i++) {
        const Process *p = PROC_POSTO(i);

        if (p->state != PROC_UNUSED && p->tgid == tgid) n++;
    }
    return n;
}

/* The group's CPU is the sum of its threads; a thread's is its own */
static uint64_t tempo(const Process *p, int gruppo)
{
    uint64_t t = 0;
    uint32_t i;

    if (!gruppo) return p->ticks_total;
    for (i = 0; i < g_proc_posti; i++) {
        const Process *q = PROC_POSTO(i);

        if (q->state != PROC_UNUSED && q->tgid == p->pid) t += q->ticks_total;
    }
    return t;
}

/* The 52 fields of proc(5). ! THE ORDER IS THE FORMAT: what EX-OS does not keep
 * is a 0 in its place, never a shorter line. There is no user/system split:
 * all the ticks are in utime. */
static void f_p_stat(Usc *u, const Process *p, int gruppo)
{
    int k;

    voce(u, "", p->pid, " (");
    testo(u, breve(p->name));
    testo(u, ") ");
    metti(u, stato_lettera(p));
    voce(u, " ", p->ppid, " ");
    voce(u, "", p->tgid, " ");
    voce(u, "", p->tgid, " 0 -1 0 0 0 0 0 ");      /* session .. cmajflt */
    voce(u, "", tempo(p, gruppo), " 0 0 0 ");       /* utime stime cutime cstime */
    voce(u, "", p->priority, " 0 ");                /* priority nice */
    voce(u, "", n_fili(p->tgid), " 0 ");            /* num_threads itrealvalue */
    voce(u, "", p->created_tick, " 0 0");           /* starttime vsize rss */
    for (k = 25; k <= 52; k++) testo(u, " 0");
    metti(u, '\n');
}

static void f_p_status(Usc *u, const Process *p)
{
    testo(u, "Name:\t"); testo(u, breve(p->name)); metti(u, '\n');
    testo(u, "State:\t"); metti(u, stato_lettera(p)); metti(u, '\n');
    voce(u, "Tgid:\t", p->tgid, "\n");
    voce(u, "Pid:\t", p->pid, "\n");
    voce(u, "PPid:\t", p->ppid, "\n");
    voce(u, "Uid:\t", p->uid, "\t"); voce(u, "", p->uid, "\t");
    voce(u, "", p->uid, "\t"); voce(u, "", p->uid, "\n");
    voce(u, "Gid:\t", p->gid, "\t"); voce(u, "", p->gid, "\t");
    voce(u, "", p->gid, "\t"); voce(u, "", p->gid, "\n");
    voce(u, "FDSize:\t", p->fdt ? (uint32_t)FD_N(p) : 0u, "\n");
    voce(u, "Threads:\t", n_fili(p->tgid), "\n");
}

static void riga_limite(Usc *u, const char *nome, const char *cifre, const char *unita)
{
    uint32_t l = 0, k;

    testo(u, nome);
    while (nome[l]) l++;
    for (k = l; k < 26; k++) metti(u, ' ');
    testo(u, cifre);
    testo(u, unita);
    metti(u, '\n');
}

/* The header and the columns are parsed by name (MariaDB wants "Max open
 * files"). Only that limit exists here: the others are honestly unlimited. */
static void f_p_limits(Usc *u, const Process *p)
{
    static const char *const altri[] = {
        "Max cpu time", "Max file size", "Max data size", "Max stack size",
        "Max core file size", "Max resident set", "Max processes",
    };
    uint32_t k;

    testo(u, "Limit                     Soft Limit           Hard Limit           Units     \n");
    for (k = 0; k < sizeof(altri) / sizeof(altri[0]); k++)
        riga_limite(u, altri[k], "unlimited            unlimited            ",
                    k == 6 ? "processes" : "");
    testo(u, "Max open files            ");
    voce(u, "", p->fdt ? p->fdt->limite : 0u, "                 ");
    voce(u, "", p->fdt ? p->fdt->limite_max : 0u, "                 files     \n");
}

/* --- the interface -------------------------------------------------------- */
int procfs_stat(const char *interno, VfsStat *st)
{
    Dove d;
    int  r = capisci(interno, &d);

    if (r < 0) return r;
    st->dimensione = 0;
    st->sola_lettura = 1;
    st->ident = (uint32_t)d.cosa + 1u + (d.p ? d.p->pid * 32u : 0u);
    switch (d.cosa) {
    case Q_RADICE:
    case Q_PID:     st->is_dir = 1; st->modo = 0555; break;
    case Q_P_FD:
    case Q_P_TASK:
    case Q_T:       st->is_dir = 1; st->modo = 0500; break;
    case Q_MEMINFO: case Q_CPUINFO: case Q_STAT: case Q_UPTIME:
    case Q_MOUNTS:  case Q_VERSION: st->is_dir = 0; st->modo = 0444; break;
    default:        st->is_dir = 0; st->modo = 0400; break;
    }
    if (d.p) { st->uid = (uint16_t)d.p->uid; st->gid = (uint16_t)d.p->gid; }
    return 0;
}

int procfs_read(const char *interno, void *buf, uint32_t size, uint32_t offset)
{
    Dove d;
    Usc  u;
    int  r = capisci(interno, &d);

    if (r < 0) return r;
    u.buf = buf; u.size = size; u.salta = offset; u.scritti = 0;
    switch (d.cosa) {
    case Q_MEMINFO:   f_meminfo(&u); break;
    case Q_CPUINFO:   f_cpuinfo(&u); break;
    case Q_STAT:      f_stat(&u); break;
    case Q_UPTIME:    f_uptime(&u); break;
    case Q_MOUNTS:    f_mounts(&u); break;
    case Q_VERSION:   testo(&u, "EX-OS version " EXOS_VERSION "\n"); break;
    case Q_P_STAT:    f_p_stat(&u, d.p, 1); break;
    case Q_P_STATUS:  f_p_status(&u, d.p); break;
    case Q_P_CMDLINE: testo(&u, d.p->name); metti(&u, '\0'); break;
    case Q_P_EXE:     testo(&u, d.p->name); break;
    case Q_P_CWD:     testo(&u, d.p->cwd[0] ? d.p->cwd : "/"); break;
    case Q_P_LIMITS:  f_p_limits(&u, d.p); break;
    case Q_P_FD_N:    voce(&u, "fd:", FDV(d.p, d.fd).type, ""); break;
    case Q_T_STAT:    f_p_stat(&u, d.t, 0); break;
    case Q_T_STATUS:  f_p_status(&u, d.t); break;
    default:          return ERR(EISDIR);
    }
    return (int)u.scritti;
}

static void nome_num(char *o, uint32_t v)
{
    char b[12];
    int  n = 0, k = 0;

    do { b[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n > 0) o[k++] = b[--n];
    o[k] = '\0';
}

static void nome_txt(char *o, const char *s)
{
    while ((*o++ = *s++) != '\0') ;
}

/* One entry at a time, counted by position: a table with holes has no other
 * stable way to page through it. */
static int voce_dir(uint32_t *visti, uint32_t da, uint32_t cap, VfsDirEntry *out,
                    uint32_t *n, const char *testo_n, uint32_t num_n, int dir)
{
    if ((*visti)++ < da || *n >= cap) return *n >= cap;
    if (testo_n) nome_txt(out[*n].nome, testo_n);
    else         nome_num(out[*n].nome, num_n);
    out[*n].dimensione = 0;
    out[*n].ident = *visti + 1000000u;
    out[*n].is_dir = (uint8_t)dir;
    (*n)++;
    return 0;
}

int procfs_readdir(const char *interno, VfsDirEntry *out, uint32_t cap, uint32_t da)
{
    Dove     d;
    uint32_t visti = 0, n = 0, i;
    int      r = capisci(interno, &d);

    if (r < 0) return r;
    switch (d.cosa) {
    case Q_RADICE:
        for (i = 0; i < N_CIMA; i++)
            if (voce_dir(&visti, da, cap, out, &n, g_cima[i].nome, 0, 0)) return (int)n;
        if (voce_dir(&visti, da, cap, out, &n, "self", 0, 1)) return (int)n;
        for (i = 0; i < g_proc_posti; i++) {
            const Process *p = PROC_POSTO(i);

            if (p->state == PROC_UNUSED || p->pid == 0 || p->tgid != p->pid) continue;
            if (voce_dir(&visti, da, cap, out, &n, 0, p->pid, 1)) return (int)n;
        }
        return (int)n;
    case Q_PID:
        for (i = 0; i < N_PID; i++)
            if (voce_dir(&visti, da, cap, out, &n, g_pid[i].nome, 0,
                         g_pid[i].cosa == Q_P_FD || g_pid[i].cosa == Q_P_TASK)) return (int)n;
        return (int)n;
    case Q_P_FD:
        if (!d.p->fdt) return 0;
        for (i = 0; i < (uint32_t)FD_N(d.p); i++) {
            if (!fd_aperto(d.p, i)) continue;
            if (voce_dir(&visti, da, cap, out, &n, 0, i, 0)) return (int)n;
        }
        return (int)n;
    case Q_P_TASK:
        for (i = 0; i < g_proc_posti; i++) {
            const Process *p = PROC_POSTO(i);

            if (p->state == PROC_UNUSED || p->tgid != d.p->pid) continue;
            if (voce_dir(&visti, da, cap, out, &n, 0, p->pid, 1)) return (int)n;
        }
        return (int)n;
    case Q_T:
        if (voce_dir(&visti, da, cap, out, &n, "stat", 0, 0)) return (int)n;
        voce_dir(&visti, da, cap, out, &n, "status", 0, 0);
        return (int)n;
    default:
        return ERR(ENOTDIR);
    }
}
