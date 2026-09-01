#include "drivers/lp001.h"

#include <stddef.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "[LP001]";

/*
 * Old set-top-box panel connector (J1), ESP32-L wiring:
 *
 *   J1 signal  ESP32 GPIO  ESP32-L header label
 *   D1         25          A18 / DAC1
 *   D2         26          A19 / DAC2
 *   D3         32          A4
 *   D4         33          A5
 *   CLK        22          SCL
 *   DATA       21          SDA
 *   LOCK       19          MISO
 *   K0         34          A6 (input only)
 *   IR         35          A7 (input only)
 *   3.3V       3V3
 *   GND        GND
 */
#define LP001_PIN_D1 GPIO_NUM_25
#define LP001_PIN_D2 GPIO_NUM_26
#define LP001_PIN_D3 GPIO_NUM_32
#define LP001_PIN_D4 GPIO_NUM_33
#define LP001_PIN_CLK GPIO_NUM_22
#define LP001_PIN_DATA GPIO_NUM_21
#define LP001_PIN_LOCK GPIO_NUM_19
#define LP001_PIN_K0 GPIO_NUM_34
#define LP001_PIN_IR GPIO_NUM_35

#define LP001_GREEN_LED_ON_LEVEL 1

/*
 * SM310361K-0 is common-anode. The panel digit-select interface is active low,
 * while the 74HC164 cathode outputs light segments at a low level.
 */
#define LP001_SEGMENTS_ACTIVE_HIGH 0
#define LP001_DIGITS_ACTIVE_LOW 1
#define LP001_AUX_BANK_ACTIVE_HIGH 1
#define LP001_SEGMENT_CALIBRATION 0
#define LP001_KEY_SCAN_ENABLED 0
#define LP001_KEY_ADC_CALIBRATION 0
#define LP001_KEY_ACTIVE_SCAN_ENABLED 1
#define LP001_KEY_TEST_DISPLAY 0
#define LP001_INDICATOR_CALIBRATION 0
#define LP001_LOCK_CALIBRATION 0
#define LP001_DISPLAY_API_DEMO 0
#define LP001_DECIMAL_POINT_CALIBRATION 0
#define LP001_IR_ENABLED 0

#define LP001_DIGIT_COUNT 3
#define LP001_SCAN_BANK_COUNT 4
#define LP001_KEY_COUNT 8
#define LP001_SCAN_SLOT_US 2000
#define LP001_KEY_SCAN_PERIOD_MS 20
#define LP001_DEBOUNCE_SAMPLES 3
#define LP001_ADC_CALIBRATION_PERIOD_MS 100
#define LP001_ADC_LOG_PERIOD_MS 1000
#define LP001_ADC_SAMPLE_COUNT 9
#define LP001_ADC_CHANGE_LOG_THRESHOLD 12
#define LP001_KEY_PROBE_SETTLE_US 250
#define LP001_KEY_PROBE_PRESS_THRESHOLD 1500
#define LP001_INDICATOR_ON_MS 1500
#define LP001_INDICATOR_OFF_MS 500
#define LP001_LOCK_CALIBRATION_PHASE_MS 2000
#define LP001_DISPLAY_DEMO_PHASE_MS 3000
#define LP001_DECIMAL_POINT_PHASE_MS 2000
#define LP001_BLINK_MIN_INTERVAL_MS 100
#define LP001_KEY_EVENT_QUEUE_LENGTH 12
#define LP001_LED_RED 0
#define LP001_LED_GREEN 1

typedef enum {
    LP001_KEY_NONE = -1,
    LP001_KEY_MENU = 0,
    LP001_KEY_CHANNEL_DOWN,
    LP001_KEY_CHANNEL_UP,
    LP001_KEY_VOLUME_DOWN,
    LP001_KEY_VOLUME_UP,
    LP001_KEY_OK,
} lp001_key_t;

static const old_panel_caps_t s_lp001_caps = {
    .digits = 3,
    .keys = 6,
    .leds = 2,
    .has_decimal_point = true,
    .has_ir = true,
    .has_card_slot = true,
};

