#include "pt6312_tools.h"
#include "pt6312.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "PT6312";

void pt6312_all_on(void)
{
    uint8_t data[PT6312_TEST_RAM_SIZE];
    memset(data, 0xFF, sizeof(data));
    pt6312_clear();
    ESP_LOGI(TAG, "write FF x 8");
    pt6312_write_ram(0, data, sizeof(data));
    pt6312_display_on(PT6312_BRIGHTNESS);
    ESP_LOGI(TAG, "display ON");
}

