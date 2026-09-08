#include "drivers/lp003.h"

#include <stddef.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "[LP003]";

/* Konka SDC251 wiring established by the ct1668-test fixture. */
#define LP003_PIN_STB GPIO_NUM_25
#define LP003_PIN_CLK GPIO_NUM_26
#define LP003_PIN_DIO GPIO_NUM_27
#define LP003_PIN_IR GPIO_NUM_32
#define LP003_PIN_GREEN_LED GPIO_NUM_33

#define LP003_EDGE_DELAY_US 5U
#define LP003_RAM_SIZE 14U
#define LP003_KEY_BYTES 5U
#define LP003_DIGIT_COUNT 4U
#define LP003_KEY_COUNT 7U
#define LP003_LED_COUNT 1U
#define LP003_KEY_POLL_MS 20U
#define LP003_KEY_STABLE_SAMPLES 3U
#define LP003_KEY_EVENT_QUEUE_LENGTH 16U
#define LP003_BLINK_MIN_INTERVAL_MS 100U

#define LP003_RAM_BASE 0xC0U
#define LP003_CMD_MODE_4_GRID 0x00U
#define LP003_CMD_WRITE_AUTO 0x40U
#define LP003_CMD_READ_KEYS 0x42U
#define LP003_CMD_DISPLAY 0x80U
#define LP003_DISPLAY_ENABLE 0x08U

typedef uint8_t lp003_key_mask_t;

typedef struct {
    uint8_t byte;
    uint8_t mask;
    old_panel_key_t key;
} lp003_key_mapping_t;

typedef struct {
    lp003_key_mask_t held;
    lp003_key_mask_t candidate;
    uint8_t matching[LP003_KEY_COUNT];
} lp003_key_state_t;

/*
 * Physical key IDs follow the order established during reverse engineering:
 * MENU, EXIT, OK, VOL-, VOL+, CH-, CH+. Applications only see KEY_1..KEY_7.
 */
static const lp003_key_mapping_t s_key_mapping[LP003_KEY_COUNT] = {
    {1, 0x10, OLD_PANEL_KEY_1},
    {1, 0x08, OLD_PANEL_KEY_2},
    {1, 0x01, OLD_PANEL_KEY_3},
    {0, 0x02, OLD_PANEL_KEY_4},
    {0, 0x01, OLD_PANEL_KEY_5},
    {0, 0x08, OLD_PANEL_KEY_6},
    {0, 0x10, OLD_PANEL_KEY_7},
};

static const uint8_t s_known_key_bits[LP003_KEY_BYTES] = {
    0x1B, 0x19, 0x00, 0x00, 0x00,
};

/* Segment bits a..g are bit 0..6 on each measured digit RAM byte. */
static const uint8_t s_digit_segments[10] = {
    0x3F, /* 0 */
    0x06, /* 1 */
    0x5B, /* 2 */
    0x4F, /* 3 */
    0x66, /* 4 */
    0x6D, /* 5 */
    0x7D, /* 6 */
    0x07, /* 7 */
    0x7F, /* 8 */
    0x6F, /* 9 */
};

static SemaphoreHandle_t s_bus_mutex;
static QueueHandle_t s_key_event_queue;
static TaskHandle_t s_panel_task_handle;
static portMUX_TYPE s_key_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile lp003_key_mask_t s_pressed_keys;
static lp003_key_state_t s_key_state;
static bool s_invalid_key_sample;

static uint8_t s_brightness_percent = 100;
static uint8_t s_brightness_level = 7;
static bool s_blink_enabled;
static bool s_blink_visible = true;
static uint32_t s_blink_interval_ms = 500;
static TickType_t s_blink_phase_start;

static void edge_delay(void)
{
    esp_rom_delay_us(LP003_EDGE_DELAY_US);
}

static esp_err_t frame_begin_locked(void)
{
    esp_err_t err = gpio_set_level(LP003_PIN_CLK, 1);
    if (err != ESP_OK) {
        return err;
    }
    edge_delay();
    err = gpio_set_level(LP003_PIN_STB, 0);
    edge_delay();
    return err;
}

