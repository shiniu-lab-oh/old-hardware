#include "old_panel.h"

#include <stddef.h>
#include <string.h>

#include "old_panel_driver.h"
#include "old_panel_registry.h"

static const old_panel_driver_t *s_driver;

esp_err_t old_panel_init(const old_panel_config_t *config)
{
    if (config == NULL || config->profile_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_driver != NULL) {
        return strcmp(s_driver->profile_id, config->profile_id) == 0
                   ? ESP_OK
                   : ESP_ERR_INVALID_STATE;
    }

    const old_panel_driver_t *driver =
        old_panel_registry_find(config->profile_id);
    if (driver == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    const esp_err_t err = driver->init();
    if (err != ESP_OK) {
        return err;
    }

    s_driver = driver;
    return ESP_OK;
}

esp_err_t old_panel_get_capabilities(old_panel_caps_t *caps)
{
    if (caps == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_driver == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    *caps = s_driver->caps;
    return ESP_OK;
}

esp_err_t old_panel_display_number(int value)
{
    return old_panel_display_value(value, false, 0);
}

esp_err_t old_panel_display_number_padded(int value)
{
    return old_panel_display_value(value, true, 0);
}

esp_err_t old_panel_display_value(int value,
                                  bool leading_zeroes,
                                  uint8_t decimal_points)
{
    if (s_driver == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_driver->display_value == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    return s_driver->display_value(value, leading_zeroes, decimal_points);
}

esp_err_t old_panel_display_blank(void)
{
    if (s_driver == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_driver->display_blank == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    return s_driver->display_blank();
}

esp_err_t old_panel_set_brightness(uint8_t percent)
{
    if (s_driver == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_driver->set_brightness == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    return s_driver->set_brightness(percent);
}

esp_err_t old_panel_set_blink(bool enabled, uint32_t interval_ms)
{
    if (s_driver == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_driver->set_blink == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    return s_driver->set_blink(enabled, interval_ms);
}

esp_err_t old_panel_set_led(uint8_t index, bool on)
{
    if (s_driver == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_driver->set_led == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    return s_driver->set_led(index, on);
}

old_panel_key_t old_panel_get_key(void)
{
    if (s_driver == NULL || s_driver->get_key == NULL) {
        return OLD_PANEL_KEY_NONE;
    }

    return s_driver->get_key();
}

bool old_panel_wait_key_event(old_panel_key_event_t *event,
                              TickType_t wait_ticks)
{
    return s_driver != NULL &&
           s_driver->wait_key_event != NULL &&
           s_driver->wait_key_event(event, wait_ticks);
}
