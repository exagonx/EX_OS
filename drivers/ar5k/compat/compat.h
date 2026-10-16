/* =============================================================================
 * drivers/ar5k/compat/compat.h — the few Linux kernel pieces ath5k's hardware
 * code expects, for EX-OS (ring 3, libc)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The ath5k files in drivers/ar5k/ath5k are Linux's (ISC) and are kept as
 * they are; every Linux header they include (linux/..., net/mac80211.h,
 * asm/unaligned.h) is a one-line file in this directory that includes this
 * one. What the hardware code really uses is here: fixed-size types, the
 * register access, delays, allocation, a few macros. Locks, tasklets and
 * deferred work are names only - one thread drives the card.
 * ============================================================================= */
#ifndef AR5K_COMPAT_H
#define AR5K_COMPAT_H

#include "libc.h"

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed char        s8;
typedef short              s16;
typedef int                s32;
typedef long long          s64;
typedef u16 __le16;
typedef u32 __le32;
typedef u16 __be16;
typedef u32 __be32;
#ifndef __cplusplus
typedef _Bool bool;
#define true  1
#define false 0
#endif
typedef unsigned int gfp_t;
typedef u64 dma_addr_t;

#define __iomem
#define __aligned(x)   __attribute__((aligned(x)))
#define __packed       __attribute__((packed))
#define __must_check
#define __always_inline inline
#define likely(x)      (x)
#define unlikely(x)    (x)
#define fallthrough    do { } while (0)
#define EXPORT_SYMBOL(x)
#define READ_ONCE(x)   (x)
#define WRITE_ONCE(x, v) ((x) = (v))

#define ARRAY_SIZE(a)  (sizeof(a) / sizeof((a)[0]))
#define BIT(n)         (1u << (n))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define min(a, b)      ((a) < (b) ? (a) : (b))
#define max(a, b)      ((a) > (b) ? (a) : (b))
#define min_t(t, a, b) ((t)(a) < (t)(b) ? (t)(a) : (t)(b))
#define max_t(t, a, b) ((t)(a) > (t)(b) ? (t)(a) : (t)(b))
#define clamp(v, lo, hi) min(max(v, lo), hi)
#define abs(x)         ((x) < 0 ? -(x) : (x))
#define ETH_ALEN       6
#define BITS_PER_LONG  (8 * (int)sizeof(long))
#define BITS_TO_LONGS(n) (((n) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define DECLARE_BITMAP(name, bits) unsigned long name[BITS_TO_LONGS(bits)]
#define __set_bit(n, a)   ((a)[(n) / BITS_PER_LONG] |= 1ul << ((n) % BITS_PER_LONG))
#define __clear_bit(n, a) ((a)[(n) / BITS_PER_LONG] &= ~(1ul << ((n) % BITS_PER_LONG)))
#define set_bit(n, a)     __set_bit(n, a)
#define clear_bit(n, a)   __clear_bit(n, a)
#define test_bit(n, a)    (((a)[(n) / BITS_PER_LONG] >> ((n) % BITS_PER_LONG)) & 1ul)

#define cpu_to_le16(x) ((u16)(x))
#define cpu_to_le32(x) ((u32)(x))
#define le16_to_cpu(x) ((u16)(x))
#define le32_to_cpu(x) ((u32)(x))
#define get_unaligned_le16(p) ((u16)(((const u8 *)(p))[0] | (((const u8 *)(p))[1] << 8)))
#define get_unaligned_le32(p) ((u32)get_unaligned_le16(p) | ((u32)get_unaligned_le16((const u8 *)(p) + 2) << 16))

static inline int ilog2(unsigned long v) { int r = -1; while (v) { v >>= 1; r++; } return r; }
static inline int is_power_of_2(unsigned long v) { return v && !(v & (v - 1)); }
#define roundup_pow_of_two(n) (1ul << (ilog2((n) - 1) + 1))

/* delays. EX-OS usleep sleeps at least one 10 ms tick, and ath5k polls with
 * udelay(15) up to 20000 times (AR5K_TUNE_REGISTER_TIMEOUT): through usleep
 * one such wait took minutes, and the first scan on the laptop never got
 * past channel 1. Short waits spin on the TSC instead (glue.c). */