static esp_err_t frame_end_locked(void)
{
    edge_delay();
    const esp_err_t err = gpio_set_level(LP003_PIN_STB, 1);
    edge_delay();
    return err;
}

static esp_err_t write_byte_locked(uint8_t data)
{
    for (unsigned bit = 0; bit < 8U; ++bit) {
        esp_err_t err = gpio_set_level(LP003_PIN_CLK, 0);
        if (err != ESP_OK) {
            return err;
        }
        edge_delay();
        err = gpio_set_level(LP003_PIN_DIO, data & 1U);
        if (err != ESP_OK) {
            return err;
        }
        edge_delay();
        err = gpio_set_level(LP003_PIN_CLK, 1);
        if (err != ESP_OK) {
            return err;
        }
        edge_delay();
        data >>= 1;
    }
    return ESP_OK;
}

static esp_err_t send_command_locked(uint8_t command)
{
    esp_err_t err = frame_begin_locked();
    if (err == ESP_OK) {
        err = write_byte_locked(command);
    }
    const esp_err_t end_err = frame_end_locked();
    return err != ESP_OK ? err : end_err;
}

static esp_err_t write_ram_locked(const uint8_t ram[LP003_RAM_SIZE])
{
    esp_err_t err = send_command_locked(LP003_CMD_WRITE_AUTO);
    if (err != ESP_OK) {
        return err;
    }

    err = frame_begin_locked();
    if (err == ESP_OK) {
        err = write_byte_locked(LP003_RAM_BASE);
    }
    for (unsigned i = 0; i < LP003_RAM_SIZE && err == ESP_OK; ++i) {
        err = write_byte_locked(ram[i]);
    }
    const esp_err_t end_err = frame_end_locked();
    return err != ESP_OK ? err : end_err;
}

static esp_err_t update_display_control_locked(void)
{
    const bool enabled = s_brightness_percent > 0 &&
                         (!s_blink_enabled || s_blink_visible);
    const uint8_t command = LP003_CMD_DISPLAY | s_brightness_level |
                            (enabled ? LP003_DISPLAY_ENABLE : 0U);
    return send_command_locked(command);
}

static void keep_first_error(esp_err_t *err, esp_err_t candidate)
{
    if (*err == ESP_OK && candidate != ESP_OK) {
        *err = candidate;
    }
}

static esp_err_t read_keys_locked(uint8_t keys[LP003_KEY_BYTES])
{
    memset(keys, 0, LP003_KEY_BYTES);

    esp_err_t err = frame_begin_locked();
    if (err == ESP_OK) {
        err = write_byte_locked(LP003_CMD_READ_KEYS);
    }
    if (err == ESP_OK) {
        err = gpio_set_direction(LP003_PIN_DIO, GPIO_MODE_INPUT);
    }
    if (err == ESP_OK) {
        err = gpio_set_pull_mode(LP003_PIN_DIO, GPIO_PULLUP_ONLY);
    }
    edge_delay();

    for (unsigned i = 0; i < LP003_KEY_BYTES && err == ESP_OK; ++i) {
        for (unsigned bit = 0; bit < 8U; ++bit) {
            err = gpio_set_level(LP003_PIN_CLK, 0);
            if (err != ESP_OK) {
                break;
            }
            edge_delay();
            err = gpio_set_level(LP003_PIN_CLK, 1);
            if (err != ESP_OK) {
                break;
            }
            edge_delay();
            keys[i] |= (uint8_t)((unsigned)gpio_get_level(LP003_PIN_DIO)
                                 << bit);
            edge_delay();
        }
    }

    keep_first_error(&err, frame_end_locked());
    keep_first_error(&err, gpio_set_level(LP003_PIN_DIO, 1));
    keep_first_error(
        &err, gpio_set_direction(LP003_PIN_DIO, GPIO_MODE_OUTPUT));
    keep_first_error(&err, gpio_set_pull_mode(LP003_PIN_DIO, GPIO_FLOATING));
    edge_delay();

    if (err != ESP_OK) {
        memset(keys, 0, LP003_KEY_BYTES);
    }
    return err;
}

