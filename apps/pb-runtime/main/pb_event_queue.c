#include "pb_event_queue.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"

#define PB_EVENT_QUEUE_SCHEMA_VERSION 1U
#define PB_EVENT_QUEUE_NAMESPACE "pb_events"
#define PB_EVENT_QUEUE_KEY "pending"
#define PB_MIN_VALID_UNIX_TIME 1577836800LL

static const char *TAG = "pb_event_queue";

typedef struct {
    uint32_t schema_version;
    pb_event_queue_t queue;
} stored_event_queue_t;

static stored_event_queue_t s_stored_queue;
static pb_event_queue_t s_next_queue;

static bool event_valid(const pb_event_t *event)
{
    if (event == NULL ||
        strnlen(event->event_id, sizeof(event->event_id)) != PB_EVENT_ID_LENGTH) {
        return false;
    }
    if (event->type == PB_EVENT_TYPE_ACTION) {
        return event->action == PB_ACTION_PRIMARY ||
               event->action == PB_ACTION_PRIMARY_LONG;
    }
    return event->type == PB_EVENT_TYPE_TIMER &&
           event->timer_event <= PB_TIMER_EVENT_FINISHED;
}

static esp_err_t store_queue(const pb_event_queue_t *queue)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(PB_EVENT_QUEUE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    s_stored_queue.schema_version = PB_EVENT_QUEUE_SCHEMA_VERSION;
    s_stored_queue.queue = *queue;
    err = nvs_set_blob(
        handle,
        PB_EVENT_QUEUE_KEY,
        &s_stored_queue,
        sizeof(s_stored_queue));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static esp_err_t clear_stored_queue(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(PB_EVENT_QUEUE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_key(handle, PB_EVENT_QUEUE_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static int64_t current_unix_time(void)
{
    const time_t now = time(NULL);
    return (int64_t)now >= PB_MIN_VALID_UNIX_TIME ? (int64_t)now : 0;
}

static esp_err_t make_event_id(char event_id[PB_EVENT_ID_LENGTH + 1])
{
    uint8_t bytes[16];
    esp_fill_random(bytes, sizeof(bytes));
    bytes[6] = (uint8_t)((bytes[6] & 0x0fU) | 0x40U);
    bytes[8] = (uint8_t)((bytes[8] & 0x3fU) | 0x80U);

    const int written = snprintf(
        event_id,
        PB_EVENT_ID_LENGTH + 1,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3],
        bytes[4], bytes[5], bytes[6], bytes[7],
        bytes[8], bytes[9], bytes[10], bytes[11],
        bytes[12], bytes[13], bytes[14], bytes[15]);
    return written == PB_EVENT_ID_LENGTH ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t initialize_event(pb_event_t *event, pb_event_type_t type)
{
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(event, 0, sizeof(*event));
    event->type = type;
    event->occurred_at = current_unix_time();
    return make_event_id(event->event_id);
}

esp_err_t pb_event_queue_init(pb_event_queue_t *queue)
{
    if (queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(queue, 0, sizeof(*queue));

    nvs_handle_t handle;
    esp_err_t err = nvs_open(PB_EVENT_QUEUE_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    size_t size = sizeof(s_stored_queue);
    err = nvs_get_blob(
        handle,
        PB_EVENT_QUEUE_KEY,
        &s_stored_queue,
        &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }

    bool valid = err == ESP_OK && size == sizeof(s_stored_queue) &&
                 s_stored_queue.schema_version == PB_EVENT_QUEUE_SCHEMA_VERSION &&
                 s_stored_queue.queue.count <= PB_EVENT_QUEUE_CAPACITY;
    for (uint32_t index = 0; valid && index < s_stored_queue.queue.count; ++index) {
        valid = event_valid(&s_stored_queue.queue.items[index]);
    }
    if (!valid) {
        ESP_LOGW(TAG, "Discarding an invalid persisted event queue");
        return clear_stored_queue();
    }

    *queue = s_stored_queue.queue;
    ESP_LOGI(TAG, "Loaded %" PRIu32 " pending event(s)", queue->count);
    return ESP_OK;
}

size_t pb_event_queue_count(const pb_event_queue_t *queue)
{
    return queue == NULL ? 0 : queue->count;
}

const pb_event_t *pb_event_queue_peek(const pb_event_queue_t *queue)
{
    return queue == NULL || queue->count == 0 ? NULL : &queue->items[0];
}

esp_err_t pb_event_queue_enqueue(pb_event_queue_t *queue, const pb_event_t *event)
{
    if (queue == NULL || !event_valid(event)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (queue->count >= PB_EVENT_QUEUE_CAPACITY) {
        return ESP_ERR_NO_MEM;
    }

    s_next_queue = *queue;
    s_next_queue.items[s_next_queue.count++] = *event;
    const esp_err_t err = store_queue(&s_next_queue);
    if (err == ESP_OK) {
        *queue = s_next_queue;
    }
    return err;
}

esp_err_t pb_event_queue_pop(pb_event_queue_t *queue)
{
    if (queue == NULL || queue->count == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    s_next_queue = *queue;
    --s_next_queue.count;
    if (s_next_queue.count > 0) {
        memmove(
            s_next_queue.items,
            s_next_queue.items + 1,
            s_next_queue.count * sizeof(s_next_queue.items[0]));
    }
    memset(
        &s_next_queue.items[s_next_queue.count],
        0,
        sizeof(s_next_queue.items[s_next_queue.count]));
    const esp_err_t err = store_queue(&s_next_queue);
    if (err == ESP_OK) {
        *queue = s_next_queue;
    }
    return err;
}

esp_err_t pb_event_make_action(pb_event_t *event, pb_action_t action)
{
    if (action != PB_ACTION_PRIMARY && action != PB_ACTION_PRIMARY_LONG) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t err = initialize_event(event, PB_EVENT_TYPE_ACTION);
    if (err == ESP_OK) {
        event->action = action;
    }
    return err;
}

esp_err_t pb_event_make_timer(
    pb_event_t *event,
    pb_timer_event_t timer_event,
    uint32_t duration_seconds,
    uint32_t remaining_seconds
)
{
    if (timer_event > PB_TIMER_EVENT_FINISHED || duration_seconds == 0 ||
        remaining_seconds > duration_seconds) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t err = initialize_event(event, PB_EVENT_TYPE_TIMER);
    if (err == ESP_OK) {
        event->timer_event = timer_event;
        event->duration_seconds = duration_seconds;
        event->remaining_seconds = remaining_seconds;
    }
    return err;
}
