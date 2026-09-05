#ifndef PB_APP_PROTOCOL_H
#define PB_APP_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#define PB_APP_PROTOCOL_VERSION 2U
#define PB_APP_ID_MAX_LENGTH 32
#define PB_EVENT_ID_LENGTH 36
#define PB_VIEW_MAX_LEDS 4
#define PB_TIMER_MAX_PRESETS 4

typedef enum {
    PB_ACTION_NONE = 0,
    PB_ACTION_PRIMARY,
    PB_ACTION_PRIMARY_LONG,
} pb_action_t;

typedef enum {
    PB_TIMER_EVENT_STARTED = 0,
    PB_TIMER_EVENT_PAUSED,
    PB_TIMER_EVENT_RESUMED,
    PB_TIMER_EVENT_FINISHED,
} pb_timer_event_t;

typedef enum {
    PB_EVENT_TYPE_ACTION = 0,
    PB_EVENT_TYPE_TIMER,
} pb_event_type_t;

typedef struct {
    char event_id[PB_EVENT_ID_LENGTH + 1];
    int64_t occurred_at;
    pb_event_type_t type;
    pb_action_t action;
    pb_timer_event_t timer_event;
    uint32_t duration_seconds;
    uint32_t remaining_seconds;
} pb_event_t;

typedef struct {
    int value;
    bool leading_zeroes;
    uint8_t brightness;
    bool blink;
    bool leds[PB_VIEW_MAX_LEDS];
    uint8_t led_count;
} pb_view_t;

typedef struct {
    bool enabled;
    uint32_t default_seconds;
    uint32_t presets_seconds[PB_TIMER_MAX_PRESETS];
    uint8_t preset_count;
} pb_timer_config_t;

typedef struct {
    bool enabled;
    int value;
    uint32_t duration_ms;
    bool blink;
} pb_state_overlay_t;

typedef struct {
    uint64_t revision;
    char app_id[PB_APP_ID_MAX_LENGTH + 1];
    pb_view_t view;
    pb_timer_config_t timer;
    pb_state_overlay_t overlay;
} pb_app_state_t;

#endif
