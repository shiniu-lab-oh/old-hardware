#include "pt6312.h"
#include "vfd_mapper.h"
#include "vfd_demo.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef enum {
    PT_TEST_ALL_ON,
    PT_TEST_ALL_OFF,
    PT_TEST_MAPPER,
    PT_TEST_SELF_TEST,
    PT_TEST_TIME,
    PT_TEST_STABILITY,
} pt_test_mode_t;

// All startup modes retain serial controls. Default: static 12:34, no clock service.
static const pt_test_mode_t TEST_MODE = PT_TEST_TIME;
static const char *TAG = "PT6312_TEST";

static void handle_command(char command)
{
    if (command == '\r' || command == '\n' || command == ' ' || command == '\t') {
        return;
    }
    if (command == '?') {
        vfd_mapper_print_help();
        vfd_demo_print_help();
        return;
    }
    // Stop only on recognized commands; raw tools and Renderer never scan together.
    switch (command) {
    case '1': case '2': case '3': case 't': case 'b': case 'm':
    case 'n': case 'p': case 'r': case 'c': case 'a': case 's': case 'f':
        vfd_demo_stop();
        vfd_mapper_auto_stop();
        break;
    default:
        vfd_mapper_handle_command(command);
        return;
    }
    switch (command) {
    case '1': vfd_demo_number(1234); break;
    case '2': vfd_demo_number(8888); break;
    case '3': vfd_demo_time(); break;
    case 't': vfd_demo_self_test_start(); break;
    case 'b': vfd_demo_stability_start(); break;
    case 'm': vfd_mapper_init(); break;
    default: vfd_mapper_handle_command(command); break;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "PT6312 test start");
    ESP_LOGI(TAG, "CLK GPIO=%d", PT6312_PIN_CLK);
    ESP_LOGI(TAG, "STB GPIO=%d", PT6312_PIN_STB);
    ESP_LOGI(TAG, "DIN GPIO=%d", PT6312_PIN_DIN);
    ESP_LOGI(TAG, "bit-bang delay=%d us", PT6312_DELAY_US);
    pt6312_init();
    pt6312_clear();
    vTaskDelay(pdMS_TO_TICKS(500));

    // Reuse the configured console UART and its existing baud/pins. Input is
    // read directly, so commands do not depend on stdin line buffering.
    ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM,
                                        256, 0, 0, NULL, 0));
    vfd_demo_print_help();
    switch (TEST_MODE) {
    case PT_TEST_ALL_ON:
        vfd_mapper_print_help();
        vfd_mapper_all_on();
        ESP_LOGI(TAG, "PT6312 ALL ON test complete (commands sent; inspect VFD)");
        break;
    case PT_TEST_ALL_OFF:
        vfd_mapper_print_help();
        vfd_mapper_clear();
        pt6312_display_off();
        ESP_LOGI(TAG, "PT6312 ALL OFF test complete");
        break;
    case PT_TEST_MAPPER:
        vfd_mapper_init();
        break;
    case PT_TEST_SELF_TEST:
        vfd_demo_self_test_start();
        break;
    case PT_TEST_TIME:
        vfd_demo_time();
        break;
    case PT_TEST_STABILITY:
        vfd_demo_stability_start();
        break;
    }
    for (;;) {
        uint8_t command;
        // One tick wait keeps manual input responsive and lets the idle task run.
        int count = uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &command, 1, 1);
        if (count > 0) {
            handle_command((char)command);
            // Drain pending keys before considering an automatic advance.
            continue;
        }
        vfd_mapper_poll();
        vfd_demo_poll();
    }
}
