#ifndef PB_RUNTIME_H
#define PB_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "pb_app_protocol.h"

#define PB_RUNTIME_VERSION "0.3.0"
#define PB_RUNTIME_FIRMWARE_VERSION "pb-runtime/" PB_RUNTIME_VERSION

typedef enum {
    PB_RUNTIME_TRANSPORT_RESULT_STATE = 0,
    PB_RUNTIME_TRANSPORT_RESULT_EVENT,
} pb_runtime_transport_result_type_t;

typedef struct {
    char event_id[PB_EVENT_ID_LENGTH + 1];
    uint64_t revision;
} pb_runtime_event_result_t;

typedef struct {
    pb_runtime_transport_result_type_t type;
    esp_err_t error;
    union {
        pb_app_state_t state;
        pb_runtime_event_result_t event;
    } data;
} pb_runtime_transport_result_t;

typedef struct {
    bool enabled;
    void *context;
    bool (*is_online)(void *context);
    bool (*take_offline_transition)(void *context);
    esp_err_t (*request_state)(void *context);
    esp_err_t (*post_event)(void *context, const pb_event_t *event);
    bool (*receive)(
        void *context,
        pb_runtime_transport_result_t *result,
        uint32_t timeout_ms);
} pb_runtime_transport_t;

typedef struct {
    const char *cloud_base_url;
    const char *device_serial;
    uint32_t poll_interval_ms;
    uint32_t primary_long_press_ms;
    uint32_t boot_overlay_ms;
    uint32_t offline_overlay_ms;
    pb_runtime_transport_t transport;
} pb_runtime_config_t;

typedef struct {
    bool has_binding;
    char app_id[PB_APP_ID_MAX_LENGTH + 1];
    uint64_t revision;
} pb_runtime_status_t;

// NVS and the PB HAL must be initialized before the Runtime Core.
esp_err_t pb_runtime_init(
    const pb_runtime_config_t *config,
    pb_runtime_status_t *status);

// Runs the single-writer Core loop and only returns when initialization is invalid.
esp_err_t pb_runtime_run(void);

#endif
