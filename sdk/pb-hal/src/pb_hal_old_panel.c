#include "pb_hal.h"

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "old_panel.h"

static old_panel_caps_t s_panel_caps;
static old_panel_key_t s_primary_key = OLD_PANEL_KEY_NONE;
static bool s_initialized;

static pb_render_result_t render_result(
    pb_render_status_t status,
    esp_err_t error
)
{
    const pb_render_result_t result = {
        .status = status,
        .error = error,
    };
    return result;
}

static pb_render_result_t render_error(esp_err_t error)
{
    if (error == ESP_ERR_NOT_SUPPORTED || error == ESP_ERR_INVALID_ARG) {
        return render_result(PB_RENDER_UNSUPPORTED, error);
    }
    if (error == ESP_ERR_TIMEOUT) {
        return render_result(PB_RENDER_BUSY, error);
    }
    return render_result(PB_RENDER_ERROR, error);
}

esp_err_t pb_hal_init(const pb_hal_config_t *config)
{
    if (config == NULL || config->profile_id == NULL ||
        config->profile_id[0] == '\0' || config->primary_key_index == 0 ||
        config->primary_key_index > 8) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_initialized) {
        const old_panel_config_t panel_config = {
            .profile_id = config->profile_id,
        };
        const esp_err_t err = old_panel_init(&panel_config);
        if (err != ESP_OK) {
            return err;
        }
        const old_panel_key_t requested =
            (old_panel_key_t)(OLD_PANEL_KEY_1 + config->primary_key_index - 1);
        return requested == s_primary_key ? ESP_OK : ESP_ERR_INVALID_STATE;
    }

    const old_panel_config_t panel_config = {
        .profile_id = config->profile_id,
    };
    esp_err_t err = old_panel_init(&panel_config);
    if (err != ESP_OK) {
        return err;
    }

    err = old_panel_get_capabilities(&s_panel_caps);
    if (err != ESP_OK) {
        return err;
    }
    if (config->primary_key_index > s_panel_caps.keys ||
        s_panel_caps.keys > 8 || s_panel_caps.leds > PB_HAL_MAX_LEDS) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    s_primary_key =
        (old_panel_key_t)(OLD_PANEL_KEY_1 + config->primary_key_index - 1);
    s_initialized = true;
    return ESP_OK;
}

esp_err_t pb_hal_get_capabilities(pb_hal_caps_t *caps)
{
    if (caps == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    *caps = (pb_hal_caps_t){
        .display_digits = s_panel_caps.digits,
        .physical_controls = s_panel_caps.keys,
        .controllable_leds = s_panel_caps.leds,
        .has_decimal_point = s_panel_caps.has_decimal_point,
        .supports_brightness = s_panel_caps.supports_brightness,
        .supports_blink = s_panel_caps.supports_blink,
    };
    return ESP_OK;
}

pb_render_result_t pb_hal_render(const pb_presentation_t *presentation)
{
    if (!s_initialized) {
        return render_error(ESP_ERR_INVALID_STATE);
    }
    if (presentation == NULL || presentation->brightness > 100 ||
        presentation->led_count > PB_HAL_MAX_LEDS ||
        (presentation->blink && presentation->blink_interval_ms == 0)) {
        return render_result(PB_RENDER_ERROR, ESP_ERR_INVALID_ARG);
    }
    if (presentation->type != PB_PRESENTATION_NUMBER) {
        return render_result(PB_RENDER_UNSUPPORTED, ESP_ERR_NOT_SUPPORTED);
    }

    bool degraded = false;
    if (presentation->decimal_points != 0 && !s_panel_caps.has_decimal_point) {
        degraded = true;
    }

    esp_err_t err = old_panel_display_value(
        presentation->number,
        presentation->leading_zeroes,
        s_panel_caps.has_decimal_point ? presentation->decimal_points : 0);
    if (err != ESP_OK) {
        return render_error(err);
    }

    if (s_panel_caps.supports_brightness) {
        err = old_panel_set_brightness(presentation->brightness);
        if (err != ESP_OK) {
            return render_error(err);
        }
    } else if (presentation->brightness != 100) {
        degraded = true;
    }

    if (s_panel_caps.supports_blink) {
        const uint32_t interval_ms = presentation->blink
                                         ? presentation->blink_interval_ms
                                         : 0;
        err = old_panel_set_blink(presentation->blink, interval_ms);
        if (err != ESP_OK) {
            return render_error(err);
        }
    } else if (presentation->blink) {
        degraded = true;
    }

    if (presentation->led_count > s_panel_caps.leds) {
        degraded = true;
    }
    for (uint8_t i = 0; i < s_panel_caps.leds; ++i) {
        const bool on = i < presentation->led_count
                            ? presentation->leds[i]
                            : false;
        err = old_panel_set_led(i, on);
        if (err != ESP_OK) {
            return render_error(err);
        }
    }

    return render_result(
        degraded ? PB_RENDER_DEGRADED : PB_RENDER_APPLIED,
        ESP_OK);
}

bool pb_hal_wait_input_event(pb_input_event_t *event, uint32_t timeout_ms)
{
    if (!s_initialized || event == NULL) {
        return false;
    }

    const TickType_t wait_ticks = timeout_ms == PB_HAL_WAIT_FOREVER
                                      ? portMAX_DELAY
                                      : pdMS_TO_TICKS(timeout_ms);
    old_panel_key_event_t panel_event;
    if (!old_panel_wait_key_event(&panel_event, wait_ticks)) {
        return false;
    }

    event->control = panel_event.key == s_primary_key
                         ? PB_CONTROL_PRIMARY
                         : PB_CONTROL_NONE;
    event->pressed = panel_event.pressed;
    event->sampled_at_ms = panel_event.sampled_at_ms;
    return true;
}
