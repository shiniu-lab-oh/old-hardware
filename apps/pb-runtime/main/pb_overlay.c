#include "pb_overlay.h"

#include "esp_timer.h"
#include "pb_view.h"

void pb_overlay_init(pb_overlay_t *overlay)
{
    if (overlay != NULL) {
        overlay->active = false;
        overlay->expires_at_us = 0;
    }
}

esp_err_t pb_overlay_show_code(
    pb_overlay_t *overlay,
    int code,
    uint32_t duration_ms,
    bool blink,
    const old_panel_caps_t *caps
)
{
    if (overlay == NULL || caps == NULL || duration_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    pb_view_t view;
    pb_view_default(&view);
    view.value = code;
    view.blink = blink;
    const esp_err_t err = pb_view_render(&view, caps);
    if (err == ESP_OK) {
        overlay->active = true;
        overlay->expires_at_us = esp_timer_get_time() + (int64_t)duration_ms * 1000LL;
    }
    return err;
}

bool pb_overlay_active(const pb_overlay_t *overlay)
{
    return overlay != NULL && overlay->active;
}

bool pb_overlay_take_expired(pb_overlay_t *overlay)
{
    if (!pb_overlay_active(overlay) || esp_timer_get_time() < overlay->expires_at_us) {
        return false;
    }
    overlay->active = false;
    return true;
}
