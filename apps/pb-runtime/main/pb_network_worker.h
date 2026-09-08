#ifndef PB_NETWORK_WORKER_H
#define PB_NETWORK_WORKER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "pb_cloud.h"
#include "pb_runtime.h"

esp_err_t pb_network_worker_start(const pb_cloud_config_t *config);
esp_err_t pb_network_worker_request_state(void);
esp_err_t pb_network_worker_post_event(const pb_event_t *event);
bool pb_network_worker_receive(
    pb_runtime_transport_result_t *result,
    uint32_t timeout_ms);

#endif
