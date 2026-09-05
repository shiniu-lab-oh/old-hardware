#ifndef PB_EVENT_QUEUE_H
#define PB_EVENT_QUEUE_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "pb_app_protocol.h"

#define PB_EVENT_QUEUE_CAPACITY 24

typedef struct {
    uint32_t count;
    pb_event_t items[PB_EVENT_QUEUE_CAPACITY];
} pb_event_queue_t;

esp_err_t pb_event_queue_init(pb_event_queue_t *queue);
size_t pb_event_queue_count(const pb_event_queue_t *queue);
const pb_event_t *pb_event_queue_peek(const pb_event_queue_t *queue);
esp_err_t pb_event_queue_enqueue(pb_event_queue_t *queue, const pb_event_t *event);
esp_err_t pb_event_queue_pop(pb_event_queue_t *queue);

esp_err_t pb_event_make_action(pb_event_t *event, pb_action_t action);
esp_err_t pb_event_make_timer(
    pb_event_t *event,
    pb_timer_event_t timer_event,
    uint32_t duration_seconds,
    uint32_t remaining_seconds
);

#endif