static esp_err_t lock_bus(void)
{
    if (s_bus_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xSemaphoreTake(s_bus_mutex, portMAX_DELAY) == pdTRUE
               ? ESP_OK
               : ESP_FAIL;
}

static void unlock_bus(void)
{
    xSemaphoreGive(s_bus_mutex);
}

static esp_err_t decode_keys(const uint8_t raw[LP003_KEY_BYTES],
                             lp003_key_mask_t *pressed)
{
    *pressed = 0;
    for (unsigned i = 0; i < LP003_KEY_BYTES; ++i) {
        if ((raw[i] & (uint8_t)~s_known_key_bits[i]) != 0U) {
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    for (unsigned i = 0; i < LP003_KEY_COUNT; ++i) {
        if ((raw[s_key_mapping[i].byte] & s_key_mapping[i].mask) != 0U) {
            *pressed |= (lp003_key_mask_t)(1U << i);
        }
    }
    return ESP_OK;
}

static void reset_pending_key_samples(void)
{
    memset(s_key_state.matching, 0, sizeof(s_key_state.matching));
    s_key_state.candidate = s_key_state.held;
}

static void publish_key_event(unsigned index, bool pressed)
{
    const old_panel_key_event_t event = {
        .key = s_key_mapping[index].key,
        .pressed = pressed,
        .sampled_at_ms = (uint64_t)(esp_timer_get_time() / 1000),
    };
    if (xQueueSend(s_key_event_queue, &event, 0) != pdPASS) {
        old_panel_key_event_t discarded;
        xQueueReceive(s_key_event_queue, &discarded, 0);
        xQueueSend(s_key_event_queue, &event, 0);
        ESP_LOGW(TAG, "key event queue full; dropped oldest event");
    }
}

static esp_err_t update_keys(const uint8_t raw[LP003_KEY_BYTES])
{
    lp003_key_mask_t observed;
    const esp_err_t err = decode_keys(raw, &observed);
    if (err != ESP_OK) {
        reset_pending_key_samples();
        return err;
    }

    lp003_key_mask_t down = 0;
    lp003_key_mask_t up = 0;
    for (unsigned i = 0; i < LP003_KEY_COUNT; ++i) {
        const lp003_key_mask_t key = (lp003_key_mask_t)(1U << i);
        if (s_key_state.matching[i] == 0U ||
            ((s_key_state.candidate ^ observed) & key) != 0U) {
            s_key_state.candidate =
                (lp003_key_mask_t)((s_key_state.candidate & (uint8_t)~key) |
                                   (observed & key));
            s_key_state.matching[i] = 1;
        } else if (s_key_state.matching[i] < LP003_KEY_STABLE_SAMPLES) {
            ++s_key_state.matching[i];
        }

        if (s_key_state.matching[i] == LP003_KEY_STABLE_SAMPLES &&
            ((s_key_state.held ^ observed) & key) != 0U) {
            s_key_state.held ^= key;
            if ((s_key_state.held & key) != 0U) {
                down |= key;
            } else {
                up |= key;
            }
        }
    }

    portENTER_CRITICAL(&s_key_lock);
    s_pressed_keys = s_key_state.held;
    portEXIT_CRITICAL(&s_key_lock);

    for (unsigned i = 0; i < LP003_KEY_COUNT; ++i) {
        const lp003_key_mask_t key = (lp003_key_mask_t)(1U << i);
        if ((down & key) != 0U) {
            publish_key_event(i, true);
        }
        if ((up & key) != 0U) {
            publish_key_event(i, false);
        }
    }
    return ESP_OK;
}

static void report_key_sample_status(esp_err_t err)
{
    if (err != ESP_OK && !s_invalid_key_sample) {
        ESP_LOGW(TAG, "key sample ignored: %s", esp_err_to_name(err));
    } else if (err == ESP_OK && s_invalid_key_sample) {
        ESP_LOGI(TAG, "key samples valid again");
    }
    s_invalid_key_sample = err != ESP_OK;
}

static void lp003_task(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        uint8_t raw[LP003_KEY_BYTES];
        esp_err_t err = lock_bus();
        if (err == ESP_OK) {
            const TickType_t now = xTaskGetTickCount();
            const TickType_t blink_ticks =
                pdMS_TO_TICKS(s_blink_interval_ms);
            if (s_blink_enabled &&
                (TickType_t)(now - s_blink_phase_start) >= blink_ticks) {
                s_blink_visible = !s_blink_visible;
                s_blink_phase_start = now;
                err = update_display_control_locked();
            }
            if (err == ESP_OK) {
                err = read_keys_locked(raw);
            }
            unlock_bus();
        }

        if (err == ESP_OK) {
            err = update_keys(raw);
        } else {
            reset_pending_key_samples();
        }
        report_key_sample_status(err);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(LP003_KEY_POLL_MS));
    }
}

static esp_err_t hardware_init(void)
{
    const gpio_config_t bus_config = {
        .pin_bit_mask = (1ULL << LP003_PIN_STB) |
                        (1ULL << LP003_PIN_CLK) |
                        (1ULL << LP003_PIN_DIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&bus_config);
    if (err != ESP_OK) {
        return err;
    }
    if ((err = gpio_set_level(LP003_PIN_STB, 1)) != ESP_OK ||
        (err = gpio_set_level(LP003_PIN_CLK, 1)) != ESP_OK ||
        (err = gpio_set_level(LP003_PIN_DIO, 1)) != ESP_OK) {
        return err;
    }

    const gpio_config_t ir_config = {
        .pin_bit_mask = 1ULL << LP003_PIN_IR,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&ir_config);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_set_level(LP003_PIN_GREEN_LED, 0);
    if (err != ESP_OK) {
        return err;
    }
    const gpio_config_t led_config = {
        .pin_bit_mask = 1ULL << LP003_PIN_GREEN_LED,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&led_config);
    if (err != ESP_OK) {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    const uint8_t blank[LP003_RAM_SIZE] = {0};
    err = send_command_locked(LP003_CMD_MODE_4_GRID);
    if (err == ESP_OK) {
        err = write_ram_locked(blank);
    }
    if (err == ESP_OK) {
        err = update_display_control_locked();
    }
    return err;
}

static esp_err_t lp003_init(void)
{
    if (s_panel_task_handle != NULL) {
        return ESP_OK;
    }

    s_bus_mutex = xSemaphoreCreateMutex();
    if (s_bus_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_key_event_queue = xQueueCreate(
        LP003_KEY_EVENT_QUEUE_LENGTH, sizeof(old_panel_key_event_t));
    if (s_key_event_queue == NULL) {
        vSemaphoreDelete(s_bus_mutex);
        s_bus_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    memset(&s_key_state, 0, sizeof(s_key_state));
    s_pressed_keys = 0;
    s_invalid_key_sample = false;
    s_brightness_percent = 100;
    s_brightness_level = 7;
    s_blink_enabled = false;
    s_blink_visible = true;
    s_blink_interval_ms = 500;
    s_blink_phase_start = xTaskGetTickCount();

    esp_err_t err = hardware_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "hardware initialization failed: %s",
                 esp_err_to_name(err));
        vQueueDelete(s_key_event_queue);
        s_key_event_queue = NULL;
        vSemaphoreDelete(s_bus_mutex);
        s_bus_mutex = NULL;
        return err;
    }

    const BaseType_t result = xTaskCreate(
        lp003_task, "lp003_panel", 3072, NULL, 6, &s_panel_task_handle);
    if (result != pdPASS) {
        s_panel_task_handle = NULL;
        gpio_set_level(LP003_PIN_GREEN_LED, 0);
        vQueueDelete(s_key_event_queue);
        s_key_event_queue = NULL;
        vSemaphoreDelete(s_bus_mutex);
        s_bus_mutex = NULL;
        ESP_LOGE(TAG, "failed to create panel task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Konka SDC251 ready (CT1668 compatibility mode)");
    return ESP_OK;
}

static esp_err_t lp003_display_value(int value,
                                     bool leading_zeroes,
                                     uint8_t decimal_points)
{
    if (decimal_points != 0U) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (value < 0) {
        value = 0;
    } else if (value > 9999) {
        value = 9999;
    }

    static const int divisors[LP003_DIGIT_COUNT] = {1000, 100, 10, 1};
    uint8_t ram[LP003_RAM_SIZE] = {0};
    for (unsigned i = 0; i < LP003_DIGIT_COUNT; ++i) {
        if (leading_zeroes || value >= divisors[i] ||
            i == LP003_DIGIT_COUNT - 1U) {
            ram[i * 2U] = s_digit_segments[(value / divisors[i]) % 10];
        }
    }

    esp_err_t err = lock_bus();
    if (err == ESP_OK) {
        err = write_ram_locked(ram);
        unlock_bus();
    }
    return err;
}

static esp_err_t lp003_display_blank(void)
{
    const uint8_t ram[LP003_RAM_SIZE] = {0};
    esp_err_t err = lock_bus();
    if (err == ESP_OK) {
        err = write_ram_locked(ram);
        unlock_bus();
    }
    return err;
}

static esp_err_t lp003_set_brightness(uint8_t percent)
{
    if (percent > 100U) {
        percent = 100U;
    }

    esp_err_t err = lock_bus();
    if (err == ESP_OK) {
        s_brightness_percent = percent;
        s_brightness_level = percent == 0U
                                 ? 0U
                                 : (uint8_t)(((unsigned)percent * 8U - 1U) /
                                             100U);
        err = update_display_control_locked();
        unlock_bus();
    }
    return err;
}

static esp_err_t lp003_set_blink(bool enabled, uint32_t interval_ms)
{
    if (interval_ms < LP003_BLINK_MIN_INTERVAL_MS) {
        interval_ms = LP003_BLINK_MIN_INTERVAL_MS;
    }

    esp_err_t err = lock_bus();
    if (err == ESP_OK) {
        s_blink_enabled = enabled;
        s_blink_visible = true;
        s_blink_interval_ms = interval_ms;
        s_blink_phase_start = xTaskGetTickCount();
        err = update_display_control_locked();
        unlock_bus();
    }
    return err;
}

static esp_err_t lp003_set_led(uint8_t index, bool on)
{
    if (index >= LP003_LED_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    return gpio_set_level(LP003_PIN_GREEN_LED, on ? 1U : 0U);
}

static old_panel_key_t lp003_get_key(void)
{
    lp003_key_mask_t pressed;
    portENTER_CRITICAL(&s_key_lock);
    pressed = s_pressed_keys;
    portEXIT_CRITICAL(&s_key_lock);

    for (unsigned i = 0; i < LP003_KEY_COUNT; ++i) {
        if ((pressed & (1U << i)) != 0U) {
            return s_key_mapping[i].key;
        }
    }
    return OLD_PANEL_KEY_NONE;
}

static bool lp003_wait_key_event(old_panel_key_event_t *event,
                                 TickType_t wait_ticks)
{
    return event != NULL && s_key_event_queue != NULL &&
           xQueueReceive(s_key_event_queue, event, wait_ticks) == pdPASS;
}

const old_panel_driver_t old_panel_driver_lp003 = {
    .profile_id = "LP-003",
    .caps = {
        .digits = LP003_DIGIT_COUNT,
        .keys = LP003_KEY_COUNT,
        .leds = LP003_LED_COUNT,
        .has_decimal_point = false,
        .supports_brightness = true,
        .supports_blink = true,
    },
    .init = lp003_init,
    .display_value = lp003_display_value,
    .display_blank = lp003_display_blank,
    .set_brightness = lp003_set_brightness,
    .set_blink = lp003_set_blink,
    .set_led = lp003_set_led,
    .get_key = lp003_get_key,
    .wait_key_event = lp003_wait_key_event,
};
