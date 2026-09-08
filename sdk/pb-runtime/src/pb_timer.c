#include "pb_timer.h"

#include <string.h>

#include "esp_timer.h"

#define PB_MICROSECONDS_PER_SECOND 1000000LL

void pb_timer_init(pb_local_timer_t *timer)
{
    if (timer == NULL) {
        return;
    }
    memset(timer, 0, sizeof(*timer));
}

void pb_timer_configure(pb_local_timer_t *timer, const pb_timer_config_t *config)
{
    if (timer == NULL || config == NULL) {
        return;
    }

    timer->config = *config;
    if (!config->enabled || config->default_seconds == 0) {
        timer->status = PB_LOCAL_TIMER_READY;
        timer->duration_seconds = 0;
        timer->paused_remaining_seconds = 0;
        timer->deadline_us = 0;
    }
}

bool pb_timer_enabled(const pb_local_timer_t *timer)
{
    return timer != NULL && timer->config.enabled &&
           timer->config.default_seconds > 0;
}

bool pb_timer_active(const pb_local_timer_t *timer)
{
    return timer != NULL && timer->status != PB_LOCAL_TIMER_READY;
}

uint32_t pb_timer_remaining_seconds(const pb_local_timer_t *timer)
{
    if (timer == NULL) {
        return 0;
    }
    if (timer->status == PB_LOCAL_TIMER_PAUSED) {
        return timer->paused_remaining_seconds;
    }
    if (timer->status != PB_LOCAL_TIMER_RUNNING) {
        return 0;
    }

    const int64_t remaining_us = timer->deadline_us - esp_timer_get_time();
    if (remaining_us <= 0) {
        return 0;
    }
    return (uint32_t)((remaining_us + PB_MICROSECONDS_PER_SECOND - 1) /
                      PB_MICROSECONDS_PER_SECOND);
}

pb_timer_event_t pb_timer_toggle(pb_local_timer_t *timer)
{
    if (!pb_timer_enabled(timer)) {
        return PB_TIMER_EVENT_FINISHED;
    }

    if (timer->status == PB_LOCAL_TIMER_READY) {
        timer->duration_seconds = timer->config.default_seconds;
        timer->deadline_us = esp_timer_get_time() +
                             (int64_t)timer->duration_seconds * PB_MICROSECONDS_PER_SECOND;
        timer->status = PB_LOCAL_TIMER_RUNNING;
        timer->last_displayed_minutes = UINT32_MAX;
        return PB_TIMER_EVENT_STARTED;
    }

    if (timer->status == PB_LOCAL_TIMER_RUNNING) {
        timer->paused_remaining_seconds = pb_timer_remaining_seconds(timer);
        timer->status = PB_LOCAL_TIMER_PAUSED;
        timer->last_displayed_minutes = UINT32_MAX;
        return PB_TIMER_EVENT_PAUSED;
    }

    timer->deadline_us = esp_timer_get_time() +
                         (int64_t)timer->paused_remaining_seconds * PB_MICROSECONDS_PER_SECOND;
    timer->status = PB_LOCAL_TIMER_RUNNING;
    timer->last_displayed_minutes = UINT32_MAX;
    return PB_TIMER_EVENT_RESUMED;
}

bool pb_timer_poll_finished(pb_local_timer_t *timer)
{
    if (timer == NULL || timer->status != PB_LOCAL_TIMER_RUNNING ||
        pb_timer_remaining_seconds(timer) > 0) {
        return false;
    }

    timer->status = PB_LOCAL_TIMER_READY;
    timer->paused_remaining_seconds = 0;
    timer->deadline_us = 0;
    timer->last_displayed_minutes = UINT32_MAX;
    return true;
}

void pb_timer_make_view(
    pb_local_timer_t *timer,
    const pb_view_t *base_view,
    pb_view_t *timer_view
)
{
    if (timer == NULL || base_view == NULL || timer_view == NULL) {
        return;
    }

    *timer_view = *base_view;
    const uint32_t remaining = pb_timer_remaining_seconds(timer);
    timer_view->value = (int)((remaining + 59U) / 60U);
    timer_view->leading_zeroes = true;
    timer_view->blink = timer->status == PB_LOCAL_TIMER_PAUSED;
    timer->last_displayed_minutes = (uint32_t)timer_view->value;
}
