#include "pb_runtime.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pb_actions.h"
#include "pb_binding.h"
#include "pb_event_queue.h"
#include "pb_overlay.h"
#include "pb_timer.h"
#include "pb_view.h"

#define PB_EVENT_RETRY_INTERVAL_MS 5000U
#define PB_BINDING_STORE_RETRY_INTERVAL_MS 5000U
#define PB_INPUT_WAIT_MS 100U

static const char *TAG = "pb_runtime";

typedef struct {
    pb_runtime_config_t config;
    pb_event_queue_t event_queue;
    pb_binding_t binding;
    pb_local_timer_t timer;
    pb_overlay_t overlay;
    TickType_t next_poll;
    TickType_t next_event_retry;
    TickType_t next_binding_store_retry;
    uint64_t primary_pressed_at_ms;
    bool initialized;
    bool has_binding;
    bool binding_store_dirty;
    bool state_request_in_flight;
    bool event_request_in_flight;
    bool primary_pressed;
} pb_runtime_context_t;

static pb_runtime_context_t s_runtime;

static bool transport_config_valid(const pb_runtime_transport_t *transport)
{
    return !transport->enabled ||
           (transport->is_online != NULL &&
            transport->take_offline_transition != NULL &&
            transport->request_state != NULL &&
            transport->post_event != NULL &&
            transport->receive != NULL);
}

static bool transport_online(const pb_runtime_context_t *runtime)
{
    return runtime->config.transport.enabled &&
           runtime->config.transport.is_online(
               runtime->config.transport.context);
}

static esp_err_t render_current(pb_runtime_context_t *runtime)
{
    if (!pb_timer_active(&runtime->timer)) {
        return pb_view_render(&runtime->binding.view);
    }

    pb_view_t timer_view;
    pb_timer_make_view(&runtime->timer, &runtime->binding.view, &timer_view);
    return pb_view_render(&timer_view);
}

static esp_err_t apply_state(
    pb_runtime_context_t *runtime,
    const pb_app_state_t *next_state,
    bool *binding_replaced)
{
    *binding_replaced = false;
    if (runtime->has_binding &&
        next_state->revision < runtime->binding.revision) {
        ESP_LOGW(TAG,
                 "Ignoring stale state revision=%" PRIu64 " current=%" PRIu64,
                 next_state->revision,
                 runtime->binding.revision);
        return ESP_OK;
    }

    if (runtime->has_binding &&
        next_state->revision == runtime->binding.revision) {
        if (!pb_binding_matches_state(&runtime->binding, next_state)) {
            ESP_LOGE(TAG,
                     "Conflicting state at revision=%" PRIu64 " ignored",
                     runtime->binding.revision);
            return ESP_ERR_INVALID_RESPONSE;
        }
        ESP_LOGD(TAG,
                 "State revision=%" PRIu64 " unchanged",
                 runtime->binding.revision);
        return ESP_OK;
    }

    pb_binding_t next_binding;
    esp_err_t err = pb_binding_from_state(&next_binding, next_state);
    if (err != ESP_OK) {
        return err;
    }

    const bool app_changed = !runtime->has_binding ||
        strcmp(runtime->binding.app_id, next_binding.app_id) != 0;
    err = pb_binding_store(
        runtime->config.cloud_base_url,
        runtime->config.device_serial,
        &next_binding);
    if (err != ESP_OK) {
        runtime->binding_store_dirty = true;
        ESP_LOGW(TAG,
                 "Could not persist binding; retry scheduled: %s",
                 esp_err_to_name(err));
    } else {
        runtime->binding_store_dirty = false;
    }

    if (app_changed) {
        pb_timer_init(&runtime->timer);
        pb_overlay_init(&runtime->overlay);
        *binding_replaced = true;
    }
    pb_timer_configure(&runtime->timer, &next_binding.timer);
    runtime->binding = next_binding;
    runtime->has_binding = true;

    ESP_LOGI(TAG,
             "%s App binding: app=%s revision=%" PRIu64,
             app_changed ? "Activated" : "Updated",
             runtime->binding.app_id,
             runtime->binding.revision);

    if (next_state->overlay.enabled) {
        return pb_overlay_show_code(
            &runtime->overlay,
            next_state->overlay.value,
            next_state->overlay.duration_ms,
            next_state->overlay.blink);
    }
    if (!pb_overlay_active(&runtime->overlay)) {
        return render_current(runtime);
    }
    return ESP_OK;
}

