#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "old_panel.h"
#include "pb_actions.h"
#include "pb_cloud.h"
#include "pb_view.h"
#include "sdkconfig.h"

#define PB_WIFI_CONNECTED_BIT BIT0
#define PB_RUNTIME_VERSION "pb-runtime/0.1.0"

static const char *TAG = "pb_runtime";
static EventGroupHandle_t s_wifi_events;

static void wifi_event_handler(
    void *argument,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
)
{
    (void)argument;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, PB_WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "Wi-Fi disconnected; keeping last known view");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;
        ESP_LOGI(TAG, "Wi-Fi connected: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_events, PB_WIFI_CONNECTED_BIT);
    }
}

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static esp_err_t start_wifi(void)
{
    if (CONFIG_PB_WIFI_SSID[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    s_wifi_events = xEventGroupCreate();
    if (s_wifi_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    ESP_RETURN_ON_ERROR(
        esp_event_loop_create_default(), TAG, "event loop creation failed");
    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "esp_wifi_init failed");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL),
        TAG,
        "Wi-Fi event registration failed");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL),
        TAG,
        "IP event registration failed");

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, CONFIG_PB_WIFI_SSID,
            sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, CONFIG_PB_WIFI_PASSWORD,
            sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Wi-Fi mode failed");
    ESP_RETURN_ON_ERROR(
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "Wi-Fi config failed");
    return esp_wifi_start();
}

static bool wifi_connected(void)
{
    return s_wifi_events != NULL &&
           (xEventGroupGetBits(s_wifi_events) & PB_WIFI_CONNECTED_BIT) != 0;
}

static esp_err_t sync_view(
    pb_cloud_t *cloud,
    const old_panel_caps_t *caps,
    pb_view_t *current_view,
    uint64_t *current_revision,
    bool *has_current_revision
)
{
    pb_app_state_t next_state;
    esp_err_t err = pb_cloud_fetch_state(cloud, &next_state);
    if (err != ESP_OK) {
        return err;
    }

    if (*has_current_revision && next_state.revision < *current_revision) {
        ESP_LOGW(TAG,
                 "Ignoring stale state revision=%" PRIu64 " current=%" PRIu64,
                 next_state.revision,
                 *current_revision);
        return ESP_OK;
    }
    if (*has_current_revision && next_state.revision == *current_revision) {
        ESP_LOGD(TAG, "State revision=%" PRIu64 " unchanged", *current_revision);
        return ESP_OK;
    }

    err = pb_view_render(&next_state.view, caps);
    if (err != ESP_OK) {
        return err;
    }

    *current_view = next_state.view;
    *current_revision = next_state.revision;
    *has_current_revision = true;
    err = pb_view_store_last(current_view, *current_revision);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not store last view: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}

void app_main(void)
{
    ESP_ERROR_CHECK(init_nvs());

    const old_panel_config_t panel_config = {
        .profile_id = CONFIG_PB_PANEL_PROFILE,
    };
    ESP_ERROR_CHECK(old_panel_init(&panel_config));

    old_panel_caps_t caps;
    ESP_ERROR_CHECK(old_panel_get_capabilities(&caps));

    pb_view_t current_view;
    uint64_t current_revision = 0;
    bool has_current_revision =
        pb_view_load_last(&current_view, &current_revision) == ESP_OK;
    if (!has_current_revision) {
        pb_view_default(&current_view);
    }
    ESP_ERROR_CHECK(pb_view_render(&current_view, &caps));

    const pb_cloud_config_t cloud_config = {
        .base_url = CONFIG_PB_CLOUD_BASE_URL,
        .device_serial = CONFIG_PB_DEVICE_SERIAL,
        .device_token = CONFIG_PB_DEVICE_TOKEN,
        .firmware_version = PB_RUNTIME_VERSION,
        .timeout_ms = CONFIG_PB_HTTP_TIMEOUT_MS,
    };
    pb_cloud_t cloud;
    const esp_err_t cloud_err = pb_cloud_init(&cloud, &cloud_config);
    const esp_err_t wifi_err = start_wifi();
    if (cloud_err != ESP_OK || wifi_err != ESP_OK) {
        ESP_LOGE(TAG,
                 "Runtime configuration incomplete; set Wi-Fi, Cloud URL and token via menuconfig or sdkconfig.secrets");
    }

    ESP_LOGI(TAG,
             "PB Runtime ready: serial=%s profile=%s cached_revision=%llu",
             CONFIG_PB_DEVICE_SERIAL,
             CONFIG_PB_PANEL_PROFILE,
             (unsigned long long)current_revision);

    TickType_t next_poll = 0;
    while (true) {
        const TickType_t now = xTaskGetTickCount();
        if (cloud_err == ESP_OK && wifi_connected() &&
            (next_poll == 0 || (int32_t)(now - next_poll) >= 0)) {
            const esp_err_t err = sync_view(
                &cloud,
                &caps,
                &current_view,
                &current_revision,
                &has_current_revision);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "State sync failed; keeping last known view");
            }
            next_poll = now + pdMS_TO_TICKS(CONFIG_PB_POLL_INTERVAL_SECONDS * 1000);
        }

        old_panel_key_event_t event;
        if (!old_panel_wait_key_event(&event, pdMS_TO_TICKS(200)) || !event.pressed) {
            continue;
        }

        const pb_action_t action = pb_action_from_panel_key(event.key);
        if (action == PB_ACTION_NONE) {
            continue;
        }

        ESP_LOGI(TAG, "Action: %s", pb_action_name(action));
        if (cloud_err != ESP_OK || !wifi_connected()) {
            ESP_LOGW(TAG, "Action not sent while offline");
            continue;
        }

        uint64_t event_revision;
        esp_err_t err = pb_cloud_post_action(&cloud, action, &event_revision);
        if (err == ESP_OK) {
            err = sync_view(
                &cloud,
                &caps,
                &current_view,
                &current_revision,
                &has_current_revision);
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Action delivery failed: %s", esp_err_to_name(err));
        }
        next_poll = xTaskGetTickCount() +
                    pdMS_TO_TICKS(CONFIG_PB_POLL_INTERVAL_SECONDS * 1000);
    }
}
