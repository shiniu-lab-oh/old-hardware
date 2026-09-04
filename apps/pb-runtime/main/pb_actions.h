#ifndef PB_ACTIONS_H
#define PB_ACTIONS_H

#include "old_panel.h"
#include "pb_app_protocol.h"

pb_action_t pb_action_from_panel_key(old_panel_key_t key);
const char *pb_action_name(pb_action_t action);

#endif
