#pragma once

#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* User-confirmed board wiring; these signals are separate from CT1668.
 * Panel remains powered from the independent 3.3V bench supply. */
#define SDC251_GPIO_GREEN_LED GPIO_NUM_33 /* Panel pin6, HIGH = ON */
#define SDC251_GPIO_IR_IN     GPIO_NUM_32 /* Panel pin5, input only for now */

/* Single caller, no ISR use. Sets green OFF and reserves IR as a floating
 * input (no interrupt, pull-up, pull-down or decoding). Red POWER LED has
 * no software API. Call before using the green LED functions. */
esp_err_t sdc251_panel_init(void);
esp_err_t sdc251_green_led_set(bool on);
esp_err_t sdc251_green_led_toggle(void);
/* Returns the last commanded state, not an optical or electrical measurement. */
esp_err_t sdc251_green_led_get(bool *on);

#ifdef __cplusplus
}
#endif
