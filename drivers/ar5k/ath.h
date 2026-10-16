/* drivers/ar5k/ath.h - the part of Linux's drivers/net/wireless/ath/ath.h
 * (ISC) that ath5k's hardware code uses, for EX-OS. */
#ifndef AR5K_ATH_H
#define AR5K_ATH_H
#include "compat.h"
#include "net/mac80211.h"

enum ath_bus_type { ATH_PCI, ATH_AHB, ATH_USB };
enum ath_crypt_caps { ATH_CRYPT_CAP_CIPHER_AESCCM = BIT(0), ATH_CRYPT_CAP_MIC_COMBINED = BIT(1) };
#define ATH_KEYMAX 128
struct ath_regulatory {
    char alpha2[2];
    enum nl80211_dfs_regions region;
    u16 country_code;
    u16 max_power_level;
    u16 current_rd;
    s16 power_limit;
    void *regpair;
};
struct ath_common;
struct ath_bus_ops;          /* ath5k.h defines it */
struct ath_common {
    void *ah;
    void *priv;
    struct ieee80211_hw *hw;
    int debug_mask;
    u16 cachelsz;
    u16 curaid;
    u8 macaddr[ETH_ALEN];
    u8 curbssid[ETH_ALEN] __aligned(2);
    u8 bssidmask[ETH_ALEN];
    u32 rx_bufsize;
    u32 keymax;
    enum ath_crypt_caps crypt_caps;
    unsigned int clockrate;
    struct ath_regulatory regulatory;
    const struct ath_bus_ops *bus_ops;
};
void ath_hw_setbssidmask(struct ath_common *common);
static inline int ath_is_49ghz_allowed(u16 rd) { (void)rd; return 0; }
#define ath_regd_get_band_ctl(reg, band) 0x10   /* CTL_FCC: ath5k's tx power code only */
#endif
