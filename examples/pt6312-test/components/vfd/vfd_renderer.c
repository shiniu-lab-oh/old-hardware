#include "vfd_renderer.h"
#include "boe_vfm041ssbr1_s1.h"
#include "pt6312.h"

static uint8_t frame[PT6312_RAM_SIZE];
static uint8_t brightness = PT6312_BRIGHTNESS;

// Logical font A..G. Only the measured Profile converts these to RAM bits.
static const uint8_t decimal_font[10] = {
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,         // 0
    SEG_B | SEG_C,                                         // 1
    SEG_A | SEG_B | SEG_D | SEG_E | SEG_G,                 // 2
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_G,                 // 3
    SEG_B | SEG_C | SEG_F | SEG_G,                         // 4
    SEG_A | SEG_C | SEG_D | SEG_F | SEG_G,                 // 5
    SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,         // 6
    SEG_A | SEG_B | SEG_C,                                 // 7
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G, // 8
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G,         // 9
};

static void reset_frame(void)
{
    for (unsigned i = 0; i < sizeof(frame); ++i) {
        frame[i] = 0;
    }
}

static void set_segment(vfd_segment_t segment, bool enabled)
{
    const uint8_t mask = (uint8_t)(1U << segment.bit);
    if (enabled) {
        frame[segment.addr] |= mask;
    } else {
        frame[segment.addr] &= (uint8_t)~mask;
    }
}

static void render_digit(uint8_t position, uint8_t digit)
{
    for (unsigned segment = 0; segment < VFD_SEGMENT_COUNT; ++segment) {
        set_segment(boe_vfm041ssbr1_s1.digits[position][segment],
                    (decimal_font[digit] & (1U << segment)) != 0);
    }
}

static void flush(void)
{
    // Entire RAM image also removes leftover diagnostic bits beyond byte 7.
    pt6312_write_ram(0, frame, sizeof(frame));
    pt6312_display_on(brightness);
}

void vfd_clear(void)
{
    reset_frame();
    flush();
}

esp_err_t vfd_set_digit(uint8_t position, uint8_t digit)
{
    if (position >= VFD_DIGIT_COUNT || digit > 9) {
        return ESP_ERR_INVALID_ARG;
    }
    render_digit(position, digit);
    flush();
    return ESP_OK;
}

void vfd_set_colon(bool enabled)
{
    set_segment(boe_vfm041ssbr1_s1.colon, enabled);
    flush();
}

esp_err_t vfd_set_brightness(uint8_t level)
{
    if (level > 7) {
        return ESP_ERR_INVALID_ARG;
    }
    brightness = level;
    pt6312_display_on(brightness);
    return ESP_OK;
}

esp_err_t vfd_show_number(uint16_t value)
{
    if (value > 9999) {
        return ESP_ERR_INVALID_ARG;
    }
    reset_frame();
    for (int position = VFD_DIGIT_COUNT - 1; position >= 0; --position) {
        render_digit((uint8_t)position, value % 10);
        value /= 10;
    }
    flush();
    return ESP_OK;
}

esp_err_t vfd_show_time(uint8_t hour, uint8_t minute)
{
    if (hour > 23 || minute > 59) {
        return ESP_ERR_INVALID_ARG;
    }
    reset_frame();
    render_digit(0, hour / 10);
    render_digit(1, hour % 10);
    render_digit(2, minute / 10);
    render_digit(3, minute % 10);
    set_segment(boe_vfm041ssbr1_s1.colon, true);
    flush();
    return ESP_OK;
}