/*
 * Solved from the independent-digit F0/CC/AA photograph plus the hand-drawn
 * result for 123: Q0 is not routed on this PCB; Q1=D, Q2=E, Q3=A, Q4=C,
 * Q5=G, Q6=F, Q7=B.
 */
static const uint8_t s_digit_segments[10] = {
    0xDE, /* 0: A B C D E F */
    0x90, /* 1: B C */
    0xAE, /* 2: A B D E G */
    0xBA, /* 3: A B C D G */
    0xF0, /* 4: B C F G */
    0x7A, /* 5: A C D F G */
    0x7E, /* 6: A C D E F G */
    0x98, /* 7: A B C */
    0xFE, /* 8: A B C D E F G */
    0xFA, /* 9: A B C D F G */
};

static const gpio_num_t s_digit_pins[4] = {
    LP001_PIN_D1,
    LP001_PIN_D2,
    LP001_PIN_D3,
    LP001_PIN_D4,
};

/*
 * Determined by the active-Q probe, in Q0..Q7 order. Q0 and Q5 do not
 * correspond to any of the six front-panel buttons.
 */
static const lp001_key_t s_q_to_key[LP001_KEY_COUNT] = {
    LP001_KEY_NONE,
    LP001_KEY_VOLUME_UP,
    LP001_KEY_MENU,
    LP001_KEY_OK,
    LP001_KEY_CHANNEL_DOWN,
    LP001_KEY_NONE,
    LP001_KEY_VOLUME_DOWN,
    LP001_KEY_CHANNEL_UP,
};

static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_display_segments[LP001_SCAN_BANK_COUNT];
static uint8_t s_brightness_percent = 100;
static bool s_blink_enabled;
static bool s_blink_visible = true;
static TickType_t s_blink_phase_start;
static uint32_t s_blink_interval_ms = 500;
static volatile lp001_key_t s_pressed_key = LP001_KEY_NONE;
static QueueHandle_t s_key_event_queue;
static TaskHandle_t s_scan_task_handle;
static adc_oneshot_unit_handle_t s_kd_adc_handle;
static int s_probe_low;
static int s_probe_high;
static int s_probe_score;

static void display_value_internal(int value, bool leading_zeroes,
                                   uint8_t decimal_points);

static old_panel_key_t lp001_key_to_old_panel_key(lp001_key_t key)
{
    switch (key) {
        case LP001_KEY_MENU:
            return OLD_PANEL_KEY_1;
        case LP001_KEY_CHANNEL_DOWN:
            return OLD_PANEL_KEY_2;
        case LP001_KEY_CHANNEL_UP:
            return OLD_PANEL_KEY_3;
        case LP001_KEY_VOLUME_DOWN:
            return OLD_PANEL_KEY_4;
        case LP001_KEY_VOLUME_UP:
            return OLD_PANEL_KEY_5;
        case LP001_KEY_OK:
            return OLD_PANEL_KEY_6;
        default:
            return OLD_PANEL_KEY_NONE;
    }
}

static const char *key_name(lp001_key_t key)
{
    switch (key) {
        case LP001_KEY_MENU: return "MENU";
        case LP001_KEY_CHANNEL_DOWN: return "CH-";
        case LP001_KEY_CHANNEL_UP: return "CH+";
        case LP001_KEY_VOLUME_DOWN: return "VOL-";
        case LP001_KEY_VOLUME_UP: return "VOL+";
        case LP001_KEY_OK: return "OK";
        default: return "NONE";
    }
}

static void publish_key_event(lp001_key_t key, bool pressed)
{
    if (s_key_event_queue == NULL || key == LP001_KEY_NONE) {
        return;
    }

    const old_panel_key_event_t event = {
        .key = lp001_key_to_old_panel_key(key),
        .pressed = pressed,
    };
    if (xQueueSend(s_key_event_queue, &event, 0) != pdPASS) {
        old_panel_key_event_t discarded;
        xQueueReceive(s_key_event_queue, &discarded, 0);
        xQueueSend(s_key_event_queue, &event, 0);
        ESP_LOGW(TAG, "key event queue full; dropped oldest event");
    }
}

