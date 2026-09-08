#include "pb_actions.h"

pb_action_t pb_action_from_control(pb_control_t control)
{
    return control == PB_CONTROL_PRIMARY ? PB_ACTION_PRIMARY : PB_ACTION_NONE;
}

const char *pb_action_name(pb_action_t action)
{
    switch (action) {
        case PB_ACTION_PRIMARY:
            return "primary";
        case PB_ACTION_PRIMARY_LONG:
            return "primary_long";
        case PB_ACTION_NONE:
        default:
            return "none";
    }
}
