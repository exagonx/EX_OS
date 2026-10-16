/* =============================================================================
 * drivers/ar5k/ar5k_hw.h — what the scan and the connection share
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * ar5k_apri wakes the chip and runs ath5k_hw_init (EEPROM, capabilities,
 * MAC); ar5k_chiudi puts the GPIOs, the reset and the sleep state back as
 * the BIOS left them. ar5k_radio_e_led is ath5k's rfkill_hw_start plus the
 * LED, called after the first ath5k_hw_reset as base.c does. All three live
 * in ar5k_scan.c.
 * ============================================================================= */
#ifndef AR5K_HW_H
#define AR5K_HW_H

#include "ath5k.h"

typedef struct {
    struct ath5k_hw *ah;
    u8   mac[ETH_ALEN];
    u32  reset0, sonno0, gpiocr0, gpiodo0;
} Ar5kChip;

int  ar5k_apri(Ar5kChip *k, volatile void *registri, unsigned int sotto_id);
void ar5k_chiudi(Ar5kChip *k);
void ar5k_radio_e_led(struct ath5k_hw *ah);

/* ath5k's channel for 2.4 GHz channel n, mode 11g */
void ar5k_canale_g(struct ieee80211_channel *ch, int n);

#endif