static inline int bank_inactive_level(size_t bank)
{
    if (bank == 3) {
        return LP001_AUX_BANK_ACTIVE_HIGH ? 0 : 1;
    }
    return LP001_DIGITS_ACTIVE_LOW ? 1 : 0;
}

static inline int bank_active_level(size_t bank)
{
    if (bank == 3) {
        return LP001_AUX_BANK_ACTIVE_HIGH ? 1 : 0;
    }
    return LP001_DIGITS_ACTIVE_LOW ? 0 : 1;
}

static void all_digits_off(void)
{
    for (size_t i = 0; i < 4; ++i) {
        gpio_set_level(s_digit_pins[i], bank_inactive_level(i));
    }
}

static void shift_register_write_raw(uint8_t physical)
{
    for (int bit = 7; bit >= 0; --bit) {
        gpio_set_level(LP001_PIN_CLK, 0);
        gpio_set_level(LP001_PIN_DATA, (physical >> bit) & 1U);
        esp_rom_delay_us(1);
        gpio_set_level(LP001_PIN_CLK, 1);
        esp_rom_delay_us(1);
    }
    gpio_set_level(LP001_PIN_CLK, 0);
}

static void shift_register_write(uint8_t value)
{
    shift_register_write_raw(
        LP001_SEGMENTS_ACTIVE_HIGH ? value : (uint8_t)~value);
}

static void display_scan_slot(size_t digit)
{
    uint8_t segments;
    uint8_t brightness;
    bool visible;

    portENTER_CRITICAL(&s_state_lock);
    segments = s_display_segments[digit];
    brightness = digit < LP001_DIGIT_COUNT ? s_brightness_percent : 100;
    visible = digit >= LP001_DIGIT_COUNT || !s_blink_enabled || s_blink_visible;
    portEXIT_CRITICAL(&s_state_lock);

    all_digits_off();
    shift_register_write(segments);
    if (segments == 0 || brightness == 0 || !visible) {
        esp_rom_delay_us(LP001_SCAN_SLOT_US);
        return;
    }

    const uint32_t active_us = (LP001_SCAN_SLOT_US * brightness) / 100;
    gpio_set_level(s_digit_pins[digit], bank_active_level(digit));
    esp_rom_delay_us(active_us);
    gpio_set_level(s_digit_pins[digit], bank_inactive_level(digit));
    if (active_us < LP001_SCAN_SLOT_US) {
        esp_rom_delay_us(LP001_SCAN_SLOT_US - active_us);
    }
}

static uint8_t scan_key_matrix(void)
{
    uint8_t pressed_mask = 0;

    all_digits_off();
    for (int key = 0; key < LP001_KEY_COUNT; ++key) {
        const uint8_t bit = (uint8_t)(1U << key);

        shift_register_write_raw(0x00);
        esp_rom_delay_us(20);
        const int low_sample = gpio_get_level(LP001_PIN_K0);
        shift_register_write_raw(bit);
        esp_rom_delay_us(20);
        const int high_sample = gpio_get_level(LP001_PIN_K0);

        if (low_sample == 0 && high_sample == 1) {
            pressed_mask |= bit;
        }
    }
    shift_register_write_raw(0x00);
    return pressed_mask;
}

