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

    OLD_PANEL_KEY_POWER,
    OLD_PANEL_KEY_MENU,
    OLD_PANEL_KEY_OK,
    OLD_PANEL_KEY_UP,
    OLD_PANEL_KEY_DOWN,
    OLD_PANEL_KEY_LEFT,
    OLD_PANEL_KEY_RIGHT,
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
    bool has_ir;
    bool has_card_slot;
} old_panel_caps_t;

esp_err_t old_panel_init(void);

esp_err_t old_panel_get_capabilities(old_panel_caps_t *caps);

esp_err_t old_panel_display_number(int value);
esp_err_t old_panel_display_number_padded(int value);
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
