/* =============================================================================
 * kernel/fs/inotify.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * inotify (0.373): why and how in kernel/include/inotify.h.
 *
 * Everything below runs with interrupts off, like the pipes and shmfd:
 * kmalloc and kfree put IF back as they found it, so they are fine here.
 * User memory is touched only in inotify_leggi, with interrupts on.
 * ============================================================================= */
#include "kernel.h"
#include "inotify.h"
#include "sched.h"
#include "kmalloc.h"
#include "syscall.h"
#include "cfg.h"
#include "vfs.h"

typedef struct Sorv {
    struct Sorv *succ;
    int32_t      wd;
    uint32_t     mask;
    char         p[];               /* the absolute path */
} Sorv;

typedef struct Ev {
    struct Ev *succ;
    int32_t    wd;
    uint32_t   mask, cookie, len;   /* len: the name with its padding */
    char       nome[];
} Ev;

typedef struct {
    int       usato;
    uint32_t  rif;                  /* descriptors open on it */
    Sorv     *sorv;
    int32_t   wd_dopo;              /* the next wd to give */
    Ev       *testa, *coda;
    uint32_t  n_ev, byte;
    uint32_t *attesi;               /* pids asleep in read or poll */
    uint32_t  attesi_n, attesi_posti;
} Ino;

volatile uint32_t g_inotify_sorv;

/* AS MANY AS ARE NEEDED, as in shmfd.c: never freed, the handle is an index */
static Ino     **volatile g_dir;
static uint32_t          g_dir_posti;
static volatile uint32_t g_n;
static uint32_t          g_max_ev;  /* inotify_eventi, read at the first create */
static uint32_t          g_cookie;

