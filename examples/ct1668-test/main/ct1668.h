#pragma once

#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CT1668 compatibility assumption: TM1668 commands/timing; display/key subsets
 * tested on this panel, full compatibility and electrical specs unverified. */
#define CT1668_GPIO_STB          GPIO_NUM_32
#define CT1668_GPIO_CLK          GPIO_NUM_33
#define CT1668_GPIO_DIO          GPIO_NUM_25
#define CT1668_EDGE_DELAY_US     5U
#define CT1668_RAM_SIZE          14U
#define CT1668_KEY_BYTES         5U
#define CT1668_RAM_BASE          0xC0U
#define CT1668_CMD_MODE_4_GRID   0x00U /* 4 GRID / 13 SEG */
#define CT1668_CMD_WRITE_AUTO    0x40U /* RAM write, auto increment, normal */
#define CT1668_CMD_READ_KEYS     0x42U
#define CT1668_CMD_DISPLAY       0x80U
#define CT1668_DISPLAY_ENABLE    0x08U

/* Single caller only; call init first. No ISR use or concurrent transactions.
 * Void APIs fail fast on GPIO errors. Buffers must have the declared size.
 * Raw byte/command APIs do not update cached display brightness/on state.
 */
esp_err_t ct1668_init(void);
/* Raw LSB-first byte; caller owns STB framing (normally use higher APIs). */
void ct1668_write_byte(uint8_t data);
void ct1668_send_command(uint8_t command);
void ct1668_write_ram(const uint8_t ram[CT1668_RAM_SIZE]);
void ct1668_clear(void);
void ct1668_set_brightness(uint8_t level); /* 0..7; larger values clamp to 7 */
void ct1668_display_on(void);
void ct1668_display_off(void);
/* Sets exactly 14 RAM bytes to FF; preserves brightness/display enable. */
void ct1668_all_segments_on(void);
/* Reads exactly 5 raw bytes; no Konka key mapping assumed. Samples in CLK HIGH.
 * ESP_OK reports GPIO operations only, not a device ACK or validated key data.
 * TM1668 open-drain reply assumption: check panel DIO pull-up when testing.
 */
esp_err_t ct1668_read_keys(uint8_t keys[CT1668_KEY_BYTES]);

#ifdef __cplusplus
}
#endif
