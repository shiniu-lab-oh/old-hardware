#include "vfd_mapper.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>
#include <errno.h>
#include "esp_timer.h"

_Static_assert(VFD_MAPPER_STATES > 0 && VFD_MAPPER_STATES <= UINT16_MAX,
               "Invalid state count");
_Static_assert(VFD_MAPPER_AUTO_INTERVAL_MS > 0 && VFD_MAPPER_AUTO_PAUSE_MS >= 0,
               "Invalid auto timing");

static uint16_t current_state;
static bool auto_running;
static uint32_t pass;
static int64_t next_step_us;
static bool goto_pending;
static bool goto_overflow;
static char goto_buffer[16];
static size_t goto_length;

void vfd_mapper_print_help(void)
{
    printf("========================================\n"
           "PT6319 VFD Interactive Mapper v0.1\n"
           "RAM bytes : %u\nStates    : %u\n"
           "n - next\np - previous\nr - restart\nc - clear\nf - all on\n"
           "a - auto\ns - stop auto\n? - help\n"
           "g <state> + Enter - goto decimal state (Esc cancels)\n"
           "Single-key commands need no Enter. Manual commands stop AUTO.\n"
           "========================================\n",
           (unsigned)PT6319_RAM_BYTES, (unsigned)VFD_MAPPER_STATES);
    fflush(stdout);
}

void vfd_mapper_show_state(uint16_t state)
{
    if (state >= VFD_MAPPER_STATES) {
        printf("Invalid state: %u; range 0..%u\n", (unsigned)state,
               (unsigned)(VFD_MAPPER_STATES - 1));
        fflush(stdout);
        return;
    }
    uint8_t ram[PT6319_RAM_BYTES] = {0};
    const uint8_t addr = state / 8;
    const uint8_t bit = state % 8;
    const uint8_t value = (uint8_t)(1U << bit);
    ram[addr] = value;
    // Clear first: backward byte transitions must not briefly leave old and new
    // bits set while the chip receives the sequential full-frame write.
    pt6319_clear();
    pt6319_write_frame(ram);
    current_state = state;
    // Preserve the already initialized brightness; no per-state display commands.
    printf("========================================\nPT6319 VFD MAPPER\n"
           "State : %u / %u\nAddr  : 0x%02X\nBit   : %u\nValue : 0x%02X\n"
           "Command: n=next  p=prev  c=clear  f=full\n"
           "========================================\n"
           "MAP,state=%u,addr=0x%02X,bit=%u,value=0x%02X\n",
           (unsigned)state, (unsigned)(VFD_MAPPER_STATES - 1), (unsigned)addr,
           (unsigned)bit, (unsigned)value, (unsigned)state, (unsigned)addr,
           (unsigned)bit, (unsigned)value);
    fflush(stdout);
}

void vfd_mapper_auto_stop(void)
{
    if (auto_running) {
        printf("AUTO stopped; holding display, cursor=%u\n", (unsigned)current_state);
        fflush(stdout);
    }
    auto_running = false;
}

void vfd_mapper_next(void)
{
    vfd_mapper_auto_stop();
    vfd_mapper_show_state((current_state + 1) % VFD_MAPPER_STATES);
}

void vfd_mapper_previous(void)
{
    vfd_mapper_auto_stop();
    vfd_mapper_show_state((current_state + VFD_MAPPER_STATES - 1) % VFD_MAPPER_STATES);
}

void vfd_mapper_restart(void)
{
    vfd_mapper_auto_stop();
    vfd_mapper_show_state(0);
}

void vfd_mapper_clear(void)
{
    vfd_mapper_auto_stop();
    pt6319_clear();
    printf("CLEAR; cursor=%u retained\n", (unsigned)current_state);
    fflush(stdout);
}

void vfd_mapper_full(void)
{
    vfd_mapper_auto_stop();
    pt6319_all_on(); // Original Golden Test, no alternate FULL implementation.
    printf("ALL ON; cursor=%u retained, single-bit mode suspended\n", (unsigned)current_state);
    fflush(stdout);
}

static void schedule_next(void)
{
    int64_t delay_ms = VFD_MAPPER_AUTO_INTERVAL_MS;
    if (current_state == VFD_MAPPER_STATES - 1) {
        delay_ms += VFD_MAPPER_AUTO_PAUSE_MS;
    }
    next_step_us = esp_timer_get_time() + delay_ms * 1000;
}

void vfd_mapper_auto_start(void)
{
    auto_running = true;
    pass = 1;
    printf("===== MAPPER AUTO PASS %" PRIu32 " =====\n", pass);
    vfd_mapper_show_state(0);
    schedule_next();
}

void vfd_mapper_poll(void)
{
    if (!auto_running || goto_pending || esp_timer_get_time() < next_step_us) {
        return;
    }
    if (current_state == VFD_MAPPER_STATES - 1) {
        printf("===== MAPPER AUTO PASS %" PRIu32 " =====\n", ++pass);
    }
    vfd_mapper_show_state((current_state + 1) % VFD_MAPPER_STATES);
    schedule_next(); // No catch-up burst after delayed processing.
}

static void finish_goto(void)
{
    goto_buffer[goto_length] = '\0';
    char *start = goto_buffer;
    while (isspace((unsigned char)*start)) { ++start; }
    errno = 0;
    char *end;
    const unsigned long state = strtoul(start, &end, 10);
    const bool digits = isdigit((unsigned char)*start) && end != start;
    while (isspace((unsigned char)*end)) { ++end; }
    goto_pending = false;
    if (goto_overflow || errno == ERANGE || !digits || *end != '\0' ||
        state >= VFD_MAPPER_STATES) {
        printf("Invalid GOTO; use g <state> with decimal 0..%u; display unchanged\n",
               (unsigned)(VFD_MAPPER_STATES - 1));
        fflush(stdout);
        return;
    }
    vfd_mapper_show_state((uint16_t)state);
}

void vfd_mapper_handle_char(char input)
{
    if (goto_pending) {
        if (input == '\r' || input == '\n') {
            finish_goto();
        } else if (input == '\x1b') {
            goto_pending = false;
            printf("GOTO cancelled\n");
            fflush(stdout);
        } else if (input == '\b' || input == '\x7f') {
            if (goto_length > 0) { --goto_length; }
        } else if (goto_length < sizeof(goto_buffer) - 1) {
            goto_buffer[goto_length++] = input;
        } else {
            goto_overflow = true; // Reject the complete line, never truncate a state.
        }
        return;
    }
    switch (input) {
    case 'n': vfd_mapper_next(); break;
    case 'p': vfd_mapper_previous(); break;
    case 'r': vfd_mapper_restart(); break;
    case 'c': vfd_mapper_clear(); break;
    case 'f': vfd_mapper_full(); break;
    case 'a': vfd_mapper_auto_start(); break;
    case 's': vfd_mapper_auto_stop(); break;
    case '?': vfd_mapper_print_help(); break;
    case 'g':
        vfd_mapper_auto_stop();
        goto_pending = true;
        goto_length = 0;
        goto_overflow = false;
        printf("GOTO: type decimal state and Enter (Esc cancels)\n");
        fflush(stdout);
        break;
    case '\r': case '\n': case ' ': case '\t': break;
    default:
        printf("Unknown command 0x%02X; press ?\n", (unsigned char)input);
        fflush(stdout);
        break;
    }
}

void vfd_mapper_init(void)
{
    current_state = 0;
    auto_running = false;
    goto_pending = false;
    pt6319_clear();
    vfd_mapper_print_help();
    vfd_mapper_show_state(0);
}
