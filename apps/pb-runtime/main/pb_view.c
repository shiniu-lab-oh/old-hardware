#include "pb_view.h"

#include <string.h>

#include "nvs.h"

#define PB_VIEW_STORE_MAGIC 0x50425631U
#define PB_VIEW_STORE_VERSION 2U
#define PB_VIEW_SOURCE_HASH_OFFSET UINT64_C(14695981039346656037)
#define PB_VIEW_SOURCE_HASH_PRIME UINT64_C(1099511628211)

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

esp_err_t pb_view_render(const pb_view_t *view, const old_panel_caps_t *caps)
{
    if (view == NULL || caps == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = old_panel_display_value(view->value, view->leading_zeroes, 0);
    if (err != ESP_OK) {
        return err;
    }

    if (caps->supports_brightness) {
        err = old_panel_set_brightness(view->brightness);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (caps->supports_blink) {
        err = old_panel_set_blink(view->blink, view->blink ? 500 : 0);
        if (err != ESP_OK) {
            return err;
        }
    }

    for (uint8_t i = 0; i < caps->leds; ++i) {
        const bool on = i < view->led_count ? view->leds[i] : false;
        err = old_panel_set_led(i, on);
        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_OK;
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
