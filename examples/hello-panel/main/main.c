#include "old_panel.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "hello_panel";

void app_main(void)
{
    ESP_ERROR_CHECK(old_panel_init());

    old_panel_caps_t caps;
    ESP_ERROR_CHECK(old_panel_get_capabilities(&caps));

    ESP_LOGI(TAG,
             "Old Panel ready: digits=%u keys=%u leds=%u",
             caps.digits,
             caps.keys,
             caps.leds);

    ESP_ERROR_CHECK(old_panel_display_number_padded(123));

    if (caps.leds > 0) {
        ESP_ERROR_CHECK(old_panel_set_led(0, true));
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP_ERROR_CHECK(old_panel_set_led(0, false));
    }

    ESP_LOGI(TAG, "Press a panel key...");

    while (true) {
        old_panel_key_event_t event;

        if (!old_panel_wait_key_event(&event, portMAX_DELAY)) {
            continue;
        }

        if (!event.pressed) {
            continue;
        }

        if (event.key >= OLD_PANEL_KEY_1 &&
            event.key <= OLD_PANEL_KEY_8) {
            const int number =
                (int)event.key - (int)OLD_PANEL_KEY_1 + 1;

            ESP_ERROR_CHECK(old_panel_display_number_padded(number));

            if (caps.leds > 0) {
                ESP_ERROR_CHECK(old_panel_set_led(0, true));
                vTaskDelay(pdMS_TO_TICKS(100));
                ESP_ERROR_CHECK(old_panel_set_led(0, false));
            }
        }
    }
}
