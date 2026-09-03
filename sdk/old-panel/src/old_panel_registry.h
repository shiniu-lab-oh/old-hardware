#ifndef OLD_PANEL_REGISTRY_H
#define OLD_PANEL_REGISTRY_H

#include "old_panel_driver.h"

const old_panel_driver_t *old_panel_registry_find(const char *profile_id);

#endif