static uint32_t s_lung(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }
static int s_uguali(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static void m_copia(void *d, const void *s, uint32_t n)
{
    uint8_t *dd = (uint8_t *)d; const uint8_t *ss = (const uint8_t *)s;
    while (n--) *dd++ = *ss++;
}

static Ino *da_handle(int h)
{
    if (h < 0 || (uint32_t)h >= g_n) return NULL;
    if (!g_dir[h]->usato)            return NULL;
    return g_dir[h];
}

/* the waiters wake and look again (read loops, poll looks at all) */
static void sveglia(Ino *x)
{
    uint32_t i;

    for (i = 0; i < x->attesi_n; i++) sched_unblock_locked(x->attesi[i]);
}

/* one event at the end; the same as the last unread one is not repeated */
static void metti(Ino *x, int32_t wd, uint32_t mask, uint32_t cookie, const char *nome)
{
    uint32_t n = nome ? s_lung(nome) : 0, len = n ? (n + 1u + 15u) & ~15u : 0;
    Ev      *e;

    if (x->coda && x->coda->wd == wd && x->coda->mask == mask &&
        x->coda->cookie == cookie && x->coda->len == len &&
        (len == 0 || s_uguali(x->coda->nome, nome)))
        return;
    if (x->n_ev >= g_max_ev) {
        if (x->coda && x->coda->mask == IN_Q_OVERFLOW) return;
        wd = -1; mask = IN_Q_OVERFLOW; cookie = 0; n = len = 0;
    }
    e = (Ev *)kmalloc(sizeof(Ev) + len);
    if (e == NULL) return;      /* no memory: lost, as an overflow would be */
    e->succ = NULL; e->wd = wd; e->mask = mask; e->cookie = cookie; e->len = len;
    if (len) {
        uint32_t i;
        m_copia(e->nome, nome, n);
        for (i = n; i < len; i++) e->nome[i] = 0;
    }
    if (x->coda) x->coda->succ = e; else x->testa = e;
    x->coda = e;
    x->n_ev++;
    x->byte += 16u + len;
    sveglia(x);
}

static void sorv_via(Ino *x, Sorv **pp)
{
    Sorv *s = *pp;

    *pp = s->succ;
    metti(x, s->wd, IN_IGNORED, 0, NULL);
    kfree(s);
    if (g_inotify_sorv) g_inotify_sorv--;
}

int inotify_crea(void)
{
    if (g_max_ev == 0) {
        const char *s = cfg_get_option("inotify_eventi");
        uint32_t    v = 0;

        while (s && *s >= '0' && *s <= '9') v = v * 10u + (uint32_t)(*s++ - '0');
        g_max_ev = v ? v : 16384u;
    }
    for (;;) {
        Ino     *nuovo, **dir = NULL, **scarto = NULL;
        uint32_t h;

        interrupts_disable();
        for (h = 0; h < g_n; h++) {
            Ino *x = g_dir[h];

            if (!x->usato) {
                x->usato = 1; x->rif = 1; x->wd_dopo = 1;
                interrupts_enable();
                return (int)h;
            }
        }
        interrupts_enable();

        nuovo = (Ino *)kmalloc(sizeof(Ino));
        if (nuovo == NULL) return ERR(ENOMEM);
        { uint8_t *b = (uint8_t *)nuovo; uint32_t i; for (i = 0; i < sizeof(Ino); i++) b[i] = 0; }
        if (g_n >= g_dir_posti) {
            uint32_t posti = g_dir_posti ? g_dir_posti * 2u : 16u;

            dir = (Ino **)kmalloc(posti * sizeof(Ino *));
            if (dir == NULL) { kfree(nuovo); return ERR(ENOMEM); }
            interrupts_disable();
            if (g_n >= g_dir_posti) {
                uint32_t i;

                for (i = 0; i < g_n; i++) dir[i] = g_dir[i];
                g_dir = dir; g_dir_posti = posti;
            } else scarto = dir;
        } else interrupts_disable();
        if (g_n < g_dir_posti) { g_dir[g_n] = nuovo; g_n++; nuovo = NULL; }
        interrupts_enable();
        if (scarto) kfree(scarto);
        if (nuovo)  kfree(nuovo);
    }
}

void inotify_apri(int h)
{
    Ino *x;

    interrupts_disable();
    if ((x = da_handle(h)) != NULL) x->rif++;
    interrupts_enable();
}

/* the last descriptor: watches and events go, the slot stays for the next */
void inotify_chiudi_locked(int h)
{
    Ino *x = da_handle(h);

    if (x == NULL) return;
    if (x->rif > 0) x->rif--;
    if (x->rif > 0) return;
    while (x->sorv) {
        Sorv *s = x->sorv;
        x->sorv = s->succ;
        kfree(s);
        if (g_inotify_sorv) g_inotify_sorv--;
    }
    while (x->testa) {
        Ev *e = x->testa;
        x->testa = e->succ;
        kfree(e);
    }
    x->coda = NULL; x->n_ev = x->byte = 0; x->attesi_n = 0;
    x->usato = 0;               /* attesi stays allocated for the next one */
}

void inotify_chiudi(int h)
{
    interrupts_disable();
    inotify_chiudi_locked(h);
    interrupts_enable();
}

int32_t inotify_guarda(int h, const char *abs, uint32_t mask)
{
    uint32_t n = s_lung(abs);
    Ino     *x;
    Sorv    *s, *nuova;
    int32_t  wd;

    if ((mask & IN_ALL_EVENTS) == 0) return ERR(EINVAL);
    if ((mask & (IN_MASK_ADD | IN_MASK_CREATE)) == (IN_MASK_ADD | IN_MASK_CREATE)) return ERR(EINVAL);
    while (n > 1 && abs[n - 1] == '/') n--;             /* "/tmp/" is "/tmp" */
    nuova = (Sorv *)kmalloc(sizeof(Sorv) + n + 1u);
    if (nuova == NULL) return ERR(ENOMEM);
    m_copia(nuova->p, abs, n);
    nuova->p[n] = 0;

    interrupts_disable();
    if ((x = da_handle(h)) == NULL) { interrupts_enable(); kfree(nuova); return ERR(EBADF); }
    for (s = x->sorv; s; s = s->succ)
        if (s_uguali(s->p, nuova->p)) {
            if (mask & IN_MASK_CREATE) { interrupts_enable(); kfree(nuova); return ERR(EEXIST); }
            s->mask = (mask & IN_MASK_ADD) ? (s->mask | mask) : mask;
            s->mask &= ~(IN_MASK_ADD | IN_MASK_CREATE);
            wd = s->wd;
            interrupts_enable();
            kfree(nuova);
            return wd;
        }
    nuova->wd   = wd = x->wd_dopo++;
    nuova->mask = mask & ~(IN_MASK_ADD | IN_MASK_CREATE);
    nuova->succ = x->sorv;
    x->sorv     = nuova;
    g_inotify_sorv++;
    interrupts_enable();
    return wd;
}

int32_t inotify_togli(int h, int32_t wd)
{
    Ino   *x;
    Sorv **pp;

    interrupts_disable();
    if ((x = da_handle(h)) == NULL) { interrupts_enable(); return ERR(EBADF); }
    for (pp = &x->sorv; *pp; pp = &(*pp)->succ)
        if ((*pp)->wd == wd) { sorv_via(x, pp); interrupts_enable(); return 0; }
    interrupts_enable();
    return ERR(EINVAL);
}

uint32_t inotify_byte(int h)
{
    Ino     *x;
    uint32_t n = 0;

    interrupts_disable();
    if ((x = da_handle(h)) != NULL) n = x->byte;
    interrupts_enable();
    return n;
}

int inotify_pronto_locked(int h)
{
    Ino *x = da_handle(h);

    return x != NULL && x->testa != NULL;
}

int inotify_attesa_registra_locked(int h, uint32_t pid)
{
    Ino     *x = da_handle(h);
    uint32_t i;

    if (x == NULL) return 0;
    for (i = 0; i < x->attesi_n; i++) if (x->attesi[i] == pid) return 1;
    if (x->attesi_n >= x->attesi_posti) {
        uint32_t  posti = x->attesi_posti ? x->attesi_posti * 2u : 4u;
        uint32_t *a     = (uint32_t *)kmalloc(posti * sizeof(uint32_t));

        if (a == NULL) return 0;
        for (i = 0; i < x->attesi_n; i++) a[i] = x->attesi[i];
        if (x->attesi) kfree(x->attesi);
        x->attesi = a; x->attesi_posti = posti;
    }
    x->attesi[x->attesi_n++] = pid;
    return 1;
}

void inotify_attesa_togli_locked(int h, uint32_t pid)
{
    Ino     *x = da_handle(h);
    uint32_t i;

    if (x == NULL) return;
    for (i = 0; i < x->attesi_n; i++)
        if (x->attesi[i] == pid) { x->attesi[i] = x->attesi[--x->attesi_n]; return; }
}

int32_t inotify_leggi(int h, char *ubuf, uint32_t n, int nonblocca)
{
    Process *self = proc_get_current();

    for (;;) {
        Ino     *x;
        Ev      *presi = NULL, *ultimo = NULL;
        uint32_t fatto = 0;

        interrupts_disable();
        if ((x = da_handle(h)) == NULL) { interrupts_enable(); return ERR(EBADF); }
        if (x->testa != NULL) {
            if (16u + x->testa->len > n) { interrupts_enable(); return ERR(EINVAL); }
            /* whole events, as many as fit: taken off under the cli, copied after */
            while (x->testa && fatto + 16u + x->testa->len <= n) {
                Ev *e = x->testa;

                x->testa = e->succ;
                if (x->testa == NULL) x->coda = NULL;
                x->n_ev--;
                x->byte -= 16u + e->len;
                fatto   += 16u + e->len;
                e->succ  = NULL;
                if (ultimo) ultimo->succ = e; else presi = e;
                ultimo = e;
            }
            interrupts_enable();
            fatto = 0;
            while (presi) {
                Ev      *e = presi;
                uint32_t t[4];

                t[0] = (uint32_t)e->wd; t[1] = e->mask; t[2] = e->cookie; t[3] = e->len;
                m_copia(ubuf + fatto, t, 16u);
                if (e->len) m_copia(ubuf + fatto + 16u, e->nome, e->len);
                fatto += 16u + e->len;
                presi = e->succ;
                kfree(e);
            }
            return (int32_t)fatto;
        }
        if (nonblocca) { interrupts_enable(); return ERR(EAGAIN); }
        if (!inotify_attesa_registra_locked(h, self->pid)) self->block_until = g_ticks + 2u;
        else self->block_until = 0;
        sched_block(PROC_BLOCKED);
        interrupts_disable();
        inotify_attesa_togli_locked(h, self->pid);
        self->block_until = 0;
        interrupts_enable();
        if (proc_da_interrompere()) return ERR(EINTR);
    }
}

/* the parent of abs and the name in it; "/" has none */
static int padre_di(const char *abs, char *padre, uint32_t max, const char **nome)
{
    uint32_t n = s_lung(abs), u = n;

    while (u > 0 && abs[u - 1] != '/') u--;
    if (u == 0 || u == n) return 0;
    *nome = abs + u;
    if (u == 1) u = 2;                      /* "/x": the parent is "/" */
    if (u - 1u >= max) return 0;
    m_copia(padre, abs, u - 1u);
    padre[u - 1u] = 0;
    return 1;
}

static uint32_t forma_propria(uint32_t mask)
{
    uint32_t m = mask & ~IN_ISDIR, r = 0;

    if (m & IN_DELETE)  r |= IN_DELETE_SELF;
    r |= m & (IN_ACCESS | IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE | IN_CLOSE_NOWRITE | IN_OPEN);
    if (r && (mask & IN_ISDIR) && !(r & IN_DELETE_SELF)) r |= IN_ISDIR;
    return r;
}

/* one watch hit: what of `ev` it asked, then ONESHOT and DELETE_SELF */
static void colpo(Ino *x, Sorv **pp, uint32_t ev, uint32_t cookie, const char *nome)
{
    Sorv    *s = *pp;
    uint32_t m = ev & s->mask & IN_ALL_EVENTS;

    if (m) metti(x, s->wd, m | (ev & IN_ISDIR), cookie, nome);
    if ((ev & IN_DELETE_SELF) || (m && (s->mask & IN_ONESHOT))) sorv_via(x, pp);
}

/* an event seen by the watch of the parent directory, with the name */
static void metti_se(Ino *x, Sorv *s, uint32_t ev, uint32_t cookie, const char *nome)
{
    uint32_t m = ev & s->mask & IN_ALL_EVENTS;

    if (m) metti(x, s->wd, m | (ev & IN_ISDIR), cookie, nome);
}

void inotify_avvisa(const char *abs, uint32_t mask)
{
    char        padre[VFS_PATH_MAX];
    const char *nome = NULL;
    int         ha_padre;
    uint32_t    proprio = forma_propria(mask), h;

    if (!inotify_attivo() || abs == NULL) return;
    ha_padre = padre_di(abs, padre, sizeof(padre), &nome);
    interrupts_disable();
    for (h = 0; h < g_n; h++) {
        Ino   *x = g_dir[h];
        Sorv **pp;

        if (!x->usato) continue;
        for (pp = &x->sorv; *pp; ) {
            Sorv *s = *pp;

            if (ha_padre && s_uguali(s->p, padre)) {
                uint32_t prima = s->mask;

                metti_se(x, s, mask, 0, nome);
                if ((prima & IN_ONESHOT) && (mask & prima & IN_ALL_EVENTS)) {
                    sorv_via(x, pp);
                    continue;
                }
            }
            if (proprio && s_uguali(s->p, abs)) {
                colpo(x, pp, proprio, 0, NULL);
                if (*pp != s) continue;         /* it went: *pp is the next one */
            }
            pp = &s->succ;
        }
    }
    interrupts_enable();
}

/* a path that starts with `da` and goes on below it ("/a" for "/a/b") */
static int sotto(const char *p, const char *da)
{
    while (*da && *p == *da) { p++; da++; }
    return *da == 0 && *p == '/';
}

/* rename: MOVED_FROM and MOVED_TO with one cookie in the two parents,
 * MOVE_SELF to the watch of the thing moved; its watch and those below it
 * now name the new path. A watched file the rename wrote over: DELETE_SELF
 * and IGNORED, it is gone. */
void inotify_rinomina(const char *da, const char *a, int cartella)
{
    char        p_da[VFS_PATH_MAX], p_a[VFS_PATH_MAX];
    const char *n_da = NULL, *n_a = NULL;
    int         h_da, h_a;
    uint32_t    cookie, isdir = cartella ? IN_ISDIR : 0u, h;
    uint32_t    l_da = s_lung(da), l_a = s_lung(a);

    if (!inotify_attivo() || da == NULL || a == NULL || s_uguali(da, a)) return;
    h_da = padre_di(da, p_da, sizeof(p_da), &n_da);
    h_a  = padre_di(a,  p_a,  sizeof(p_a),  &n_a);
    interrupts_disable();
    cookie = ++g_cookie;
    if (cookie == 0) cookie = ++g_cookie;
    for (h = 0; h < g_n; h++) {
        Ino   *x = g_dir[h];
        Sorv **pp;

        if (!x->usato) continue;
        for (pp = &x->sorv; *pp; ) {
            Sorv *s = *pp;

            if (s_uguali(s->p, a)) { colpo(x, pp, IN_DELETE_SELF, 0, NULL); continue; }
            pp = &s->succ;
        }
        for (pp = &x->sorv; *pp; ) {
            Sorv *s = *pp;

            if (h_da && s_uguali(s->p, p_da)) metti_se(x, s, IN_MOVED_FROM | isdir, cookie, n_da);
            if (h_a  && s_uguali(s->p, p_a))  metti_se(x, s, IN_MOVED_TO   | isdir, cookie, n_a);
            if (s_uguali(s->p, da) || sotto(s->p, da)) {
                uint32_t l = s_lung(s->p), nl = l - l_da + l_a;
                Sorv    *nuova;

                if (s_uguali(s->p, da) && (s->mask & IN_MOVE_SELF))
                    metti(x, s->wd, IN_MOVE_SELF, 0, NULL);
                nuova = (Sorv *)kmalloc(sizeof(Sorv) + nl + 1u);
                if (nuova != NULL) {
                    m_copia(nuova->p, a, l_a);
                    m_copia(nuova->p + l_a, s->p + l_da, l - l_da + 1u);
                    nuova->wd = s->wd; nuova->mask = s->mask; nuova->succ = s->succ;
                    *pp = nuova;
                    kfree(s);
                    s = nuova;
                }
            }
            pp = &s->succ;
        }
    }
    interrupts_enable();
}
