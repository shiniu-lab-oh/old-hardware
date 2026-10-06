#pragma once
#include <stdint.h>
#include "pt6319.h"

#define VFD_MAPPER_STATES (PT6319_RAM_BYTES * 8)
#define VFD_MAPPER_AUTO_INTERVAL_MS 1500
#define VFD_MAPPER_AUTO_PAUSE_MS 2000

// Single caller, after pt6319_init(). No physical segment names or power control.
void vfd_mapper_init(void);
void vfd_mapper_show_state(uint16_t state);
void vfd_mapper_next(void);
void vfd_mapper_previous(void);
void vfd_mapper_restart(void);
void vfd_mapper_clear(void);
void vfd_mapper_full(void);
void vfd_mapper_auto_start(void);
void vfd_mapper_auto_stop(void);
void vfd_mapper_print_help(void);
void vfd_mapper_handle_char(char input);
void vfd_mapper_poll(void);
