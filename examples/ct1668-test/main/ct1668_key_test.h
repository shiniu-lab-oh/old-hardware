#pragma once

/* Runs in app_main: seven measured keys, raw logging and GPIO27 LED demo.
 * Call sdc251_panel_init() and ct1668_init() first. MENU/EXIT/OK and UART
 * o/f/t control green; numeric UART labels only annotate the raw log. */
void ct1668_key_test_run(void);
