#include "pb_cloud.h"

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#define PB_CLOUD_RESPONSE_CAPACITY 1536
#define PB_CLOUD_URL_CAPACITY 256
#define PB_CLOUD_AUTH_CAPACITY 192

static const char *TAG = "pb_cloud";

typedef struct {
    char data[PB_CLOUD_RESPONSE_CAPACITY];
    size_t length;
    bool overflowed;
} response_buffer_t;

static response_buffer_t s_response;

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }

    response_buffer_t *response = event->user_data;
    const size_t available = sizeof(response->data) - response->length - 1;
    if ((size_t)event->data_len > available) {
        response->overflowed = true;
        return ESP_ERR_NO_MEM;
    }

    memcpy(response->data + response->length, event->data, event->data_len);
    response->length += (size_t)event->data_len;
    response->data[response->length] = '\0';
    return ESP_OK;
}

static esp_err_t make_url(
    const pb_cloud_t *cloud,
    const char *suffix,
    char *url,
    size_t capacity
)
{
    const size_t base_length = strlen(cloud->config.base_url);
    const char *separator =
        base_length > 0 && cloud->config.base_url[base_length - 1] == '/' ? "" : "/";
    const int written = snprintf(
        url,
        capacity,
        "%s%sapi/pb/v1/devices/%s/%s",
        cloud->config.base_url,
        separator,
        cloud->config.device_serial,
        suffix);
    return written > 0 && (size_t)written < capacity ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t perform_request(
    pb_cloud_t *cloud,
    esp_http_client_method_t method,
    const char *suffix,
    const char *body,
    response_buffer_t *response
)
{
    char url[PB_CLOUD_URL_CAPACITY];
    esp_err_t err = make_url(cloud, suffix, url, sizeof(url));
    if (err != ESP_OK) {
        return err;
    }

    memset(response, 0, sizeof(*response));
    const esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = cloud->config.timeout_ms,
        .event_handler = http_event_handler,
        .user_data = response,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    char authorization[PB_CLOUD_AUTH_CAPACITY];
    const int auth_length = snprintf(
        authorization,
        sizeof(authorization),
        "Bearer %s",
        cloud->config.device_token);
    if (auth_length <= 0 || (size_t)auth_length >= sizeof(authorization)) {
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_http_client_set_method(client, method);
    esp_http_client_set_header(client, "Authorization", authorization);
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "X-PB-Firmware", cloud->config.firmware_version);

    if (body != NULL) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, strlen(body));
    }

    err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_ERR_HTTP_INCOMPLETE_DATA &&
        status >= 200 && status < 300 &&
        response->length > 0 && !response->overflowed) {
        ESP_LOGW(TAG,
                 "%s used a close-delimited response; validating the received JSON body",
                 suffix);
        err = ESP_OK;
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s failed: %s", suffix, esp_err_to_name(err));
        return err;
    }
    if (response->overflowed) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGW(TAG, "%s returned HTTP %d", suffix, status);
        return ESP_ERR_HTTP_BASE + status;
    }

    return ESP_OK;
}

static bool json_boolean(const cJSON *object, const char *name, bool *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsBool(item)) {
        return false;
    }
    *value = cJSON_IsTrue(item);
    return true;
}

static bool json_uint64(const cJSON *item, uint64_t maximum, uint64_t *value)
{
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        item->valuedouble < 0 ||
        item->valuedouble > (double)maximum) {
        return false;
    }
    const uint64_t parsed = (uint64_t)item->valuedouble;
    if ((double)parsed != item->valuedouble) {
        return false;
    }
    *value = parsed;
    return true;
}

static bool json_uint32(const cJSON *item, uint32_t *value)
{
    uint64_t parsed;
    if (!json_uint64(item, UINT32_MAX, &parsed)) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static bool json_int(const cJSON *item, int *value)
{
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        item->valuedouble < INT_MIN ||
        item->valuedouble > INT_MAX) {
        return false;
    }
    const int parsed = (int)item->valuedouble;
    if ((double)parsed != item->valuedouble) {
        return false;
    }
    *value = parsed;
    return true;
}

