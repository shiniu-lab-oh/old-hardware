#ifndef PB_HAL_H
#define PB_HAL_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PB_HAL_MAX_LEDS 4U
#define PB_HAL_WAIT_FOREVER UINT32_MAX

typedef enum {
    PB_CONTROL_NONE = -1,
    PB_CONTROL_PRIMARY = 0,
} pb_control_t;

typedef enum {
    PB_PRESENTATION_NUMBER = 0,
} pb_presentation_type_t;

typedef enum {
    PB_RENDER_APPLIED = 0,
    PB_RENDER_DEGRADED,
    PB_RENDER_UNSUPPORTED,
    PB_RENDER_BUSY,
    PB_RENDER_ERROR,
} pb_render_status_t;

typedef struct {
    pb_render_status_t status;
    esp_err_t error;
} pb_render_result_t;

typedef struct {
    pb_presentation_type_t type;
    int number;
    bool leading_zeroes;
    uint8_t decimal_points;
    uint8_t brightness;
    bool blink;
    uint32_t blink_interval_ms;
    bool leds[PB_HAL_MAX_LEDS];
    uint8_t led_count;
} pb_presentation_t;

typedef struct {
    uint8_t display_digits;
    uint8_t physical_controls;
    uint8_t controllable_leds;
    bool has_decimal_point;
    bool supports_brightness;
    bool supports_blink;
} pb_hal_caps_t;

typedef struct {
    pb_control_t control;
    bool pressed;
    uint64_t sampled_at_ms;
} pb_input_event_t;

typedef struct {
    const char *profile_id;
    // Transitional local binding: one-based physical key mapped to PRIMARY.
    uint8_t primary_key_index;
} pb_hal_config_t;

esp_err_t pb_hal_init(const pb_hal_config_t *config);
esp_err_t pb_hal_get_capabilities(pb_hal_caps_t *caps);
pb_render_result_t pb_hal_render(const pb_presentation_t *presentation);
bool pb_hal_wait_input_event(pb_input_event_t *event, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