static void update_debounced_key(uint8_t raw_mask)
{
    static uint8_t candidate_mask;
    static uint8_t stable_mask;
    static unsigned candidate_count;

    if (raw_mask == candidate_mask) {
        if (candidate_count < LP001_DEBOUNCE_SAMPLES) {
            ++candidate_count;
        }
    } else {
        candidate_mask = raw_mask;
        candidate_count = 1;
    }

    if (candidate_count < LP001_DEBOUNCE_SAMPLES ||
        stable_mask == candidate_mask) {
        return;
    }

    stable_mask = candidate_mask;
    int new_q = -1;
    for (int key = 0; key < LP001_KEY_COUNT; ++key) {
        if (stable_mask & (1U << key)) {
            new_q = key;
            break;
        }
    }

    const lp001_key_t new_key =
        new_q >= 0 ? s_q_to_key[new_q] : LP001_KEY_NONE;
    const lp001_key_t old_key = s_pressed_key;
    s_pressed_key = new_key;
    if (old_key != LP001_KEY_NONE && old_key != new_key) {
        ESP_LOGI(TAG, "key %s released", key_name(old_key));
        publish_key_event(old_key, false);
    }
    if (new_key != LP001_KEY_NONE && new_key != old_key) {
        ESP_LOGI(TAG, "key %s (Q%d) pressed: ADC low=%d high=%d score=%d",
                 key_name(new_key), new_q, s_probe_low, s_probe_high,
                 s_probe_score);
        publish_key_event(new_key, true);
#if LP001_KEY_TEST_DISPLAY
        lp001_display_number((int)lp001_key_to_old_panel_key(new_key) + 1);
#endif
    } else if (new_key == LP001_KEY_NONE && old_key != LP001_KEY_NONE) {
#if LP001_KEY_TEST_DISPLAY
        lp001_display_number(0);
#endif
    }
}

static int read_key_adc_filtered(int *minimum, int *maximum)
{
    int samples[LP001_ADC_SAMPLE_COUNT];

    for (size_t i = 0; i < LP001_ADC_SAMPLE_COUNT; ++i) {
        ESP_ERROR_CHECK(
            adc_oneshot_read(s_kd_adc_handle, ADC_CHANNEL_6, &samples[i]));
    }

    for (size_t i = 1; i < LP001_ADC_SAMPLE_COUNT; ++i) {
        const int value = samples[i];
        size_t position = i;
        while (position > 0 && samples[position - 1] > value) {
            samples[position] = samples[position - 1];
            --position;
        }
        samples[position] = value;
    }

    *minimum = samples[0];
    *maximum = samples[LP001_ADC_SAMPLE_COUNT - 1];
    return samples[LP001_ADC_SAMPLE_COUNT / 2];
}

#if LP001_KEY_ADC_CALIBRATION
static void display_adc_value(int raw)
{
    const int scaled = (raw * 999 + 2047) / 4095;
    uint8_t next[LP001_DIGIT_COUNT] = {
        s_digit_segments[(scaled / 100) % 10],
        s_digit_segments[(scaled / 10) % 10],
        s_digit_segments[scaled % 10],
    };

    portENTER_CRITICAL(&s_state_lock);
    for (size_t i = 0; i < LP001_DIGIT_COUNT; ++i) {
        s_display_segments[i] = next[i];
    }
    portEXIT_CRITICAL(&s_state_lock);
}
#endif

static uint8_t scan_key_outputs_adc(void)
{
    int unused_minimum;
    int unused_maximum;

    all_digits_off();

    shift_register_write_raw(0x00);
    esp_rom_delay_us(LP001_KEY_PROBE_SETTLE_US);
    const int all_low = read_key_adc_filtered(&unused_minimum, &unused_maximum);
    shift_register_write_raw(0xFF);
    esp_rom_delay_us(LP001_KEY_PROBE_SETTLE_US);
    const int all_high = read_key_adc_filtered(&unused_minimum, &unused_maximum);

    if ((all_high - all_low) < LP001_KEY_PROBE_PRESS_THRESHOLD) {
        return 0;
    }

    int best_key = -1;
    int best_low = 0;
    int best_high = 0;
    int best_score = LP001_KEY_PROBE_PRESS_THRESHOLD;

    for (int key = 0; key < LP001_KEY_COUNT; ++key) {
        const uint8_t bit = (uint8_t)(1U << key);

        shift_register_write_raw((uint8_t)~bit);
        esp_rom_delay_us(LP001_KEY_PROBE_SETTLE_US);
        const int low = read_key_adc_filtered(&unused_minimum, &unused_maximum);

        shift_register_write_raw(bit);
        esp_rom_delay_us(LP001_KEY_PROBE_SETTLE_US);
        const int high = read_key_adc_filtered(&unused_minimum, &unused_maximum);
        const int score = high - low;

        if (score > best_score) {
            best_key = key;
            best_low = low;
            best_high = high;
            best_score = score;
        }
    }

    shift_register_write_raw(0xFF);
    if (best_key < 0) {
        return 0;
    }

    s_probe_low = best_low;
    s_probe_high = best_high;
    s_probe_score = best_score;
    return (uint8_t)(1U << best_key);
}

