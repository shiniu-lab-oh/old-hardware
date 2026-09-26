#include "vfd_mapper.h"
#include "pt6312.h"
#include "pt6312_tools.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include "esp_timer.h"

_Static_assert(VFD_MAPPER_STATE_COUNT == PT6312_TEST_RAM_SIZE * 8,
               "Mapper must cover exactly the first eight RAM bytes");
_Static_assert(VFD_MAPPER_AUTO_INTERVAL_MS > 0 && VFD_MAPPER_AUTO_PAUSE_MS >= 0,
               "Invalid auto timing");

static uint8_t current_state;
static bool auto_running;
static uint32_t auto_pass;
static int64_t next_step_us;

void vfd_mapper_print_help(void)
{
    printf("PT6312 VFD Mapper v0.1\n"
           "Commands:\n"
           "  n - next\n  p - previous\n  r - restart\n"
           "  c - clear\n  a - auto (restart from State 0)\n"
           "  s - stop (hold display)\n  f - full / ALL ON\n"
           "  ? - help\nMapper range: 64 states, RAM 0x00..0x07\n"
           "Single keys; Enter is optional. n/p/r/c/f stop AUTO.\n");
    fflush(stdout);
}

void vfd_mapper_show_state(uint8_t state)
{
    if (state >= VFD_MAPPER_STATE_COUNT) {
        printf("Invalid mapper state: %u\n", (unsigned)state);
        return;
    }
    uint8_t ram[PT6312_TEST_RAM_SIZE] = {0};
    const uint8_t addr = state / 8;
    const uint8_t bit = state % 8;
    const uint8_t value = (uint8_t)(1U << bit);
    ram[addr] = value;

    // Clear the whole chip FIRST. Writing only a new image can briefly leave
    // two bits set when moving backwards across byte addresses.
    pt6312_clear();
    pt6312_write_ram(0, ram, sizeof(ram));
    pt6312_display_on(PT6312_BRIGHTNESS);
    current_state = state;
    printf("========================================\n"
           "VFD MAPPER\nState : %u / 63\nAddr  : 0x%02X\n"
           "Bit   : %u\nValue : 0x%02X\n"
           "========================================\n"
           "MAP,state=%u,addr=0x%02X,bit=%u,value=0x%02X\n",
           (unsigned)state, (unsigned)addr, (unsigned)bit, (unsigned)value,
           (unsigned)state, (unsigned)addr, (unsigned)bit, (unsigned)value);
    fflush(stdout);
}

void vfd_mapper_auto_stop(void)
{
    if (auto_running) {
        printf("AUTO stopped; holding current display, cursor=%u\n",
               (unsigned)current_state);
        fflush(stdout);
    }
    auto_running = false;
}

void vfd_mapper_next(void)
{
    vfd_mapper_auto_stop();
    vfd_mapper_show_state((current_state + 1) % VFD_MAPPER_STATE_COUNT);
}

void vfd_mapper_previous(void)
{
    vfd_mapper_auto_stop();
    vfd_mapper_show_state((current_state + VFD_MAPPER_STATE_COUNT - 1) %
                          VFD_MAPPER_STATE_COUNT);
}

void vfd_mapper_restart(void)
{
    vfd_mapper_auto_stop();
    vfd_mapper_show_state(0);
}

void vfd_mapper_clear(void)
{
    vfd_mapper_auto_stop();
    pt6312_clear();
    printf("CLEAR; cursor=%u retained, all RAM zero\n", (unsigned)current_state);
    fflush(stdout);
}

void vfd_mapper_all_on(void)
{
    vfd_mapper_auto_stop();
    pt6312_all_on(); // Keep the proven ALL ON implementation as-is.
    printf("ALL ON; cursor=%u retained (single-bit mode suspended)\n",
           (unsigned)current_state);
    fflush(stdout);
}

static void schedule_next(void)
{
    int64_t delay_ms = VFD_MAPPER_AUTO_INTERVAL_MS;
    // Keep bit 63 displayed during the extra inter-pass pause. Still accepts keys.
    if (current_state == VFD_MAPPER_STATE_COUNT - 1) {
        delay_ms += VFD_MAPPER_AUTO_PAUSE_MS;
    }
    next_step_us = esp_timer_get_time() + delay_ms * 1000;
}

void vfd_mapper_auto_start(void)
{
    auto_running = true;
    auto_pass = 1;
    printf("===== AUTO PASS %" PRIu32 " =====\n", auto_pass);
    vfd_mapper_show_state(0);
    schedule_next();
}

void vfd_mapper_poll(void)
{
    if (!auto_running || esp_timer_get_time() < next_step_us) {
        return;
    }
    if (current_state == VFD_MAPPER_STATE_COUNT - 1) {
        printf("===== AUTO PASS %" PRIu32 " =====\n", ++auto_pass);
    }
    vfd_mapper_show_state((current_state + 1) % VFD_MAPPER_STATE_COUNT);
    // No catch-up burst after slow serial output or delayed scheduling.
    schedule_next();
}

void vfd_mapper_handle_command(char command)
{
    switch (command) {
    case 'n': vfd_mapper_next(); break;
    case 'p': vfd_mapper_previous(); break;
    case 'r': vfd_mapper_restart(); break;
    case 'c': vfd_mapper_clear(); break;
    case 'a': vfd_mapper_auto_start(); break;
    case 's': vfd_mapper_auto_stop(); break;
    case 'f': vfd_mapper_all_on(); break;
    case '?': vfd_mapper_print_help(); break;
    case '\r': case '\n': case ' ': case '\t': break;
    default:
        printf("Unknown command 0x%02X; press ? for help\n", (unsigned char)command);
        fflush(stdout);
        break;
    }
}

void vfd_mapper_init(void)
{
    current_state = 0;
    auto_running = false;
    auto_pass = 0;
    pt6312_clear();
    vfd_mapper_print_help();
    vfd_mapper_show_state(0);
}
