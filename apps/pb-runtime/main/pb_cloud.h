#ifndef PB_CLOUD_H
#define PB_CLOUD_H

#include <stdint.h>

#include "esp_err.h"
#include "pb_actions.h"
#include "pb_view.h"

typedef struct {
    const char *base_url;
    const char *device_serial;
    const char *device_token;
    const char *firmware_version;
    int timeout_ms;
} pb_cloud_config_t;

typedef struct {
    pb_cloud_config_t config;
} pb_cloud_t;

esp_err_t pb_cloud_init(pb_cloud_t *cloud, const pb_cloud_config_t *config);
esp_err_t pb_cloud_fetch_state(pb_cloud_t *cloud, pb_app_state_t *state);
esp_err_t pb_cloud_post_action(
    pb_cloud_t *cloud,
    pb_action_t action,
    uint64_t *revision
);

#endif
