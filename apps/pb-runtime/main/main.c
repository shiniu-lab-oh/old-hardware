#include <inttypes.h>
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
#include "freertos/task.h"
#include "nvs_flash.h"
#include "old_panel.h"
#include "pb_actions.h"
#include "pb_cloud.h"
#include "pb_event_queue.h"
#include "pb_overlay.h"
#include "pb_timer.h"
#include "pb_view.h"
#include "sdkconfig.h"

#define PB_WIFI_CONNECTED_BIT BIT0
#define PB_RUNTIME_VERSION "pb-runtime/0.2.0"
#define PB_EVENT_RETRY_INTERVAL_MS 5000

static const char *TAG = "pb_runtime";
static EventGroupHandle_t s_wifi_events;
static volatile bool s_had_wifi_connection;
static volatile bool s_offline_overlay_pending;

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

    esp_sntp_config_t sntp_config =
        ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_PB_SNTP_SERVER);
    ESP_RETURN_ON_ERROR(
        esp_netif_sntp_init(&sntp_config), TAG, "SNTP initialization failed");

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

static esp_err_t render_current(
    pb_local_timer_t *timer,
    const pb_view_t *app_view,
    const old_panel_caps_t *caps
)
{
    if (!pb_timer_active(timer)) {
        return pb_view_render(app_view, caps);
    }

    pb_view_t timer_view;
    pb_timer_make_view(timer, app_view, &timer_view);
    return pb_view_render(&timer_view, caps);
}

static esp_err_t sync_view(
    pb_cloud_t *cloud,
    const old_panel_caps_t *caps,
    pb_view_t *current_view,
    uint64_t *current_revision,
    bool *has_current_revision,
    pb_local_timer_t *timer,
    pb_overlay_t *overlay
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

    const bool timer_was_active = pb_timer_active(timer);
    pb_timer_configure(timer, &next_state.timer);
    if (*has_current_revision && next_state.revision == *current_revision) {
        if (timer_was_active && !pb_timer_active(timer) &&
            !pb_overlay_active(overlay)) {
            return pb_view_render(current_view, caps);
        }
        ESP_LOGD(TAG, "State revision=%" PRIu64 " unchanged", *current_revision);
        return ESP_OK;
    }

    *current_view = next_state.view;
    *current_revision = next_state.revision;
    *has_current_revision = true;
    err = pb_view_store_last(current_view, *current_revision);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not store last view: %s", esp_err_to_name(err));
    }

    if (next_state.overlay.enabled) {
        return pb_overlay_show_code(
            overlay,
            next_state.overlay.value,
            next_state.overlay.duration_ms,
            next_state.overlay.blink,
            caps);
    }
    if (!pb_overlay_active(overlay)) {
        return render_current(timer, current_view, caps);
    }
    return ESP_OK;
}

static esp_err_t enqueue_timer_event(
    pb_event_queue_t *queue,
    pb_local_timer_t *timer,
    pb_timer_event_t event
)
{
    static const char *const names[] = {"started", "paused", "resumed", "finished"};
    ESP_LOGI(TAG,
             "Timer %s: duration=%" PRIu32 " remaining=%" PRIu32,
             names[event],
             timer->duration_seconds,
             pb_timer_remaining_seconds(timer));
    pb_event_t pending;
    esp_err_t err = pb_event_make_timer(
        &pending,
        event,
        timer->duration_seconds,
        event == PB_TIMER_EVENT_FINISHED ? 0 : pb_timer_remaining_seconds(timer));
    if (err == ESP_OK) {
        err = pb_event_queue_enqueue(queue, &pending);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not queue timer event: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG,
             "Queued event %s (%u pending)",
             pending.event_id,
             (unsigned)pb_event_queue_count(queue));
    return ESP_OK;
}

static esp_err_t enqueue_action_event(
    pb_event_queue_t *queue,
    pb_action_t action
)
{
    pb_event_t pending;
    esp_err_t err = pb_event_make_action(&pending, action);
    if (err == ESP_OK) {
        err = pb_event_queue_enqueue(queue, &pending);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not queue action event: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG,
             "Queued event %s (%u pending)",
             pending.event_id,
             (unsigned)pb_event_queue_count(queue));
    return ESP_OK;
}

