#include "old_panel_registry.h"

#include <stddef.h>
#include <string.h>

#include "drivers/lp001.h"

static const old_panel_driver_t *const s_drivers[] = {
    &old_panel_driver_lp001,
};

const old_panel_driver_t *old_panel_registry_find(const char *profile_id)
{
    if (profile_id == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < sizeof(s_drivers) / sizeof(s_drivers[0]); ++i) {
        if (strcmp(s_drivers[i]->profile_id, profile_id) == 0) {
            return s_drivers[i];
        }
    }

    return NULL;
}