static bool parse_timer(const cJSON *root, pb_timer_config_t *timer)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "timer");
    if (item == NULL) {
        return true;
    }
    if (!cJSON_IsObject(item) || !json_boolean(item, "enabled", &timer->enabled)) {
        return false;
    }
    if (!timer->enabled) {
        return true;
    }

    const cJSON *default_seconds =
        cJSON_GetObjectItemCaseSensitive(item, "default_seconds");
    if (!json_uint32(default_seconds, &timer->default_seconds) ||
        timer->default_seconds == 0) {
        return false;
    }

    const cJSON *presets = cJSON_GetObjectItemCaseSensitive(item, "presets_seconds");
    if (presets == NULL) {
        return true;
    }
    if (!cJSON_IsArray(presets)) {
        return false;
    }

    const int count = cJSON_GetArraySize(presets);
    if (count < 0 || count > PB_TIMER_MAX_PRESETS) {
        return false;
    }
    timer->preset_count = (uint8_t)count;
    for (int index = 0; index < count; ++index) {
        const cJSON *preset = cJSON_GetArrayItem(presets, index);
        if (!json_uint32(preset, &timer->presets_seconds[index]) ||
            timer->presets_seconds[index] == 0) {
            return false;
        }
    }
    return true;
}

static bool parse_overlay(const cJSON *root, pb_state_overlay_t *overlay)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "overlay");
    if (item == NULL) {
        return true;
    }
    if (!cJSON_IsObject(item)) {
        return false;
    }

    const cJSON *value = cJSON_GetObjectItemCaseSensitive(item, "value");
    const cJSON *duration = cJSON_GetObjectItemCaseSensitive(item, "duration_ms");
    if (!json_int(value, &overlay->value) ||
        !json_uint32(duration, &overlay->duration_ms) ||
        overlay->duration_ms == 0 ||
        !json_boolean(item, "blink", &overlay->blink)) {
        return false;
    }
    overlay->enabled = true;
    return true;
}

