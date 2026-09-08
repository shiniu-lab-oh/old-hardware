#ifndef PB_NETWORK_WORKER_H
#define PB_NETWORK_WORKER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "pb_app_protocol.h"
#include "pb_cloud.h"

typedef enum {
    PB_NETWORK_RESULT_STATE = 0,
    PB_NETWORK_RESULT_EVENT,
} pb_network_result_type_t;

typedef struct {
    char event_id[PB_EVENT_ID_LENGTH + 1];
    uint64_t revision;
} pb_network_event_result_t;

typedef struct {
    pb_network_result_type_t type;
    esp_err_t error;
    union {
        pb_app_state_t state;
        pb_network_event_result_t event;
    } data;
} pb_network_result_t;

esp_err_t pb_network_worker_start(const pb_cloud_config_t *config);
esp_err_t pb_network_worker_request_state(void);
esp_err_t pb_network_worker_post_event(const pb_event_t *event);
bool pb_network_worker_receive(pb_network_result_t *result, uint32_t timeout_ms);

#endif
