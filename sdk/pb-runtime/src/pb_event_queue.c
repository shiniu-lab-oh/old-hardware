#include "pb_event_queue.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "pb_binding.h"
#include "pb_source.h"
#include "pb_view.h"

#define PB_EVENT_QUEUE_SCHEMA_VERSION 3U
#define PB_EVENT_QUEUE_NAMESPACE "pb_events"
#define PB_EVENT_QUEUE_LEGACY_KEY "pending"
#define PB_EVENT_QUEUE_QUARANTINE_KEY "legacy_blocked"
#define PB_EVENT_QUEUE_KEY_CAPACITY 16
#define PB_EVENT_QUEUE_KEY_HASH_MASK UINT64_C(0x00ffffffffffffff)
#define PB_MIN_VALID_UNIX_TIME 1577836800LL

static const char *TAG = "pb_event_queue";

typedef struct {
    uint32_t schema_version;
    uint64_t source_hash;
    pb_event_queue_t queue;
} stored_event_queue_t;

typedef struct {
    uint32_t schema_version;
    pb_event_queue_t queue;
} stored_event_queue_v2_t;

typedef struct {
    char event_id[PB_EVENT_ID_LENGTH + 1];
    int64_t occurred_at;
    pb_event_type_t type;
    pb_action_t action;
    pb_timer_event_t timer_event;
    uint32_t duration_seconds;
    uint32_t remaining_seconds;
} pb_event_v1_t;

typedef struct {
    uint32_t count;
    pb_event_v1_t items[PB_EVENT_QUEUE_CAPACITY];
} pb_event_queue_v1_t;

typedef struct {
    uint32_t schema_version;
    pb_event_queue_v1_t queue;
} stored_event_queue_v1_t;

static stored_event_queue_t s_stored_queue;
static stored_event_queue_v2_t s_stored_queue_v2;
static stored_event_queue_v1_t s_stored_queue_v1;
static pb_event_queue_t s_next_queue;
static char s_queue_key[PB_EVENT_QUEUE_KEY_CAPACITY];
static uint64_t s_source_hash;
static bool s_source_initialized;

static bool event_valid(const pb_event_t *event)
{
    if (event == NULL ||
        strnlen(event->event_id, sizeof(event->event_id)) != PB_EVENT_ID_LENGTH) {
        return false;
    }
    const size_t app_id_length = strnlen(event->app_id, sizeof(event->app_id));
    if (app_id_length > PB_APP_ID_MAX_LENGTH ||
        (app_id_length == 0 && event->state_revision != 0) ||
        event->state_revision > PB_PROTOCOL_MAX_REVISION) {
        return false;
    }
    if (event->type == PB_EVENT_TYPE_ACTION) {
        return event->action == PB_ACTION_PRIMARY ||
               event->action == PB_ACTION_PRIMARY_LONG;
    }
    return event->type == PB_EVENT_TYPE_TIMER &&
           event->timer_event >= PB_TIMER_EVENT_STARTED &&
           event->timer_event <= PB_TIMER_EVENT_FINISHED;
}