static void lp001_hardware_init(void)
{
    const uint64_t output_mask =
        (1ULL << LP001_PIN_D1) | (1ULL << LP001_PIN_D2) |
        (1ULL << LP001_PIN_D3) | (1ULL << LP001_PIN_D4) |
        (1ULL << LP001_PIN_CLK) | (1ULL << LP001_PIN_DATA) |
        (1ULL << LP001_PIN_LOCK);
    gpio_config_t output_config = {
        .pin_bit_mask = output_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&output_config));

    gpio_config_t input_config = {
        .pin_bit_mask = (1ULL << LP001_PIN_K0) | (1ULL << LP001_PIN_IR),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&input_config));

    adc_oneshot_unit_init_cfg_t adc_unit_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_unit_config, &s_kd_adc_handle));
    adc_oneshot_chan_cfg_t adc_channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(
        s_kd_adc_handle, ADC_CHANNEL_6, &adc_channel_config));

#if LP001_IR_ENABLED
    /*
     * IR is intentionally outside the Old Panel SDK v0.1 API. The original
     * experiment kept this path disabled until receiver power was repaired.
     */
#else
    ESP_LOGI(TAG, "IR receiver disabled until panel receiver power is repaired");
#endif

    all_digits_off();
    gpio_set_level(LP001_PIN_CLK, 0);
    gpio_set_level(LP001_PIN_DATA, 0);
    gpio_set_level(LP001_PIN_LOCK, LP001_GREEN_LED_ON_LEVEL);
    shift_register_write(0x00);

    ESP_LOGI(TAG,
             "ready: D1..D4=%d/%d/%d/%d CLK=%d DATA=%d LOCK=%d K0=%d IR=%d",
             LP001_PIN_D1, LP001_PIN_D2, LP001_PIN_D3, LP001_PIN_D4,
             LP001_PIN_CLK, LP001_PIN_DATA, LP001_PIN_LOCK, LP001_PIN_K0,
             LP001_PIN_IR);
}

