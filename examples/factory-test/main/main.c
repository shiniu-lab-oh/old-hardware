#include "old_panel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define FACTORY_TEST_PROFILE "LP-001"
#define FACTORY_TEST_MAX_KEYS 8
#define FACTORY_TEST_LED_INDEX 0

static const char *TAG = "factory_test";

static void wait_ms(uint32_t milliseconds)
{
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

static void run_display_test(void)
{
    ESP_LOGI(TAG, "[DISPLAY] Starting segment and digit test");

    ESP_ERROR_CHECK(old_panel_set_blink(false, 0));
    ESP_ERROR_CHECK(old_panel_set_brightness(100));

    ESP_ERROR_CHECK(old_panel_display_number_padded(888));
    wait_ms(1000);
    ESP_ERROR_CHECK(old_panel_display_number_padded(0));
    wait_ms(1000);
    ESP_ERROR_CHECK(old_panel_display_number_padded(123));
    wait_ms(1000);

    ESP_LOGI(TAG, "[DISPLAY] Brightness 100%%");
    ESP_ERROR_CHECK(old_panel_display_number_padded(888));
    ESP_ERROR_CHECK(old_panel_set_brightness(100));
    wait_ms(1000);

    ESP_LOGI(TAG, "[DISPLAY] Brightness 30%%");
    ESP_ERROR_CHECK(old_panel_set_brightness(30));
    wait_ms(1000);

    ESP_LOGI(TAG, "[DISPLAY] Blink twice");
    ESP_ERROR_CHECK(old_panel_set_brightness(100));
    ESP_ERROR_CHECK(old_panel_set_blink(true, 250));
    wait_ms(1000);
    ESP_ERROR_CHECK(old_panel_set_blink(false, 0));

    ESP_LOGI(TAG, "[DISPLAY] PASS");
}

static void flash_led(uint8_t index, uint8_t count, uint32_t interval_ms)
{
    for (uint8_t i = 0; i < count; ++i) {
        ESP_ERROR_CHECK(old_panel_set_led(index, true));
        wait_ms(interval_ms);
        ESP_ERROR_CHECK(old_panel_set_led(index, false));
        wait_ms(interval_ms);
    }
}

static void run_led_test(void)
{
    ESP_LOGW(TAG, "[MANUAL] Verify red power LED is ON");
    ESP_LOGI(TAG, "[LED] Testing green LED");

    flash_led(FACTORY_TEST_LED_INDEX, 2, 500);

    ESP_LOGI(TAG, "[LED] PASS");
}

static void log_remaining_keys(const bool passed[FACTORY_TEST_MAX_KEYS],
                               uint8_t key_count)
{
    char remaining[32] = {0};
    size_t offset = 0;

    for (uint8_t i = 0; i < key_count; ++i) {
        if (passed[i]) {
            continue;
        }

        const int written = snprintf(
            remaining + offset,
            sizeof(remaining) - offset,
            "%s%u",
            offset == 0 ? "" : ", ",
            (unsigned)(i + 1));
        if (written < 0 || (size_t)written >= sizeof(remaining) - offset) {
            break;
        }
        offset += (size_t)written;
    }

    ESP_LOGI(TAG, "Remaining: %s", offset == 0 ? "none" : remaining);
}

static void discard_pending_key_events(void)
{
    old_panel_key_event_t event;
    while (old_panel_wait_key_event(&event, 0)) {
    }
}

static void run_key_test(uint8_t key_count)
{
    bool passed[FACTORY_TEST_MAX_KEYS] = {false};
    uint8_t passed_count = 0;

    discard_pending_key_events();
    ESP_ERROR_CHECK(old_panel_display_number_padded(0));

    ESP_LOGI(TAG, "[KEY] Press all %u panel keys", (unsigned)key_count);
    log_remaining_keys(passed, key_count);

    while (passed_count < key_count) {
        old_panel_key_event_t event;
        if (!old_panel_wait_key_event(&event, portMAX_DELAY) ||
            !event.pressed ||
            event.key < OLD_PANEL_KEY_1 ||
            event.key > OLD_PANEL_KEY_8) {
            continue;
        }

        const uint8_t key_index =
            (uint8_t)((int)event.key - (int)OLD_PANEL_KEY_1);
        if (key_index >= key_count) {
            continue;
        }

        ESP_ERROR_CHECK(old_panel_display_number_padded(key_index + 1));

        if (!passed[key_index]) {
            passed[key_index] = true;
            ++passed_count;
            ESP_LOGI(TAG, "[KEY] %u PASS", (unsigned)(key_index + 1));
            log_remaining_keys(passed, key_count);
        }
    }

    flash_led(FACTORY_TEST_LED_INDEX, 3, 100);
    ESP_LOGI(TAG,
             "[KEY] PASS (%u/%u)",
             (unsigned)key_count,
             (unsigned)key_count);
}

static void show_pass_result(uint8_t key_count)
{
    ESP_ERROR_CHECK(old_panel_set_brightness(100));
    ESP_ERROR_CHECK(old_panel_set_blink(false, 0));
    ESP_ERROR_CHECK(old_panel_display_number_padded(888));
    ESP_ERROR_CHECK(old_panel_set_led(FACTORY_TEST_LED_INDEX, true));

    ESP_LOGI(TAG, "==============================");
    ESP_LOGI(TAG, " OLD PANEL FACTORY TEST");
    ESP_LOGI(TAG, " Profile : %s", FACTORY_TEST_PROFILE);
    ESP_LOGI(TAG, "%s", "");
    ESP_LOGI(TAG, " Display : PASS");
    ESP_LOGI(TAG, " LED     : PASS");
    ESP_LOGI(TAG,
             " Keys    : PASS (%u/%u)",
             (unsigned)key_count,
             (unsigned)key_count);
    ESP_LOGI(TAG, "%s", "");
    ESP_LOGI(TAG, " RESULT  : PASS");
    ESP_LOGI(TAG, "==============================");
}

void app_main(void)
{
    ESP_ERROR_CHECK(old_panel_init());

    old_panel_caps_t caps;
    ESP_ERROR_CHECK(old_panel_get_capabilities(&caps));

    ESP_LOGI(TAG,
             "Starting factory test: profile=%s digits=%u keys=%u leds=%u",
             FACTORY_TEST_PROFILE,
             (unsigned)caps.digits,
             (unsigned)caps.keys,
             (unsigned)caps.leds);

    if (caps.digits < 3 ||
        caps.keys == 0 ||
        caps.keys > FACTORY_TEST_MAX_KEYS ||
        caps.leds == 0) {
        ESP_LOGE(TAG, "Unsupported panel capabilities");
        ESP_ERROR_CHECK(ESP_ERR_NOT_SUPPORTED);
    }

    run_display_test();
    run_led_test();
    run_key_test(caps.keys);
    show_pass_result(caps.keys);
}
