#pragma once
/* Host-only error constants, matching ESP-IDF 6.0.2 esp_err.h.
 * Production builds include the real ESP-IDF header. */
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_RESPONSE 0x108
