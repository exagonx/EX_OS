/* =============================================================================
 * kernel/include/ahci.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * I dischi SATA dietro un controller AHCI: vedi kernel/block/ahci.c. Chi li
 * usa e' kernel/block/ata.c, che li mette fra hd0..hd3 accanto agli altri.
 * ============================================================================= */
#ifndef AHCI_H
#define AHCI_H

#include "kernel.h"

#define AHCI_MAX_DISCHI 4

/* Cerca i controller e i loro dischi. `anche_emulati` = 0: solo i controller
 * che si dichiarano AHCI; 1: anche quelli presentati come IDE o RAID che hanno
 * i registri AHCI a BAR5. Rende quanti dischi ci sono in tutto. */
int             ahci_cerca(int anche_emulati);
int             ahci_controller_preso(uint32_t bus, uint32_t slot, uint32_t fn);
int             ahci_candidati(void);       /* c'e' un disco che potrebbe passare? (sola lettura) */
int             ahci_guarda(void);          /* i registri nel log, senza scrivere niente */
int             ahci_dischi(void);
const uint16_t *ahci_identify(int k);       /* le 256 parole di IDENTIFY */
uint64_t        ahci_settori(int k);
int             ahci_rw(int k, uint64_t lba, uint32_t n, void *buf, int scrivi);
int             ahci_flush(int k);

#endif /* AHCI_H */
