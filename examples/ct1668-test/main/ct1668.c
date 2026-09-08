#include "ct1668.h"

#include <stdbool.h>
#include <string.h>
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "CT1668";
static bool initialized;
static bool display_enabled;
static uint8_t brightness;

static void edge_delay(void)
{
    /* IDF ROM microsecond delay; no CPU-cycle guessing. Scheduling may
     * lengthen intervals. This slow diagnostic driver is not hard realtime. */
    esp_rom_delay_us(CT1668_EDGE_DELAY_US);
}

static void pin_set(gpio_num_t pin, uint32_t level)
{
    ESP_ERROR_CHECK(gpio_set_level(pin, level));
}

static void frame_begin(void)
{
    pin_set(CT1668_GPIO_CLK, 1);
    edge_delay();
    pin_set(CT1668_GPIO_STB, 0); /* Falling STB starts a transaction. */
    edge_delay();
}

static void frame_end(void)
{
    edge_delay();
    pin_set(CT1668_GPIO_STB, 1); /* Rising STB ends the transaction. */
    edge_delay();
}

void ct1668_write_byte(uint8_t data)
{
    for (unsigned bit = 0; bit < 8U; ++bit) {
        pin_set(CT1668_GPIO_CLK, 0);
        edge_delay();
        pin_set(CT1668_GPIO_DIO, data & 1U);
        edge_delay(); /* Data setup before the sampling rising edge. */
        pin_set(CT1668_GPIO_CLK, 1);
        edge_delay(); /* Data hold; leave CLK high after the last bit. */
        data >>= 1;
    }
}

void ct1668_send_command(uint8_t command)
{
    frame_begin();
    ct1668_write_byte(command);
    frame_end();
}

void ct1668_write_ram(const uint8_t ram[CT1668_RAM_SIZE])
{
    if (ram == NULL) {
        ESP_LOGE(TAG, "NULL RAM buffer");
        return;
    }
    /* Restore write mode even if the previous transaction read keys. */
    ct1668_send_command(CT1668_CMD_WRITE_AUTO);
    frame_begin();
    ct1668_write_byte(CT1668_RAM_BASE);
    for (unsigned i = 0; i < CT1668_RAM_SIZE; ++i) {
        ct1668_write_byte(ram[i]); /* C0..CD only; never write CE or beyond. */
    }
    frame_end();
}

void ct1668_clear(void)
{
    const uint8_t ram[CT1668_RAM_SIZE] = {0};
    ct1668_write_ram(ram);
}

static void update_display_control(void)
{
    ct1668_send_command((uint8_t)(CT1668_CMD_DISPLAY | brightness |
                        (display_enabled ? CT1668_DISPLAY_ENABLE : 0U)));
}

void ct1668_set_brightness(uint8_t level)
{
    if (level > 7U) {
        ESP_LOGW(TAG, "Brightness %u clamped to 7", (unsigned)level);
        level = 7U;
    }
    brightness = level;
    update_display_control(); /* Preserve the current display on/off state. */
}

void ct1668_display_on(void)
{
    display_enabled = true;
    update_display_control();
}

void ct1668_display_off(void)
{
    display_enabled = false;
    update_display_control();
}

void ct1668_all_segments_on(void)
{
    uint8_t ram[CT1668_RAM_SIZE];
    memset(ram, 0xFF, sizeof(ram));
    ct1668_write_ram(ram);
}

esp_err_t ct1668_init(void)
{
    initialized = false;
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << CT1668_GPIO_STB) |
                        (1ULL << CT1668_GPIO_CLK) |
                        (1ULL << CT1668_GPIO_DIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_set_level(CT1668_GPIO_STB, 1);
    if (err != ESP_OK) { return err; }
    err = gpio_set_level(CT1668_GPIO_CLK, 1);
    if (err != ESP_OK) { return err; }
    err = gpio_set_level(CT1668_GPIO_DIO, 1);
    if (err != ESP_OK) { return err; }
    vTaskDelay(pdMS_TO_TICKS(100));

    /* CT1668 compatibility assumption: command meanings follow TM1668.
     * clear() sends 40, then C0 + all 14 zero bytes, BEFORE enabling display.
     * No display-ON command is issued before this RAM clear completes. */
    ct1668_send_command(CT1668_CMD_MODE_4_GRID);
    ct1668_clear();
    brightness = 0;
    ct1668_display_on(); /* 0x88: display ON, lowest brightness. */
    initialized = true;
    return ESP_OK;
}

esp_err_t ct1668_read_keys(uint8_t keys[CT1668_KEY_BYTES])
{
    if (keys == NULL) { return ESP_ERR_INVALID_ARG; }
    memset(keys, 0, CT1668_KEY_BYTES);
    if (!initialized) { return ESP_ERR_INVALID_STATE; }

    frame_begin();
    ct1668_write_byte(CT1668_CMD_READ_KEYS);
    /* CT1668 compatibility assumption: TM1668-style open-drain key reply.
     * Keep STB LOW across command and all 40 read clocks. Release DIO
     * before the first falling CLK. Internal pull-up assists initial testing;
     * TM1668 specifies an external 1k..10k pull-up (10k recommended). Verify
     * the panel's existing DIO pull-up/rise time if readings are unstable. */
    esp_err_t err = gpio_set_direction(CT1668_GPIO_DIO, GPIO_MODE_INPUT);
    if (err == ESP_OK) {
        err = gpio_set_pull_mode(CT1668_GPIO_DIO, GPIO_PULLUP_ONLY);
    }
    edge_delay(); /* 5 us turnaround, exceeding the required >= 2 us. */
    if (err == ESP_OK) {
        for (unsigned i = 0; i < CT1668_KEY_BYTES; ++i) {
            for (unsigned bit = 0; bit < 8U; ++bit) {
                pin_set(CT1668_GPIO_CLK, 0);
                /* Slave changes its open-drain output after falling CLK.
                 * Wait through LOW, raise CLK, then sample in the HIGH phase
                 * after another 5us. Do not sample on the changing edge. */
                edge_delay();
                pin_set(CT1668_GPIO_CLK, 1);
                edge_delay();
                keys[i] |= (uint8_t)((unsigned)gpio_get_level(CT1668_GPIO_DIO)
                                     << bit);
                edge_delay(); /* Hold HIGH before the next falling edge. */
            }
        }
    }
    /* End the read BEFORE driving DIO again, including on error paths. */
    frame_end();
    pin_set(CT1668_GPIO_DIO, 1); /* Preload the output latch while still input. */
    esp_err_t restore = gpio_set_direction(CT1668_GPIO_DIO, GPIO_MODE_OUTPUT);
    esp_err_t pull = gpio_set_pull_mode(CT1668_GPIO_DIO, GPIO_FLOATING);
    edge_delay();
    if (err == ESP_OK) { err = restore; }
    if (err == ESP_OK) { err = pull; }
    if (err != ESP_OK) {
        memset(keys, 0, CT1668_KEY_BYTES);
        ESP_LOGE(TAG, "Key read GPIO error: %s", esp_err_to_name(err));
    }
    return err; /* No ACK exists: ESP_OK is not proof of chip compatibility. */
}
