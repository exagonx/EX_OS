/* =============================================================================
 * drivers/ar5k/glue.c — what ath5k's hardware code calls from the parts of
 * Linux that were not ported (base.c, gpio.c, ani.c, ath/hw.c)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * ath_hw_setbssidmask is Linux's drivers/net/wireless/ath/hw.c (ISC); the
 * rest is two lines each.
 * ============================================================================= */
#include "ath5k.h"
#include "ath5k/reg.h"      /* AR5K_SREV */
#include "../ar5k/reg.h"    /* ath/reg.h: AR_STA_ID0... */

int ath5k_hw_read_srev(struct ath5k_hw *ah)
{
    ah->ah_mac_srev = ath5k_hw_reg_read(ah, AR5K_SREV);
    return 0;
}

/* Linux's ath5k gpio.c (ISC), AR5211 and later only. The card's LED pin on
 * the mini-PCIe slot follows these PCICFG bits - on the user's HP laptop the
 * WiFi light is that pin, not a GPIO (still orange with the radio on and
 * GPIO 3 driven, 15 October 2026). The ANI hook and the printer are in
 * glue_libero.c. */
void ath5k_hw_set_ledstate(struct ath5k_hw *ah, unsigned int state)
{
    u32 led;

    AR5K_REG_DISABLE_BITS(ah, AR5K_PCICFG, AR5K_PCICFG_LEDMODE | AR5K_PCICFG_LED);
    switch (state) {
    case AR5K_LED_SCAN:
    case AR5K_LED_AUTH:  led = AR5K_PCICFG_LEDMODE_PROP | AR5K_PCICFG_LED_PEND;  break;
    case AR5K_LED_INIT:  led = AR5K_PCICFG_LEDMODE_PROP | AR5K_PCICFG_LED_NONE;  break;
    case AR5K_LED_ASSOC:
    case AR5K_LED_RUN:   led = AR5K_PCICFG_LEDMODE_PROP | AR5K_PCICFG_LED_ASSOC; break;
    default:             led = AR5K_PCICFG_LEDMODE_PROM | AR5K_PCICFG_LED_NONE;  break;
    }
    AR5K_REG_ENABLE_BITS(ah, AR5K_PCICFG, led);
}

/* ath/hw.c */
void ath_hw_setbssidmask(struct ath_common *common)
{
    struct ath5k_hw *ah = common->ah;
    u32 id1;

    ath5k_hw_reg_write(ah, get_unaligned_le32(common->macaddr), AR_STA_ID0);
    id1 = ath5k_hw_reg_read(ah, AR_STA_ID1) & ~AR_STA_ID1_SADH_MASK;
    id1 |= get_unaligned_le16(common->macaddr + 4);
    ath5k_hw_reg_write(ah, id1, AR_STA_ID1);
    ath5k_hw_reg_write(ah, get_unaligned_le32(common->bssidmask), AR_BSSMSKL);
    ath5k_hw_reg_write(ah, get_unaligned_le16(common->bssidmask + 4), AR_BSSMSKU);
}

/* --- short delays on the TSC --------------------------------------------------
 * Calibrated once against uptime_ms (two tick edges apart, 50 ms). Waits of
 * 10 ms and more go to usleep and yield; shorter ones spin. A machine without
 * a usable TSC (calibration reads 0) falls back to usleep. */
static unsigned long long g_tsc_per_us;

static inline unsigned long long tsc(void)
{
    unsigned int lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned long long)hi << 32) | lo;
}

static void tsc_calibrate(void)
{
    unsigned int t0, t1;
    unsigned long long c0, c1;

    t0 = uptime_ms();
    while (uptime_ms() == t0) ;             /* start on a tick edge */
    t0 = uptime_ms();
    c0 = tsc();
    while ((t1 = uptime_ms()) - t0 < 50) ;
    c1 = tsc();
    /* 32-bit division (no libgcc at 32 bit): 50 ms of a TSC up to 80 GHz fit */
    g_tsc_per_us = (unsigned int)(c1 - c0) / ((t1 - t0) * 1000u);
    if (g_tsc_per_us == 0) g_tsc_per_us = (unsigned long long)-1;
}

void ar5k_udelay(unsigned int us)
{
    unsigned long long fine;

    if (us >= 10000) { usleep(us); return; }
    if (g_tsc_per_us == 0) tsc_calibrate();
    if (g_tsc_per_us == (unsigned long long)-1) { usleep(us); return; }
    fine = tsc() + (unsigned long long)us * g_tsc_per_us;
    while (tsc() < fine) __asm__ __volatile__("pause");
}

/* --- GPIO: Linux's ath5k gpio.c (ISC), the four calls the radio switch and
 * the LED need. AR5K_GPIOCR holds two mode bits per pin, AR5K_GPIODO the
 * output levels, AR5K_GPIODI the input levels. */
int ath5k_hw_set_gpio_input(struct ath5k_hw *ah, u32 gpio)
{
    if (gpio >= AR5K_NUM_GPIO) return -EINVAL;
    ath5k_hw_reg_write(ah, (ath5k_hw_reg_read(ah, AR5K_GPIOCR) & ~AR5K_GPIOCR_OUT(gpio))
                       | AR5K_GPIOCR_IN(gpio), AR5K_GPIOCR);
    return 0;
}

int ath5k_hw_set_gpio_output(struct ath5k_hw *ah, u32 gpio)
{
    if (gpio >= AR5K_NUM_GPIO) return -EINVAL;
    ath5k_hw_reg_write(ah, (ath5k_hw_reg_read(ah, AR5K_GPIOCR) & ~AR5K_GPIOCR_OUT(gpio))
                       | AR5K_GPIOCR_OUT(gpio), AR5K_GPIOCR);
    return 0;
}

u32 ath5k_hw_get_gpio(struct ath5k_hw *ah, u32 gpio)
{
    if (gpio >= AR5K_NUM_GPIO) return 0xffffffff;
    return ((ath5k_hw_reg_read(ah, AR5K_GPIODI) & AR5K_GPIODI_M) >> gpio) & 0x1;
}

int ath5k_hw_set_gpio(struct ath5k_hw *ah, u32 gpio, u32 val)
{
    u32 data;

    if (gpio >= AR5K_NUM_GPIO) return -EINVAL;
    data = ath5k_hw_reg_read(ah, AR5K_GPIODO);
    data &= ~(1u << gpio);
    data |= (val & 1u) << gpio;
    ath5k_hw_reg_write(ah, data, AR5K_GPIODO);
    return 0;
}