static void lp001_task(void *arg)
{
    (void)arg;
    lp001_hardware_init();
#if LP001_SEGMENT_CALIBRATION
    portENTER_CRITICAL(&s_state_lock);
    s_display_segments[0] = 0xF0;
    s_display_segments[1] = 0xCC;
    s_display_segments[2] = 0xAA;
    s_display_segments[3] = 0x00;
    portEXIT_CRITICAL(&s_state_lock);
    ESP_LOGI(TAG, "segment calibration pattern: left=F0 middle=CC right=AA");
#else
#if LP001_KEY_TEST_DISPLAY
    lp001_display_number(0);
#else
    lp001_display_number_padded(0);
#endif
    portENTER_CRITICAL(&s_state_lock);
    s_display_segments[3] = 0x00;
    portEXIT_CRITICAL(&s_state_lock);
#endif

    TickType_t last_key_scan = xTaskGetTickCount();
#if LP001_DECIMAL_POINT_CALIBRATION
    TickType_t decimal_point_phase_start = last_key_scan;
    int decimal_point_phase = 0;
    lp001_set_blink(false, 0);
    lp001_set_brightness(100);
    display_value_internal(111, true, 0x04);
    ESP_LOGI(TAG, "decimal-point calibration: left DP");
#endif
#if LP001_DISPLAY_API_DEMO
    TickType_t display_demo_phase_start = last_key_scan;
    int display_demo_phase = 0;
    lp001_set_blink(false, 0);
    lp001_set_brightness(100);
    display_value_internal(7, false, 0);
    ESP_LOGI(TAG, "display demo: 7 (leading zeroes blanked)");
#endif
#if LP001_LOCK_CALIBRATION
    TickType_t lock_phase_start = last_key_scan;
    bool lock_high = true;
    lp001_display_number(111);
    portENTER_CRITICAL(&s_state_lock);
    s_display_segments[3] = 0x00;
    portEXIT_CRITICAL(&s_state_lock);
    ESP_LOGI(TAG, "LOCK calibration: display 111 means high; blank means low");
#endif
#if LP001_INDICATOR_CALIBRATION
    TickType_t indicator_phase_start = last_key_scan;
    int indicator_q = 0;
    bool indicator_on = true;
    lp001_display_number(1);
    portENTER_CRITICAL(&s_state_lock);
    s_display_segments[3] = 1U << indicator_q;
    portEXIT_CRITICAL(&s_state_lock);
    ESP_LOGI(TAG, "indicator calibration: display=1 tests Q0");
#endif
#if LP001_KEY_ADC_CALIBRATION
    TickType_t last_adc_sample = last_key_scan;
    TickType_t last_adc_log = last_key_scan;
    int last_logged_adc = -4096;
    ESP_LOGI(TAG,
             "ADC key calibration: display=round(raw*999/4095), serial=raw 0..4095");
#endif
#if LP001_KEY_ACTIVE_SCAN_ENABLED
    ESP_LOGI(TAG,
             "active key scan: KEY1=MENU KEY2=CH- KEY3=CH+ KEY4=VOL- KEY5=VOL+ KEY6=OK");
#endif

    while (true) {
#if LP001_LOCK_CALIBRATION
        if (lock_high) {
            for (size_t bank = 0; bank < LP001_SCAN_BANK_COUNT; ++bank) {
                display_scan_slot(bank);
            }
        } else {
            all_digits_off();
        }
#else
        for (size_t bank = 0; bank < LP001_SCAN_BANK_COUNT; ++bank) {
            display_scan_slot(bank);
        }
#endif

        vTaskDelay(1);

        const TickType_t now = xTaskGetTickCount();
        portENTER_CRITICAL(&s_state_lock);
        if (s_blink_enabled &&
            (now - s_blink_phase_start) >= pdMS_TO_TICKS(s_blink_interval_ms)) {
            s_blink_visible = !s_blink_visible;
            s_blink_phase_start = now;
        }
        portEXIT_CRITICAL(&s_state_lock);

#if LP001_DECIMAL_POINT_CALIBRATION
        if ((now - decimal_point_phase_start) >=
            pdMS_TO_TICKS(LP001_DECIMAL_POINT_PHASE_MS)) {
            decimal_point_phase_start = now;
            decimal_point_phase = (decimal_point_phase + 1) % 4;

            switch (decimal_point_phase) {
                case 0:
                    display_value_internal(111, true, 0x04);
                    ESP_LOGI(TAG, "decimal-point calibration: left DP");
                    break;
                case 1:
                    display_value_internal(222, true, 0x02);
                    ESP_LOGI(TAG, "decimal-point calibration: middle DP");
                    break;
                case 2:
                    display_value_internal(333, true, 0x01);
                    ESP_LOGI(TAG, "decimal-point calibration: right DP");
                    break;
                default:
                    lp001_display_blank();
                    display_value_internal(0, false, 0x07);
                    ESP_LOGI(TAG, "decimal-point calibration: all DP, digits blank");
                    break;
            }
        }
#endif
#if LP001_DISPLAY_API_DEMO
        if ((now - display_demo_phase_start) >=
            pdMS_TO_TICKS(LP001_DISPLAY_DEMO_PHASE_MS)) {
            display_demo_phase_start = now;
            display_demo_phase = (display_demo_phase + 1) % 5;
            lp001_set_blink(false, 0);
            lp001_set_brightness(100);

            switch (display_demo_phase) {
                case 0:
                    display_value_internal(7, false, 0);
                    ESP_LOGI(TAG, "display demo: 7 (leading zeroes blanked)");
                    break;
                case 1:
                    lp001_display_number_padded(7);
                    ESP_LOGI(TAG, "display demo: 007 (leading zeroes shown)");
                    break;
                case 2:
                    display_value_internal(123, true, 0x02);
                    ESP_LOGI(TAG, "display demo: 12.3 (middle decimal point)");
                    break;
                case 3:
                    lp001_display_number_padded(888);
                    lp001_set_brightness(25);
                    ESP_LOGI(TAG, "display demo: 888 at 25%% brightness");
                    break;
                default:
                    lp001_display_number_padded(456);
                    lp001_set_blink(true, 400);
                    ESP_LOGI(TAG, "display demo: blinking 456");
                    break;
            }
        }
#endif
#if LP001_LOCK_CALIBRATION
        if ((now - lock_phase_start) >=
            pdMS_TO_TICKS(LP001_LOCK_CALIBRATION_PHASE_MS)) {
            lock_phase_start = now;
            all_digits_off();
            lock_high = !lock_high;
            gpio_set_level(LP001_PIN_LOCK, lock_high ? 1 : 0);
            ESP_LOGI(TAG, "LOCK calibration level=%d", lock_high ? 1 : 0);
        }
#endif
#if LP001_INDICATOR_CALIBRATION
        const TickType_t indicator_phase_ms = pdMS_TO_TICKS(
            indicator_on ? LP001_INDICATOR_ON_MS : LP001_INDICATOR_OFF_MS);
        if ((now - indicator_phase_start) >= indicator_phase_ms) {
            indicator_phase_start = now;
            if (indicator_on) {
                indicator_on = false;
                portENTER_CRITICAL(&s_state_lock);
                s_display_segments[3] = 0x00;
                portEXIT_CRITICAL(&s_state_lock);
            } else {
                indicator_q = (indicator_q + 1) % 8;
                indicator_on = true;
                lp001_display_number(indicator_q + 1);
                portENTER_CRITICAL(&s_state_lock);
                s_display_segments[3] = (uint8_t)(1U << indicator_q);
                portEXIT_CRITICAL(&s_state_lock);
                ESP_LOGI(TAG, "indicator calibration: display=%d tests Q%d",
                         indicator_q + 1, indicator_q);
            }
        }
#endif
#if LP001_KEY_ADC_CALIBRATION
        if ((now - last_adc_sample) >=
            pdMS_TO_TICKS(LP001_ADC_CALIBRATION_PERIOD_MS)) {
            last_adc_sample = now;
            int minimum;
            int maximum;
            const int raw = read_key_adc_filtered(&minimum, &maximum);
            display_adc_value(raw);

            const int delta = raw >= last_logged_adc
                                  ? raw - last_logged_adc
                                  : last_logged_adc - raw;
            if (delta >= LP001_ADC_CHANGE_LOG_THRESHOLD ||
                (now - last_adc_log) >= pdMS_TO_TICKS(LP001_ADC_LOG_PERIOD_MS)) {
                ESP_LOGI(TAG, "KD ADC raw=%d min=%d max=%d display=%03d",
                         raw, minimum, maximum, (raw * 999 + 2047) / 4095);
                last_logged_adc = raw;
                last_adc_log = now;
            }
        }
#endif
        if ((now - last_key_scan) >= pdMS_TO_TICKS(LP001_KEY_SCAN_PERIOD_MS)) {
            last_key_scan = now;
            if (LP001_KEY_SCAN_ENABLED) {
                update_debounced_key(scan_key_matrix());
            }
#if LP001_KEY_ACTIVE_SCAN_ENABLED
            update_debounced_key(scan_key_outputs_adc());
#endif
        }
    }
}

