/* =============================================================================
 * tools/prova-ar5k/finto/ar5k_hw.h — a pretend chip for drivers/ar5k/ar5k_conn.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Stands in for drivers/ar5k/ar5k_hw.h when ar5k_conn.c is compiled for the
 * host (tools/prova-ar5k/prova.sh): only the names ar5k_conn.c uses, with
 * the host's libc under them. The chip itself is in prova.c.
 * ============================================================================= */
#ifndef AR5K_HW_H
#define AR5K_HW_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <sched.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int32_t  s32;
#define ETH_ALEN 6

typedef struct { unsigned int byte; uintptr_t virt; u32 fisico; } DmaZona;
int dma_alloc(DmaZona *z);
unsigned int uptime_ms(void);

struct ieee80211_channel { int center_freq; };

struct ath5k_desc {
    u32 ds_link, ds_data;
    u32 ud[6];                      /* [0] done, [1] length, [2] rate0 */
};

struct ath5k_rx_status { u16 rs_datalen; u8 rs_status; s8 rs_rssi; };
struct ath5k_tx_status { u8 ts_status, ts_final_idx; };

struct ath5k_txq_info {
    int tqi_type, tqi_subtype;
    u16 tqi_flags;
    u8  tqi_aifs;
    u16 tqi_cw_min, tqi_cw_max;
};

struct ath_common {
    u16 curaid;
    u8  macaddr[ETH_ALEN], curbssid[ETH_ALEN], bssidmask[ETH_ALEN];
};

struct ath5k_hw {
    struct ath_common common;
    s32 ah_noise_floor;
    u8  ah_tx_ant;
    struct ieee80211_channel *ah_current_channel;
    int (*ah_setup_tx_desc)(struct ath5k_hw *, struct ath5k_desc *, unsigned int, unsigned int,
                            int, int, unsigned int, unsigned int, unsigned int, unsigned int,
                            unsigned int, unsigned int, unsigned int, unsigned int);
    int (*ah_proc_tx_desc)(struct ath5k_hw *, struct ath5k_desc *, struct ath5k_tx_status *);
    int (*ah_proc_rx_desc)(struct ath5k_hw *, struct ath5k_desc *, struct ath5k_rx_status *);
};

typedef struct {
    struct ath5k_hw *ah;
    u8   mac[ETH_ALEN];
} Ar5kChip;

int  ar5k_apri(Ar5kChip *k, volatile void *registri, unsigned int sotto_id);
void ar5k_chiudi(Ar5kChip *k);
void ar5k_canale_g(struct ieee80211_channel *ch, int n);

#define NL80211_IFTYPE_STATION        2
#define AR5K_RX_FILTER_UCAST          0x01
#define AR5K_RX_FILTER_MCAST          0x02
#define AR5K_RX_FILTER_BCAST          0x04
#define AR5K_RX_FILTER_BEACON         0x10
#define AR5K_RXERR_DECRYPT            0x08
#define AR5K_RXERR_MIC                0x10
#define AR5K_TXERR_XRETRY             0x01
#define AR5K_TXDESC_CLRDMASK          0x01
#define AR5K_TXDESC_NOACK             0x02
#define AR5K_TXDESC_INTREQ            0x10
#define AR5K_PKT_TYPE_NORMAL          0
#define AR5K_TUNE_MAX_TXPOWER         63
#define AR5K_TUNE_AIFS                2
#define AR5K_TUNE_CWMIN               15
#define AR5K_TUNE_CWMAX               1023
#define AR5K_TXKEYIX_INVALID          ((u32)-1)
#define AR5K_WME_AC_BE                1
#define AR5K_TX_QUEUE_DATA            1
#define AR5K_TXQ_FLAG_TXEOLINT_ENABLE  0x2
#define AR5K_TXQ_FLAG_TXDESCINT_ENABLE 0x8

int  ath5k_hw_reset(struct ath5k_hw *ah, int op, struct ieee80211_channel *ch, bool fast, bool skip_pcu);
int  ath5k_hw_setup_rx_desc(struct ath5k_hw *ah, struct ath5k_desc *d, u32 size, unsigned int flags);
int  ath5k_hw_set_rxdp(struct ath5k_hw *ah, u32 phys);
u32  ath5k_hw_get_rxdp(struct ath5k_hw *ah);
int  ath5k_hw_start_rx_dma(struct ath5k_hw *ah);
void ath5k_hw_set_rx_filter(struct ath5k_hw *ah, u32 filtro);
void ath5k_hw_set_mcast_filter(struct ath5k_hw *ah, u32 a, u32 b);
void ath5k_hw_start_rx_pcu(struct ath5k_hw *ah);
void ath5k_hw_stop_rx_pcu(struct ath5k_hw *ah);
int  ath5k_hw_dma_stop(struct ath5k_hw *ah);
int  ath5k_hw_setup_tx_queue(struct ath5k_hw *ah, int tipo, struct ath5k_txq_info *qi);
int  ath5k_hw_set_txdp(struct ath5k_hw *ah, unsigned int q, u32 phys);
int  ath5k_hw_start_tx_dma(struct ath5k_hw *ah, unsigned int q);
int  ath5k_hw_setup_mrr_tx_desc(struct ath5k_hw *ah, struct ath5k_desc *d, unsigned int r1, unsigned int t1,
                                unsigned int r2, unsigned int t2, unsigned int r3, unsigned int t3);
void ath5k_hw_set_bssid(struct ath5k_hw *ah);
void ath5k_hw_reg_write(struct ath5k_hw *ah, u32 val, u16 reg);

#endif
