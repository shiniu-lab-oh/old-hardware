#include "vfd_demo.h"
#include "vfd_renderer.h"
#include "esp_err.h"
#include "esp_timer.h"
#include <stdbool.h>
#include <stdio.h>

static bool self_test_running;
static unsigned step;
static int64_t deadline_us;

void vfd_demo_print_help(void)
{
    printf("BOE VFM041SSBR1-S1 Support Package v0.1\n"
           "  1 - show 1234\n  2 - show 8888\n  3 - show 12:34\n"
           "  t - SELF_TEST (one pass)\n"
           "  b - stability: 8888 + colon + brightness 7, hold indefinitely\n"
           "  m - MAPPER State 0\n  f - raw ALL ON\n"
           "  c - clear\n  s - stop scans / hold\n"
           "  n/p/r/a/? - Mapper next/previous/restart/auto/help\n");
    fflush(stdout);
}

void vfd_demo_stop(void)
{
    if (self_test_running) {
        printf("SELF_TEST stopped; holding current display\n");
        fflush(stdout);
    }
    self_test_running = false;
}

static void prepare_renderer(void)
{
    vfd_demo_stop();
    vfd_clear(); // Resynchronize the shadow frame after raw Mapper / ALL ON.
    ESP_ERROR_CHECK(vfd_set_brightness(7));
}

void vfd_demo_number(uint16_t number)
{
    prepare_renderer();
    ESP_ERROR_CHECK(vfd_show_number(number));
    printf("DEMO %04u (inspect digit order/font)\n", (unsigned)number);
    fflush(stdout);
}

void vfd_demo_time(void)
{
    prepare_renderer();
    ESP_ERROR_CHECK(vfd_show_time(12, 34));
    printf("DEMO 12:34 (static frame, no clock service)\n");
    fflush(stdout);
}

void vfd_demo_stability_start(void)
{
    prepare_renderer();
    ESP_ERROR_CHECK(vfd_show_number(8888));
    vfd_set_colon(true);
    printf("STABILITY: 8888 + colon ON + brightness 7, holding indefinitely.\n"
           "Record 30 minutes of display, current and temperature manually.\n"
           "No hardware stability pass is claimed. Any display command exits this frame.\n");
    fflush(stdout);
}

static void show_test_step(void)
{
    const char *label = NULL;
    switch (step) {
    case 0: vfd_clear(); label = "CLEAR"; break;
    case 1: ESP_ERROR_CHECK(vfd_show_number(0)); label = "0000"; break;
    case 2: ESP_ERROR_CHECK(vfd_show_number(1111)); label = "1111"; break;
    case 3: ESP_ERROR_CHECK(vfd_show_number(1234)); label = "1234"; break;
    case 4: ESP_ERROR_CHECK(vfd_show_number(5678)); label = "5678"; break;
    case 5: ESP_ERROR_CHECK(vfd_show_number(8888)); label = "8888"; break;
    case 6: ESP_ERROR_CHECK(vfd_show_time(0, 0)); label = "00:00"; break;
    case 7: ESP_ERROR_CHECK(vfd_show_time(12, 34)); label = "12:34"; break;
    case 8: ESP_ERROR_CHECK(vfd_show_time(23, 59)); label = "23:59"; break;
    case 9: vfd_set_colon(false); label = "23 59 / Colon OFF"; break;
    case 10: vfd_set_colon(true); label = "23:59 / Colon ON"; break;
    default:
        if (step < 19) {
            ESP_ERROR_CHECK(vfd_set_brightness((uint8_t)(step - 11)));
            printf("SELF_TEST Brightness %u / 23:59\n", step - 11);
        } else {
            self_test_running = false;
            printf("SELF_TEST sequence complete; hardware inspection still required.\n");
        }
        break;
    }
    if (label) {
        printf("SELF_TEST %s\n", label);
    }
    fflush(stdout);
    deadline_us = esp_timer_get_time() + (int64_t)VFD_SELF_TEST_INTERVAL_MS * 1000;
}

void vfd_demo_self_test_start(void)
{
    prepare_renderer();
    step = 0;
    self_test_running = true;
    show_test_step();
}

void vfd_demo_poll(void)
{
    if (self_test_running && esp_timer_get_time() >= deadline_us) {
        ++step;
        show_test_step();
    }
}
