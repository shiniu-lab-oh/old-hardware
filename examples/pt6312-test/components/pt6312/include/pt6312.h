#pragma once

#include <stddef.h>
#include <stdint.h>

// Bench configuration: edit here, then rebuild and flash.
#define PT6312_PIN_CLK 18
#define PT6312_PIN_STB 19
#define PT6312_PIN_DIN 23
#define PT6312_DELAY_US 5
#define PT6312_DISPLAY_MODE 0x00
#define PT6312_BRIGHTNESS 7

#define PT6312_RAM_SIZE 22

// Single caller only. Call init before other APIs. No power control or readback.
void pt6312_init(void);
// Raw byte: caller owns STB framing; this function never changes STB.
void pt6312_write_byte(uint8_t data);
void pt6312_write_command(uint8_t cmd);
// Valid range 0x00..0x15. Invalid requests are logged and send nothing.
void pt6312_write_ram(uint8_t start_addr, const uint8_t *data, size_t len);
// Clears ALL 22 bytes, including RAM outside the first four digits.
void pt6312_clear(void);
// brightness is 0..7 (not a command byte); 7 selects 14/16 pulse width.
void pt6312_display_on(uint8_t brightness);
// Display off does not erase RAM or switch off the board's power supply.
void pt6312_display_off(void);
