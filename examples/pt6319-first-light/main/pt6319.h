#pragma once

#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

// First-light bench configuration. No automatic mode/pin/timing fallback.
#define PT6319_PIN_CLK GPIO_NUM_18
#define PT6319_PIN_STB GPIO_NUM_19
#define PT6319_PIN_DAT GPIO_NUM_23
#define PT6319_DELAY_US 10
#define PT6319_STARTUP_DELAY_MS 20

// PT6312 / uPD16312 family assumptions requested for this experiment.
// PT6319 public 3-page brief does NOT confirm the full command table.
#define PT6319_DISPLAY_MODE 0x02
#define PT6319_RAM_BYTES 12
#define PT6319_RAM_LENGTH PT6319_RAM_BYTES
#define PT6319_CMD_DATA_WRITE_AUTO 0x40
#define PT6319_CMD_ADDR_BASE 0xC0
#define PT6319_CMD_DISPLAY_ON_BASE 0x88
#define PT6319_CMD_DISPLAY_ON_MAX 0x8F
#define PT6319_CMD_DISPLAY_OFF 0x80
#define PT6319_BRIGHTNESS_MAX 7

// 1: CLEAR -> wait 1 second -> ALL ON, hold. 0: CLEAR, hold.
#define PT6319_TEST_MODE_ALL_ON 1

// Single caller, init first. This version never issues a read command.
esp_err_t pt6319_init(void);
void pt6319_clear(void);
void pt6319_all_on(void);
void pt6319_display_on(uint8_t brightness); // 0..7; 0 is dim, not OFF
void pt6319_display_off(void);
// Mapper bridge: write one complete configured frame through the original writer.
// Caller supplies PT6319_RAM_BYTES bytes, after successful init.
void pt6319_write_frame(const uint8_t ram[PT6319_RAM_BYTES]);
