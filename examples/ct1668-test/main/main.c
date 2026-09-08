#include "ct1668.h"
#include "ct1668_key_test.h"
#include "sdc251_panel.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define ENABLE_KEY_TEST 1
/* Diagnostic mode takes priority over the manual/automatic scan selection. */
#define ENABLE_HARDWARE_DIAG 0
#define ENABLE_MANUAL_SCAN 0
#define DIAG_HOLD_MS 8000U
#define SCAN_STATE_COUNT (CT1668_RAM_SIZE * 8U)

static const char *TAG = "CT1668";

static void show_scan_state(unsigned index)
{
    const unsigned addr = index / 8U;
    const unsigned bit = index % 8U;
    uint8_t ram[CT1668_RAM_SIZE] = {0};
    ct1668_clear();
    ram[addr] = (uint8_t)(1U << bit);
    ct1668_write_ram(ram);
    ESP_LOGI(TAG, "[SCAN] addr=%02X bit=%u value=0x%02X",
             (unsigned)(CT1668_RAM_BASE + addr), bit, (unsigned)ram[addr]);
    ESP_LOGI(TAG, "[MANUAL] step=%u/%u HOLD (waiting for key)",
             index + 1U, (unsigned)SCAN_STATE_COUNT);
}

static void manual_help(void)
{
    ESP_LOGI(TAG, "[MANUAL] n/SPACE=next, p=previous, r=redraw, 0=first, c=clear, h=help");
    ESP_LOGI(TAG, "[MANUAL] Keys act immediately; no Enter needed. No auto advance or wrap.");
}

