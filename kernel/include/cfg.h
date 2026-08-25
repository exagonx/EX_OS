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

    /* verboseboot: 1 = mostra il log di avvio e i banner,
     * 0 = avvio silenzioso, solo l'output dei programmi (DEFAULT dalla
     * 0.142; fino alla 0.141 il default era 1).
     *
     * Il default viene impostato da cfg_load() PRIMA di leggere il file,
     * quindi vale anche se /boot/kernel.cfg manca o è illeggibile.
     *
     * Silenzioso NON vuol dire muto sui problemi: con 0 il livello di log
     * scende a WARN, gli ERROR restano incondizionati e gli avvisi già
     * emessi vengono riproposti dopo la pulizia dello schermo (PASSO 13c
     * di kernel_main.c). Un avvio che nasconde un fallimento sarebbe
     * peggio di uno rumoroso; qui non succede. */
    uint32_t    verbose_boot;

    /* [boot] */
    /* Disposizione della tastiera: la legge /dev/kbd.drv all'avvio con
     * SYS_GETENV. Sta in [kernel] e non in [env] perche' e' una scelta di
     * sistema, non una variabile che i programmi ereditano. */
    char        keymap[CFG_NAME_LEN];

    /* La lingua del sistema: "it", "en", ... La sceglie l'installatore e la
     * riconsegna `cfg_get_option`, esattamente come `keymap`.
     *
     * ! IL KERNEL NON LA USA PER NIENTE, ed e' voluto. Tradurre e' lavoro dei
     * programmi, e ognuno sa quali messaggi ha; il kernel sa solo DOVE sta
     * scritta la scelta, cosi' che non ce ne siano due. La stessa ragione per
     * cui `keymap` sta qui e l'elenco delle disposizioni sta nel driver. */
    char        lingua[CFG_NAME_LEN];

    /* La risoluzione della console voluta: "testo", "640x480", "800x600",
     * "1024x768". Il kernel NON la usa per impostare niente — non puo': la
     * modalita' grafica si imposta con il BIOS, in modo reale, e quando
     * questo kernel gira quella porta e' chiusa. La imposta Stage 2, che
     * legge un proprio byte interno; questa voce serve a CONFRONTARE quel
     * byte con cio' che l'utente ha dichiarato, e a dirlo se divergono.
     * Vedi /bin/svga, che scrive tutti e due. */
    char        svga[CFG_NAME_LEN];

    char        shell_path[CFG_PATH_LEN];

    /* Chi viene lanciato su ogni console AL POSTO della shell. Vuoto = si
     * lancia la shell, come si e' sempre fatto: un sistema gia' installato
     * si avvia identico finche' non si aggiunge la voce.
     *
     * Con /bin/login si passa dall'autenticazione, e in piu' si ottiene che
     * `exit` torni al prompt di accesso invece di lasciare la console morta
     * — login e' un ciclo che rilancia la shell ogni volta che finisce. */
    char        login_path[CFG_PATH_LEN];
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
