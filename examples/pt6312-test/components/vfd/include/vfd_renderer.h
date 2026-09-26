#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// Call pt6312_init() first. Single caller; each successful call applies immediately.
// vfd_clear() resets the software frame after raw ALL ON / Mapper use.
void vfd_clear(void);
// Position 0..3 -> measured Digit1..4. Digit 0..9; preserves other digits/colon.
esp_err_t vfd_set_digit(uint8_t position, uint8_t digit);
void vfd_set_colon(bool enabled);
// 0..7; level 0 is minimum brightness, NOT display OFF. Preserves RAM.
esp_err_t vfd_set_brightness(uint8_t level);
// 0..9999, four digits with leading zeros; clears colon and all unused bits.
esp_err_t vfd_show_number(uint16_t value);
// hour 0..23, minute 0..59; leading zeros and colon ON. No clock/timer service.
esp_err_t vfd_show_time(uint8_t hour, uint8_t minute);
// Invalid arguments return ESP_ERR_INVALID_ARG without changing the display.
