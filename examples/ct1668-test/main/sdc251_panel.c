#include "sdc251_panel.h"

#include <stddef.h>
#include "esp_log.h"

static const char *TAG = "CT1668";
static bool initialized;
static bool green_on;

static void log_green(void)
{
    /* Input/output mode enables digital pad readback. A matching pad value
     * checks the ESP32 pin only; it does not prove the LED is visibly lit. */
    const int pad = gpio_get_level(SDC251_GPIO_GREEN_LED);
    ESP_LOGI(TAG, "[GREEN_LED] %s GPIO%d=%u pad=%d",
             green_on ? "ON" : "OFF", (int)SDC251_GPIO_GREEN_LED,
             green_on ? 1U : 0U, pad);
    if (pad != (green_on ? 1 : 0)) {
        ESP_LOGW(TAG, "[GREEN_LED] Pad differs from commanded level; check connection/loading");
    }
}

esp_err_t sdc251_panel_init(void)
{
    initialized = false;
    const gpio_config_t ir_config = {
        .pin_bit_mask = 1ULL << SDC251_GPIO_IR_IN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&ir_config);
    if (err != ESP_OK) { return err; }

    /* Preload LOW before enabling the output. The external panel may make
     * green glow while ESP32 is reset; OFF is guaranteed only after init.
     * Push-pull follows the user's confirmed HIGH=bright, LOW=off behavior;
     * releasing an open-drain output would produce the reported dim glow. */
    err = gpio_set_level(SDC251_GPIO_GREEN_LED, 0);
    if (err != ESP_OK) { return err; }
    const gpio_config_t green_config = {
        .pin_bit_mask = 1ULL << SDC251_GPIO_GREEN_LED,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&green_config);
    if (err != ESP_OK) { return err; }
    green_on = false;
    initialized = true;
    ESP_LOGI(TAG, "[PANEL] GPIO%d -> J1 Pin 3 GREEN_LED_CTRL, HIGH active",
             (int)SDC251_GPIO_GREEN_LED);
    ESP_LOGI(TAG, "[PANEL] GPIO%d <- J1 Pin 4 IR_OUT: input only, decoding disabled",
             (int)SDC251_GPIO_IR_IN);
    log_green();
    return ESP_OK;
}

esp_err_t sdc251_green_led_set(bool on)
{
    if (!initialized) { return ESP_ERR_INVALID_STATE; }
    const esp_err_t err = gpio_set_level(SDC251_GPIO_GREEN_LED, on ? 1U : 0U);
    if (err != ESP_OK) { return err; }
    green_on = on;
    log_green();
    return ESP_OK;
}

esp_err_t sdc251_green_led_toggle(void)
{
    if (!initialized) { return ESP_ERR_INVALID_STATE; }
    return sdc251_green_led_set(!green_on);
}

esp_err_t sdc251_green_led_get(bool *on)
{
    if (on == NULL) { return ESP_ERR_INVALID_ARG; }
    if (!initialized) { return ESP_ERR_INVALID_STATE; }
    *on = green_on;
    return ESP_OK;
}