static esp_err_t store_queue(const pb_event_queue_t *queue)
{
    if (!s_source_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(PB_EVENT_QUEUE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    s_stored_queue.schema_version = PB_EVENT_QUEUE_SCHEMA_VERSION;
    s_stored_queue.source_hash = s_source_hash;
    s_stored_queue.queue = *queue;
    err = nvs_set_blob(
        handle,
        s_queue_key,
        &s_stored_queue,
        sizeof(s_stored_queue));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static esp_err_t erase_stored_queue(const char *key)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(PB_EVENT_QUEUE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_key(handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static esp_err_t quarantine_legacy_queue(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(PB_EVENT_QUEUE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(handle, PB_EVENT_QUEUE_QUARANTINE_KEY, 1U);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static esp_err_t make_queue_key(uint64_t source_hash)
{
    const int written = snprintf(
        s_queue_key,
        sizeof(s_queue_key),
        "q%014" PRIx64,
        source_hash & PB_EVENT_QUEUE_KEY_HASH_MASK);
    return written == (PB_EVENT_QUEUE_KEY_CAPACITY - 1)
               ? ESP_OK
               : ESP_ERR_INVALID_SIZE;
}

static bool queue_valid(const pb_event_queue_t *queue)
{
    if (queue == NULL || queue->count > PB_EVENT_QUEUE_CAPACITY) {
        return false;
    }
    for (uint32_t index = 0; index < queue->count; ++index) {
        if (!event_valid(&queue->items[index])) {
            return false;
        }
    }
    return true;
}

static bool legacy_source_matches(
    const char *cloud_base_url,
    const char *device_serial
)
{
    pb_binding_t binding;
    if (pb_binding_load(cloud_base_url, device_serial, &binding) == ESP_OK) {
        return true;
    }

    pb_view_t view;
    uint64_t revision;
    return pb_view_load_last(
               cloud_base_url,
               device_serial,
               &view,
               &revision) == ESP_OK;
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

esp_err_t pb_event_queue_init(
    pb_event_queue_t *queue,
    const char *cloud_base_url,
    const char *device_serial
)
{
    if (queue == NULL || cloud_base_url == NULL || cloud_base_url[0] == '\0' ||
        device_serial == NULL || device_serial[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    memset(queue, 0, sizeof(*queue));
    s_source_initialized = false;
    s_source_hash = pb_source_hash(cloud_base_url, device_serial);
    esp_err_t err = make_queue_key(s_source_hash);
    if (err != ESP_OK) {
        return err;
    }
    s_source_initialized = true;

    nvs_handle_t handle;
    err = nvs_open(PB_EVENT_QUEUE_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    size_t size = 0;
    err = nvs_get_blob(handle, s_queue_key, NULL, &size);
    if (err == ESP_OK) {
        if (size != sizeof(s_stored_queue)) {
            nvs_close(handle);
            ESP_LOGW(TAG, "Discarding an invalid source event queue");
            return erase_stored_queue(s_queue_key);
        }

        err = nvs_get_blob(
            handle,
            s_queue_key,
            &s_stored_queue,
            &size);
        nvs_close(handle);
        if (err != ESP_OK) {
            return err;
        }
        if (s_stored_queue.source_hash != s_source_hash) {
            ESP_LOGE(TAG, "Event queue source key collision detected");
            s_source_initialized = false;
            return ESP_ERR_INVALID_STATE;
        }
        if (s_stored_queue.schema_version != PB_EVENT_QUEUE_SCHEMA_VERSION ||
            !queue_valid(&s_stored_queue.queue)) {
            ESP_LOGW(TAG, "Discarding an invalid source event queue");
            return erase_stored_queue(s_queue_key);
        }

        *queue = s_stored_queue.queue;
        ESP_LOGI(TAG,
                 "Loaded %" PRIu32 " pending event(s) for this device source",
                 queue->count);
        return ESP_OK;
    }
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }

    size = 0;
    err = nvs_get_blob(handle, PB_EVENT_QUEUE_LEGACY_KEY, NULL, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    uint8_t legacy_quarantined = 0;
    err = nvs_get_u8(
        handle,
        PB_EVENT_QUEUE_QUARANTINE_KEY,
        &legacy_quarantined);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }
    if (legacy_quarantined != 0) {
        nvs_close(handle);
        ESP_LOGW(TAG, "Legacy pending events remain quarantined");
        return ESP_OK;
    }

    bool valid = false;
    bool missing_app_context = false;
    if (size == sizeof(s_stored_queue_v2)) {
        err = nvs_get_blob(
            handle,
            PB_EVENT_QUEUE_LEGACY_KEY,
            &s_stored_queue_v2,
            &size);
        valid = err == ESP_OK &&
                s_stored_queue_v2.schema_version == 2U &&
                queue_valid(&s_stored_queue_v2.queue);
        if (valid) {
            *queue = s_stored_queue_v2.queue;
        }
    } else if (size == sizeof(s_stored_queue_v1)) {
        err = nvs_get_blob(
            handle,
            PB_EVENT_QUEUE_LEGACY_KEY,
            &s_stored_queue_v1,
            &size);
        valid = err == ESP_OK && s_stored_queue_v1.schema_version == 1U &&
                s_stored_queue_v1.queue.count <= PB_EVENT_QUEUE_CAPACITY;
        if (valid) {
            queue->count = s_stored_queue_v1.queue.count;
        }
        for (uint32_t index = 0; valid && index < queue->count; ++index) {
            const pb_event_v1_t *source = &s_stored_queue_v1.queue.items[index];
            pb_event_t *target = &queue->items[index];
            if (strnlen(source->event_id, sizeof(source->event_id)) !=
                PB_EVENT_ID_LENGTH) {
                valid = false;
                break;
            }
            memcpy(target->event_id, source->event_id, sizeof(target->event_id));
            target->occurred_at = source->occurred_at;
            target->type = source->type;
            target->action = source->action;
            target->timer_event = source->timer_event;
            target->duration_seconds = source->duration_seconds;
            target->remaining_seconds = source->remaining_seconds;
            valid = event_valid(target);
        }
        missing_app_context = valid;
    }
    nvs_close(handle);

    if (!valid) {
        ESP_LOGW(TAG, "Discarding an invalid persisted event queue");
        memset(queue, 0, sizeof(*queue));
        return erase_stored_queue(PB_EVENT_QUEUE_LEGACY_KEY);
    }

    if (queue->count == 0) {
        return erase_stored_queue(PB_EVENT_QUEUE_LEGACY_KEY);
    }

    if (!legacy_source_matches(cloud_base_url, device_serial)) {
        ESP_LOGW(TAG,
                 "Legacy pending events remain quarantined because their source "
                 "cannot be verified");
        memset(queue, 0, sizeof(*queue));
        return quarantine_legacy_queue();
    }

    if (missing_app_context) {
        ESP_LOGW(TAG,
                 "Migrated pending events without App context; compatibility routing applies");
    }
    err = store_queue(queue);
    if (err != ESP_OK) {
        return err;
    }
    err = erase_stored_queue(PB_EVENT_QUEUE_LEGACY_KEY);
    if (err != ESP_OK) {
        const esp_err_t quarantine_err = quarantine_legacy_queue();
        if (quarantine_err != ESP_OK) {
            return err;
        }
        ESP_LOGW(TAG,
                 "Migrated events, but the legacy queue could only be quarantined");
    }
    ESP_LOGI(TAG,
             "Migrated %" PRIu32 " pending event(s) to this device source",
             queue->count);
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
    if (timer_event < PB_TIMER_EVENT_STARTED ||
        timer_event > PB_TIMER_EVENT_FINISHED || duration_seconds == 0 ||
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

esp_err_t pb_event_set_context(
    pb_event_t *event,
    const char *app_id,
    uint64_t state_revision
)
{
    if (event == NULL || app_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t app_id_length = strnlen(app_id, PB_APP_ID_MAX_LENGTH + 1);
    if (app_id_length == 0 || app_id_length > PB_APP_ID_MAX_LENGTH) {
        return ESP_ERR_INVALID_ARG;
    }
    if (state_revision > PB_PROTOCOL_MAX_REVISION) {
        return ESP_ERR_INVALID_ARG;
    }

    strlcpy(event->app_id, app_id, sizeof(event->app_id));
    event->state_revision = state_revision;
    return ESP_OK;
}
