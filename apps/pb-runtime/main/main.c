#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "pb_hal.h"
#include "pb_network_worker.h"
#include "pb_runtime.h"
#include "sdkconfig.h"

#define PB_WIFI_CONNECTED_BIT BIT0

static const char *TAG = "pb_runtime";
static EventGroupHandle_t s_wifi_events;
static volatile bool s_had_wifi_connection;
static volatile bool s_offline_overlay_pending;

static void wifi_event_handler(
    void *argument,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    (void)argument;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, PB_WIFI_CONNECTED_BIT);
        if (s_had_wifi_connection) {
            s_offline_overlay_pending = true;
            s_had_wifi_connection = false;
        }
        ESP_LOGW(TAG, "Wi-Fi disconnected; local state continues");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;
        ESP_LOGI(TAG, "Wi-Fi connected: " IPSTR, IP2STR(&event->ip_info.ip));
        s_had_wifi_connection = true;
        xEventGroupSetBits(s_wifi_events, PB_WIFI_CONNECTED_BIT);
    }
}

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
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
        esp_event_loop_create_default(),
        TAG,
        "event loop creation failed");
    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_sntp_config_t sntp_config =
        ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_PB_SNTP_SERVER);
    ESP_RETURN_ON_ERROR(
        esp_netif_sntp_init(&sntp_config),
        TAG,
        "SNTP initialization failed");

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "esp_wifi_init failed");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            wifi_event_handler,
            NULL),
        TAG,
        "Wi-Fi event registration failed");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            wifi_event_handler,
            NULL),
        TAG,
        "IP event registration failed");

    wifi_config_t wifi_config = {0};
    strlcpy(
        (char *)wifi_config.sta.ssid,
        CONFIG_PB_WIFI_SSID,
        sizeof(wifi_config.sta.ssid));
    strlcpy(
        (char *)wifi_config.sta.password,
        CONFIG_PB_WIFI_PASSWORD,
        sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    ESP_RETURN_ON_ERROR(
        esp_wifi_set_mode(WIFI_MODE_STA),
        TAG,
        "Wi-Fi mode failed");
    ESP_RETURN_ON_ERROR(
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config),
        TAG,
        "Wi-Fi config failed");
    return esp_wifi_start();
}

static bool transport_is_online(void *context)
{
    (void)context;
    return s_wifi_events != NULL &&
           (xEventGroupGetBits(s_wifi_events) & PB_WIFI_CONNECTED_BIT) != 0;
}

static bool transport_take_offline_transition(void *context)
{
    (void)context;
    if (!s_offline_overlay_pending) {
        return false;
    }
    s_offline_overlay_pending = false;
    return true;
}

static esp_err_t transport_request_state(void *context)
{
    (void)context;
    return pb_network_worker_request_state();
}

static esp_err_t transport_post_event(void *context, const pb_event_t *event)
{
    (void)context;
    return pb_network_worker_post_event(event);
}

static bool transport_receive(
    void *context,
    pb_runtime_transport_result_t *result,
    uint32_t timeout_ms)
{
    (void)context;
    return pb_network_worker_receive(result, timeout_ms);
}

void app_main(void)
{
    ESP_ERROR_CHECK(init_nvs());

    const pb_hal_config_t panel_config = {
        .profile_id = CONFIG_PB_PANEL_PROFILE,
        .primary_key_index = CONFIG_PB_PRIMARY_KEY_INDEX,
    };
    ESP_ERROR_CHECK(pb_hal_init(&panel_config));

    pb_hal_caps_t caps;
    ESP_ERROR_CHECK(pb_hal_get_capabilities(&caps));

    const pb_cloud_config_t cloud_config = {
        .base_url = CONFIG_PB_CLOUD_BASE_URL,
        .device_serial = CONFIG_PB_DEVICE_SERIAL,
        .device_token = CONFIG_PB_DEVICE_TOKEN,
        .firmware_version = PB_RUNTIME_FIRMWARE_VERSION,
        .timeout_ms = CONFIG_PB_HTTP_TIMEOUT_MS,
    };
    const esp_err_t network_err = pb_network_worker_start(&cloud_config);

    const pb_runtime_config_t runtime_config = {
        .cloud_base_url = CONFIG_PB_CLOUD_BASE_URL,
        .device_serial = CONFIG_PB_DEVICE_SERIAL,
        .poll_interval_ms = CONFIG_PB_POLL_INTERVAL_SECONDS * 1000U,
        .primary_long_press_ms = CONFIG_PB_PRIMARY_LONG_PRESS_MS,
        .boot_overlay_ms = CONFIG_PB_BOOT_OVERLAY_MS,
        .offline_overlay_ms = CONFIG_PB_OFFLINE_OVERLAY_MS,
        .transport = {
            .enabled = network_err == ESP_OK,
            .context = NULL,
            .is_online = transport_is_online,
            .take_offline_transition = transport_take_offline_transition,
            .request_state = transport_request_state,
            .post_event = transport_post_event,
            .receive = transport_receive,
        },
    };
    pb_runtime_status_t runtime_status;
    ESP_ERROR_CHECK(pb_runtime_init(&runtime_config, &runtime_status));

    const esp_err_t wifi_err = start_wifi();
    if (network_err != ESP_OK || wifi_err != ESP_OK) {
        ESP_LOGE(TAG,
                 "Runtime configuration incomplete; set Wi-Fi, Cloud URL and token via menuconfig or sdkconfig.secrets");
    }

    ESP_LOGI(TAG,
             "PB Runtime ready: serial=%s profile=%s app=%s cached_revision=%llu",
             CONFIG_PB_DEVICE_SERIAL,
             CONFIG_PB_PANEL_PROFILE,
             runtime_status.has_binding ? runtime_status.app_id : "none",
             (unsigned long long)runtime_status.revision);
    ESP_LOGI(TAG,
             "PB HAL ready: digits=%u controls=%u leds=%u",
             caps.display_digits,
             caps.physical_controls,
             caps.controllable_leds);

    ESP_ERROR_CHECK(pb_runtime_run());
}
