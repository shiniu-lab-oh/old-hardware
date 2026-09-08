#include "ct1668_key_test.h"

#include <stdbool.h>
#include <string.h>
#include "ct1668.h"
#include "sdc251_keys.h"
#include "sdc251_panel.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define KEY_POLL_MS 20U
#define KEY_STABLE_SAMPLES 3U
#define KEY_SNAPSHOT_MS 5000U

static const char *TAG = "CT1668";
_Static_assert(CT1668_KEY_BYTES == SDC251_KEYS_RAW_BYTES, "Key frame size mismatch");
/* Optional annotation only; automatic BUTTON events use the measured mapping. */
static const char *const labels[] = {
    "UNLABELLED", "MENU", "EXIT", "OK", "VOL-", "VOL+", "CH-", "CH+"
};

static void print_help(void)
{
    ESP_LOGI(TAG, "[KEYTEST] 20ms polls, 3 matching samples; keep all raw 5 bytes");
    ESP_LOGI(TAG, "[KEYTEST] Seven measured keys active: press a physical button for BUTTON DOWN/UP");
    ESP_LOGI(TAG, "[KEYTEST] UART label: 1=MENU 2=EXIT 3=OK 4=VOL- 5=VOL+ 6=CH- 7=CH+");
    ESP_LOGI(TAG, "[KEYTEST] 0=unlabelled, r=snapshot, h=help; no Enter needed");
    ESP_LOGI(TAG, "[KEYTEST] UART labels are optional and never change automatic button recognition");
    ESP_LOGI(TAG, "[GREEN_LED] Test buttons: MENU=ON, EXIT=OFF, OK=toggle; hold does not repeat");
    ESP_LOGI(TAG, "[GREEN_LED] UART o=ON, f=OFF, t=toggle; startup OFF, no automatic flashing");
}

static void print_raw(const char *event, unsigned label,
                      const uint8_t raw[CT1668_KEY_BYTES])
{
    ESP_LOGI(TAG, "[KEYS] %s label=%s raw=%02X %02X %02X %02X %02X",
             event, labels[label], (unsigned)raw[0], (unsigned)raw[1],
             (unsigned)raw[2], (unsigned)raw[3], (unsigned)raw[4]);
}

