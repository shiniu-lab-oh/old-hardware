#ifndef PB_OVERLAY_H
#define PB_OVERLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "old_panel.h"

typedef struct {
    bool active;
    int64_t expires_at_us;
} pb_overlay_t;

void pb_overlay_init(pb_overlay_t *overlay);
esp_err_t pb_overlay_show_code(
    pb_overlay_t *overlay,
    int code,
    uint32_t duration_ms,
    bool blink,
    const old_panel_caps_t *caps
);
bool pb_overlay_active(const pb_overlay_t *overlay);
bool pb_overlay_take_expired(pb_overlay_t *overlay);

#endif
