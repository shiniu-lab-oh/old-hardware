#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Konka SDC251 mapping measured on 2026-09-07; see KEY_MAPPING.md.
 * CT1668 compatibility assumption still applies to the underlying transport.
 * These logical masks are independent of the CT1668 raw byte/bit locations. */
#define SDC251_KEYS_RAW_BYTES 5U
#define SDC251_KEY_COUNT 7U
#define SDC251_KEYS_STABLE_SAMPLES 3U

typedef uint8_t sdc251_key_mask_t;
enum {
    SDC251_KEY_NONE      = 0,
    SDC251_KEY_MENU      = 1U << 0,
    SDC251_KEY_EXIT      = 1U << 1,
    SDC251_KEY_OK        = 1U << 2,
    SDC251_KEY_VOL_MINUS = 1U << 3,
    SDC251_KEY_VOL_PLUS  = 1U << 4,
    SDC251_KEY_CH_MINUS  = 1U << 5,
    SDC251_KEY_CH_PLUS   = 1U << 6,
    SDC251_KEY_ALL       = 0x7F,
};

/* Zero-initialize before first use (or to reset); one state per caller.
 * Supply one raw sample every 20ms. Three matches debounce each key
 * independently, about 40ms from the first matching sample. */
typedef struct {
    sdc251_key_mask_t held;
    sdc251_key_mask_t candidate;
    uint8_t matching[SDC251_KEY_COUNT];
} sdc251_keys_state_t;

typedef struct {
    sdc251_key_mask_t held; /* Current debounced state. */
    sdc251_key_mask_t down; /* Newly pressed on this update only. */
    sdc251_key_mask_t up;   /* Newly released on this update only. */
} sdc251_key_events_t;

/* Pure decode, no GPIO, delay or debounce. High means pressed on all 7 keys.
 * Unknown raw bits return ESP_ERR_INVALID_RESPONSE and pressed=0, rather
 * than interpreting floating-bus FF as seven pressed buttons. */
esp_err_t sdc251_keys_decode(const uint8_t raw[SDC251_KEYS_RAW_BYTES],
                            sdc251_key_mask_t *pressed);

/* Pure state update; caller obtains raw with ct1668_read_keys().
 * Holding a key produces no repeat DOWN. A key held at startup produces
 * DOWN after three matches. No automatic repeat/long-press policy.
 * An invalid raw sample resets pending debounce counts, preserves held,
 * returns the decode error, and generates no DOWN/UP events. */
esp_err_t sdc251_keys_update(sdc251_keys_state_t *state,
                            const uint8_t raw[SDC251_KEYS_RAW_BYTES],
                            sdc251_key_events_t *events);

/* Accepts one logical key mask; NONE, multiple bits or unknown => UNKNOWN. */
const char *sdc251_key_name(sdc251_key_mask_t key);

#ifdef __cplusplus
}
#endif
