#ifndef PB_TIMER_H
#define PB_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#include "pb_app_protocol.h"

typedef enum {
    PB_LOCAL_TIMER_READY = 0,
    PB_LOCAL_TIMER_RUNNING,
    PB_LOCAL_TIMER_PAUSED,
} pb_local_timer_status_t;

typedef struct {
    pb_timer_config_t config;
    pb_local_timer_status_t status;
    uint32_t duration_seconds;
    uint32_t paused_remaining_seconds;
    int64_t deadline_us;
    uint32_t last_displayed_minutes;
} pb_local_timer_t;

void pb_timer_init(pb_local_timer_t *timer);
void pb_timer_configure(pb_local_timer_t *timer, const pb_timer_config_t *config);
bool pb_timer_enabled(const pb_local_timer_t *timer);
bool pb_timer_active(const pb_local_timer_t *timer);
pb_timer_event_t pb_timer_toggle(pb_local_timer_t *timer);
bool pb_timer_poll_finished(pb_local_timer_t *timer);
uint32_t pb_timer_remaining_seconds(const pb_local_timer_t *timer);
void pb_timer_make_view(
    pb_local_timer_t *timer,
    const pb_view_t *base_view,
    pb_view_t *timer_view
);

#endif
