#include "pb_cloud.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "pb_view.h"

#define PB_CLOUD_RESPONSE_CAPACITY 1536
#define PB_CLOUD_URL_CAPACITY 256
#define PB_CLOUD_AUTH_CAPACITY 192

static const char *TAG = "pb_cloud";

typedef struct {
    char data[PB_CLOUD_RESPONSE_CAPACITY];
    size_t length;
    bool overflowed;
} response_buffer_t;

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

static esp_err_t parse_state(
    const response_buffer_t *response,
    pb_app_state_t *state
)
{
    cJSON *root = cJSON_Parse(response->data);
    if (root == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }

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

    pb_view_t parsed;
    pb_view_default(&parsed);
    bool valid = cJSON_IsNumber(revision_item) && revision_item->valuedouble >= 0 &&
                 cJSON_IsString(app_item) && app_item->valuestring != NULL &&
                 app_item->valuestring[0] != '\0' &&
                 strlen(app_item->valuestring) <= PB_APP_ID_MAX_LENGTH &&
                 cJSON_IsNumber(value_item) &&
                 cJSON_IsNumber(brightness_item) &&
                 brightness_item->valueint >= 0 && brightness_item->valueint <= 100 &&
                 cJSON_IsArray(leds_item) &&
                 json_boolean(view_item, "leading_zeroes", &parsed.leading_zeroes) &&
                 json_boolean(view_item, "blink", &parsed.blink);

    if (valid) {
        parsed.value = value_item->valueint;
        parsed.brightness = (uint8_t)brightness_item->valueint;
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
        memset(state, 0, sizeof(*state));
        state->revision = (uint64_t)revision_item->valuedouble;
        strlcpy(state->app_id, app_item->valuestring, sizeof(state->app_id));
        state->view = parsed;
    }
    cJSON_Delete(root);
    return valid ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
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

    response_buffer_t response;
    esp_err_t err = perform_request(cloud, HTTP_METHOD_GET, "state", NULL, &response);
    if (err != ESP_OK) {
        return err;
    }

    err = parse_state(&response, state);
    if (err == ESP_OK) {
        ESP_LOGI(TAG,
                 "state app=%s revision=%" PRIu64,
                 state->app_id,
                 state->revision);
    }
    return err;
}

esp_err_t pb_cloud_post_action(
    pb_cloud_t *cloud,
    pb_action_t action,
    uint64_t *revision
)
{
    if (cloud == NULL || revision == NULL || action != PB_ACTION_PRIMARY) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *body = "{\"type\":\"action\",\"action\":\"primary\"}";
    response_buffer_t response;
    esp_err_t err = perform_request(cloud, HTTP_METHOD_POST, "events", body, &response);
    if (err != ESP_OK) {
        return err;
    }

    cJSON *root = cJSON_Parse(response.data);
    if (root == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    const cJSON *revision_item = cJSON_GetObjectItemCaseSensitive(root, "revision");
    const bool valid = cJSON_IsTrue(ok) && cJSON_IsNumber(revision_item) &&
                       revision_item->valuedouble >= 0;
    if (valid) {
        *revision = (uint64_t)revision_item->valuedouble;
    }
    cJSON_Delete(root);
    return valid ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}
