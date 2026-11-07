/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once
#include <stdint.h>

/* Settings model property for the grid's caption marquee. */
int cfw_cover_grid_property(uint32_t type, uint32_t property, uintptr_t *out);
/* Main and Music menu actions: the Cover Grid row opens the grid. */
int cfw_cover_grid_menu_action(void *controller, const char *action, uint32_t argument);