static esp_err_t enqueue_timer_event(
    pb_runtime_context_t *runtime,
    pb_timer_event_t event)
{
    static const char *const names[] = {
        "started",
        "paused",
        "resumed",
        "finished",
    };
    ESP_LOGI(TAG,
             "Timer %s: duration=%" PRIu32 " remaining=%" PRIu32,
             names[event],
             runtime->timer.duration_seconds,
             pb_timer_remaining_seconds(&runtime->timer));

    pb_event_t pending;
    esp_err_t err = pb_event_make_timer(
        &pending,
        event,
        runtime->timer.duration_seconds,
        event == PB_TIMER_EVENT_FINISHED
            ? 0
            : pb_timer_remaining_seconds(&runtime->timer));
    if (err == ESP_OK) {
        err = pb_event_set_context(
            &pending,
            runtime->binding.app_id,
            runtime->binding.revision);
    }
    if (err == ESP_OK) {
        err = pb_event_queue_enqueue(&runtime->event_queue, &pending);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not queue timer event: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG,
             "Queued event %s (%u pending)",
             pending.event_id,
             (unsigned)pb_event_queue_count(&runtime->event_queue));
    return ESP_OK;
}

static esp_err_t enqueue_action_event(
    pb_runtime_context_t *runtime,
    pb_action_t action)
{
    pb_event_t pending;
    esp_err_t err = pb_event_make_action(&pending, action);
    if (err == ESP_OK) {
        err = pb_event_set_context(
            &pending,
            runtime->binding.app_id,
            runtime->binding.revision);
    }
    if (err == ESP_OK) {
        err = pb_event_queue_enqueue(&runtime->event_queue, &pending);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not queue action event: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG,
             "Queued event %s (%u pending)",
             pending.event_id,
             (unsigned)pb_event_queue_count(&runtime->event_queue));
    return ESP_OK;
}

static void handle_state_result(
    pb_runtime_context_t *runtime,
    const pb_runtime_transport_result_t *result,
    TickType_t now)
{
    runtime->state_request_in_flight = false;
    if (result->error != ESP_OK) {
        ESP_LOGW(TAG,
                 "State sync failed; keeping local state: %s",
                 esp_err_to_name(result->error));
        return;
    }

    bool binding_replaced;
    const esp_err_t err = apply_state(
        runtime,
        &result->data.state,
        &binding_replaced);
    if (binding_replaced) {
        runtime->primary_pressed = false;
    }
    if (runtime->binding_store_dirty &&
        runtime->next_binding_store_retry == 0) {
        runtime->next_binding_store_retry = now +
            pdMS_TO_TICKS(PB_BINDING_STORE_RETRY_INTERVAL_MS);
    } else if (!runtime->binding_store_dirty) {
        runtime->next_binding_store_retry = 0;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "State apply failed; keeping local state: %s",
                 esp_err_to_name(err));
    }
}

static void handle_event_result(
    pb_runtime_context_t *runtime,
    const pb_runtime_transport_result_t *result,
    TickType_t now)
{
    runtime->event_request_in_flight = false;
    const pb_event_t *pending = pb_event_queue_peek(&runtime->event_queue);
    if (result->error != ESP_OK) {
        ESP_LOGW(TAG,
                 "Event %s delivery failed; retained for retry: %s",
                 result->data.event.event_id,
                 esp_err_to_name(result->error));
        runtime->next_event_retry = now +
            pdMS_TO_TICKS(PB_EVENT_RETRY_INTERVAL_MS);
        return;
    }
    if (pending == NULL ||
        strcmp(pending->event_id, result->data.event.event_id) != 0) {
        ESP_LOGE(TAG,
                 "Unexpected event acknowledgement: %s",
                 result->data.event.event_id);
        runtime->next_event_retry = now +
            pdMS_TO_TICKS(PB_EVENT_RETRY_INTERVAL_MS);
        return;
    }

    const esp_err_t err = pb_event_queue_pop(&runtime->event_queue);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "Event %s acknowledged but could not be removed: %s",
                 result->data.event.event_id,
                 esp_err_to_name(err));
        runtime->next_event_retry = now +
            pdMS_TO_TICKS(PB_EVENT_RETRY_INTERVAL_MS);
        return;
    }
    ESP_LOGI(TAG,
             "Delivered event %s at revision=%" PRIu64 " (%u pending)",
             result->data.event.event_id,
             result->data.event.revision,
             (unsigned)pb_event_queue_count(&runtime->event_queue));
    runtime->next_event_retry = 0;
    runtime->next_poll = 0;
}

static void receive_transport_results(
    pb_runtime_context_t *runtime,
    TickType_t now)
{
    pb_runtime_transport_result_t result;
    while (runtime->config.transport.enabled &&
           runtime->config.transport.receive(
               runtime->config.transport.context,
               &result,
               0)) {
        if (result.type == PB_RUNTIME_TRANSPORT_RESULT_STATE) {
            handle_state_result(runtime, &result, now);
        } else if (result.type == PB_RUNTIME_TRANSPORT_RESULT_EVENT) {
            handle_event_result(runtime, &result, now);
        } else {
            ESP_LOGE(TAG,
                     "Ignoring unknown transport result type: %d",
                     (int)result.type);
        }
    }
}

