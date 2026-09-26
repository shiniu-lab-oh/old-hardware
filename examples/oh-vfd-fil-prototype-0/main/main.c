#include <stddef.h>

#include "driver/mcpwm_prelude.h"
#include "esp_err.h"
#include "esp_log.h"

#define IN1_GPIO 18
#define IN2_GPIO 19
#define TIMER_RESOLUTION_HZ 1000000
#define PERIOD_TICKS 50
#define HALF_PERIOD_TICKS 25

static const char *TAG = "VFD_FIL";

void app_main(void)
{
    mcpwm_timer_handle_t timer = NULL;
    mcpwm_oper_handle_t oper = NULL;
    mcpwm_cmpr_handle_t comparator = NULL;
    mcpwm_gen_handle_t gen1 = NULL;
    mcpwm_gen_handle_t gen2 = NULL;

    // One shared hardware timebase: 1 us/tick, 50 us/cycle = 20 kHz.
    const mcpwm_timer_config_t timer_config = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = TIMER_RESOLUTION_HZ,
        .period_ticks = PERIOD_TICKS,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    };
    ESP_ERROR_CHECK(mcpwm_new_timer(&timer_config, &timer));

    const mcpwm_operator_config_t operator_config = {
        .group_id = 0,
    };
    ESP_ERROR_CHECK(mcpwm_new_operator(&operator_config, &oper));
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(oper, timer));

    const mcpwm_comparator_config_t comparator_config = {
        .flags.update_cmp_on_tez = true,
    };
    ESP_ERROR_CHECK(mcpwm_new_comparator(oper, &comparator_config, &comparator));
    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator, HALF_PERIOD_TICKS));

    const mcpwm_generator_config_t gen1_config = {
        .gen_gpio_num = IN1_GPIO,
    };
    const mcpwm_generator_config_t gen2_config = {
        .gen_gpio_num = IN2_GPIO,
    };
    ESP_ERROR_CHECK(mcpwm_new_generator(oper, &gen1_config, &gen1));
    ESP_ERROR_CHECK(mcpwm_new_generator(oper, &gen2_config, &gen2));

    // One-shot LOW initialization; timer/compare events take over at startup.
    // IN1=IN2=0 requests coast (high impedance) from the DRV8837.
    ESP_ERROR_CHECK(mcpwm_generator_set_force_level(gen1, 0, false));
    ESP_ERROR_CHECK(mcpwm_generator_set_force_level(gen2, 0, false));

    // IN1: HIGH for ticks 0..24, LOW for ticks 25..49.
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(gen1,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,
            MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)));
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(gen1,
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,
            comparator, MCPWM_GEN_ACTION_LOW)));

    // IN2: inverse actions on the SAME timer and comparator events.
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(gen2,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,
            MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_LOW)));
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(gen2,
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,
            comparator, MCPWM_GEN_ACTION_HIGH)));

    ESP_ERROR_CHECK(mcpwm_timer_enable(timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP));

    ESP_LOGI(TAG, "20 kHz filament AC started: IN1=GPIO%d, IN2=GPIO%d, 50%% duty",
             IN1_GPIO, IN2_GPIO);
    // MCPWM continues running after app_main returns; no software PWM task.
}
