#ifndef PB_ACTIONS_H
#define PB_ACTIONS_H

#include "pb_app_protocol.h"
#include "pb_hal.h"

pb_action_t pb_action_from_control(pb_control_t control);
const char *pb_action_name(pb_action_t action);

#endif
