#pragma once
#include <stdint.h>

#define VFD_DIGIT_COUNT 4
#define VFD_SEGMENT_COUNT 7
#define VFD_PROFILE_RAM_SIZE 8

typedef struct {
    uint8_t addr;
    uint8_t bit;
} vfd_segment_t;

// Semantic segment order, independent of chip wiring.
enum {
    SEG_A = 1U << 0, SEG_B = 1U << 1, SEG_C = 1U << 2,
    SEG_D = 1U << 3, SEG_E = 1U << 4, SEG_F = 1U << 5, SEG_G = 1U << 6,
};

typedef struct {
    const char *model;
    const char *version;
    vfd_segment_t digits[VFD_DIGIT_COUNT][VFD_SEGMENT_COUNT]; // A..G
    vfd_segment_t colon;
    // A set bit means observed visible in the 64-state raw scan.
    // Zero = UNUSED in this profile (observed no visible display, not proof of NC).
    uint8_t raw_visible_mask[VFD_PROFILE_RAM_SIZE];
} vfd_profile_t;