void ct1668_key_test_run(void)
{
    const uart_port_t console = (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM;
    if (!uart_is_driver_installed(console)) {
        ESP_ERROR_CHECK(uart_driver_install(console, 1024, 0, 0, NULL, 0));
    }
    /* Keep CT1668 RAM blank; the green indicator has a separate GPIO.
     * MENU/EXIT/OK below are demonstration controls, not original STB behavior. */
    ct1668_clear();
    print_help();

    uint8_t candidate[CT1668_KEY_BYTES] = {0};
    uint8_t stable[CT1668_KEY_BYTES] = {0};
    uint8_t raw[CT1668_KEY_BYTES] = {0};
    unsigned matching = 0;
    unsigned label = 0;
    bool have_stable = false;
    bool invalid_named_sample = false;
    sdc251_keys_state_t button_state = {0};
    TickType_t last_wake = xTaskGetTickCount();
    TickType_t last_snapshot = last_wake;

    for (;;) {
        const esp_err_t err = ct1668_read_keys(raw);
        if (err != ESP_OK) {
            /* A direction restore may have failed: stop instead of issuing
             * another bus transaction under an unknown GPIO configuration. */
            ESP_LOGE(TAG, "[KEYTEST] Read failed: %s; stopped", esp_err_to_name(err));
            return;
        }
        sdc251_key_events_t events;
        const esp_err_t decoded = sdc251_keys_update(&button_state, raw, &events);
        if (decoded != ESP_OK) {
            if (!invalid_named_sample) {
                ESP_LOGW(TAG, "[BUTTON] Unknown raw bits: %s; preserving held state, no events",
                         esp_err_to_name(decoded));
            }
            invalid_named_sample = true;
        } else {
            if (invalid_named_sample) {
                ESP_LOGI(TAG, "[BUTTON] Raw data valid again; debounce restarted");
            }
            invalid_named_sample = false;
            for (unsigned i = 0; i < SDC251_KEY_COUNT; ++i) {
                const sdc251_key_mask_t key = (sdc251_key_mask_t)(1U << i);
                if ((events.down & key) != 0U) {
                    ESP_LOGI(TAG, "[BUTTON] %s DOWN", sdc251_key_name(key));
                }
                if ((events.up & key) != 0U) {
                    ESP_LOGI(TAG, "[BUTTON] %s UP", sdc251_key_name(key));
                }
            }
            /* One action per debounced DOWN. If more than one DOWN arrives
             * together, OFF wins, then ON, then toggle. This does not claim
             * arbitrary physical key combinations are supported by the matrix. */
            if ((events.down & SDC251_KEY_EXIT) != 0U) {
                ESP_ERROR_CHECK(sdc251_green_led_set(false));
            } else if ((events.down & SDC251_KEY_MENU) != 0U) {
                ESP_ERROR_CHECK(sdc251_green_led_set(true));
            } else if ((events.down & SDC251_KEY_OK) != 0U) {
                ESP_ERROR_CHECK(sdc251_green_led_toggle());
            }
        }
        if (matching == 0U || memcmp(raw, candidate, sizeof(candidate)) != 0) {
            memcpy(candidate, raw, sizeof(candidate));
            matching = 1;
        } else if (matching < KEY_STABLE_SAMPLES) {
            ++matching;
        }
        if (matching == KEY_STABLE_SAMPLES &&
            (!have_stable || memcmp(candidate, stable, sizeof(stable)) != 0)) {
            print_raw(have_stable ? "CHANGE" : "INITIAL", label, candidate);
            if (have_stable) {
                for (unsigned i = 0; i < CT1668_KEY_BYTES; ++i) {
                    const uint8_t changed = stable[i] ^ candidate[i];
                    for (unsigned bit = 0; bit < 8U; ++bit) {
                        if ((changed & (1U << bit)) != 0U) {
                            /* Index is zero-based; do not infer active polarity. */
                            ESP_LOGI(TAG, "[KEYBIT] label=%s byte=%u bit=%u old=%u new=%u",
                                     labels[label], i, bit,
                                     (unsigned)((stable[i] >> bit) & 1U),
                                     (unsigned)((candidate[i] >> bit) & 1U));
                        }
                    }
                }
            }
            memcpy(stable, candidate, sizeof(stable));
            have_stable = true;
            bool all_ff = true;
            for (unsigned i = 0; i < CT1668_KEY_BYTES; ++i) {
                all_ff = all_ff && stable[i] == 0xFFU;
            }
            if (all_ff) {
                ESP_LOGW(TAG, "[KEYTEST] All FF: check DIO reply/wiring/pull-up; not a valid mapping yet");
            }
        }

        /* Nonblocking console input and key polling share app_main.
         * Console label changes never alter button data or declare a mapping. */
        uint8_t input;
        const int received = uart_read_bytes(console, &input, 1, 0);
        if (received < 0) {
            ESP_LOGE(TAG, "[KEYTEST] Console read failed; stopped");
            return;
        }
        bool snapshot = false;
        if (received == 1) {
            if (input >= '0' && input <= '7') {
                label = (unsigned)(input - '0');
                ESP_LOGI(TAG, "[KEYTEST] Selected label=%s; release, then press/release twice", labels[label]);
                snapshot = true;
            } else if (input == 'r') {
                snapshot = true;
            } else if (input == 'o') {
                ESP_ERROR_CHECK(sdc251_green_led_set(true));
            } else if (input == 'f') {
                ESP_ERROR_CHECK(sdc251_green_led_set(false));
            } else if (input == 't') {
                ESP_ERROR_CHECK(sdc251_green_led_toggle());
            } else if (input == 'h' || input == '?') {
                print_help();
            }
        }
        const TickType_t now = xTaskGetTickCount();
        if (snapshot || (TickType_t)(now - last_snapshot) >= pdMS_TO_TICKS(KEY_SNAPSHOT_MS)) {
            /* Report the latest unfiltered sample too, to expose unstable data. */
            print_raw("SAMPLE", label, raw);
            if (have_stable) {
                print_raw("STABLE", label, stable);
            }
            ESP_LOGI(TAG, "[BUTTON] held=0x%02X raw_valid=%s",
                     (unsigned)button_state.held, invalid_named_sample ? "no" : "yes");
            last_snapshot = now;
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(KEY_POLL_MS));
    }
}
