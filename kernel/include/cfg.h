/* =============================================================================
 * kernel/include/cfg.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#ifndef CFG_H
#define CFG_H

#include "kernel.h"

#define CFG_MAX_MODULES  16
#define CFG_MAX_ENV      32
#define CFG_MAX_MOUNT    5    /* la root e' gia' occupata, vedi VFS_MAX_MOUNT */
#define CFG_NAME_LEN     64
#define CFG_PATH_LEN     128

typedef struct {
    char name[CFG_NAME_LEN];
    char path[CFG_PATH_LEN];
} CfgModule;

typedef struct {
    char key[CFG_NAME_LEN];
    char value[CFG_PATH_LEN];
} CfgEnvVar;

/* Una riga della sezione [mount]: `punto = dispositivo`, per esempio
 *     /disk = hd0p1
 * Il punto sta a sinistra perche' e' la chiave: due dispositivi sullo
 * stesso punto sarebbero un conflitto, lo stesso dispositivo su due punti
 * e' solo un errore da rifiutare al montaggio. */
typedef struct {
    char punto[CFG_NAME_LEN];
    char dev[CFG_NAME_LEN];
} CfgMount;

typedef struct {
    /* [kernel] */
    uint32_t    loglevel;
    uint32_t    timer_hz;

    /* verboseboot: 1 = mostra il log di avvio e i banner (default),
     * 0 = avvio silenzioso, solo l'output dei programmi.
     * Il default è 1 e viene impostato da cfg_load() PRIMA di leggere il
     * file: se /boot/kernel.cfg manca o è illeggibile, il sistema deve
     * parlare, non tacere — un boot muto che nasconde un fallimento è
     * peggio di un boot rumoroso. */
    uint32_t    verbose_boot;

    /* [boot] */
    char        shell_path[CFG_PATH_LEN];
    char        modules_list[CFG_PATH_LEN];

    /* [modules] */
    CfgModule   modules[CFG_MAX_MODULES];
    uint32_t    module_count;

    /* [env] */
    CfgEnvVar   env[CFG_MAX_ENV];
    uint32_t    env_count;

    /* [mount] */
    CfgMount    mounts[CFG_MAX_MOUNT];
    uint32_t    mount_count;
} KernelConfig;

KernelConfig *cfg_load(void);
KernelConfig *cfg_get(void);
const char   *cfg_getenv(const char *key);

/* Opzioni scalari fuori da [env] (verboseboot, shell): vedi cfg.c */
const char   *cfg_get_option(const char *key);

#endif /* CFG_H */
