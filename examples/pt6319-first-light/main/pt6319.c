#include "pt6319.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

_Static_assert(PT6319_PIN_CLK != PT6319_PIN_STB &&
               PT6319_PIN_CLK != PT6319_PIN_DAT &&
               PT6319_PIN_STB != PT6319_PIN_DAT, "Pins must be distinct");
_Static_assert(PT6319_DELAY_US >= 1, "Bit delay must be positive");
_Static_assert(PT6319_DISPLAY_MODE >= 0 && PT6319_DISPLAY_MODE <= 7,
               "Family display-mode command must be 0..7");
_Static_assert(PT6319_RAM_LENGTH > 0 && PT6319_RAM_LENGTH <= 32,
               "RAM experiment must fit the five-bit family address field");
_Static_assert(PT6319_TEST_MODE_ALL_ON == 0 || PT6319_TEST_MODE_ALL_ON == 1,
               "Test mode must be CLEAR (0) or ALL ON (1)");
_Static_assert((PT6319_CMD_DISPLAY_ON_BASE | PT6319_BRIGHTNESS_MAX) ==
               PT6319_CMD_DISPLAY_ON_MAX, "Inconsistent brightness commands");

static const char *TAG = "PT6319";
static bool initialized;

static void pt6319_dat_output(void)
{
    ESP_ERROR_CHECK(gpio_set_direction(PT6319_PIN_DAT, GPIO_MODE_OUTPUT));
}

// Reserved for future half-duplex reads; not used in this write-only experiment.
static void __attribute__((unused)) pt6319_dat_input(void)
{
    ESP_ERROR_CHECK(gpio_set_direction(PT6319_PIN_DAT, GPIO_MODE_INPUT));
}

static void delay_edge(void)
{
    esp_rom_delay_us(PT6319_DELAY_US);
}

static void frame_begin(void)
{
    // STB is HIGH and CLK is HIGH between frames. Only write commands are sent.
    ESP_ERROR_CHECK(gpio_set_level(PT6319_PIN_STB, 0));
    delay_edge();
}

static void frame_end(void)
{
    // write_byte already held the last rising clock for PT6319_DELAY_US.
    ESP_ERROR_CHECK(gpio_set_level(PT6319_PIN_STB, 1));
    delay_edge();
    ESP_ERROR_CHECK(gpio_set_level(PT6319_PIN_DAT, 0));
    delay_edge();
}

static void pt6319_write_byte(uint8_t data)
{
    // No STB changes within a byte or between bytes in the same RAM frame.
    for (unsigned bit = 0; bit < 8; ++bit) {
        ESP_ERROR_CHECK(gpio_set_level(PT6319_PIN_CLK, 0));
        ESP_ERROR_CHECK(gpio_set_level(PT6319_PIN_DAT, (data >> bit) & 1U));
        delay_edge(); // DAT setup time and CLK LOW interval
        ESP_ERROR_CHECK(gpio_set_level(PT6319_PIN_CLK, 1));
        delay_edge(); // Sample on rising edge; data hold / CLK HIGH interval
    }
}

static void pt6319_write_command(uint8_t cmd)
{
    frame_begin();
    pt6319_write_byte(cmd);
    frame_end();
}

static void pt6319_write_ram(uint8_t start_addr, const uint8_t *data, size_t len)
{
    // Bound writes to this experiment's configured window (default 00..0B).
    if (start_addr >= PT6319_RAM_LENGTH || len > PT6319_RAM_LENGTH - start_addr ||
        (len > 0 && data == NULL)) {
        ESP_LOGE(TAG, "Invalid RAM write: addr=0x%02X len=%u",
                 (unsigned)start_addr, (unsigned)len);
        return;
    }
    if (len == 0) {
        return;
    }
    pt6319_write_command(PT6319_CMD_DATA_WRITE_AUTO);
    frame_begin();
    pt6319_write_byte(PT6319_CMD_ADDR_BASE | start_addr);
    for (size_t i = 0; i < len; ++i) {
        pt6319_write_byte(data[i]);
    }
    frame_end();
}

static void require_init(void)
{
    ESP_ERROR_CHECK(initialized ? ESP_OK : ESP_ERR_INVALID_STATE);
}

void pt6319_clear(void)
{
    require_init();
    const uint8_t zeros[PT6319_RAM_LENGTH] = {0};
    pt6319_write_ram(0, zeros, sizeof(zeros));
}

void pt6319_display_on(uint8_t brightness)
{
    require_init();
    if (brightness > PT6319_BRIGHTNESS_MAX) {
        ESP_LOGE(TAG, "Brightness must be 0..7");
        return;
    }
    const uint8_t command = brightness == PT6319_BRIGHTNESS_MAX
        ? PT6319_CMD_DISPLAY_ON_MAX : (PT6319_CMD_DISPLAY_ON_BASE | brightness);
    pt6319_write_command(command);
}

void pt6319_display_off(void)
{
    require_init();
    pt6319_write_command(PT6319_CMD_DISPLAY_OFF);
}

void pt6319_all_on(void)
{
    require_init();
    uint8_t data[PT6319_RAM_LENGTH];
    memset(data, 0xFF, sizeof(data));
    ESP_LOGI(TAG, "STEP 5 write FF x %d", PT6319_RAM_LENGTH);
    pt6319_write_ram(0, data, sizeof(data));
    ESP_LOGI(TAG, "STEP 6 display ON brightness=%d", PT6319_BRIGHTNESS_MAX);
    pt6319_display_on(PT6319_BRIGHTNESS_MAX);
}

esp_err_t pt6319_init(void)
{
    if (initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!GPIO_IS_VALID_OUTPUT_GPIO(PT6319_PIN_CLK) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(PT6319_PIN_STB) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(PT6319_PIN_DAT)) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "STEP 1 GPIO init");
    // Raise STB before enabling the clock/data outputs to establish idle framing.
    ESP_RETURN_ON_ERROR(gpio_set_level(PT6319_PIN_STB, 1), TAG, "STB latch");
    const gpio_config_t stb_config = {
        .pin_bit_mask = 1ULL << PT6319_PIN_STB,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&stb_config), TAG, "STB config");
    ESP_RETURN_ON_ERROR(gpio_set_level(PT6319_PIN_CLK, 1), TAG, "CLK latch");
    ESP_RETURN_ON_ERROR(gpio_set_level(PT6319_PIN_DAT, 0), TAG, "DAT latch");
    gpio_config_t signals = stb_config;
    signals.pin_bit_mask = (1ULL << PT6319_PIN_CLK) | (1ULL << PT6319_PIN_DAT);
    ESP_RETURN_ON_ERROR(gpio_config(&signals), TAG, "CLK/DAT config");
    pt6319_dat_output();
    vTaskDelay(pdMS_TO_TICKS(PT6319_STARTUP_DELAY_MS));

    ESP_LOGI(TAG, "STEP 2 PT6319 init");
    pt6319_write_command(PT6319_DISPLAY_MODE);
    const uint8_t zeros[PT6319_RAM_LENGTH] = {0};
    pt6319_write_ram(0, zeros, sizeof(zeros));
    pt6319_write_command(PT6319_CMD_DISPLAY_ON_MAX);
    initialized = true;
    return ESP_OK;
}

void pt6319_write_frame(const uint8_t ram[PT6319_RAM_BYTES])
{
    require_init();
    pt6319_write_ram(0, ram, PT6319_RAM_BYTES);
}
