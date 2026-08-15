/* =============================================================================
 * kernel/include/drvmgr.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#ifndef DRVMGR_H
#define DRVMGR_H

#include "kernel.h"

#define DRV_NAME_LEN    32

/* Tipi puntatore alle funzioni del driver */
typedef int  (*DrvInitFn)(void);
typedef int  (*DrvReadFn)(void *buf, size_t n);
typedef int  (*DrvWriteFn)(const void *buf, size_t n);
typedef int  (*DrvIoctlFn)(int cmd, void *arg);
typedef void (*DrvExitFn)(void);

/* Entry nella tabella driver */
typedef struct {
    char        name[DRV_NAME_LEN]; /* Nome driver (es. "floppy") */
    uint32_t    phys_base;          /* Indirizzo fisico base codice driver */
    uint32_t    pages;              /* Pagine allocate */
    uint8_t     loaded;             /* 1 = caricato e attivo */
    /* Puntatori alle funzioni del driver */
    DrvInitFn   drv_init;
    DrvReadFn   drv_read;
    DrvWriteFn  drv_write;
    DrvIoctlFn  drv_ioctl;
    DrvExitFn   drv_exit;
} DriverEntry;

/* Interfaccia pubblica */
void         drvmgr_init(void);
DriverEntry *drvmgr_get(const char *name);
int          drvmgr_unload(const char *name);
void         drvmgr_dump(void);

#endif /* DRVMGR_H */