esp_err_t lp001_init(void)
{
    if (s_scan_task_handle != NULL) {
        return ESP_OK;
    }

    if (s_key_event_queue == NULL) {
        s_key_event_queue = xQueueCreate(
            LP001_KEY_EVENT_QUEUE_LENGTH, sizeof(old_panel_key_event_t));
        if (s_key_event_queue == NULL) {
            ESP_LOGE(TAG, "failed to create key event queue");
            return ESP_ERR_NO_MEM;
        }
    }

    const BaseType_t result = xTaskCreate(
        lp001_task, "lp001_panel", 3072, NULL, 6, &s_scan_task_handle);
    if (result != pdPASS) {
        s_scan_task_handle = NULL;
        ESP_LOGE(TAG, "failed to create scan task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t lp001_get_capabilities(old_panel_caps_t *caps)
{
    if (caps == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(caps, &s_lp001_caps, sizeof(*caps));
    return ESP_OK;
}

esp_err_t lp001_display_number(int value)
{
    display_value_internal(value, false, 0);
    return ESP_OK;
}

esp_err_t lp001_display_number_padded(int value)
{
    display_value_internal(value, true, 0);
    return ESP_OK;
}

static void display_value_internal(int value, bool leading_zeroes,
                                   uint8_t decimal_points)
{
    if (value < 0) {
        value = 0;
    } else if (value > 999) {
        value = 999;
    }

    uint8_t next[LP001_DIGIT_COUNT] = {0};
    next[2] = s_digit_segments[value % 10];
    next[1] = (leading_zeroes || value >= 10)
                  ? s_digit_segments[(value / 10) % 10]
                  : 0;
    next[0] = (leading_zeroes || value >= 100)
                  ? s_digit_segments[(value / 100) % 10]
                  : 0;

    for (size_t i = 0; i < LP001_DIGIT_COUNT; ++i) {
        const uint8_t point_bit = (uint8_t)(1U << (LP001_DIGIT_COUNT - 1 - i));
        if (decimal_points & point_bit) {
            next[i] |= 0x01;
        }
    }

    portENTER_CRITICAL(&s_state_lock);
    for (size_t i = 0; i < LP001_DIGIT_COUNT; ++i) {
        s_display_segments[i] = next[i];
    }
    portEXIT_CRITICAL(&s_state_lock);
}

esp_err_t lp001_display_blank(void)
{
    portENTER_CRITICAL(&s_state_lock);
    for (size_t i = 0; i < LP001_DIGIT_COUNT; ++i) {
        s_display_segments[i] = 0;
    }
    portEXIT_CRITICAL(&s_state_lock);
    return ESP_OK;
}

esp_err_t lp001_set_brightness(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }

    portENTER_CRITICAL(&s_state_lock);
    s_brightness_percent = percent;
    portEXIT_CRITICAL(&s_state_lock);
    return ESP_OK;
}

esp_err_t lp001_set_blink(bool enabled, uint32_t interval_ms)
{
    const TickType_t now = xTaskGetTickCount();
    if (interval_ms < LP001_BLINK_MIN_INTERVAL_MS) {
        interval_ms = LP001_BLINK_MIN_INTERVAL_MS;
    }

    portENTER_CRITICAL(&s_state_lock);
    s_blink_enabled = enabled;
    s_blink_visible = true;
    s_blink_interval_ms = interval_ms;
    s_blink_phase_start = now;
    portEXIT_CRITICAL(&s_state_lock);
    return ESP_OK;
}

esp_err_t lp001_set_led(uint8_t index, bool on)
{
    if (index >= s_lp001_caps.leds) {
        return ESP_ERR_INVALID_ARG;
    }

    if (index == LP001_LED_RED) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (index == LP001_LED_GREEN) {
        gpio_set_level(LP001_PIN_LOCK, on ? LP001_GREEN_LED_ON_LEVEL
                                          : !LP001_GREEN_LED_ON_LEVEL);
        return ESP_OK;
    }

    return ESP_ERR_INVALID_ARG;
}

old_panel_key_t lp001_get_key(void)
{
    return lp001_key_to_old_panel_key(s_pressed_key);
}

bool lp001_wait_key_event(
    old_panel_key_event_t *event,
    TickType_t wait_ticks
)
{
    return event != NULL && s_key_event_queue != NULL &&
           xQueueReceive(s_key_event_queue, event, wait_ticks) == pdPASS;
}
