#ifndef PB_APP_PROTOCOL_H
#define PB_APP_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#define PB_APP_ID_MAX_LENGTH 32
#define PB_VIEW_MAX_LEDS 4

typedef enum {
    PB_ACTION_NONE = 0,
    PB_ACTION_PRIMARY,
} pb_action_t;

typedef struct {
    int value;
    bool leading_zeroes;
    uint8_t brightness;
    bool blink;
    bool leds[PB_VIEW_MAX_LEDS];
    uint8_t led_count;
} pb_view_t;

typedef struct {
    uint64_t revision;
    char app_id[PB_APP_ID_MAX_LENGTH + 1];
    pb_view_t view;
} pb_app_state_t;

#endif