static esp_err_t flush_pending_events(
    pb_event_queue_t *queue,
    pb_cloud_t *cloud
)
{
    if (!wifi_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    const pb_event_t *event;
    while ((event = pb_event_queue_peek(queue)) != NULL) {
        uint64_t revision;
        esp_err_t err = pb_cloud_post_event(cloud, event, &revision);
        if (err != ESP_OK) {
            ESP_LOGW(TAG,
                     "Event %s delivery failed; retained for retry: %s",
                     event->event_id,
                     esp_err_to_name(err));
            return err;
        }

        char delivered_id[PB_EVENT_ID_LENGTH + 1];
        strlcpy(delivered_id, event->event_id, sizeof(delivered_id));
        err = pb_event_queue_pop(queue);
        if (err != ESP_OK) {
            ESP_LOGE(TAG,
                     "Event %s acknowledged but could not be removed: %s",
                     delivered_id,
                     esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG,
                 "Delivered event %s (%u pending)",
                 delivered_id,
                 (unsigned)pb_event_queue_count(queue));
    }
    return ESP_OK;
}

void app_main(void)
{
    ESP_ERROR_CHECK(init_nvs());

    static pb_event_queue_t event_queue;
    ESP_ERROR_CHECK(pb_event_queue_init(&event_queue));

    const old_panel_config_t panel_config = {
        .profile_id = CONFIG_PB_PANEL_PROFILE,
    };
    ESP_ERROR_CHECK(old_panel_init(&panel_config));

    old_panel_caps_t caps;
    ESP_ERROR_CHECK(old_panel_get_capabilities(&caps));

    pb_overlay_t overlay;
    pb_overlay_init(&overlay);
    ESP_ERROR_CHECK(pb_overlay_show_code(
        &overlay, 888, CONFIG_PB_BOOT_OVERLAY_MS, false, &caps));
    vTaskDelay(pdMS_TO_TICKS(CONFIG_PB_BOOT_OVERLAY_MS));
    pb_overlay_init(&overlay);

    pb_view_t current_view;
    uint64_t current_revision = 0;
    bool has_current_revision =
        pb_view_load_last(&current_view, &current_revision) == ESP_OK;
    if (!has_current_revision) {
        pb_view_default(&current_view);
    }

    pb_local_timer_t timer;
    pb_timer_init(&timer);
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
    TickType_t next_event_retry = 0;
    bool primary_pressed = false;
    TickType_t primary_pressed_at = 0;

    while (true) {
        const TickType_t now = xTaskGetTickCount();

        if (s_offline_overlay_pending) {
            s_offline_overlay_pending = false;
            ESP_ERROR_CHECK_WITHOUT_ABORT(pb_overlay_show_code(
                &overlay, 404, CONFIG_PB_OFFLINE_OVERLAY_MS, false, &caps));
        }

        if (pb_timer_poll_finished(&timer)) {
            if (!pb_overlay_active(&overlay)) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(pb_view_render(&current_view, &caps));
            }
            if (cloud_err == ESP_OK) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(enqueue_timer_event(
                    &event_queue, &timer, PB_TIMER_EVENT_FINISHED));
                next_event_retry = 0;
            }
        } else if (pb_timer_active(&timer) && !pb_overlay_active(&overlay)) {
            const uint32_t minutes =
                (pb_timer_remaining_seconds(&timer) + 59U) / 60U;
            if (minutes != timer.last_displayed_minutes) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(render_current(&timer, &current_view, &caps));
            }
        }

        if (pb_overlay_take_expired(&overlay)) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(render_current(&timer, &current_view, &caps));
        }

        if (cloud_err == ESP_OK && wifi_connected() &&
            pb_event_queue_count(&event_queue) > 0 &&
            (next_event_retry == 0 ||
             (int32_t)(now - next_event_retry) >= 0)) {
            const esp_err_t err = flush_pending_events(&event_queue, &cloud);
            if (err == ESP_OK) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(sync_view(
                    &cloud,
                    &caps,
                    &current_view,
                    &current_revision,
                    &has_current_revision,
                    &timer,
                    &overlay));
            }
            next_event_retry = now + pdMS_TO_TICKS(PB_EVENT_RETRY_INTERVAL_MS);
        }

        if (cloud_err == ESP_OK && wifi_connected() &&
            (next_poll == 0 || (int32_t)(now - next_poll) >= 0)) {
            const esp_err_t err = sync_view(
                &cloud,
                &caps,
                &current_view,
                &current_revision,
                &has_current_revision,
                &timer,
                &overlay);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "State sync failed; keeping local state");
            }
            next_poll = now + pdMS_TO_TICKS(CONFIG_PB_POLL_INTERVAL_SECONDS * 1000);
        }

        old_panel_key_event_t key_event;
        if (!old_panel_wait_key_event(&key_event, pdMS_TO_TICKS(100))) {
            continue;
        }
        if (pb_action_from_panel_key(key_event.key) == PB_ACTION_NONE) {
            continue;
        }

        if (key_event.pressed) {
            primary_pressed = true;
            primary_pressed_at = xTaskGetTickCount();
            continue;
        }
        if (!primary_pressed) {
            continue;
        }

        primary_pressed = false;
        const uint32_t held_ms =
            (uint32_t)((xTaskGetTickCount() - primary_pressed_at) * portTICK_PERIOD_MS);
        if (held_ms >= CONFIG_PB_PRIMARY_LONG_PRESS_MS) {
            if (timer.status == PB_LOCAL_TIMER_RUNNING) {
                ESP_LOGI(TAG, "Long action ignored while timer is running");
                continue;
            }
            ESP_LOGI(TAG, "Action: primary_long (%" PRIu32 " ms)", held_ms);
            if (cloud_err == ESP_OK) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(enqueue_action_event(
                    &event_queue, PB_ACTION_PRIMARY_LONG));
                next_event_retry = 0;
            }
        } else if (pb_timer_enabled(&timer)) {
            const pb_timer_event_t event = pb_timer_toggle(&timer);
            if (!pb_overlay_active(&overlay)) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(render_current(&timer, &current_view, &caps));
            }
            if (cloud_err == ESP_OK) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(enqueue_timer_event(
                    &event_queue, &timer, event));
                next_event_retry = 0;
            }
        } else {
            ESP_LOGI(TAG, "Action: primary");
            if (cloud_err == ESP_OK) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(enqueue_action_event(
                    &event_queue, PB_ACTION_PRIMARY));
                next_event_retry = 0;
            }
        }

        next_poll = xTaskGetTickCount() +
                    pdMS_TO_TICKS(CONFIG_PB_POLL_INTERVAL_SECONDS * 1000);
    }
}