static void retry_dirty_binding(pb_runtime_context_t *runtime, TickType_t now)
{
    if (!runtime->binding_store_dirty ||
        (runtime->next_binding_store_retry != 0 &&
         (int32_t)(now - runtime->next_binding_store_retry) < 0)) {
        return;
    }

    const esp_err_t err = pb_binding_store(
        runtime->config.cloud_base_url,
        runtime->config.device_serial,
        &runtime->binding);
    if (err == ESP_OK) {
        runtime->binding_store_dirty = false;
        runtime->next_binding_store_retry = 0;
        ESP_LOGI(TAG, "Persisted previously dirty App binding");
    } else {
        runtime->next_binding_store_retry = now +
            pdMS_TO_TICKS(PB_BINDING_STORE_RETRY_INTERVAL_MS);
        ESP_LOGW(TAG,
                 "Binding persistence retry failed: %s",
                 esp_err_to_name(err));
    }
}

static void poll_timer_and_overlay(pb_runtime_context_t *runtime)
{
    if (pb_timer_poll_finished(&runtime->timer)) {
        if (!pb_overlay_active(&runtime->overlay)) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(pb_view_render(&runtime->binding.view));
        }
        if (runtime->config.transport.enabled && runtime->has_binding) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(enqueue_timer_event(
                runtime,
                PB_TIMER_EVENT_FINISHED));
            runtime->next_event_retry = 0;
        }
    } else if (pb_timer_active(&runtime->timer) &&
               !pb_overlay_active(&runtime->overlay)) {
        const uint32_t minutes =
            (pb_timer_remaining_seconds(&runtime->timer) + 59U) / 60U;
        if (minutes != runtime->timer.last_displayed_minutes) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(render_current(runtime));
        }
    }

    if (pb_overlay_take_expired(&runtime->overlay)) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(render_current(runtime));
    }
}

static void schedule_network_work(
    pb_runtime_context_t *runtime,
    TickType_t now)
{
    if (!transport_online(runtime)) {
        return;
    }

    if (pb_event_queue_count(&runtime->event_queue) > 0 &&
        !runtime->event_request_in_flight &&
        (runtime->next_event_retry == 0 ||
         (int32_t)(now - runtime->next_event_retry) >= 0)) {
        const pb_event_t *pending = pb_event_queue_peek(&runtime->event_queue);
        const esp_err_t err = runtime->config.transport.post_event(
            runtime->config.transport.context,
            pending);
        if (err == ESP_OK) {
            runtime->event_request_in_flight = true;
        } else {
            ESP_LOGW(TAG,
                     "Could not queue event delivery: %s",
                     esp_err_to_name(err));
            runtime->next_event_retry = now +
                pdMS_TO_TICKS(PB_EVENT_RETRY_INTERVAL_MS);
        }
    }

    if (!runtime->state_request_in_flight &&
        (runtime->next_poll == 0 ||
         (int32_t)(now - runtime->next_poll) >= 0)) {
        const esp_err_t err = runtime->config.transport.request_state(
            runtime->config.transport.context);
        if (err == ESP_OK) {
            runtime->state_request_in_flight = true;
            runtime->next_poll = now +
                pdMS_TO_TICKS(runtime->config.poll_interval_ms);
        } else {
            ESP_LOGW(TAG,
                     "Could not queue state request: %s",
                     esp_err_to_name(err));
            runtime->next_poll = now + pdMS_TO_TICKS(1000U);
        }
    }
}

