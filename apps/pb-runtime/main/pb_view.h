#ifndef PB_VIEW_H
#define PB_VIEW_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "old_panel.h"
#include "pb_app_protocol.h"

void pb_view_default(pb_view_t *view);
esp_err_t pb_view_render(const pb_view_t *view, const old_panel_caps_t *caps);
esp_err_t pb_view_load_last(pb_view_t *view, uint64_t *revision);
esp_err_t pb_view_store_last(const pb_view_t *view, uint64_t revision);

#endif
