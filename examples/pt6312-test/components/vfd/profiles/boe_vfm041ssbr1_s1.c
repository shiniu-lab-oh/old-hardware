#include "boe_vfm041ssbr1_s1.h"

// User-measured Mapper results, archived in docs/boe_vfm041ssbr1_s1_map.md.
// API position 0..3 corresponds to measured Digit1..4; no inferred remapping.
#define DIGIT_SEGMENTS(addr_) \
    {{addr_, 0}, {addr_, 1}, {addr_, 2}, {addr_, 3}, \
     {addr_, 4}, {addr_, 5}, {addr_, 6}}

const vfd_profile_t boe_vfm041ssbr1_s1 = {
    .model = "BOE VFM041SSBR1-S1",
    .version = "0.1",
    .digits = {
        DIGIT_SEGMENTS(0x00), DIGIT_SEGMENTS(0x02),
        DIGIT_SEGMENTS(0x04), DIGIT_SEGMENTS(0x06),
    },
    .colon = {0x04, 7}, // State 39: one combined colon, not two independent dots.
    .raw_visible_mask = {0x7F, 0x00, 0x7F, 0x00, 0xFF, 0x00, 0x7F, 0x00},
};
