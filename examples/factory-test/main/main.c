#include "old_panel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define FACTORY_TEST_MAX_KEYS 8
#define FACTORY_TEST_KEY_TIMEOUT_MS 60000

static const char *TAG = "factory_test";

static const old_panel_config_t s_panel_config = {
    .profile_id = "LP-001",
};

static void wait_ms(uint32_t milliseconds)
{
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

static uint8_t tested_digit_count(uint8_t digits)
{
    return digits > 9 ? 9 : digits;
}

static int repeated_digit(uint8_t digit, uint8_t count)
{
    int value = 0;
    for (uint8_t i = 0; i < count; ++i) {
        value = value * 10 + digit;
    }
    return value;
}

static int ascending_digits(uint8_t count)
{
    int value = 0;
    for (uint8_t i = 0; i < count; ++i) {
        value = value * 10 + (i + 1) % 10;
    }
    return value;
}

static uint8_t all_decimal_points(uint8_t digits)
{
    if (digits >= 8) {
        return UINT8_MAX;
    }
    return (uint8_t)((1U << digits) - 1U);
}

static void run_display_test(const old_panel_caps_t *caps)
{
    if (caps->digits == 0) {
        ESP_LOGW(TAG, "[DISPLAY] SKIP (not supported)");
        return;
    }

    const uint8_t digits = tested_digit_count(caps->digits);
    const int eights = repeated_digit(8, digits);
    const int sequence = ascending_digits(digits);

    ESP_LOGI(TAG, "[DISPLAY] Starting segment and digit test");

    ESP_ERROR_CHECK(old_panel_display_number_padded(eights));
    wait_ms(1000);
    ESP_ERROR_CHECK(old_panel_display_number_padded(0));
    wait_ms(1000);
    ESP_ERROR_CHECK(old_panel_display_number_padded(sequence));
    wait_ms(1000);

    if (caps->has_decimal_point) {
        ESP_LOGI(TAG, "[DISPLAY] Decimal points");
        ESP_ERROR_CHECK(old_panel_display_value(
            eights, true, all_decimal_points(caps->digits)));
        wait_ms(1000);
    }

    ESP_ERROR_CHECK(old_panel_display_number_padded(eights));

    if (caps->supports_brightness) {
        ESP_LOGI(TAG, "[DISPLAY] Brightness 100%%");
        ESP_ERROR_CHECK(old_panel_set_brightness(100));
        wait_ms(1000);

        ESP_LOGI(TAG, "[DISPLAY] Brightness 30%%");
        ESP_ERROR_CHECK(old_panel_set_brightness(30));
        wait_ms(1000);

        ESP_ERROR_CHECK(old_panel_set_brightness(100));
    } else {
        ESP_LOGW(TAG, "[DISPLAY] Brightness SKIP (not supported)");
    }

    if (caps->supports_blink) {
        ESP_LOGI(TAG, "[DISPLAY] Blink twice");
        ESP_ERROR_CHECK(old_panel_set_blink(true, 250));
        wait_ms(1000);
        ESP_ERROR_CHECK(old_panel_set_blink(false, 0));
    } else {
        ESP_LOGW(TAG, "[DISPLAY] Blink SKIP (not supported)");
    }

    ESP_LOGI(TAG, "[DISPLAY] TEST SEQUENCE COMPLETE");
}

static void set_all_leds(uint8_t led_count, bool on)
{
    for (uint8_t i = 0; i < led_count; ++i) {
        ESP_ERROR_CHECK(old_panel_set_led(i, on));
    }
}

static void flash_all_leds(uint8_t led_count,
                           uint8_t count,
                           uint32_t interval_ms)
{
    for (uint8_t i = 0; i < count; ++i) {
        set_all_leds(led_count, true);
        wait_ms(interval_ms);
        set_all_leds(led_count, false);
        wait_ms(interval_ms);
    }
}

static void run_led_test(uint8_t led_count)
{
    if (led_count == 0) {
        ESP_LOGW(TAG, "[LED] SKIP (no controllable LEDs)");
        return;
    }

    for (uint8_t i = 0; i < led_count; ++i) {
        ESP_LOGI(TAG, "[LED] Testing LED %u", (unsigned)i);
        for (uint8_t cycle = 0; cycle < 2; ++cycle) {
            ESP_ERROR_CHECK(old_panel_set_led(i, true));
            wait_ms(500);
            ESP_ERROR_CHECK(old_panel_set_led(i, false));
            wait_ms(500);
        }
    }

    ESP_LOGI(TAG, "[LED] TEST SEQUENCE COMPLETE");
}

static void log_keys(const char *label,
                     const bool passed[FACTORY_TEST_MAX_KEYS],
                     uint8_t key_count)
{
    char keys[32] = {0};
    size_t offset = 0;

    for (uint8_t i = 0; i < key_count; ++i) {
        if (passed[i]) {
            continue;
        }

        const int written = snprintf(
            keys + offset,
            sizeof(keys) - offset,
            "%s%u",
            offset == 0 ? "" : ", ",
            (unsigned)(i + 1));
        if (written < 0 || (size_t)written >= sizeof(keys) - offset) {
            break;
        }
        offset += (size_t)written;
    }

    ESP_LOGI(TAG, "%s: %s", label, offset == 0 ? "none" : keys);
}

static void discard_pending_key_events(void)
{
    old_panel_key_event_t event;
    while (old_panel_wait_key_event(&event, 0)) {
    }
}

static bool run_key_test(const old_panel_caps_t *caps,
                         uint8_t *passed_count_out)
{
    bool passed[FACTORY_TEST_MAX_KEYS] = {false};
    uint8_t passed_count = 0;

    *passed_count_out = 0;

    if (caps->keys == 0) {
        ESP_LOGW(TAG, "[KEY] SKIP (not supported)");
        return true;
    }
    if (caps->keys > FACTORY_TEST_MAX_KEYS) {
        ESP_LOGE(TAG, "[KEY] FAIL (maximum supported key count is %u)",
                 (unsigned)FACTORY_TEST_MAX_KEYS);
        return false;
    }

    discard_pending_key_events();
    if (caps->digits > 0) {
        ESP_ERROR_CHECK(old_panel_display_number_padded(0));
    }

    ESP_LOGI(TAG,
             "[KEY] Press all %u panel keys within %u seconds",
             (unsigned)caps->keys,
             (unsigned)(FACTORY_TEST_KEY_TIMEOUT_MS / 1000));
    log_keys("Remaining", passed, caps->keys);

    const TickType_t timeout_ticks =
        pdMS_TO_TICKS(FACTORY_TEST_KEY_TIMEOUT_MS);
    const TickType_t started_at = xTaskGetTickCount();

    while (passed_count < caps->keys) {
        const TickType_t elapsed = xTaskGetTickCount() - started_at;
        if (elapsed >= timeout_ticks) {
            break;
        }

        old_panel_key_event_t event;
        if (!old_panel_wait_key_event(&event, timeout_ticks - elapsed) ||
            !event.pressed ||
            event.key < OLD_PANEL_KEY_1 ||
            event.key > OLD_PANEL_KEY_8) {
            continue;
        }

        const uint8_t key_index =
            (uint8_t)((int)event.key - (int)OLD_PANEL_KEY_1);
        if (key_index >= caps->keys) {
            continue;
        }

        if (caps->digits > 0) {
            ESP_ERROR_CHECK(old_panel_display_number_padded(key_index + 1));
        }

        if (!passed[key_index]) {
            passed[key_index] = true;
            ++passed_count;
            *passed_count_out = passed_count;
            ESP_LOGI(TAG, "[KEY] %u PASS", (unsigned)(key_index + 1));
            log_keys("Remaining", passed, caps->keys);
        }
    }

    if (passed_count != caps->keys) {
        ESP_LOGE(TAG, "[KEY] TIMEOUT");
        log_keys("Missing", passed, caps->keys);
        return false;
    }

    if (caps->leds > 0) {
        flash_all_leds(caps->leds, 3, 100);
    }
    ESP_LOGI(TAG,
             "[KEY] PASS (%u/%u)",
             (unsigned)passed_count,
             (unsigned)caps->keys);
    return true;
}

static void prepare_final_display(const old_panel_caps_t *caps, bool passed)
{
    if (caps->supports_brightness) {
        ESP_ERROR_CHECK(old_panel_set_brightness(100));
    }
    if (caps->supports_blink) {
        ESP_ERROR_CHECK(old_panel_set_blink(false, 0));
    }

    if (caps->digits > 0) {
        if (passed) {
            const int eights = repeated_digit(8, tested_digit_count(caps->digits));
            ESP_ERROR_CHECK(old_panel_display_number_padded(eights));
        } else {
            ESP_ERROR_CHECK(old_panel_display_number_padded(0));
            if (caps->supports_blink) {
                ESP_ERROR_CHECK(old_panel_set_blink(true, 500));
            }
        }
    }

    if (caps->leds > 0) {
        set_all_leds(caps->leds, passed);
    }
}

static void show_result(const old_panel_caps_t *caps,
                        bool keys_passed,
                        uint8_t passed_count)
{
    prepare_final_display(caps, keys_passed);

    ESP_LOGI(TAG, "==============================");
    ESP_LOGI(TAG, " OLD PANEL FACTORY TEST");
    ESP_LOGI(TAG, " Profile : %s", s_panel_config.profile_id);
    ESP_LOGI(TAG, "%s", "");
    ESP_LOGI(TAG,
             " Display : %s",
             caps->digits > 0 ? "COMPLETE (VISUAL CHECK REQUIRED)" : "SKIP");
    ESP_LOGI(TAG,
             " LED     : %s",
             caps->leds > 0 ? "COMPLETE (VISUAL CHECK REQUIRED)" : "SKIP");

    if (caps->keys == 0) {
        ESP_LOGI(TAG, " Keys    : SKIP");
    } else {
        ESP_LOGI(TAG,
                 " Keys    : %s (%u/%u)",
                 keys_passed ? "PASS" : "FAIL",
                 (unsigned)passed_count,
                 (unsigned)caps->keys);
    }

    ESP_LOGI(TAG, "%s", "");
    ESP_LOGI(TAG,
             " RESULT  : %s",
             keys_passed ? "COMPLETE - VISUAL CONFIRMATION REQUIRED" : "FAIL");
    ESP_LOGI(TAG, "==============================");
}

void app_main(void)
{
    ESP_ERROR_CHECK(old_panel_init(&s_panel_config));

    old_panel_caps_t caps;
    ESP_ERROR_CHECK(old_panel_get_capabilities(&caps));

    ESP_LOGI(TAG,
             "Starting factory test: profile=%s digits=%u keys=%u leds=%u",
             s_panel_config.profile_id,
             (unsigned)caps.digits,
             (unsigned)caps.keys,
             (unsigned)caps.leds);

    run_display_test(&caps);
    run_led_test(caps.leds);

    uint8_t passed_count = 0;
    const bool keys_passed = run_key_test(&caps, &passed_count);
    show_result(&caps, keys_passed, passed_count);
}
