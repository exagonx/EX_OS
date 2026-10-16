/* drivers/ar5k/compat/net/mac80211.h - the 802.11 types ath5k's hardware
 * code names, for EX-OS. Not Linux's mac80211: the frames are built and read
 * by drivers/ar5k itself. GPL-2.0-or-later, (C) 2026 Graziano Falcone. */
#ifndef AR5K_MAC80211_H
#define AR5K_MAC80211_H
#include "compat.h"

enum nl80211_band { NL80211_BAND_2GHZ, NL80211_BAND_5GHZ, NL80211_BAND_60GHZ, NL80211_BAND_6GHZ, NUM_NL80211_BANDS };
enum nl80211_iftype { NL80211_IFTYPE_UNSPECIFIED, NL80211_IFTYPE_ADHOC, NL80211_IFTYPE_STATION,
                      NL80211_IFTYPE_AP, NL80211_IFTYPE_AP_VLAN, NL80211_IFTYPE_WDS,
                      NL80211_IFTYPE_MONITOR, NL80211_IFTYPE_MESH_POINT };
enum nl80211_dfs_regions { NL80211_DFS_UNSET };

struct ieee80211_channel {
    enum nl80211_band band;
    u32 center_freq;
    u16 hw_value;
    u32 flags;
    int max_power;
};
#define IEEE80211_RATE_SHORT_PREAMBLE  BIT(0)
#define IEEE80211_RATE_SUPPORTS_5MHZ   BIT(5)
#define IEEE80211_RATE_SUPPORTS_10MHZ  BIT(6)
struct ieee80211_rate { u32 flags; u16 bitrate; u16 hw_value, hw_value_short; };
struct ieee80211_supported_band {
    struct ieee80211_channel *channels;
    struct ieee80211_rate *bitrates;
    enum nl80211_band band;
    int n_channels, n_bitrates;
};
struct ieee80211_hw { void *priv; };
struct ieee80211_vif { enum nl80211_iftype type; };
struct ieee80211_ops { int x; };
#define IEEE80211_MAX_FRAME_LEN 2352

static inline int ieee80211_frequency_to_channel(int f)
{
    if (f == 2484) return 14;
    if (f < 2484)  return (f - 2407) / 5;
    return (f - 5000) / 5;
}

/* ieee80211_generic_frame_duration (net/mac80211/util.c), what ath5k's
 * ath5k_hw_get_frame_duration needs: the time on air of a frame, in us */
static inline u16 ieee80211_generic_frame_duration(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                                                   enum nl80211_band band, size_t len,
                                                   struct ieee80211_rate *rate)
{
    int erp = !(rate->bitrate == 10 || rate->bitrate == 20 || rate->bitrate == 55 || rate->bitrate == 110);
    int dur;
    (void)hw; (void)vif; (void)band;
    if (erp) {
        dur = 16 + 4 + 4 * DIV_ROUND_UP(16 + 8 * (int)len + 6, 4 * rate->bitrate / 10);
    } else {
        dur = 192 + 10 * ((8 * (int)len * 10 + rate->bitrate - 1) / rate->bitrate) / 10;
    }
    return (u16)dur;
}
#endif