void ar5k_udelay(unsigned int us);
#define udelay(us)               ar5k_udelay(us)
#define mdelay(ms)               ar5k_udelay((ms) * 1000)
#define msleep(ms)               usleep((ms) * 1000)
#define usleep_range(a, b)       ar5k_udelay(a)

/* registers: ath5k_hw_reg_read/write go through these */
static inline u32  ioread32(const volatile void *p)        { return *(const volatile u32 *)p; }
static inline void iowrite32(u32 v, volatile void *p)      { *(volatile u32 *)p = v; }

/* allocation */
#define GFP_KERNEL 0
#define GFP_ATOMIC 0
#define kmalloc(n, f)            malloc(n)
#define kzalloc(n, f)            calloc(1, n)
#define kcalloc(n, s, f)         calloc(n, s)
#define kfree(p)                 free((void *)(p))

/* messages */
#define pr_err(...)              printf(__VA_ARGS__)
#define pr_warn(...)             printf(__VA_ARGS__)
#define pr_info(...)             printf(__VA_ARGS__)
#define pr_debug(...)            do { } while (0)
#define WARN_ON(c)               (c)
#define WARN_ON_ONCE(c)          (c)
#define BUG_ON(c)                do { if (c) printf("ar5k: BUG_ON at %s:%d\n", __FILE__, __LINE__); } while (0)

/* names only: one thread drives the card */
typedef struct { int x; } spinlock_t;
struct mutex { int x; };
struct work_struct { int x; };
struct delayed_work { int x; };
struct tasklet_struct { int x; };
struct timer_list { int x; };
struct device { int x; };
struct pci_dev { int is_pcie; };
struct platform_device { int id; };
struct led_classdev { int x; };
struct sk_buff;
#define spin_lock_init(l)        do { } while (0)
#define spin_lock(l)             do { } while (0)
#define spin_unlock(l)           do { } while (0)
#define spin_lock_bh(l)          do { } while (0)
#define spin_unlock_bh(l)        do { } while (0)
#define spin_lock_irqsave(l, f)  do { (void)(f); } while (0)
#define spin_unlock_irqrestore(l, f) do { (void)(f); } while (0)
#define mutex_lock(m)            do { } while (0)
#define mutex_unlock(m)          do { } while (0)
#define pci_is_pcie(p)           ((p) ? (p)->is_pcie : 0)
#define to_platform_device(d)    ((struct platform_device *)(d))

#define __printf(a, b)           __attribute__((format(printf, a, b)))
typedef unsigned int u_int;
struct list_head { struct list_head *next, *prev; };
#define INIT_LIST_HEAD(l)        do { (l)->next = (l); (l)->prev = (l); } while (0)
/* the cycle counters of ath/hw.c, named by ath5k's ANI state */
struct ath_cycle_counters { u32 cycles, rx_busy, rx_frame, tx_frame; };
/* include/linux/average.h: an exponentially weighted moving average */
#define DECLARE_EWMA(name, _prec, _wrcp) \
    struct ewma_##name { unsigned long internal; }; \
    static inline void ewma_##name##_init(struct ewma_##name *e) { e->internal = 0; } \
    static inline unsigned long ewma_##name##_read(struct ewma_##name *e) { return e->internal >> (_prec); } \
    static inline void ewma_##name##_add(struct ewma_##name *e, unsigned long val) { \
        unsigned long i = e->internal; \
        e->internal = i ? (((i << (_wrcp)) - i) + (val << (_prec))) >> (_wrcp) : (val << (_prec)); }

#define KERN_ERR      "<3>"
#define KERN_WARNING  "<4>"
#define KERN_INFO     "<6>"
#define KERN_DEBUG    "<7>"
#define ENOTSUPP      524
#define FCS_LEN       4
#define net_ratelimit() 1
struct survey_info { int x; };

#define kmalloc_array(n, s, f)   calloc(n, s)
#define local_irq_save(f)        do { (f) = 0; } while (0)
#define local_irq_restore(f)     do { (void)(f); } while (0)
#define roundup(x, y)            ((((x) + ((y) - 1)) / (y)) * (y))
static inline s32 sign_extend32(u32 v, int bit) { int s = 31 - bit; return (s32)(v << s) >> s; }
static inline void eth_broadcast_addr(u8 *a) { memset(a, 0xff, ETH_ALEN); }

#endif /* AR5K_COMPAT_H */
