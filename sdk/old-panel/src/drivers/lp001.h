#ifndef LP001_H
#define LP001_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "old_panel.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t lp001_init(void);

esp_err_t lp001_get_capabilities(old_panel_caps_t *caps);

esp_err_t lp001_display_number(int value);
esp_err_t lp001_display_number_padded(int value);
esp_err_t lp001_display_blank(void);

esp_err_t lp001_set_brightness(uint8_t percent);
esp_err_t lp001_set_blink(bool enabled, uint32_t interval_ms);

esp_err_t lp001_set_led(uint8_t index, bool on);

old_panel_key_t lp001_get_key(void);

bool lp001_wait_key_event(
    old_panel_key_event_t *event,
    TickType_t wait_ticks
);

#ifdef __cplusplus
}
#endif

#endif
