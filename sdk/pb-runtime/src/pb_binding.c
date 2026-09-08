#include "pb_binding.h"

#include <string.h>

#include "nvs.h"
#include "pb_source.h"
#include "pb_view.h"

#define PB_BINDING_STORE_MAGIC 0x50424231U
#define PB_BINDING_STORE_VERSION 1U

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t source_hash;
    pb_binding_t binding;
} pb_binding_store_t;

static bool view_valid(const pb_view_t *view)
{
    return view != NULL && view->brightness <= 100 &&
           view->led_count <= PB_VIEW_MAX_LEDS;
}

static bool timer_valid(const pb_timer_config_t *timer)
{
    if (timer == NULL || timer->preset_count > PB_TIMER_MAX_PRESETS) {
        return false;
    }
    if (!timer->enabled) {
        return true;
    }
    if (timer->default_seconds == 0) {
        return false;
    }
    for (uint8_t index = 0; index < timer->preset_count; ++index) {
        if (timer->presets_seconds[index] == 0) {
            return false;
        }
    }
    return true;
}

static bool binding_valid(const pb_binding_t *binding)
{
    if (binding == NULL) {
        return false;
    }
    const size_t app_id_length = strnlen(
        binding->app_id,
        sizeof(binding->app_id));
    return app_id_length > 0 && app_id_length <= PB_APP_ID_MAX_LENGTH &&
           binding->revision <= PB_PROTOCOL_MAX_REVISION &&
           view_valid(&binding->view) && timer_valid(&binding->timer);
}

static bool view_equal(const pb_view_t *left, const pb_view_t *right)
{
    if (left->value != right->value ||
        left->leading_zeroes != right->leading_zeroes ||
        left->brightness != right->brightness ||
        left->blink != right->blink ||
        left->led_count != right->led_count) {
        return false;
    }
    for (uint8_t index = 0; index < left->led_count; ++index) {
        if (left->leds[index] != right->leds[index]) {
            return false;
        }
    }
    return true;
}

static bool timer_equal(
    const pb_timer_config_t *left,
    const pb_timer_config_t *right
)
{
    if (left->enabled != right->enabled) {
        return false;
    }
    if (!left->enabled) {
        return true;
    }
    if (left->default_seconds != right->default_seconds ||
        left->preset_count != right->preset_count) {
        return false;
    }
    for (uint8_t index = 0; index < left->preset_count; ++index) {
        if (left->presets_seconds[index] != right->presets_seconds[index]) {
            return false;
        }
    }
    return true;
}

void pb_binding_init(pb_binding_t *binding)
{
    if (binding == NULL) {
        return;
    }
    memset(binding, 0, sizeof(*binding));
    pb_view_default(&binding->view);
}

esp_err_t pb_binding_from_state(
    pb_binding_t *binding,
    const pb_app_state_t *state
)
{
    if (binding == NULL || state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t app_id_length = strnlen(
        state->app_id,
        sizeof(state->app_id));
    if (app_id_length == 0 || app_id_length > PB_APP_ID_MAX_LENGTH) {
        return ESP_ERR_INVALID_ARG;
    }

    pb_binding_t next = {
        .revision = state->revision,
        .view = state->view,
        .timer = state->timer,
    };
    memcpy(next.app_id, state->app_id, app_id_length + 1);
    if (!binding_valid(&next)) {
        return ESP_ERR_INVALID_ARG;
    }

    *binding = next;
    return ESP_OK;
}

bool pb_binding_matches_state(
    const pb_binding_t *binding,
    const pb_app_state_t *state
)
{
    pb_binding_t candidate;
    return binding_valid(binding) &&
           pb_binding_from_state(&candidate, state) == ESP_OK &&
           binding->revision == candidate.revision &&
           strcmp(binding->app_id, candidate.app_id) == 0 &&
           view_equal(&binding->view, &candidate.view) &&
           timer_equal(&binding->timer, &candidate.timer);
}

esp_err_t pb_binding_load(
    const char *cloud_base_url,
    const char *device_serial,
    pb_binding_t *binding
)
{
    if (cloud_base_url == NULL || cloud_base_url[0] == '\0' ||
        device_serial == NULL || device_serial[0] == '\0' ||
        binding == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open("pb_runtime", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    pb_binding_store_t stored;
    size_t size = sizeof(stored);
    err = nvs_get_blob(handle, "binding", &stored, &size);
    nvs_close(handle);
    if (err != ESP_OK) {
        return err;
    }
    if (size != sizeof(stored) ||
        stored.magic != PB_BINDING_STORE_MAGIC ||
        stored.version != PB_BINDING_STORE_VERSION ||
        stored.source_hash != pb_source_hash(cloud_base_url, device_serial) ||
        !binding_valid(&stored.binding)) {
        return ESP_ERR_INVALID_VERSION;
    }

    *binding = stored.binding;
    return ESP_OK;
}

esp_err_t pb_binding_store(
    const char *cloud_base_url,
    const char *device_serial,
    const pb_binding_t *binding
)
{
    if (cloud_base_url == NULL || cloud_base_url[0] == '\0' ||
        device_serial == NULL || device_serial[0] == '\0' ||
        !binding_valid(binding)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open("pb_runtime", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    const pb_binding_store_t stored = {
        .magic = PB_BINDING_STORE_MAGIC,
        .version = PB_BINDING_STORE_VERSION,
        .source_hash = pb_source_hash(cloud_base_url, device_serial),
        .binding = *binding,
    };
    err = nvs_set_blob(handle, "binding", &stored, sizeof(stored));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
