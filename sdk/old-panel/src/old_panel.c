#include "old_panel.h"

#include "drivers/lp001.h"

esp_err_t old_panel_init(void)
{
    return lp001_init();
}

esp_err_t old_panel_get_capabilities(old_panel_caps_t *caps)
{
    return lp001_get_capabilities(caps);
}

esp_err_t old_panel_display_number(int value)
{
    return lp001_display_number(value);
}

esp_err_t old_panel_display_number_padded(int value)
{
    return lp001_display_number_padded(value);
}

esp_err_t old_panel_display_blank(void)
{
    return lp001_display_blank();
}

esp_err_t old_panel_set_brightness(uint8_t percent)
{
    return lp001_set_brightness(percent);
}

esp_err_t old_panel_set_blink(bool enabled, uint32_t interval_ms)
{
    return lp001_set_blink(enabled, interval_ms);
}

esp_err_t old_panel_set_led(uint8_t index, bool on)
{
    return lp001_set_led(index, on);
}

old_panel_key_t old_panel_get_key(void)
{
    return lp001_get_key();
}

bool old_panel_wait_key_event(
    old_panel_key_event_t *event,
    TickType_t wait_ticks
)
{
    return lp001_wait_key_event(event, wait_ticks);
}