static void handle_primary_input(
    pb_runtime_context_t *runtime,
    const pb_input_event_t *input_event)
{
    if (pb_action_from_control(input_event->control) == PB_ACTION_NONE) {
        return;
    }
    if (input_event->pressed) {
        runtime->primary_pressed = true;
        runtime->primary_pressed_at_ms = input_event->sampled_at_ms;
        return;
    }
    if (!runtime->primary_pressed) {
        return;
    }

    runtime->primary_pressed = false;
    const uint32_t held_ms =
        input_event->sampled_at_ms >= runtime->primary_pressed_at_ms
            ? (uint32_t)(input_event->sampled_at_ms -
                         runtime->primary_pressed_at_ms)
            : 0;
    if (held_ms >= runtime->config.primary_long_press_ms) {
        if (runtime->timer.status == PB_LOCAL_TIMER_RUNNING) {
            ESP_LOGI(TAG, "Long action ignored while timer is running");
            return;
        }
        ESP_LOGI(TAG, "Action: primary_long (%" PRIu32 " ms)", held_ms);
        if (runtime->config.transport.enabled && runtime->has_binding) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(enqueue_action_event(
                runtime,
                PB_ACTION_PRIMARY_LONG));
            runtime->next_event_retry = 0;
        }
    } else if (pb_timer_enabled(&runtime->timer)) {
        const pb_timer_event_t event = pb_timer_toggle(&runtime->timer);
        if (!pb_overlay_active(&runtime->overlay)) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(render_current(runtime));
        }
        if (runtime->config.transport.enabled && runtime->has_binding) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(enqueue_timer_event(runtime, event));
            runtime->next_event_retry = 0;
        }
    } else {
        ESP_LOGI(TAG, "Action: primary");
        if (runtime->config.transport.enabled && runtime->has_binding) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(enqueue_action_event(
                runtime,
                PB_ACTION_PRIMARY));
            runtime->next_event_retry = 0;
        }
    }

    runtime->next_poll = xTaskGetTickCount() +
        pdMS_TO_TICKS(runtime->config.poll_interval_ms);
}

esp_err_t pb_runtime_init(
    const pb_runtime_config_t *config,
    pb_runtime_status_t *status)
{
    if (s_runtime.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (config == NULL ||
        config->cloud_base_url == NULL || config->cloud_base_url[0] == '\0' ||
        config->device_serial == NULL || config->device_serial[0] == '\0' ||
        config->poll_interval_ms == 0 ||
        config->primary_long_press_ms == 0 ||
        config->boot_overlay_ms == 0 ||
        config->offline_overlay_ms == 0 ||
        !transport_config_valid(&config->transport)) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&s_runtime, 0, sizeof(s_runtime));
    s_runtime.config = *config;
    ESP_RETURN_ON_ERROR(
        pb_event_queue_init(
            &s_runtime.event_queue,
            config->cloud_base_url,
            config->device_serial),
        TAG,
        "event queue initialization failed");

    pb_overlay_init(&s_runtime.overlay);
    ESP_RETURN_ON_ERROR(
        pb_overlay_show_code(
            &s_runtime.overlay,
            888,
            config->boot_overlay_ms,
            false),
        TAG,
        "boot overlay failed");
    vTaskDelay(pdMS_TO_TICKS(config->boot_overlay_ms));
    pb_overlay_init(&s_runtime.overlay);

    pb_binding_init(&s_runtime.binding);
    s_runtime.has_binding =
        pb_binding_load(
            config->cloud_base_url,
            config->device_serial,
            &s_runtime.binding) == ESP_OK;
    if (!s_runtime.has_binding) {
        uint64_t legacy_revision;
        if (pb_view_load_last(
                config->cloud_base_url,
                config->device_serial,
                &s_runtime.binding.view,
                &legacy_revision) == ESP_OK) {
            ESP_LOGW(TAG,
                     "Loaded legacy View revision=%" PRIu64
                     " without App binding; waiting for a complete State",
                     legacy_revision);
        }
    }

    pb_timer_init(&s_runtime.timer);
    if (s_runtime.has_binding) {
        pb_timer_configure(&s_runtime.timer, &s_runtime.binding.timer);
    }
    ESP_RETURN_ON_ERROR(
        pb_view_render(&s_runtime.binding.view),
        TAG,
        "initial View render failed");

    s_runtime.initialized = true;
    if (status != NULL) {
        memset(status, 0, sizeof(*status));
        status->has_binding = s_runtime.has_binding;
        status->revision = s_runtime.binding.revision;
        if (s_runtime.has_binding) {
            strlcpy(
                status->app_id,
                s_runtime.binding.app_id,
                sizeof(status->app_id));
        }
    }
    return ESP_OK;
}

esp_err_t pb_runtime_run(void)
{
    if (!s_runtime.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    while (true) {
        const TickType_t now = xTaskGetTickCount();
        receive_transport_results(&s_runtime, now);

        if (s_runtime.config.transport.enabled &&
            s_runtime.config.transport.take_offline_transition(
                s_runtime.config.transport.context)) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(pb_overlay_show_code(
                &s_runtime.overlay,
                404,
                s_runtime.config.offline_overlay_ms,
                false));
        }

        retry_dirty_binding(&s_runtime, now);
        poll_timer_and_overlay(&s_runtime);
        schedule_network_work(&s_runtime, now);

        pb_input_event_t input_event;
        if (pb_hal_wait_input_event(&input_event, PB_INPUT_WAIT_MS)) {
            handle_primary_input(&s_runtime, &input_event);
        }
    }
}
