#ifndef PB_BINDING_H
#define PB_BINDING_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "pb_app_protocol.h"

typedef struct {
    uint64_t revision;
    char app_id[PB_APP_ID_MAX_LENGTH + 1];
    pb_view_t view;
    pb_timer_config_t timer;
} pb_binding_t;

void pb_binding_init(pb_binding_t *binding);
esp_err_t pb_binding_from_state(
    pb_binding_t *binding,
    const pb_app_state_t *state
);
bool pb_binding_matches_state(
    const pb_binding_t *binding,
    const pb_app_state_t *state
);
esp_err_t pb_binding_load(
    const char *cloud_base_url,
    const char *device_serial,
    pb_binding_t *binding
);
esp_err_t pb_binding_store(
    const char *cloud_base_url,
    const char *device_serial,
    const pb_binding_t *binding
);

#endif
