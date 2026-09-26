#pragma once

#include <stdint.h>

#define VFD_MAPPER_AUTO_INTERVAL_MS 1000
#define VFD_MAPPER_AUTO_PAUSE_MS 2000
#define VFD_MAPPER_STATE_COUNT 64

// Single caller, after pt6312_init(). No GPIO/protocol ownership here.
void vfd_mapper_init(void);
void vfd_mapper_show_state(uint8_t state);
void vfd_mapper_next(void);
void vfd_mapper_previous(void);
void vfd_mapper_restart(void);
void vfd_mapper_clear(void);
void vfd_mapper_all_on(void);
void vfd_mapper_auto_start(void);
void vfd_mapper_auto_stop(void);
void vfd_mapper_print_help(void);
void vfd_mapper_handle_command(char command);
// Nonblocking scheduler; call only after servicing queued manual input.
void vfd_mapper_poll(void);
