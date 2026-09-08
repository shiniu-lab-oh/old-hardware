#include "pb_network_worker.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define PB_NETWORK_QUEUE_LENGTH 4U
#define PB_NETWORK_TASK_STACK_SIZE 6144U
#define PB_NETWORK_TASK_PRIORITY 5U

typedef enum {
    PB_NETWORK_REQUEST_STATE = 0,
    PB_NETWORK_REQUEST_EVENT,
} pb_network_request_type_t;

typedef struct {
    pb_network_request_type_t type;
    pb_event_t event;
} pb_network_request_t;

static pb_cloud_t s_cloud;
static QueueHandle_t s_request_queue;
static QueueHandle_t s_result_queue;
static bool s_started;

static TickType_t timeout_ticks(uint32_t timeout_ms)
{
    return timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
}

static void network_task(void *argument)
{
    (void)argument;

    pb_network_request_t request;
    while (true) {
        if (xQueueReceive(s_request_queue, &request, portMAX_DELAY) != pdPASS) {
            continue;
        }

        pb_network_result_t result = {0};
        if (request.type == PB_NETWORK_REQUEST_STATE) {
            result.type = PB_NETWORK_RESULT_STATE;
            result.error = pb_cloud_fetch_state(&s_cloud, &result.data.state);
        } else if (request.type == PB_NETWORK_REQUEST_EVENT) {
            result.type = PB_NETWORK_RESULT_EVENT;
            strlcpy(
                result.data.event.event_id,
                request.event.event_id,
                sizeof(result.data.event.event_id));
            result.error = pb_cloud_post_event(
                &s_cloud,
                &request.event,
                &result.data.event.revision);
        } else {
            continue;
        }

        xQueueSend(s_result_queue, &result, portMAX_DELAY);
    }
}

esp_err_t pb_network_worker_start(const pb_cloud_config_t *config)
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = pb_cloud_init(&s_cloud, config);
    if (err != ESP_OK) {
        return err;
    }

    s_request_queue = xQueueCreate(
        PB_NETWORK_QUEUE_LENGTH,
        sizeof(pb_network_request_t));
    s_result_queue = xQueueCreate(
        PB_NETWORK_QUEUE_LENGTH,
        sizeof(pb_network_result_t));
    if (s_request_queue == NULL || s_result_queue == NULL) {
        if (s_request_queue != NULL) {
            vQueueDelete(s_request_queue);
            s_request_queue = NULL;
        }
        if (s_result_queue != NULL) {
            vQueueDelete(s_result_queue);
            s_result_queue = NULL;
        }
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(
            network_task,
            "pb_network",
            PB_NETWORK_TASK_STACK_SIZE,
            NULL,
            PB_NETWORK_TASK_PRIORITY,
            NULL) != pdPASS) {
        vQueueDelete(s_request_queue);
        vQueueDelete(s_result_queue);
        s_request_queue = NULL;
        s_result_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    return ESP_OK;
}

esp_err_t pb_network_worker_request_state(void)
{
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    const pb_network_request_t request = {
        .type = PB_NETWORK_REQUEST_STATE,
    };
    return xQueueSend(s_request_queue, &request, 0) == pdPASS
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

esp_err_t pb_network_worker_post_event(const pb_event_t *event)
{
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const pb_network_request_t request = {
        .type = PB_NETWORK_REQUEST_EVENT,
        .event = *event,
    };
    return xQueueSend(s_request_queue, &request, 0) == pdPASS
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

bool pb_network_worker_receive(pb_network_result_t *result, uint32_t timeout_ms)
{
    return s_started && result != NULL &&
           xQueueReceive(s_result_queue, result, timeout_ticks(timeout_ms)) == pdPASS;
}
