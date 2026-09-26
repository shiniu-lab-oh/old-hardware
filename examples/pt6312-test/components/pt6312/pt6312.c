#include "pt6312.h"

#include <inttypes.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// PTC PT6312B V1.2 (December 2005), pp. 4-7, 10, 12-15.
// Command 2: b6=1, b3=0 normal, b2=0 auto-increment, b1:b0=00 RAM write.
#define CMD_WRITE_AUTO 0x40
#define CMD_ADDRESS 0xC0
#define CMD_DISPLAY_OFF 0x80
#define CMD_DISPLAY_ON 0x88

_Static_assert(PT6312_DISPLAY_MODE >= 0 && PT6312_DISPLAY_MODE <= 7,
               "Display mode must be 0x00..0x07");
_Static_assert(PT6312_DELAY_US >= 1, "Delay must be at least 1 us");
_Static_assert(PT6312_BRIGHTNESS >= 0 && PT6312_BRIGHTNESS <= 7,
               "Brightness must be 0..7");
_Static_assert(PT6312_PIN_CLK != PT6312_PIN_STB &&
               PT6312_PIN_CLK != PT6312_PIN_DIN &&
               PT6312_PIN_STB != PT6312_PIN_DIN, "Pins must be distinct");

static const char *TAG = "PT6312";

static void pin_level(gpio_num_t pin, uint32_t level)
{
    ESP_ERROR_CHECK(gpio_set_level(pin, level));
    esp_rom_delay_us(PT6312_DELAY_US);
}

static void frame_begin(void)
{
    // Previous frame left CLK HIGH. STB setup time precedes the first falling CLK.
    pin_level(PT6312_PIN_STB, 0);
}

static void frame_end(void)
{
    // Last CLK rising edge already has a full hold delay.
    pin_level(PT6312_PIN_STB, 1);
    pin_level(PT6312_PIN_DIN, 0);
}

void pt6312_write_byte(uint8_t data)
{
    for (unsigned bit = 0; bit < 8; ++bit) {
        pin_level(PT6312_PIN_CLK, 0);
        pin_level(PT6312_PIN_DIN, (data >> bit) & 1U);
        // PT6312B samples DIN on the rising edge, least significant bit first.
        pin_level(PT6312_PIN_CLK, 1);
    }
}

void pt6312_write_command(uint8_t cmd)
{
    frame_begin();
    pt6312_write_byte(cmd);
    frame_end();
}

void pt6312_write_ram(uint8_t start_addr, const uint8_t *data, size_t len)
{
    if (start_addr >= PT6312_RAM_SIZE || len > PT6312_RAM_SIZE - start_addr ||
        (len != 0 && data == NULL)) {
        ESP_LOGE(TAG, "Invalid RAM write: addr=0x%02X len=%u",
                 (unsigned)start_addr, (unsigned)len);
        return;
    }
    if (len == 0) {
        return;
    }
    pt6312_write_command(CMD_WRITE_AUTO);
    frame_begin();
    pt6312_write_byte(CMD_ADDRESS | start_addr);
    for (size_t i = 0; i < len; ++i) {
        pt6312_write_byte(data[i]);
    }
    frame_end();
}

void pt6312_clear(void)
{
    const uint8_t zeros[PT6312_RAM_SIZE] = {0};
    pt6312_write_ram(0, zeros, sizeof(zeros));
}

void pt6312_display_on(uint8_t brightness)
{
    if (brightness > 7) {
        ESP_LOGE(TAG, "Brightness must be 0..7");
        return;
    }
    pt6312_write_command(CMD_DISPLAY_ON | brightness);
}

void pt6312_display_off(void)
{
    pt6312_write_command(CMD_DISPLAY_OFF);
}

void pt6312_init(void)
{
    ESP_LOGI(TAG, "PT6312 init");
    ESP_ERROR_CHECK(GPIO_IS_VALID_OUTPUT_GPIO(PT6312_PIN_CLK) &&
                    GPIO_IS_VALID_OUTPUT_GPIO(PT6312_PIN_STB) &&
                    GPIO_IS_VALID_OUTPUT_GPIO(PT6312_PIN_DIN)
                    ? ESP_OK : ESP_ERR_INVALID_ARG);

    // Preload output latches before enabling output; bring STB HIGH first.
    ESP_ERROR_CHECK(gpio_set_level(PT6312_PIN_STB, 1));
    const gpio_config_t stb_config = {
        .pin_bit_mask = 1ULL << PT6312_PIN_STB,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&stb_config));
    ESP_ERROR_CHECK(gpio_set_level(PT6312_PIN_CLK, 1));
    ESP_ERROR_CHECK(gpio_set_level(PT6312_PIN_DIN, 0));
    gpio_config_t data_config = stb_config;
    data_config.pin_bit_mask = (1ULL << PT6312_PIN_CLK) | (1ULL << PT6312_PIN_DIN);
    ESP_ERROR_CHECK(gpio_config(&data_config));

    // Datasheet recommended startup delay. Supply must already be stable.
    vTaskDelay(pdMS_TO_TICKS(200));
    // Also handles an ESP32-only reset while PT6312B is already displaying.
    pt6312_display_off();
    ESP_LOGI(TAG, "clear RAM (22 bytes)");
    pt6312_clear();
    ESP_LOGI(TAG, "display mode = 0x%02X", PT6312_DISPLAY_MODE);
    pt6312_write_command(PT6312_DISPLAY_MODE);
    pt6312_display_on(PT6312_BRIGHTNESS);
    ESP_LOGI(TAG, "display ON, brightness=%d", PT6312_BRIGHTNESS);
}

