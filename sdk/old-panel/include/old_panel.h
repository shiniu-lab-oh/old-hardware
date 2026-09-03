#ifndef OLD_PANEL_H
#define OLD_PANEL_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OLD_PANEL_KEY_NONE = -1,
    OLD_PANEL_KEY_1 = 0,
    OLD_PANEL_KEY_2,
    OLD_PANEL_KEY_3,
    OLD_PANEL_KEY_4,
    OLD_PANEL_KEY_5,
    OLD_PANEL_KEY_6,
    OLD_PANEL_KEY_7,
    OLD_PANEL_KEY_8,
} old_panel_key_t;

typedef struct {
    old_panel_key_t key;
    bool pressed;
} old_panel_key_event_t;

typedef struct {
    uint8_t digits;
    uint8_t keys;
    // Number of LEDs that applications can control through old_panel_set_led().
    uint8_t leds;
    bool has_decimal_point;
    bool supports_brightness;
    bool supports_blink;
} old_panel_caps_t;

typedef struct {
    const char *profile_id;
} old_panel_config_t;

esp_err_t old_panel_init(const old_panel_config_t *config);

esp_err_t old_panel_get_capabilities(old_panel_caps_t *caps);

esp_err_t old_panel_display_number(int value);
esp_err_t old_panel_display_number_padded(int value);
// Decimal-point bit 0 is the rightmost digit; higher bits move left.
esp_err_t old_panel_display_value(
    int value,
    bool leading_zeroes,
    uint8_t decimal_points
);
esp_err_t old_panel_display_blank(void);

esp_err_t old_panel_set_brightness(uint8_t percent);
esp_err_t old_panel_set_blink(bool enabled, uint32_t interval_ms);

// LED indices are logical and run from 0 to capabilities.leds - 1.
esp_err_t old_panel_set_led(uint8_t index, bool on);

old_panel_key_t old_panel_get_key(void);

bool old_panel_wait_key_event(
    old_panel_key_event_t *event,
    TickType_t wait_ticks
);

#ifdef __cplusplus
}
#endif

#endif
