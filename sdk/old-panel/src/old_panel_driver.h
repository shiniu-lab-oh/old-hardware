#ifndef OLD_PANEL_DRIVER_H
#define OLD_PANEL_DRIVER_H

#include "old_panel.h"

typedef struct {
    const char *profile_id;
    old_panel_caps_t caps;

    esp_err_t (*init)(void);
    esp_err_t (*display_value)(int value,
                               bool leading_zeroes,
                               uint8_t decimal_points);
    esp_err_t (*display_blank)(void);
    esp_err_t (*set_brightness)(uint8_t percent);
    esp_err_t (*set_blink)(bool enabled, uint32_t interval_ms);
    esp_err_t (*set_led)(uint8_t index, bool on);
    old_panel_key_t (*get_key)(void);
    bool (*wait_key_event)(old_panel_key_event_t *event,
                           TickType_t wait_ticks);
} old_panel_driver_t;

#endif
