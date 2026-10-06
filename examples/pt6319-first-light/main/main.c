#include "pt6319.h"
#include "vfd_mapper.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "PT6319_TEST";

void app_main(void)
{
    ESP_LOGI(TAG, "===== PT6319 INTERACTIVE MAPPER =====");
    ESP_LOGI(TAG, "CLK GPIO : %d", PT6319_PIN_CLK);
    ESP_LOGI(TAG, "STB GPIO : %d", PT6319_PIN_STB);
    ESP_LOGI(TAG, "DAT GPIO : %d", PT6319_PIN_DAT);
    ESP_LOGI(TAG, "Display mode : 0x%02X", PT6319_DISPLAY_MODE);
    ESP_LOGI(TAG, "RAM length   : %d bytes", PT6319_RAM_LENGTH);
    ESP_ERROR_CHECK(pt6319_init());

    // Keep console baud/pins as configured; raw UART input avoids line buffering.
    ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM,
                                        256, 0, 0, NULL, 0));
    vfd_mapper_init();
    while (1) {
        uint8_t input;
        int count = uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &input, 1, 1);
        if (count > 0) {
            vfd_mapper_handle_char((char)input);
            continue; // Service queued manual input before any automatic step.
        }
        vfd_mapper_poll();
    }
}
