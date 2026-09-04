#include "pb_actions.h"

pb_action_t pb_action_from_panel_key(old_panel_key_t key)
{
    const old_panel_key_t primary_key =
        (old_panel_key_t)(OLD_PANEL_KEY_1 + CONFIG_PB_PRIMARY_KEY_INDEX - 1);
    return key == primary_key ? PB_ACTION_PRIMARY : PB_ACTION_NONE;
}

const char *pb_action_name(pb_action_t action)
{
    switch (action) {
        case PB_ACTION_PRIMARY:
            return "primary";
        case PB_ACTION_NONE:
        default:
            return "none";
    }
}
