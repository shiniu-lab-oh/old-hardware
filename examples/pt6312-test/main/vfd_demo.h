#pragma once
#include <stdint.h>

#define VFD_SELF_TEST_INTERVAL_MS 1500

void vfd_demo_number(uint16_t number);
void vfd_demo_time(void);
void vfd_demo_self_test_start(void);
void vfd_demo_stability_start(void);
void vfd_demo_stop(void);
void vfd_demo_poll(void);
void vfd_demo_print_help(void);