static esp_err_t parse_state(
    const response_buffer_t *response,
    pb_app_state_t *state
)
{
    cJSON *root = cJSON_Parse(response->data);
    if (root == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    memset(state, 0, sizeof(*state));
    const cJSON *revision_item = cJSON_GetObjectItemCaseSensitive(root, "revision");
    const cJSON *app_item = cJSON_GetObjectItemCaseSensitive(root, "app");
    const cJSON *view_item = cJSON_GetObjectItemCaseSensitive(root, "view");
    if (!cJSON_IsObject(view_item)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *value_item = cJSON_GetObjectItemCaseSensitive(view_item, "value");
    const cJSON *brightness_item = cJSON_GetObjectItemCaseSensitive(view_item, "brightness");
    const cJSON *leds_item = cJSON_GetObjectItemCaseSensitive(view_item, "leds");

    pb_view_t parsed = {
        .leading_zeroes = true,
        .brightness = 100,
    };
    uint64_t revision;
    uint32_t brightness;
    bool valid = json_uint64(
                     revision_item,
                     PB_PROTOCOL_MAX_REVISION,
                     &revision) &&
                 cJSON_IsString(app_item) && app_item->valuestring != NULL &&
                 app_item->valuestring[0] != '\0' &&
                 strlen(app_item->valuestring) <= PB_APP_ID_MAX_LENGTH &&
                 json_int(value_item, &parsed.value) &&
                 json_uint32(brightness_item, &brightness) &&
                 brightness <= 100 &&
                 cJSON_IsArray(leds_item) &&
                 json_boolean(view_item, "leading_zeroes", &parsed.leading_zeroes) &&
                 json_boolean(view_item, "blink", &parsed.blink) &&
                 parse_timer(root, &state->timer) &&
                 parse_overlay(root, &state->overlay);

    if (valid) {
        parsed.brightness = (uint8_t)brightness;
        const int led_count = cJSON_GetArraySize(leds_item);
        valid = led_count >= 0 && led_count <= PB_VIEW_MAX_LEDS;
        parsed.led_count = valid ? (uint8_t)led_count : 0;

        for (int i = 0; valid && i < led_count; ++i) {
            const cJSON *led = cJSON_GetArrayItem(leds_item, i);
            valid = cJSON_IsBool(led);
            parsed.leds[i] = cJSON_IsTrue(led);
        }
    }

    if (valid) {
        state->revision = revision;
        strlcpy(state->app_id, app_item->valuestring, sizeof(state->app_id));
        state->view = parsed;
    }
    cJSON_Delete(root);
    return valid ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t parse_event_response(
    const response_buffer_t *response,
    uint64_t *revision
)
{
    cJSON *root = cJSON_Parse(response->data);
    if (root == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    const cJSON *revision_item = cJSON_GetObjectItemCaseSensitive(root, "revision");
    const bool valid = cJSON_IsTrue(ok) && json_uint64(
        revision_item,
        PB_PROTOCOL_MAX_REVISION,
        revision);
    cJSON_Delete(root);
    return valid ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t post_event(
    pb_cloud_t *cloud,
    cJSON *body,
    uint64_t *revision
)
{
    char *json = cJSON_PrintUnformatted(body);
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t request_err =
        perform_request(cloud, HTTP_METHOD_POST, "events", json, &s_response);
    cJSON_free(json);
    return request_err == ESP_OK
               ? parse_event_response(&s_response, revision)
               : request_err;
}

esp_err_t pb_cloud_init(pb_cloud_t *cloud, const pb_cloud_config_t *config)
{
    if (cloud == NULL || config == NULL ||
        config->base_url == NULL || config->base_url[0] == '\0' ||
        config->device_serial == NULL || config->device_serial[0] == '\0' ||
        config->device_token == NULL || config->device_token[0] == '\0' ||
        config->firmware_version == NULL || config->timeout_ms <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    cloud->config = *config;
    return ESP_OK;
}

esp_err_t pb_cloud_fetch_state(pb_cloud_t *cloud, pb_app_state_t *state)
{
    if (cloud == NULL || state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = perform_request(cloud, HTTP_METHOD_GET, "state", NULL, &s_response);
    if (err != ESP_OK) {
        return err;
    }

    err = parse_state(&s_response, state);
    if (err == ESP_OK) {
        ESP_LOGI(TAG,
                 "state app=%s revision=%" PRIu64,
                 state->app_id,
                 state->revision);
    }
    return err;
}

esp_err_t pb_cloud_post_event(
    pb_cloud_t *cloud,
    const pb_event_t *event,
    uint64_t *revision
)
{
    const size_t app_id_length = event == NULL
        ? 0
        : strnlen(event->app_id, sizeof(event->app_id));
    if (cloud == NULL || event == NULL || revision == NULL ||
        strnlen(event->event_id, sizeof(event->event_id)) != PB_EVENT_ID_LENGTH ||
        app_id_length > PB_APP_ID_MAX_LENGTH ||
        (app_id_length == 0 && event->state_revision != 0) ||
        event->state_revision > PB_PROTOCOL_MAX_REVISION ||
        (event->type != PB_EVENT_TYPE_ACTION &&
         event->type != PB_EVENT_TYPE_TIMER)) {
        return ESP_ERR_INVALID_ARG;
    }

    static const char *const timer_event_names[] = {
        [PB_TIMER_EVENT_STARTED] = "started",
        [PB_TIMER_EVENT_PAUSED] = "paused",
        [PB_TIMER_EVENT_RESUMED] = "resumed",
        [PB_TIMER_EVENT_FINISHED] = "finished",
    };
    cJSON *root = cJSON_CreateObject();
    if (root == NULL ||
        cJSON_AddStringToObject(root, "event_id", event->event_id) == NULL ||
        (event->occurred_at > 0 &&
         cJSON_AddNumberToObject(root, "occurred_at", (double)event->occurred_at) == NULL) ||
        (event->app_id[0] != '\0' &&
         (cJSON_AddStringToObject(root, "app", event->app_id) == NULL ||
          cJSON_AddNumberToObject(
              root,
              "state_revision",
              (double)event->state_revision) == NULL))) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    bool valid = false;
    if (event->type == PB_EVENT_TYPE_ACTION &&
        (event->action == PB_ACTION_PRIMARY ||
         event->action == PB_ACTION_PRIMARY_LONG)) {
        valid = cJSON_AddStringToObject(root, "type", "action") != NULL &&
                cJSON_AddStringToObject(
                    root,
                    "action",
                    event->action == PB_ACTION_PRIMARY_LONG
                        ? "primary_long"
                        : "primary") != NULL;
    } else if (event->type == PB_EVENT_TYPE_TIMER &&
               event->timer_event >= PB_TIMER_EVENT_STARTED &&
               event->timer_event <= PB_TIMER_EVENT_FINISHED) {
        valid = cJSON_AddStringToObject(root, "type", "timer") != NULL &&
                cJSON_AddStringToObject(
                    root,
                    "event",
                    timer_event_names[event->timer_event]) != NULL &&
                cJSON_AddNumberToObject(
                    root,
                    "duration_seconds",
                    event->duration_seconds) != NULL &&
                cJSON_AddNumberToObject(
                    root,
                    "remaining_seconds",
                    event->remaining_seconds) != NULL;
    }
    if (!valid) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t err = post_event(cloud, root, revision);
    cJSON_Delete(root);
    return err;
}