static void run_manual_scan(void)
{
    /* Reuse the existing console UART/pins/baud (UART0 on this ESP32).
     * RX blocks in the app_main task; no additional application task/queue.
     * Leave the existing console logging path intact. */
    const uart_port_t console = (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM;
    if (!uart_is_driver_installed(console)) {
        ESP_ERROR_CHECK(uart_driver_install(console, 1024, 0, 0, NULL, 0));
    }
    unsigned index = 0;
    manual_help();
    show_scan_state(index);
    for (;;) {
        uint8_t key;
        const int received = uart_read_bytes(console, &key, 1, pdMS_TO_TICKS(100));
        if (received < 0) {
            ESP_LOGE(TAG, "[MANUAL] Console read failed");
            ct1668_clear();
            return;
        }
        if (received == 0) {
            continue; /* Timeout yields through the UART driver; keep RAM unchanged. */
        }
        switch (key) {
        case 'n':
        case ' ':
            if (index + 1U < SCAN_STATE_COUNT) {
                ++index;
            } else {
                ESP_LOGW(TAG, "[MANUAL] Last state; use p or 0. Display remains here.");
            }
            show_scan_state(index);
            break;
        case 'p':
            if (index > 0U) {
                --index;
            } else {
                ESP_LOGW(TAG, "[MANUAL] First state; display remains here.");
            }
            show_scan_state(index);
            break;
        case 'r':
            show_scan_state(index);
            break;
        case '0':
            index = 0;
            show_scan_state(index);
            break;
        case 'c':
            ct1668_clear();
            ESP_LOGI(TAG, "[MANUAL] CLEARED; selection kept at step=%u/%u. r=redraw.",
                     index + 1U, (unsigned)SCAN_STATE_COUNT);
            break;
        case 'h':
        case '?':
            manual_help();
            break;
        default:
            break; /* Ignore CR/LF, so terminal line endings never advance twice. */
        }
    }
}

static void diagnostic_hold(int stb, int clk, int dio)
{
    /* Change CLK/DIO only while STB is high. The STB-low test has no
     * clock edges, so it does not send a data byte under the TM1668 assumption.
     * Exit a previous STB-low hold before changing any other signal. */
    ESP_ERROR_CHECK(gpio_set_level(CT1668_GPIO_STB, 1));
    esp_rom_delay_us(CT1668_EDGE_DELAY_US);
    ESP_ERROR_CHECK(gpio_set_level(CT1668_GPIO_CLK, (uint32_t)clk));
    ESP_ERROR_CHECK(gpio_set_level(CT1668_GPIO_DIO, (uint32_t)dio));
    esp_rom_delay_us(CT1668_EDGE_DELAY_US);
    ESP_ERROR_CHECK(gpio_set_level(CT1668_GPIO_STB, (uint32_t)stb));
    esp_rom_delay_us(CT1668_EDGE_DELAY_US);
    ESP_LOGI(TAG, "[DIAG] HOLD 8s: expected STB=%d CLK=%d DIO=%d",
             stb, clk, dio);
    /* Input/output mode reads ESP32 pads only. It cannot verify PCB wiring. */
    const int actual_stb = gpio_get_level(CT1668_GPIO_STB);
    const int actual_clk = gpio_get_level(CT1668_GPIO_CLK);
    const int actual_dio = gpio_get_level(CT1668_GPIO_DIO);
    ESP_LOGI(TAG, "[DIAG] ESP32 pad readback: STB=%d CLK=%d DIO=%d",
             actual_stb, actual_clk, actual_dio);
    if (actual_stb != stb || actual_clk != clk || actual_dio != dio) {
        ESP_LOGE(TAG, "[DIAG] Pad mismatch: inspect loading, wiring and GPIO");
    }
    vTaskDelay(pdMS_TO_TICKS(DIAG_HOLD_MS));
}

static void run_hardware_diagnostic(void)
{
    ESP_LOGW(TAG, "[DIAG] Repeated init + clear/FF + meter holds; brightness 0");
    for (;;) {
        /* Repeat the complete initialization to recover if the independently
         * powered panel missed the original mode/display-ON commands. */
        ESP_LOGI(TAG, "[DIAG] REINIT: 00 | 40 | C0 + 14*00 | 88");
        ESP_ERROR_CHECK(ct1668_init());
        ESP_LOGI(TAG, "[DIAG] RAM CLEAR: hold 2s");
        vTaskDelay(pdMS_TO_TICKS(2000));
        ct1668_all_segments_on();
        ct1668_display_on(); /* Resend 88 after FF, still lowest brightness. */
        ESP_LOGI(TAG, "[DIAG] RAM FF, display ON, brightness 0: hold 5s");
        vTaskDelay(pdMS_TO_TICKS(5000));
        ct1668_clear();

        ESP_ERROR_CHECK(gpio_set_direction(CT1668_GPIO_STB, GPIO_MODE_INPUT_OUTPUT));
        ESP_ERROR_CHECK(gpio_set_direction(CT1668_GPIO_CLK, GPIO_MODE_INPUT_OUTPUT));
        ESP_ERROR_CHECK(gpio_set_direction(CT1668_GPIO_DIO, GPIO_MODE_INPUT_OUTPUT));
        diagnostic_hold(1, 1, 1);
        diagnostic_hold(1, 1, 0);
        diagnostic_hold(1, 0, 1);
        diagnostic_hold(0, 1, 1);
        ESP_ERROR_CHECK(gpio_set_level(CT1668_GPIO_STB, 1));
        esp_rom_delay_us(CT1668_EDGE_DELAY_US);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "CT1668/TM1668 compatibility test");
    ESP_LOGW(TAG, "CT1668 compatibility assumption; SDC251 mappings follow measured board data");
    ESP_LOGI(TAG, "STB GPIO%d", (int)CT1668_GPIO_STB);
    ESP_LOGI(TAG, "CLK GPIO%d", (int)CT1668_GPIO_CLK);
    ESP_LOGI(TAG, "DIO GPIO%d", (int)CT1668_GPIO_DIO);
    ESP_LOGI(TAG, "display mode: 4 GRID / 13 SEG");
    ESP_LOGI(TAG, "brightness: 0");

    esp_err_t err = sdc251_panel_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Panel GPIO initialization failed: %s", esp_err_to_name(err));
        return;
    }
    err = ct1668_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Initialization failed: %s", esp_err_to_name(err));
        return;
    }

    if (ENABLE_KEY_TEST) {
        ct1668_key_test_run();
        return;
    }

    if (ENABLE_HARDWARE_DIAG) {
        run_hardware_diagnostic();
        return;
    }

    if (ENABLE_MANUAL_SCAN) {
        run_manual_scan();
        return;
    }

    ct1668_clear();
    ESP_LOGI(TAG, "[CT1668] TEST A: RAM clear");
    vTaskDelay(pdMS_TO_TICKS(2000));

    ct1668_set_brightness(0);
    ct1668_all_segments_on();
    ESP_LOGI(TAG, "[CT1668] TEST B: all RAM = FF");
    vTaskDelay(pdMS_TO_TICKS(5000));

    /* A/B run once. Repeat only C, with five seconds of blanking per pass. */
    for (;;) {
        for (unsigned addr = 0; addr < CT1668_RAM_SIZE; ++addr) {
            for (unsigned bit = 0; bit < 8U; ++bit) {
                uint8_t ram[CT1668_RAM_SIZE] = {0};
                ct1668_clear(); /* Physically clear all 14 bytes first. */
                ram[addr] = (uint8_t)(1U << bit);
                ct1668_write_ram(ram);
                ESP_LOGI(TAG, "[SCAN] addr=%02X bit=%u value=0x%02X",
                         (unsigned)(CT1668_RAM_BASE + addr), bit,
                         (unsigned)ram[addr]);
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }
        ct1668_clear();
        ESP_LOGI(TAG, "[CT1668] segment scan complete");

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
