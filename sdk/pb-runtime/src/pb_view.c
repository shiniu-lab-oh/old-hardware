#include "pb_view.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "pb_hal.h"

#define PB_VIEW_STORE_MAGIC 0x50425631U
#define PB_VIEW_STORE_VERSION 2U
#define PB_VIEW_SOURCE_HASH_OFFSET UINT64_C(14695981039346656037)
#define PB_VIEW_SOURCE_HASH_PRIME UINT64_C(1099511628211)

static const char *TAG = "pb_view";

_Static_assert(PB_VIEW_MAX_LEDS <= PB_HAL_MAX_LEDS,
               "PB View LED capacity exceeds PB HAL capacity");

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t source_hash;
    uint64_t revision;
    pb_view_t view;
} pb_view_store_t;

static uint64_t source_hash(const char *cloud_base_url, const char *device_serial)
{
    uint64_t hash = PB_VIEW_SOURCE_HASH_OFFSET;
    const char *parts[] = {cloud_base_url, device_serial};

    for (size_t part = 0; part < sizeof(parts) / sizeof(parts[0]); ++part) {
        for (const unsigned char *cursor = (const unsigned char *)parts[part];
             *cursor != '\0';
             ++cursor) {
            hash ^= *cursor;
            hash *= PB_VIEW_SOURCE_HASH_PRIME;
        }
        hash ^= 0xffU;
        hash *= PB_VIEW_SOURCE_HASH_PRIME;
    }

    return hash;
}

void pb_view_default(pb_view_t *view)
{
    if (view == NULL) {
        return;
    }

    memset(view, 0, sizeof(*view));
    view->leading_zeroes = true;
    view->brightness = 100;
}

esp_err_t pb_view_render(const pb_view_t *view)
{
    if (view == NULL || view->led_count > PB_VIEW_MAX_LEDS) {
        return ESP_ERR_INVALID_ARG;
    }

    pb_presentation_t presentation = {
        .type = PB_PRESENTATION_NUMBER,
        .number = view->value,
        .leading_zeroes = view->leading_zeroes,
        .brightness = view->brightness,
        .blink = view->blink,
        .blink_interval_ms = 500,
        .led_count = view->led_count,
    };
    memcpy(presentation.leds, view->leds, sizeof(view->leds));

    const pb_render_result_t result = pb_hal_render(&presentation);
    if (result.status == PB_RENDER_APPLIED) {
        return ESP_OK;
    }
    if (result.status == PB_RENDER_DEGRADED) {
        ESP_LOGW(TAG, "Presentation applied with hardware degradation");
        return ESP_OK;
    }
    return result.error != ESP_OK ? result.error : ESP_FAIL;
}

esp_err_t pb_view_load_last(
    const char *cloud_base_url,
    const char *device_serial,
    pb_view_t *view,
    uint64_t *revision
)
{
    if (cloud_base_url == NULL || cloud_base_url[0] == '\0' ||
        device_serial == NULL || device_serial[0] == '\0' ||
        view == NULL || revision == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open("pb_runtime", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    pb_view_store_t stored;
    size_t size = sizeof(stored);
    err = nvs_get_blob(handle, "last_view", &stored, &size);
    nvs_close(handle);

    if (err != ESP_OK) {
        return err;
    }
    if (size != sizeof(stored) ||
        stored.magic != PB_VIEW_STORE_MAGIC ||
        stored.version != PB_VIEW_STORE_VERSION ||
        stored.source_hash != source_hash(cloud_base_url, device_serial)) {
        return ESP_ERR_INVALID_VERSION;
    }

    *view = stored.view;
    *revision = stored.revision;
    return ESP_OK;
}

esp_err_t pb_view_store_last(
    const char *cloud_base_url,
    const char *device_serial,
    const pb_view_t *view,
    uint64_t revision
)
{
    if (cloud_base_url == NULL || cloud_base_url[0] == '\0' ||
        device_serial == NULL || device_serial[0] == '\0' || view == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open("pb_runtime", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    const pb_view_store_t stored = {
        .magic = PB_VIEW_STORE_MAGIC,
        .version = PB_VIEW_STORE_VERSION,
        .source_hash = source_hash(cloud_base_url, device_serial),
        .revision = revision,
        .view = *view,
    };

    err = nvs_set_blob(handle, "last_view", &stored, sizeof(stored));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
