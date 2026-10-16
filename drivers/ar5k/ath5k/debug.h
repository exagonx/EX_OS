/* drivers/ar5k/ath5k/debug.h - ath5k's messages on EX-OS: errors and info
 * on the console, debug compiled out. */
#ifndef AR5K_DEBUG_H
#define AR5K_DEBUG_H
#define ATH5K_ERR(ah, fmt, ...)   printf("ar5k: " fmt, ##__VA_ARGS__)
#define ATH5K_WARN(ah, fmt, ...)  printf("ar5k: " fmt, ##__VA_ARGS__)
#define ATH5K_INFO(ah, fmt, ...)  printf("ar5k: " fmt, ##__VA_ARGS__)
#define ATH5K_PRINTF(fmt, ...)    printf("ar5k: " fmt, ##__VA_ARGS__)
#define ATH5K_DBG(ah, type, fmt, ...)         do { } while (0)
#define ATH5K_DBG_UNLIMIT(ah, type, fmt, ...) do { } while (0)
#define ATH5K_ERR_LIMIT                       ATH5K_ERR
#define ath5k_debug_printtxbuf(ah, bf)        do { } while (0)
#define ath5k_debug_init_device(ah)           do { } while (0)
#endif
